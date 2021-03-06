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
#include "altera_up_ps2_keyboard.h"
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

int freq1 = 0;
int freq2 = 0;
int freq3 = 0;
int freq4 = 0;
int freq5 = 0;
int roc1 = 0;
int roc2 = 0;
int roc3 = 0;
int roc4 = 0;
int roc5 = 0;

int SwitchState;
int MaintenanceState;
int LoadStates[5];
int TimerShed = 0;
int switchValues;
int loadManageFin = 1;

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
SemaphoreHandle_t LoadManagerFinSem;
SemaphoreHandle_t loadshedtimeSem;


//Frequency Analyser

void freq_relay(){

	double temp =  (double)IORD(FREQUENCY_ANALYSER_BASE, 0);
	//Send to Queue
	xQueueSendToBackFromISR(FrequencyUpdateQ,&temp,pdFALSE);
}

void push_buttonISR(void* context,alt_u32 id){

	xSemaphoreTake(MaintenanceStateSem,portMAX_DELAY);
	if(MaintenanceState == FLAG_HIGH){
		MaintenanceState = FLAG_LOW;
	}else{
		MaintenanceState = FLAG_HIGH;
	}
	xSemaphoreGive(MaintenanceStateSem);

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
	unsigned char keyboard_input;
	unsigned int threshvalue = 0;
	int debounce;
	while(1){
		threshvalue = 0;
		if (xQueueReceive(KeyboardInputQ,&keyboard_input,2) == pdTRUE){
			if( keyboard_input == 'F'){
				while(keyboard_input != '\n'){
					if(xQueueReceive(KeyboardInputQ,&keyboard_input,portMAX_DELAY) == pdTRUE){
						if((keyboard_input<58)&&(keyboard_input>47)){
							if((debounce%2) == 0){
								threshvalue = (threshvalue*10) + keyboard_input - '0';
								debounce++;
							}
							else {
								debounce = 0;
							}
						}
					}
				}
				xSemaphoreTake(ThresholdValueSem,portMAX_DELAY);
				ThresholdValue.freq = threshvalue;
				xSemaphoreGive(ThresholdValueSem);
			}else if (keyboard_input == 'R'){
				while(keyboard_input != '\n'){
					if(xQueueReceive(KeyboardInputQ,&keyboard_input,portMAX_DELAY) == pdTRUE){
						if((keyboard_input<58)&&(keyboard_input>47)){
							if((debounce%2) == 0){
								threshvalue = (threshvalue*10) + keyboard_input - '0';
								debounce++;
							}
							else {
								debounce = 0;
							}
						}
					}
				}
			xSemaphoreTake(ThresholdValueSem,portMAX_DELAY);
			ThresholdValue.roc = threshvalue;
			xSemaphoreGive(ThresholdValueSem);
			}
		}
		xSemaphoreTake(ThresholdValueSem,portMAX_DELAY);
		xSemaphoreGive(ThresholdValueSem);
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
			freq = SAMPLING_FREQ/period;
			if(prev_freq == 0){
				prev_freq = freq;
			}
			//calculate ROC
			roc = abs(((freq - prev_freq)*SAMPLING_FREQ)/period);

			qbody.cur_freq = freq;
			qbody.roc = roc;
			xQueueSendToBack(MonitorOutputQ,&qbody,pdFALSE);
			prev_freq = freq;
			xSemaphoreTake(ThresholdValueSem, portMAX_DELAY);
			tv = ThresholdValue;
			xSemaphoreGive(ThresholdValueSem);
			//block until semaphore obtained can cause deadlock
			if(freq < tv.freq || roc > tv.roc){
				xSemaphoreTake(LoadManagerFinSem,portMAX_DELAY);
				if(loadManageFin == 1){
					loadManageFin = 0;
					xTimerReset(reactionTimerHandle,50);
				}
				xSemaphoreGive(LoadManagerFinSem);

				unstable = 1;
				//Start reaction timer.

			}else{
				unstable = 0;
			}
			//Gives Semaphore before blocking function

			xQueueSendToBack(FreqStateQ,&unstable,pdFALSE);
		}
	}
}

