/*******************************************************************************
 * File: examples/basic_blinky/main.c
 * Description: Basic LED Blinky Example
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "hardware_env.h"
#include "log.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "task_priv.h"

/**
 * @file main.c
 * @brief Basic LED Blinky RTOS Example
 *
 * This example demonstrates the basic functionality of the RTOS by creating
 * a simple task that blinks the onboard LED.
 */

/* Task priorities */
#define BLINK_TASK_PRIORITY (2U)
#define PRINT_TASK_PRIORITY (3U)

/* Blink & Print timing */
#define LED_BLINK_DELAY_MS (200U)
#define PRINT_DELAY_MS     (200U)

/**
 * @brief LED blink task
 *
 * @param param Task parameter (unused)
 */
static void blink_task(void *param)
{
    (void) param; /* Suppress unused parameter warning */
    log_debug("IN blink_task()");
    /* Task main loop */
    while (1)
    {
        /* Toggle LED */
        led_toggle();

        log_print("START BLINK - O");

        for (int i = 0; i < 1000000; i++)
        {
            __asm volatile("nop");
        }

        log_print("STOP BLINK - X");

        /* Delay for specified time */
        rtos_delay_ms(LED_BLINK_DELAY_MS);
    }
}

/**
 * @brief Print task
 *
 * @param param Task parameter (unused)
 */
static void print_task(void *param)
{
    (void) param; /* Suppress unused parameter warning */
    log_debug("IN print_task()");
    /* Task main loop */
    while (1)
    {
        log_print("START PRINT - O");

        for (int i = 0; i < 1000000; i++)
        {
            __asm volatile("nop");
        }

        log_print("STOP PRINT - X");

        /* Delay for specified time */
        rtos_delay_ms(PRINT_DELAY_MS);
    }
}

/**
 * @brief Print task
 *
 * @param param Task parameter (unused)
 */
static void memory_mang_task(void *param)
{
    (void) param; /* Suppress unused parameter warning */
    log_debug("IN memory_mang_task()");
    /* Task main loop */
    while (1)
    {
        rtos_task_debug_print_all();
        rtos_delay_ms(1500);
    }
}

/**
 * What you should see with the different scheduling policies is the following outputs (change RTOS_SCHEDULER_TYPE in /VRTOS/config.h)
 * 
 * +-------------------------------------------+------------------------------------------+------------------------------------------+
 * | Preemptive (RTOS_SCHEDULER_PREEMPTIVE_SP) | Cooperative (RTOS_SCHEDULER_COOPERATIVE) | Round Round (RTOS_SCHEDULER_ROUND_ROBIN) |
 * |          [PRINT] START PRINT - O          |          [PRINT] START BLINK - O         |          [PRINT] START BLINK - O         |
 * |          [PRINT] STOP PRINT - X           |          [PRINT] STOP BLINK - X          |          [PRINT] START PRINT - O         |
 * |          [PRINT] START BLINK - O          |          [PRINT] START PRINT - O         |          [PRINT] STOP BLINK - X          |
 * |          [PRINT] START PRINT - O          |          [PRINT] STOP PRINT - X          |          [PRINT] STOP PRINT - X          |
 * |          [PRINT] STOP PRINT - X           |          [PRINT] START BLINK - O         |          [PRINT] START BLINK - O         |
 * |          [PRINT] STOP BLINK - X           |          [PRINT] STOP BLINK - X          |          [PRINT] START PRINT - O         |
 * |          [PRINT] START PRINT - O          |          [PRINT] START PRINT - O         |          [PRINT] STOP BLINK - X          |
 * |          [PRINT] STOP PRINT - X           |          [PRINT] STOP PRINT - X          |          [PRINT] STOP PRINT - X          |
 * +-------------------------------------------+------------------------------------------+------------------------------------------+
 * 
 */

/**
 * @brief Main function
 *
 * @return Should never return
 */
__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t      status;
    rtos_task_handle_t blink_task_handle;
    rtos_task_handle_t print_task_handle;
    rtos_task_handle_t memory_mang_task_handle;

    /* Initialize hardware environment */
    hardware_env_config();

    log_uart_init(LOG_LEVEL_INFO);

    /* Initialize RTOS */
    status = rtos_init();

    if (status != RTOS_SUCCESS)
    {
        /* Initialization failed - indicate with LED */
        indicate_system_failure();
    }

    /* Create mem task */
    status = rtos_task_create(memory_mang_task, "MEM", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &memory_mang_task_handle);

    if (status != RTOS_SUCCESS)
    {
        /* Task creation failed */
        indicate_system_failure();
    }

    /* Create blink task */
    status = rtos_task_create(blink_task, "BLINK", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, BLINK_TASK_PRIORITY,
                              &blink_task_handle);

    if (status != RTOS_SUCCESS)
    {
        /* Task creation failed */
        indicate_system_failure();
    }

    /* Create print task */
    status = rtos_task_create(print_task, "PRINT", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, PRINT_TASK_PRIORITY,
                              &print_task_handle);

    if (status != RTOS_SUCCESS)
    {
        /* Task creation failed */
        indicate_system_failure();
    }

    /* Start the RTOS scheduler */
    status = rtos_start_scheduler();

    /* Should never reach here */
    while (1)
    {
    }
}
