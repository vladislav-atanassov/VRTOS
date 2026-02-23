/*******************************************************************************
 * File: tests/scheduler/test_scheduler_rr.c
 * Description: Round Robin Scheduler Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h" // IWYU pragma: keep
#include "hardware_env.h"
#include "log.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_config.h" // TODO: MOve that file to be in the /tests directory
#include "timer.h"

/**
 * @file test_scheduler_rr.c
 * @brief Round Robin Scheduler Test
 *
 * Tests round-robin scheduling with 3 tasks at EQUAL priority.
 * All tasks use delays only (no busy loops) to allow proper time-slicing.
 *
 * Expected behavior:
 * - Tasks run in round-robin order when all are ready
 * - Time slicing occurs between tasks of equal priority
 * - Each task logs START, RUN, and DELAY events
 */

/* Task priorities (all equal for round-robin) */
#define TASK1_PRIORITY (2U)
#define TASK2_PRIORITY (2U)
#define TASK3_PRIORITY (2U)

/* Test termination flag */
static volatile bool g_test_complete = false;

/* Iteration counters */
static volatile uint32_t g_task1_count = 0;
static volatile uint32_t g_task2_count = 0;
static volatile uint32_t g_task3_count = 0;

/* =================== Test Tasks =================== */

static void task1_func(void *param)
{
    (void) param;

    test_log_task("START", "Task1");

    while (!g_test_complete && g_task1_count < TEST_TASK1_ITERATIONS)
    {
        test_log_task("RUN", "Task1");
        g_task1_count++;

        test_log_task("DELAY", "Task1");
        rtos_delay_ms(TEST_TASK1_DELAY_MS);
    }

    test_log_task("END", "Task1");

    /* Keep task alive but idle */
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
    test_log_framework("TIMEOUT", "RoundRobin");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_task_handle_t  task_handle;
    rtos_timer_handle_t test_timer;

    /* Initialize test environment */
    hardware_env_config();
    log_uart_init(LOG_LEVEL_INFO);

    for (volatile uint32_t i = 0; i < 2000000; i++)
    {
        __asm__ volatile("nop");
    }

    test_log_framework("BEGIN", "RoundRobin");
    log_info("Round Robin Scheduler Test");
    log_info("Tasks: 3 at equal priority (%u)", TASK1_PRIORITY);
    log_info("Delays: %u, %u, %u ms", TEST_TASK1_DELAY_MS, TEST_TASK2_DELAY_MS, TEST_TASK3_DELAY_MS);

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
