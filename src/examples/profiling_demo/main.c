/*******************************************************************************
 * File: examples/queue_demo/main.c
 * Description: Simple Producer-Consumer Queue Demonstration
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "log.h"
#include "profiling.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"

/* =================== Tasks =================== */

rtos_profile_stat_t prof_work = {UINT32_MAX, 0, 0, 0, "WorkBlock"};

/* Task that does some simulated work */
void WorkTask(void *param)
{
    volatile int i;
    while (1)
    {
        RTOS_USER_PROFILE_START(work);

        /* Simulate work: loop */
        for (i = 0; i < 10000; i++)
        {
            __asm volatile("nop");
        }

        led_toggle(); /* Blink to show activity */

        RTOS_USER_PROFILE_END(work, &prof_work);

        rtos_delay_ms(100);
    }
}

/* Task that prints reports */
void ReportTask(void *param)
{
    while (1)
    {
        rtos_delay_ms(5000); /* Report every 5s */

        log_info("============ PROFILING REPORT ============");

        /* User/Application profiling */
        log_info("--- User Application Stats ---");
        rtos_profiling_print_stat(&prof_work);

        /* RTOS System profiling (only if enabled) */
        rtos_profiling_report_system_stats();

        log_info("==========================================");
    }
}

int main(void)
{
    /* Hardware Initialization */
    hardware_env_config();

    /* Board Drivers */
    log_uart_init(LOG_LEVEL_INFO);

    rtos_init();
    rtos_profiling_init();

    log_info("Starting Profiling Demo...");

    rtos_task_handle_t task_handle;

    rtos_task_create(WorkTask, "WORKER", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &task_handle);
    rtos_task_create(ReportTask, "REPORTER", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 2, &task_handle);

    rtos_start_scheduler();

    /* Should not reach here */
    while (1)
    {
    }
    return 0;
}
