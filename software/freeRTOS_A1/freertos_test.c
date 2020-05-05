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

//Frequency Analyser

void freq_relay(){
#define SAMPLING_FREQ 16000.0
	double temp = SAMPLING_FREQ/(double)IORD(FREQUENCY_ANALYSER_BASE, 0);
	//temp contains Freq value
	printf("%f Hz\n", temp);
	//Send to Queue
}

int main(void)
{
	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);
	/* The RegTest tasks as described at the top of this file. */
	xTaskCreate( prvFirstRegTestTask, "Rreg1", configMINIMAL_STACK_SIZE, mainREG_TEST_1_PARAMETER, mainREG_TEST_PRIORITY, NULL);
	xTaskCreate( prvSecondRegTestTask, "Rreg2", configMINIMAL_STACK_SIZE, mainREG_TEST_2_PARAMETER, mainREG_TEST_PRIORITY, NULL);

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