void ps2_isr(void* ps2_device, alt_u32 id){
	  char ascii;
	  int status = 0;
	  unsigned char key = 0;
	  KB_CODE_TYPE decode_mode;
	  status = decode_scancode (ps2_device, &decode_mode , &key , &ascii) ;
	  if(key == 0x5A){
		  ascii = '\n';
	  }

	    // print out the result
	    switch ( decode_mode )
	    {
	      case KB_ASCII_MAKE_CODE :
	      case KB_BINARY_MAKE_CODE:
	    	  xQueueSendToBackFromISR(KeyboardInputQ,&ascii,pdFALSE);
	    	break ;
	      default :
	        break ;
	    }
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
	int target_load;
	xSemaphoreTake(FlagStableElapseSem, portMAX_DELAY);
	flagStableElapse = FLAG_HIGH;
	xSemaphoreGive(FlagStableElapseSem);
	//Changes the load as requested
	//Checks the state of the switches and MaintenanceState flag before changing the load.
	while(1){
		xSemaphoreTake(SwitchStatesSem,portMAX_DELAY);
		curswstates[0] = SwitchState & 0x01;
		curswstates[1] = (SwitchState & 0x02)>>1;
		curswstates[2] = (SwitchState & 0x04)>>2;
		curswstates[3] = (SwitchState & 0x08)>>3;
		curswstates[4] = (SwitchState & 0x10)>>4;
		xSemaphoreGive(SwitchStatesSem);
		xQueueReceive(FreqStateQ,&newnetstate,portMAX_DELAY);
		xSemaphoreTake(MaintenanceStateSem,portMAX_DELAY);
		MFlag = MaintenanceState;
//		MFlag = 1;
		xSemaphoreGive(MaintenanceStateSem);
		//WHEN target_load = 999, NO TARGET TO SEND
		target_load = 999;

		int j;
		for (j=0;j<5;j++){
			if(curswstates[j] == 0){
			curloadstates[j]= 0;
			}
		}

		if(MFlag == FLAG_HIGH){
			xSemaphoreTake(LoadStateSem,portMAX_DELAY);
			int i;
			for (i=0;i<5;i++){
				LoadStates[i] = curswstates[i];
				curloadstates[i] = LoadStates[i];
			}
			xSemaphoreGive(LoadStateSem);
			IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
			continue;
		}
		if(newnetstate == FLAG_HIGH){
			loadManaging = FLAG_HIGH;
		}
		if(loadManaging == FLAG_LOW){
			xSemaphoreTake(LoadStateSem,portMAX_DELAY);
			int i;
			for (i=0;i<5;i++){
				LoadStates[i] = curswstates[i];
				curloadstates[i] = LoadStates[i];
			}
			xSemaphoreGive(LoadStateSem);
			continue;
		}else{
			if(curnetstate != newnetstate){
				xTimerReset(stabilityTimerHandle,50);
				xSemaphoreTake(FlagStableElapseSem,portMAX_DELAY);
				xSemaphoreGive(FlagStableElapseSem);
			}
			xSemaphoreTake(FlagStableElapseSem,portMAX_DELAY);
			int stableelapse = flagStableElapse;
			xSemaphoreGive(FlagStableElapseSem);
			if(stableelapse == FLAG_HIGH){
				if(curnetstate == FLAG_LOW){
					//stable network
					//addload
					int i;
					for(i = 4;i>=0;i--){
						if(curloadstates[i] == 0 && curswstates[i] == 1){
							target_load = i;
							curloadstates[i] = 1;
							xSemaphoreTake(FlagStableElapseSem,portMAX_DELAY);
							flagStableElapse = FLAG_LOW;
							xSemaphoreGive(FlagStableElapseSem);
							xTimerReset(stabilityTimerHandle,50);
							break;
						}
					}
				}else{
					//unstable network
					//shedload
					int i;
					for(i = 0;i<5;i++){
						if(curloadstates[i] == 1 && curswstates[i] == 1){
							target_load = i;
							curloadstates[i] = 0;
							xSemaphoreTake(FlagStableElapseSem,portMAX_DELAY);
							flagStableElapse = FLAG_LOW;
							xSemaphoreGive(FlagStableElapseSem);
							xTimerReset(stabilityTimerHandle,50);
							xTimerStop(reactionTimerHandle,50);
							xSemaphoreTake(ShedTimerSem,portMAX_DELAY);
							TimerShed = FLAG_LOW;
							xSemaphoreGive(ShedTimerSem);
							break;
						}
					}
				}
			}
			xSemaphoreTake(ShedTimerSem,portMAX_DELAY);
			int locTimerShed = TimerShed;
			xSemaphoreGive(ShedTimerSem);
			if(locTimerShed == FLAG_HIGH){
				int i;
				for(i = 0;i<5;i++){
					if(curloadstates[i] == 1 && curswstates[i] == 1){
						target_load = i;
						curloadstates[i] = 0;
						xSemaphoreTake(FlagStableElapseSem,portMAX_DELAY);
						flagStableElapse = FLAG_LOW;
						xSemaphoreGive(FlagStableElapseSem);
						xTimerReset(stabilityTimerHandle,50);
						xTimerStop(reactionTimerHandle,50);
						xSemaphoreTake(ShedTimerSem,portMAX_DELAY);
						TimerShed = FLAG_LOW;
						xSemaphoreGive(ShedTimerSem);
						break;
					}
				}
			}
		}
		if(curswstates == curloadstates){
			loadManaging = FLAG_LOW;
			xSemaphoreTake(LoadManagerFinSem,portMAX_DELAY);
			loadManageFin = 1;
			xSemaphoreGive(LoadManagerFinSem);

		}
		//update and send queue
		xSemaphoreTake(LoadStateSem,portMAX_DELAY);
		int i;
		for (i=0;i<5;i++){
			LoadStates[i] = curloadstates[i];
		}
		xSemaphoreGive(LoadStateSem);
		if(target_load != 999){
			xQueueSendToBack(ShedLoadQ,&target_load,pdFALSE);
		}
		curnetstate = newnetstate;
	}
}

