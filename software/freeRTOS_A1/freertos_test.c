/* Standard includes. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

/* The parameters passed to the reg test tasks.  This is just done to check
 the parameter passing mechanism is working correctly. */
#define mainREG_TEST_1_PARAMETER    ( ( void * ) 0x12345678 )
#define mainREG_TEST_2_PARAMETER    ( ( void * ) 0x87654321 )
#define mainREG_TEST_PRIORITY       ( tskIDLE_PRIORITY + 1)
static void prvFirstRegTestTask(void *pvParameters);
static void prvSecondRegTestTask(void *pvParameters);


/*
 * Create the demo tasks then start the scheduler.
 */
// Macros
#define Flag_Raised 1
#define Flag_Low 0

// Global Variables
int ThresholdValue[2];
int flagStableElapse;
int SwitchState[5];
int MaintenanceState;
int LoadStates[5];

void * stabilityTimerHandle;
void * reactionTimerHandle;

int switchValues;
//Frequency Analyser

void freq_relay(){
#define SAMPLING_FREQ 16000.0
	double temp = SAMPLING_FREQ/(double)IORD(FREQUENCY_ANALYSER_BASE, 0);
	//temp contains Freq value
	printf("%f Hz\n", temp);
	//Send to Queue
}

void push_buttonISR(){
	if(MaintenanceState == Flag_Raised){
		MaintenanceState = Flag_Low;
	}else{
		MaintenanceState = Flag_Raised;
	}
}

void switchPolling ()
{
	// periodically poll switch states
	switchValues = IORD(SLIDE_SWITCH_BASE,0);
	SwitchState[0] = switchValues && "000000000000000001";
	SwitchState[1] = switchValues && "000000000000000010";
	SwitchState[2] = switchValues && "000000000000000100";
	SwitchState[3] = switchValues && "000000000000001000";
	SwitchState[4] = switchValues && "000000000000010000";
}

void UserInputHandler()
{
	//Handles User keyboard input to change threshold values.


}

void Monitor_Frequency()
{
	//Calculates the Instantaneous Frequency
	//Checks if the instantaneous frequency exceeds the threshold values. Calculate the value of the ROC.

	//Start reaction timer.
	xTimerStart(reactionTimerHandle,50);

}

void stableElapse(stabilityTimerHandle)
{
	//called by stability timer when timer expires
	//checks is system is stable and sets flagStableElapse
}

void Load_Controller ()
{
	//Changes the load as requested
	//Checks the state of the switches and MaintenanceState flag before changing the load.

	//Starts stability timer on network state change.
	xTimerStart(stabilityTimerHandle,50);
	//Stops reaction timer.
	xTimerStop(reactionTimerHandle,50);
}

void reactionElapse(reactionTimerHandle)
{
	//called by reaction timer when timer expires
	//adds timer value to the queue maybe
}

void Output_Load()
{
	//Outputs status of controller and loads, to LEDs, sends snapshot to UART

}

int main(void)
{
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);
	alt_irq_register(PUSH_BUTTON_IRQ,0,push_buttonISR);
	/* The RegTest tasks as described at the top of this file. */
	xTaskCreate( prvFirstRegTestTask, "Rreg1", configMINIMAL_STACK_SIZE, mainREG_TEST_1_PARAMETER, mainREG_TEST_PRIORITY, NULL);
	xTaskCreate( prvSecondRegTestTask, "Rreg2", configMINIMAL_STACK_SIZE, mainREG_TEST_2_PARAMETER, mainREG_TEST_PRIORITY, NULL);
	//create timers
	stabilityTimerHandle = xTimerCreate("Stability Timer",pdMS_TO_TICKS(500),pdTRUE,(void *) 0,stableElapse);
	reactionTimerHandle = xTimerCreate("Reaction Timer",pdMS_TO_TICKS(200),pdFALSE,(void *) 0,reactionElapse);

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Will only reach here if there is insufficient heap available to start
	 the scheduler. */
	for (;;);
}
static void prvFirstRegTestTask(void *pvParameters)
{
	while (1)
	{
		//printf("Task 1\n");
		vTaskDelay(1000);
	}

}
static void prvSecondRegTestTask(void *pvParameters)
{
	while (1)
	{
		//printf("Task 2\n");
		vTaskDelay(1000);
	}
}

