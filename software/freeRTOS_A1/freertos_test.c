/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/*Freq Analyser include*/
#include <unistd.h>
#include "system.h"
#include "sys/alt_irq.h"
#include "io.h"
#include "altera_avalon_pio_regs.h"

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
int SwitchState;
int MaintenanceState;
int LoadStates[5];

int switchValues;




TimerHandle_t stabilityTimerHandle;
TimerHandle_t reactionTimerHandle;

QueueHandle_t KeyboardInputQ;
QueueHandle_t FrequencyUpdateQ;
QueueHandle_t MonitorOutputQ;
QueueHandle_t TimerShedQ;
QueueHandle_t ShedLoadQ;
QueueHandle_t FreqStateQ;

SemaphoreHandle_t ThresholdValueSem;
SemaphoreHandle_t FlagStableElapseSem;
SemaphoreHandle_t SwitchStatesSem;
SemaphoreHandle_t MaintenanceStateSem;
SemaphoreHandle_t LoadStateSem;

//Frequency Analyser

void freq_relay(){

	double temp =  (double)IORD(FREQUENCY_ANALYSER_BASE, 0);
	//temp contains Freq value
	//printf("%f Hz\n", temp);
	//Send to Queue
	xQueueSendToBackFromISR(FrequencyUpdateQ,&temp,pdFALSE);
	xTaskNotifyGive(4);
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
	// periodically poll switch states
	switchValues = IORD(SLIDE_SWITCH_BASE,0);
	SwitchState = switchValues;
}

void UserInputHandler()
{
	//Handles User keyboard input to change threshold values.


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
			roc = abs((freq - prev_freq)/period);

			qbody.cur_freq = freq;
			qbody.roc = roc;
			xQueueSendToBack(MonitorOutputQ,&qbody,pdFALSE);

			xSemaphoreTake(ThresholdValueSem, portMAX_DELAY);
			tv = ThresholdValue;
			xSemaphoreGive(ThresholdValueSem);
			//block until semaphore obtained can cause deadlock
			if(freq < tv.freq || roc > tv.roc){
				unstable = 1;
				//Start reaction timer.
				xTimerStart(reactionTimerHandle,50);
			}else{
				unstable = 0;
			}
			//Gives Semaphore before blocking function

			xQueueSendToBack(FreqStateQ,&unstable,pdFALSE);
		}
		ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
	}




}

void stableElapse(stabilityTimerHandle)
{
	//called by stability timer when timer expires
	//checks is system is stable and sets flagStableElapse
}

void Load_Controller ()
{
	int curloadstates;
	int curswstates;
	int MFlag;
	int loadManaging = 0;
	int curnetstate = 0;
	int newnetstate;
	int	stablelapse = 1;
	int Output_LoadStates;
	//Changes the load as requested
	//Checks the state of the switches and MaintenanceState flag before changing the load.
	while(1){

		xSemaphoreTake(SwitchStatesSem,portMAX_DELAY);
		curswstates = SwitchState;
		xSemaphoreGive(SwitchStatesSem);

		xSemaphoreTake(MaintenanceStateSem,portMAX_DELAY);
		MFlag = MaintenanceState;
		xSemaphoreGive(MaintenanceStateSem);

		if(MaintenanceState == FLAG_HIGH){
			xSemaphoreTake(LoadStateSem,portMAX_DELAY);
			LoadStates[0] = curswstates && 0x01;
			LoadStates[1] = curswstates && 0x02;
			LoadStates[2] = curswstates && 0x04;
			LoadStates[3] = curswstates && 0x08;
			LoadStates[4] = curswstates && 0x10;
			curloadstates = LoadStates;
			xSemaphoreGive(LoadStateSem);
		}else{
			if(xQueueReceive(FreqStateQ,&newnetstate,portMAX_DELAY) == pdTRUE){
				if(curnetstate != newnetstate){
					//Starts stability timer on network state change.
					xTimerStart(stabilityTimerHandle,50);
				}else{
					if(stablelapse == FLAG_HIGH){
						//timerlapsed
						if(curnetstate == FLAG_LOW){
							//stable
						}else{
							//unstable - shed
						}
					}else{

					}
				}
			}
		}


	//Stops reaction timer.
		xTimerStop(reactionTimerHandle,50);
	}
}

void reactionElapse(reactionTimerHandle)
{
	//called by reaction timer when timer expires
	//adds timer value to the queue maybe
}

void Output_Load()
{
	//Outputs status of controller and loads, to LEDs, sends snapshot to UART
	struct monitor_package freq_pack;
	while(1){
		if(xQueueReceive(MonitorOutputQ,&freq_pack,portMAX_DELAY) == pdTRUE){
//			printf("freq: %f\n",freq_pack.cur_freq);
//			printf("roc: %f\n",freq_pack.roc);
			printf("%f\n",freq_pack.cur_freq);
			printf("%f\n",freq_pack.roc);
		}
	}

}

int main(void)
{
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);
	alt_irq_register(PUSH_BUTTON_IRQ,0,push_buttonISR);

	/* Queue initialisation*/
	FrequencyUpdateQ = xQueueCreate(100, sizeof(double));
	KeyboardInputQ = xQueueCreate(100, sizeof(int));
	MonitorOutputQ = xQueueCreate(100, sizeof(struct monitor_package));
	TimerShedQ = xQueueCreate(100, sizeof(double));
	ShedLoadQ = xQueueCreate(100, sizeof(int));
	FreqStateQ = xQueueCreate(100, sizeof(int));

	ThresholdValueSem = xSemaphoreCreateBinary();
	FlagStableElapseSem = xSemaphoreCreateBinary();
	SwitchStatesSem = xSemaphoreCreateBinary();
	MaintenanceStateSem = xSemaphoreCreateBinary();
	LoadStateSem = xSemaphoreCreateBinary();

	/* The RegTest tasks as described at the top of this file. */
	//xTaskCreate( prvFirstRegTestTask, "Rreg1", configMINIMAL_STACK_SIZE, mainREG_TEST_1_PARAMETER, mainREG_TEST_PRIORITY, NULL);
	//xTaskCreate( prvSecondRegTestTask, "Rreg2", configMINIMAL_STACK_SIZE, mainREG_TEST_2_PARAMETER, mainREG_TEST_PRIORITY, NULL);
	//create timers
	xTaskCreate(Monitor_Frequency, "monfreq", configMINIMAL_STACK_SIZE,NULL,4,4);
	xTaskCreate(Output_Load,"out_load",configMINIMAL_STACK_SIZE,NULL,2,2);
	stabilityTimerHandle = xTimerCreate("Stability Timer",pdMS_TO_TICKS(500),pdTRUE,(void *) 0,stableElapse);
	reactionTimerHandle = xTimerCreate("Reaction Timer",pdMS_TO_TICKS(200),pdFALSE,(void *) 0,reactionElapse);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;);
}