void reactionElapse(reactionTimerHandle)
{
	//called by reaction timer when timer expires
	xSemaphoreTake(ShedTimerSem,portMAX_DELAY);
	TimerShed = FLAG_HIGH;
	xSemaphoreGive(ShedTimerSem);

}

void Output_Load()
{
	//Outputs status of controller and loads, to LEDs, sends snapshot to UART
	struct monitor_package freq_pack;
	int shed_target;
	int freq_new;
	int roc_new;
//	int curfreqTickTime;

	while(1){
		if(xQueueReceive(MonitorOutputQ,&freq_pack,portMAX_DELAY) == pdTRUE){
			freq_new = freq_pack.cur_freq;
			roc_new = freq_pack.roc;

			freq1 = freq2;
			freq2 = freq3;
			freq3 = freq4;
			freq4 = freq5;
			freq5 = freq_new;

			roc1 = roc2;
			roc2 = roc3;
			roc3 = roc4;
			roc4 = roc5;
			roc5 = roc_new;

			printf("*********************\n"
					"frequency 1: %i\n"
					"roc 1: %i\n"
					"frequency 2: %i\n"
					"roc 2: %i\n"
					"frequency 3: %i\n"
					"roc 3: %i\n"
					"frequency 4: %i\n"
					"roc 4: %i\n"
					"frequency 5: %i\n"
					"roc 5: %i\n"
					"*********************\n",freq1,roc1,freq2,roc2,freq3,roc3,freq4,roc4,freq5,roc5);


			redLEDs = 0x00000;

			redLED0 = (LoadStates[0] << 0);
			redLED1 = (LoadStates[1] << 1);
			redLED2 = (LoadStates[2] << 2);
			redLED3 = (LoadStates[3] << 3);
			redLED4 = (LoadStates[4] << 4);

			redLEDs = redLEDs | redLED0;
			redLEDs = redLEDs | redLED1;
			redLEDs = redLEDs | redLED2;
			redLEDs = redLEDs | redLED3;
			redLEDs = redLEDs | redLED4;
			IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE,redLEDs);
		}

		if(xQueueReceive(ShedLoadQ, &shed_target, 2) == pdTRUE){
			greenLEDs = 0x00;

			if(shed_target == 0){
				if(redLED0 == FLAG_LOW){
					greenLED0 = FLAG_HIGH;
				}else{
					greenLED0 = FLAG_LOW;
				}
			}

			if(shed_target == 1){
				if(redLED1 == FLAG_LOW){
					greenLED1 = FLAG_HIGH;
				}else{
					greenLED1 = FLAG_LOW;
				}
			}

			if(shed_target == 2){
				if(redLED2 == FLAG_LOW){
					greenLED2 = FLAG_HIGH;
				}else{
					greenLED2 = FLAG_LOW;
				}
			}

			if(shed_target == 3){
				if(redLED3 == FLAG_LOW){
					greenLED3 = FLAG_HIGH;
				}else{
					greenLED3 = FLAG_LOW;
				}
			}

			if(shed_target == 4){
				if(redLED4 == FLAG_LOW){
					greenLED4 = FLAG_HIGH;
				}else{
					greenLED4 = FLAG_LOW;
				}
			}

			greenLEDs = greenLEDs | (greenLED0 << 0);
			greenLEDs = greenLEDs | (greenLED1 << 1);
			greenLEDs = greenLEDs | (greenLED2 << 2);
			greenLEDs = greenLEDs | (greenLED3 << 3);
			greenLEDs = greenLEDs | (greenLED4 << 4);
		}
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
	// Set default thresholds
	ThresholdValue.roc = 10;
	ThresholdValue.freq = 49;

	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);

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

	//Semaphore creation
	ThresholdValueSem = xSemaphoreCreateMutex();
	FlagStableElapseSem = xSemaphoreCreateMutex();
	SwitchStatesSem = xSemaphoreCreateMutex();
	MaintenanceStateSem = xSemaphoreCreateMutex();
	LoadStateSem = xSemaphoreCreateMutex();
	ShedTimerSem = xSemaphoreCreateMutex();
	LoadManagerFinSem = xSemaphoreCreateMutex();

	//create tasks
	xTaskCreate(Monitor_Frequency, "monfreq", configMINIMAL_STACK_SIZE,NULL,4,4);
	xTaskCreate(Output_Load,"out_load",configMINIMAL_STACK_SIZE,NULL,2,2);
	xTaskCreate(switchPolling,"switch_poll",configMINIMAL_STACK_SIZE,NULL,1,1);
	xTaskCreate(Load_Controller,"load_control",configMINIMAL_STACK_SIZE,NULL,3,3);
	xTaskCreate(UserInputHandler,"userinput",configMINIMAL_STACK_SIZE,NULL,1,NULL);

	//create timers
	stabilityTimerHandle = xTimerCreate("Stability Timer",pdMS_TO_TICKS(500),pdTRUE,(void *) 0,stableElapse);
	reactionTimerHandle = xTimerCreate("Reaction Timer",pdMS_TO_TICKS(200),pdFALSE,(void *) 0,reactionElapse);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;);
}


