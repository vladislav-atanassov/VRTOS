/*******************************************************************************
 * File: examples/profiling_demo/main.c
 * Description: Simple Profiling Demonstration
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "log_flush_task.h"
#include "profiling.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "uart_tx.h"
#include "ulog.h"


/* =================== Tasks =================== */

rtos_profile_stat_t prof_work = {UINT32_MAX, 0, 0, 0, "WorkBlock"};

/* Task that does some simulated work (highest user priority) */
void WorkTask(void *param)
{
    (void) param;
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

/* Task that prints reports (lower priority than worker) */
void ReportTask(void *param)
{
    (void) param;

    while (1)
    {
        rtos_delay_ms(300000); /* Report every 5s */

        ulog_info("========== PROFILING REPORT ==========");

        /* User/Application profiling */
        rtos_profiling_print_stat(&prof_work);

        /* RTOS System profiling (only if enabled) */
        rtos_profiling_report_system_stats();

        /* Reset stats — each report shows the last 5s window only */
        rtos_profiling_reset_stat(&prof_work, "WorkBlock");
        rtos_profiling_reset_system_stats();
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

    ulog_init(ULOG_LEVEL_INFO);

    ulog_info("Starting Profiling Demo...");

    rtos_task_handle_t task_handle;

    /**
     * Priority assignment:
     *   WORKER   (3) — highest, simulated real-time work
     *   REPORTER (2) — medium, periodic profiling report
     *   HEARTBEAT(1) — low
     *   KLOG     (0) — lowest, drains log buffers when idle
     */
    rtos_task_create(WorkTask, "WORKER", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &task_handle);
    rtos_task_create(ReportTask, "REPORTER", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 2, &task_handle);
    rtos_task_create(log_flush_task, "KLOG", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 0, &task_handle);

    rtos_start_scheduler();

    /* Should not reach here */
    while (1)
    {
    }
    return 0;
}
