/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include <unistd.h>
#include "system.h"
#include "sys/alt_irq.h"
#include "io.h"
#include "altera_up_avalon_ps2.h"
#include "altera_avalon_pio_regs.h"
#include "altera_up_avalon_ps2.h"

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

/* The parameters passed to the reg test tasks.  This is just done to check
 the parameter passing mechanism is working correctly. */
#define mainREG_TEST_1_PARAMETER    ( ( void * ) 0x12345678 )
#define mainREG_TEST_2_PARAMETER    ( ( void * ) 0x87654321 )
#define mainREG_TEST_PRIORITY       ( tskIDLE_PRIORITY + 1)
#define SAMPLING_FREQ 16000.0


/*
 * Create the demo tasks then start the scheduler.
 */

// Macros
#define FLAG_HIGH 1
#define FLAG_LOW 0

struct thresholdval{
	double freq;
	double roc;
};

struct monitor_package{
	double cur_freq;
	double roc;
};
// Global Variables
struct thresholdval ThresholdValue;

int flagStableElapse;
//int SwitchState[5];
int redLEDs;
int redLED0;
int redLED1;
int redLED2;
int redLED3;
int redLED4;
int greenLEDs;
int greenLED0;
int greenLED1;
int greenLED2;
int greenLED3;
int greenLED4;

int SwitchState;
int MaintenanceState;
int LoadStates[5];
int TimerShed = 0;
int switchValues;




TimerHandle_t stabilityTimerHandle;
TimerHandle_t reactionTimerHandle;

QueueHandle_t KeyboardInputQ;
QueueHandle_t FrequencyUpdateQ;
QueueHandle_t MonitorOutputQ;
QueueHandle_t ShedLoadQ;
QueueHandle_t FreqStateQ;

SemaphoreHandle_t ThresholdValueSem;
SemaphoreHandle_t FlagStableElapseSem;
SemaphoreHandle_t SwitchStatesSem;
SemaphoreHandle_t MaintenanceStateSem;
SemaphoreHandle_t LoadStateSem;
SemaphoreHandle_t ShedTimerSem;

//Frequency Analyser

void freq_relay(){

	double temp =  (double)IORD(FREQUENCY_ANALYSER_BASE, 0);
	//temp contains Freq value
	//printf("%f Hz\n", temp);
	//Send to Queue
	xQueueSendToBackFromISR(FrequencyUpdateQ,&temp,pdFALSE);
}

void push_buttonISR(){
	if(MaintenanceState == FLAG_HIGH){
		MaintenanceState = FLAG_LOW;
	}else{
		MaintenanceState = FLAG_HIGH;
	}
}

void switchPolling ()
{
	while(1){
	// periodically poll switch states
	switchValues = IORD(SLIDE_SWITCH_BASE,0);
	xSemaphoreTake(SwitchStatesSem, portMAX_DELAY);
	SwitchState = switchValues;
	xSemaphoreGive(SwitchStatesSem);
	}
}

void UserInputHandler()
{
	//Handles User keyboard input to change threshold values.
	char keyboard_input;
	unsigned int threshvalue = 30;
	while(1){
		if (xQueueReceive(KeyboardInputQ,&keyboard_input,portMAX_DELAY) == pdTRUE){
			if( keyboard_input == 'f'){
				while(keyboard_input != '\n'){
					if(xQueueReceive(KeyboardInputQ,&keyboard_input,portMAX_DELAY) == pdTRUE){
						if((keyboard_input<58)&&(keyboard_input>47)){
						threshvalue = threshvalue + keyboard_input - '0';
						threshvalue = threshvalue*10;
						}
					}
				}
				xSemaphoreTake(ThresholdValueSem,portMAX_DELAY);
				ThresholdValue.freq = threshvalue;
				xSemaphoreGive(ThresholdValueSem);
			}else if (keyboard_input == 'r'){
				while(keyboard_input != '\n'){
					if(xQueueReceive(KeyboardInputQ,&keyboard_input,portMAX_DELAY) == pdTRUE){
						if((keyboard_input<58)&&(keyboard_input>47)){
							threshvalue = threshvalue + keyboard_input - '0';
							threshvalue = threshvalue*10;
						}
					}
				}
			xSemaphoreTake(ThresholdValueSem,portMAX_DELAY);
			ThresholdValue.freq = threshvalue;
			xSemaphoreGive(ThresholdValueSem);
			}
		}
	}
}

