/*******************************************************************************
 * File: src/core/kernel.c
 * Description: Kernel Core Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "assert.h"
#include "kernel_priv.h"
#include "log.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"

/**
 * @file kernel.c
 * @brief RTOS Kernel Core Implementation
 *
 * This file contains the main kernel initialization and core functions.
 */

/* Global kernel control block */
rtos_kernel_cb_t g_kernel = {
    .state = RTOS_KERNEL_STATE_INACTIVE,
    .tick_count = 0,
    .current_task = NULL,
    .next_task = NULL,
    .scheduler_suspended = 0};

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
    status = rtos_task_create(rtos_task_idle_function, "IDLE", NULL, NULL, RTOS_IDLE_TASK_PRIORITY, &idle_task);

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

    g_kernel.current_task = g_kernel.next_task;
    g_kernel.current_task->state = RTOS_TASK_STATE_RUNNING;

    rtos_task_remove_from_ready_list(g_kernel.current_task);

    g_kernel.state = RTOS_KERNEL_STATE_RUNNING;

    rtos_port_start_systick();

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
    if (ticks == 0)
        return;

    rtos_port_enter_critical();

    if (g_kernel.current_task == NULL) {
        log_error("Delay called with no current task!");
        rtos_port_exit_critical();
        return;
    }

    /* Block current task */
    g_kernel.current_task->state = RTOS_TASK_STATE_BLOCKED;
    rtos_task_add_to_delayed_list(g_kernel.current_task, ticks);
    rtos_task_remove_from_ready_list(g_kernel.current_task);

    rtos_port_exit_critical();
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
    rtos_port_enter_critical();
    rtos_task_update_delayed_tasks();

    rtos_tcb_t *task = rtos_task_get_highest_priority_ready();

#if RTOS_SCHEDULER_TYPE_RMS
    if (task != NULL && task != g_kernel.current_task && task->priority > g_kernel.current_task->priority) {
        rtos_port_exit_critical();
        rtos_yield();
        return;
    }
#endif

    rtos_port_exit_critical();
}

/**
 * @brief Context switch handler (called by scheduler)
 */
void rtos_kernel_switch_context(void) {
    if (g_kernel.scheduler_suspended > 0)
        return;

    rtos_port_enter_critical();

    /* Handle current task state */
    if (g_kernel.current_task != NULL) {
        /* Only add to ready list if NOT unblocking */
        if (g_kernel.current_task->state != RTOS_TASK_STATE_BLOCKED) {
            g_kernel.current_task->state = RTOS_TASK_STATE_READY;
            rtos_task_add_to_ready_list(g_kernel.current_task);
        }
    }

    /* Select next task */
    g_kernel.next_task = rtos_task_get_highest_priority_ready();

    /* Handle next task activation */
    if (g_kernel.next_task != NULL) {
        rtos_task_remove_from_ready_list(g_kernel.next_task);
        g_kernel.next_task->state = RTOS_TASK_STATE_RUNNING;
        g_kernel.current_task = g_kernel.next_task;
    } else {
        g_kernel.current_task = rtos_task_get_idle_task();
        rtos_task_remove_from_ready_list(g_kernel.current_task);
        g_kernel.current_task->state = RTOS_TASK_STATE_RUNNING;
    }

    rtos_port_exit_critical();
}
