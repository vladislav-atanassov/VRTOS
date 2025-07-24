/*******************************************************************************
 * File: src/core/kernel.c
 * Description: Kernel Core Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS/VRTOS.h"
#include "VRTOS/task.h"
#include "rtos_port.h"
#include "task/task_priv.h"
#include "kernel_priv.h"

/**
 * @file kernel.c
 * @brief RTOS Kernel Core Implementation
 *
 * This file contains the main kernel initialization and core functions.
 */

/* Global kernel control block */
rtos_kernel_cb_t g_kernel = {0};

/**
 * @brief Initialize the RTOS system
 */
rtos_status_t rtos_init(void) {
    rtos_status_t status;

    /* Check if already initialized */
    if (g_kernel.state != RTOS_KERNEL_STATE_INACTIVE) {
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Initialize kernel control block */
    g_kernel.state = RTOS_KERNEL_STATE_INACTIVE;
    g_kernel.tick_count = 0;
    g_kernel.current_task = NULL;
    g_kernel.next_task = NULL;
    g_kernel.scheduler_suspended = 0;

    /* Initialize task management system */
    status = rtos_task_init_system();
    if (status != RTOS_SUCCESS) {
        return status;
    }

    /* Initialize porting layer */
    status = rtos_port_init();
    if (status != RTOS_SUCCESS) {
        return status;
    }

    /* Create idle task */
    rtos_task_handle_t idle_task;
    status = rtos_task_create(rtos_task_idle_function, "IDLE", RTOS_DEFAULT_TASK_STACK_SIZE, NULL,
                              RTOS_IDLE_TASK_PRIORITY, &idle_task);
    if (status != RTOS_SUCCESS) {
        return status;
    }

    g_kernel.state = RTOS_KERNEL_STATE_READY;
    return RTOS_SUCCESS;
}

/**
 * @brief Start the RTOS scheduler
 */
rtos_status_t rtos_start_scheduler(void) {
    if (g_kernel.state != RTOS_KERNEL_STATE_READY) {
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Find the first task to run */
    g_kernel.next_task = rtos_task_get_highest_priority_ready();
    if (g_kernel.next_task == NULL) {
        return RTOS_ERROR_GENERAL;
    }

    g_kernel.state = RTOS_KERNEL_STATE_RUNNING;

    /* Start the system tick */
    rtos_port_start_systick();

    /* Start the first task */
    rtos_port_start_first_task();

    /* Should never return */
    return RTOS_ERROR_GENERAL;
}

/**
 * @brief Get current tick count
 */
rtos_tick_t rtos_get_tick_count(void) { return g_kernel.tick_count; }

/**
 * @brief Delay current task for specified ticks
 */
void rtos_delay_ticks(rtos_tick_t ticks) {
    if (ticks == 0 || g_kernel.current_task == NULL) {
        return;
    }

    rtos_port_enter_critical();

    /* Add current task to delayed list */
    rtos_task_add_to_delayed_list(g_kernel.current_task, ticks);

    /* Remove from ready list */
    rtos_task_remove_from_ready_list(g_kernel.current_task);
    g_kernel.current_task->state = RTOS_TASK_STATE_BLOCKED;

    rtos_port_exit_critical();

    /* Force context switch */
    rtos_yield();
}

/**
 * @brief Delay current task for specified milliseconds
 */
void rtos_delay_ms(uint32_t ms) {
    rtos_tick_t ticks = (ms * RTOS_TICK_RATE_HZ) / 1000U;
    if (ticks == 0) {
        ticks = 1; /* Minimum delay of 1 tick */
    }
    rtos_delay_ticks(ticks);
}

/**
 * @brief Force task yield
 */
void rtos_yield(void) { rtos_port_yield(); }

/**
 * @brief System tick handler (called by port layer)
 */
void rtos_kernel_tick_handler(void) {
    g_kernel.tick_count++;

    /* Update delayed tasks */
    rtos_task_update_delayed_tasks();

    /* Handle time slicing for round-robin scheduling */
    if (g_kernel.current_task != NULL && g_kernel.current_task->time_slice_remaining > 0) {
        g_kernel.current_task->time_slice_remaining--;

        if (g_kernel.current_task->time_slice_remaining == 0) {
            /* Time slice expired, yield */
            rtos_yield();
        }
    }
}

/**
 * @brief Context switch handler (called by scheduler)
 */
void rtos_kernel_switch_context(void) {
    if (g_kernel.scheduler_suspended > 0) {
        return; /* Scheduler is suspended */
    }

    /* Save current task state */
    if (g_kernel.current_task != NULL && g_kernel.current_task->state == RTOS_TASK_STATE_RUNNING) {
        g_kernel.current_task->state = RTOS_TASK_STATE_READY;
    }

    /* Find next task to run */
    g_kernel.next_task = rtos_task_get_highest_priority_ready();

    /* Context switch if different task */
    if (g_kernel.next_task != g_kernel.current_task) {
        g_kernel.current_task = g_kernel.next_task;
        if (g_kernel.current_task != NULL) {
            g_kernel.current_task->state = RTOS_TASK_STATE_RUNNING;
            g_kernel.current_task->time_slice_remaining = RTOS_TIME_SLICE_TICKS;
        }
    }
}