void Monitor_Frequency()
{
	//Calculates the Instantaneous Frequency
	//Checks if the instantaneous frequency exceeds the threshold values. Calculate the value of the ROC.
	double period;
	double freq;
	double prev_freq = 0;
	double roc;
	struct monitor_package qbody;
	int unstable = 0;
	struct thresholdval tv;
	while (1){
		if(xQueueReceive(FrequencyUpdateQ,&period,portMAX_DELAY) == pdTRUE){
//			printf("monitor freq 3\n");
			freq = SAMPLING_FREQ/period;
			if(prev_freq == 0){
				prev_freq = freq;
			}
			//calculate ROC
			roc = abs(((freq - prev_freq)*SAMPLING_FREQ)/period);

			qbody.cur_freq = freq;
			qbody.roc = roc;
//			printf("%f\n",freq);
//			printf("%f\n", prev_freq);
//			printf("%f\n", period);
//			printf("%f\n", roc);
//			printf("monitor freq 4\n");
			xQueueSendToBack(MonitorOutputQ,&qbody,pdFALSE);
			prev_freq = freq;
			xSemaphoreTake(ThresholdValueSem, portMAX_DELAY);
			tv = ThresholdValue;
			xSemaphoreGive(ThresholdValueSem);
			//block until semaphore obtained can cause deadlock
			if(freq < tv.freq || roc > tv.roc){
				unstable = 1;
				//Start reaction timer.
				xTimerReset(reactionTimerHandle,50);
			}else{
				unstable = 0;
			}
			//Gives Semaphore before blocking function

			xQueueSendToBack(FreqStateQ,&unstable,pdFALSE);
		}

	}




}

void ps2_isr(void* ps2_device, alt_u32 id){
	unsigned char byte;
	alt_up_ps2_read_data_byte_timeout(ps2_device, &byte);
//	xQueueSendtoBackFromISR(KeyboardInputQ,&byte,pdFALSE);
}

void stableElapse(stabilityTimerHandle)
{
	//called by stability timer when timer expires
	//checks is system is stable and sets flagStableElapse
	xSemaphoreTake(FlagStableElapseSem, portMAX_DELAY);
	flagStableElapse = FLAG_HIGH;
	xSemaphoreGive(FlagStableElapseSem);
}

