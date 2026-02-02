/*******************************************************************************
 * File: tests/scheduler/test_scheduler_cooperative.c
 * Description: Cooperative Scheduler Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "log.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_config.h"
#include "timer.h"

/**
 * @file test_scheduler_cooperative.c
 * @brief Cooperative Scheduler Test
 *
 * Tests cooperative scheduling with 3 tasks at different priorities.
 * Tasks use ONLY delays (no busy loops) since cooperative scheduling
 * requires explicit yields - a while(1) loop would block forever.
 *
 * Expected behavior:
 * - Tasks run to completion or until they yield (via delay)
 * - No preemption - each task runs until it calls rtos_delay_ms()
 * - Higher priority matters only when multiple tasks are ready
 */

/* Task priorities */
#define TASK1_PRIORITY (2U)
#define TASK2_PRIORITY (3U)
#define TASK3_PRIORITY (4U)

/* Test termination flag */
static volatile bool g_test_complete = false;

/* Iteration counters */
static volatile uint32_t g_task1_count = 0;
static volatile uint32_t g_task2_count = 0;
static volatile uint32_t g_task3_count = 0;

/* =================== Test Tasks =================== */

/**
 * NOTE: Cooperative scheduler requires tasks to yield explicitly.
 * NO busy loops allowed - they would block indefinitely!
 */

static void task1_func(void *param)
{
    (void) param;

    test_log_task("START", "Task1");

    while (!g_test_complete && g_task1_count < TEST_TASK1_ITERATIONS)
    {
        test_log_task("RUN", "Task1");
        g_task1_count++;

        /* Yield by delaying - this allows other tasks to run */
        test_log_task("DELAY", "Task1");
        rtos_delay_ms(TEST_TASK1_DELAY_MS);
    }

    test_log_task("END", "Task1");

    while (1)
    {
        rtos_delay_ms(1000);
    }
}

static void task2_func(void *param)
{
    (void) param;

    test_log_task("START", "Task2");

    while (!g_test_complete && g_task2_count < TEST_TASK2_ITERATIONS)
    {
        test_log_task("RUN", "Task2");
        g_task2_count++;

        test_log_task("DELAY", "Task2");
        rtos_delay_ms(TEST_TASK2_DELAY_MS);
    }

    test_log_task("END", "Task2");

    while (1)
    {
        rtos_delay_ms(1000);
    }
}

static void task3_func(void *param)
{
    (void) param;

    test_log_task("START", "Task3");

    while (!g_test_complete && g_task3_count < TEST_TASK3_ITERATIONS)
    {
        test_log_task("RUN", "Task3");
        g_task3_count++;

        test_log_task("DELAY", "Task3");
        rtos_delay_ms(TEST_TASK3_DELAY_MS);
    }

    test_log_task("END", "Task3");

    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/* =================== Test Timer Callback =================== */

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;

    g_test_complete = true;
    test_log_framework("TIMEOUT", "Cooperative");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_task_handle_t  task_handle;
    rtos_timer_handle_t test_timer;

    /* Initialize test environment */
    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    test_log_framework("BEGIN", "Cooperative");
    log_info("Cooperative Scheduler Test");
    log_info("Priorities: Task1=%u, Task2=%u, Task3=%u", TASK1_PRIORITY, TASK2_PRIORITY, TASK3_PRIORITY);
    log_info("Delays: %u, %u, %u ms", TEST_TASK1_DELAY_MS, TEST_TASK2_DELAY_MS, TEST_TASK3_DELAY_MS);
    log_info("NOTE: Cooperative - tasks yield via delay only");

    /* Initialize RTOS */
    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    /* Create test termination timer */
    status =
        rtos_timer_create("TestTimer", TEST_DURATION_MS, RTOS_TIMER_ONE_SHOT, test_timeout_callback, NULL, &test_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Timer create failed: %d", status);
        indicate_system_failure();
    }

    /* Create test tasks */
    status = rtos_task_create(task1_func, "T1", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK1_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(task2_func, "T2", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK2_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(task3_func, "T3", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK3_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /* Start test timer */
    status = rtos_timer_start(test_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Timer start failed: %d", status);
        indicate_system_failure();
    }

    log_info("Starting scheduler...");

    /* Start scheduler */
    status = rtos_start_scheduler();

    /* Should never reach here */
    indicate_system_failure();
}
