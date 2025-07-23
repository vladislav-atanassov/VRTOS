/*******************************************************************************
 * File: examples/basic_blinky/main.c
 * Description: Basic LED Blinky Example
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "../include/VRTOS/VRTOS.h"
#include "../include/VRTOS/task.h"
#include "stm32f4xx_hal.h"

/**
 * @file main.c
 * @brief Basic LED Blinky RTOS Example
 * 
 * This example demonstrates the basic functionality of the RTOS by creating
 * a simple task that blinks the onboard LED.
 */

/* LED Configuration for STM32F446RE Nucleo */
#define LED_PORT                GPIOA
#define LED_PIN                 GPIO_PIN_5      /* PA5 - User LED (LD2) */
#define LED_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOA_CLK_ENABLE()

/* Task priorities */
#define BLINK_TASK_PRIORITY     (2U)

/* Task stack sizes */
#define BLINK_TASK_STACK_SIZE   (256U)

/* Blink timing */
#define LED_BLINK_DELAY_MS      (500U)

/* Function prototypes */
static void led_init(void);
static void led_toggle(void);
static void blink_task(void *param);

/**
 * @brief Initialize LED GPIO
 */
static void led_init(void)
{
    /* Enable GPIO clock */
    LED_GPIO_CLK_ENABLE();
    
    /* Configure GPIO pin */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    
    /* Turn off LED initially */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
}

/**
 * @brief Toggle LED state
 */
static void led_toggle(void)
{
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}

/**
 * @brief LED blink task
 * 
 * @param param Task parameter (unused)
 */
static void blink_task(void *param)
{
    (void)param; /* Suppress unused parameter warning */
    
    /* Task main loop */
    while (1) {
        /* Toggle LED */
        led_toggle();
        
        /* Delay for specified time */
        rtos_delay_ms(LED_BLINK_DELAY_MS);
    }
}

/**
 * @brief Main function
 * 
 * @return Should never return
 */
int main(void)
{
    rtos_status_t status;
    rtos_task_handle_t blink_task_handle;
    
    /* Initialize HAL */
    HAL_Init();
    
    /* Initialize LED */
    led_init();
    
    /* Initialize RTOS */
    status = rtos_init();
    if (status != RTOS_SUCCESS) {
        /* Initialization failed - indicate with LED */
        while (1) {
            led_toggle();
            for (volatile uint32_t i = 0; i < 100000; i++);
        }
    }
    
    /* Create blink task */
    status = rtos_task_create(blink_task,
                             "BLINK",
                             BLINK_TASK_STACK_SIZE,
                             NULL,
                             BLINK_TASK_PRIORITY,
                             &blink_task_handle);
    if (status != RTOS_SUCCESS) {
        /* Task creation failed */
        while (1) {
            led_toggle();
            for (volatile uint32_t i = 0; i < 200000; i++);
        }
    }
    
    /* Start the RTOS scheduler */
    status = rtos_start_scheduler();
    
    /* Should never reach here */
    while (1) {
        /* Error indication */
        led_toggle();
        for (volatile uint32_t i = 0; i < 50000; i++);
    }
    
    return 0;
}