void Load_Controller ()
{
	int curloadstates[5];
	int curswstates[5];
	int MFlag;
	int loadManaging = 0;
	int curnetstate = 0;
	int newnetstate;
	int	stablelapse = 1;
	int Output_LoadStates[5];
	int target_load;
	//Changes the load as requested
	//Checks the state of the switches and MaintenanceState flag before changing the load.
	while(1){
//		printf("load controller 1\n");
		xSemaphoreTake(SwitchStatesSem,portMAX_DELAY);
//		curswstates = SwitchState;
		curswstates[0] = SwitchState & 0x01;
		curswstates[1] = SwitchState & 0x02;
		curswstates[2] = SwitchState & 0x04;
		curswstates[3] = SwitchState & 0x08;
		curswstates[4] = SwitchState & 0x10;;
		xSemaphoreGive(SwitchStatesSem);
		xQueueReceive(FreqStateQ,&newnetstate,portMAX_DELAY);
		xSemaphoreTake(MaintenanceStateSem,portMAX_DELAY);
		MFlag = MaintenanceState;
//		MFlag = 1;
		xSemaphoreGive(MaintenanceStateSem);
		//WHEN target_load = 999, NO TARGET TO SEND
		target_load = 999;

		int i;
		for (i=0;i<5;i++){
			if(curswstates[i] == 0){
			curloadstates[i]= 0;
			}
		}

		if(MFlag == FLAG_HIGH){
			xSemaphoreTake(LoadStateSem,portMAX_DELAY);
//			LoadStates[0] = curswstates[0];
//			LoadStates[1] = curswstates [1];
//			LoadStates[2] = curswstates [2];
//			LoadStates[3] = curswstates [3];
//			LoadStates[4] = curswstates [4];
//			curloadstates = LoadStates;

			int i,j;
			for (i=0;i<5;i++){
				LoadStates[i] = curswstates[i];
			}

			for (j=0;j<5;j++){
				curloadstates[j] = LoadStates[j];
			}
			xSemaphoreGive(LoadStateSem);
		}else{
				if(curnetstate != newnetstate){
					//Starts stability timer on network state change.
					loadManaging = FLAG_HIGH;
				}
				if(loadManaging == FLAG_HIGH){
					//sem
					xSemaphoreTake(ShedTimerSem,portMAX_DELAY);
					if(stablelapse == FLAG_HIGH){
						//timerlapsed
						xSemaphoreGive(ShedTimerSem);
						if(curnetstate == FLAG_LOW){
							//stable - add new load
							int x;
							for(x = 4;x>=0;x--){
								if((curswstates[x] == 1) &&(curloadstates[x] == 0)){
									curloadstates[x] = 1;
									target_load = x;
									xSemaphoreTake(FlagStableElapseSem, portMAX_DELAY);
									flagStableElapse = FLAG_LOW;
									xSemaphoreGive(FlagStableElapseSem);
									xTimerReset(stabilityTimerHandle,50);


									break;
								}
							}
						}else{
							//unstable - shed
							int x;
							for(x = 0;x<5;x++){
								if((curswstates[x] == 1) && (curloadstates[x] == 1)){
									curloadstates[x] = 0;
									target_load = x;
									xSemaphoreTake(FlagStableElapseSem, portMAX_DELAY);
									flagStableElapse = FLAG_LOW;
									xSemaphoreGive(FlagStableElapseSem);
									xTimerReset(stabilityTimerHandle,50);
									xTimerStop(reactionTimerHandle,50);
									break;
								}
							}
						}
						//check if all loads have been reestablished
						if (curloadstates == curswstates){
							loadManaging = FLAG_LOW;
						}
					}else{
						if(TimerShed == FLAG_HIGH){
							int x;
							for(x = 0;x<5;x++){
								if((curswstates[x] == 1) && (curloadstates[x] == 1)){
									curloadstates[x] = 0;
									target_load = x;
									xTimerStart(stabilityTimerHandle,50);
									xTimerStop(reactionTimerHandle,50);
									break;
								}
							}
							TimerShed = FLAG_LOW;
						}
						xSemaphoreGive(ShedTimerSem);
					}
				}else{
					int i;
					for (i=0;i<5;i++){
						curloadstates[i] = curswstates[i];
					}
				}
				//update loadstates
				xSemaphoreTake(LoadStateSem,portMAX_DELAY);
//				LoadStates = curloadstates;
				int i;
				for (i=0;i<5;i++){
					LoadStates[i] = curloadstates[i];
				}
				xSemaphoreGive(LoadStateSem);
				if (target_load != 999){
					//send target load
					xQueueSendToBack(ShedLoadQ,&target_load,pdFALSE);
				}
		}
	}
}

void reactionElapse(reactionTimerHandle)
{
	//called by reaction timer when timer expires
	//adds timer value to the queue maybe
	xSemaphoreTake(ShedTimerSem,portMAX_DELAY);
	TimerShed = FLAG_HIGH;
	xSemaphoreGive(ShedTimerSem);

}

void Output_Load()
{
	//Outputs status of controller and loads, to LEDs, sends snapshot to UART
	struct monitor_package freq_pack;
	int shed_target;
	while(1){
//		printf("output load 1\n");
		if(xQueueReceive(MonitorOutputQ,&freq_pack,portMAX_DELAY) == pdTRUE){
			printf("freq: %f\n",freq_pack.cur_freq);
			printf("roc: %f\n",freq_pack.roc);

			redLEDs = 0x00000;

//			printf("%f\n",LoadStates[0]);
//			printf("%f\n",LoadStates[1]);
//			printf("%f\n",LoadStates[2]);
//			printf("%f\n",LoadStates[3]);
//			printf("%f\n",LoadStates[4]);

			redLED0 = (LoadStates[0] & 0x01);
			redLED1 = (LoadStates[1] & 0x02);
			redLED2 = (LoadStates[2] & 0x04);
			redLED3 = (LoadStates[3] & 0x08);
			redLED4 = (LoadStates[4] & 0x10);

		}

		if(xQueueReceive(ShedLoadQ, &shed_target, 2) == pdTRUE){
			printf("load shed");

			greenLEDs = 0x00;

			if(shed_target == 0){
				if(greenLED0 == FLAG_HIGH){
					greenLED0 = FLAG_LOW;
				}else{
					greenLED0 = FLAG_HIGH;
					redLED0 = 0;
				}
				printf("load 1");
			}

			if(shed_target == 1){
				if(greenLED1 == FLAG_HIGH){
					greenLED1 = FLAG_LOW;
				}else{
					greenLED1 = FLAG_HIGH;
					redLED1 = 0;
				}
				printf("load 2");
			}
			if(shed_target == 2){
				if(greenLED2 == FLAG_HIGH){
					greenLED2 = FLAG_LOW;
				}else{
					greenLED2 = FLAG_HIGH;
					redLED2 = 0;
				}
				printf("load 3");
			}

			if(shed_target == 3){
				if(greenLED3 == FLAG_HIGH){
					greenLED3 = FLAG_LOW;
				}else{
					greenLED3 = FLAG_HIGH;
					redLED3 = 0;
				}
				printf("load 4");
			}

			if(shed_target == 4){
				if(greenLED4 == FLAG_HIGH){
					greenLED4 = FLAG_LOW;
				}else{
					greenLED4 = FLAG_HIGH;
					redLED4 = 0;
				}
				printf("load 5");
			}

			greenLEDs = greenLEDs | greenLED0;
			greenLEDs = greenLEDs | greenLED1;
			greenLEDs = greenLEDs | greenLED2;
			greenLEDs = greenLEDs | greenLED3;
			greenLEDs = greenLEDs | greenLED4;

			redLEDs = redLEDs | redLED0;
			redLEDs = redLEDs | redLED1;
			redLEDs = redLEDs | redLED2;
			redLEDs = redLEDs | redLED3;
			redLEDs = redLEDs | redLED4;

//			IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE,0x00);

		}
		IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE,redLEDs);
		IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE,greenLEDs);
	}

}

int main(void)
{

	alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

		if(ps2_device == NULL){
			printf("can't find PS/2 device\n");
			return 1;
		}

	ThresholdValue.roc = 15;

	alt_up_ps2_enable_read_interrupt(ps2_device);
	alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);
	alt_irq_register(PUSH_BUTTON_IRQ,0,push_buttonISR);

	/* Queue initialisation*/
	FrequencyUpdateQ = xQueueCreate(100, sizeof(double));
	KeyboardInputQ = xQueueCreate(100, sizeof(int));
	MonitorOutputQ = xQueueCreate(100, sizeof(struct monitor_package));
	ShedLoadQ = xQueueCreate(100, sizeof(int));
	FreqStateQ = xQueueCreate(100, sizeof(int));

	ThresholdValueSem = xSemaphoreCreateMutex();
	FlagStableElapseSem = xSemaphoreCreateMutex();
	SwitchStatesSem = xSemaphoreCreateMutex();
	MaintenanceStateSem = xSemaphoreCreateMutex();
	LoadStateSem = xSemaphoreCreateMutex();
	ShedTimerSem = xSemaphoreCreateMutex();

	/* The RegTest tasks as described at the top of this file. */
	//xTaskCreate( prvFirstRegTestTask, "Rreg1", configMINIMAL_STACK_SIZE, mainREG_TEST_1_PARAMETER, mainREG_TEST_PRIORITY, NULL);
	//xTaskCreate( prvSecondRegTestTask, "Rreg2", configMINIMAL_STACK_SIZE, mainREG_TEST_2_PARAMETER, mainREG_TEST_PRIORITY, NULL);

	//create tasks
	xTaskCreate(Monitor_Frequency, "monfreq", configMINIMAL_STACK_SIZE,NULL,4,4);
	xTaskCreate(Output_Load,"out_load",configMINIMAL_STACK_SIZE,NULL,2,2);
	xTaskCreate(switchPolling,"switch_poll",configMINIMAL_STACK_SIZE,NULL,1,1);
	xTaskCreate(Load_Controller,"load_control",configMINIMAL_STACK_SIZE,NULL,3,3);

	//create timers
	stabilityTimerHandle = xTimerCreate("Stability Timer",pdMS_TO_TICKS(500),pdTRUE,(void *) 0,stableElapse);
	reactionTimerHandle = xTimerCreate("Reaction Timer",pdMS_TO_TICKS(200),pdFALSE,(void *) 0,reactionElapse);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;);
}


