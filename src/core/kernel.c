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
#include "profiling.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task.h"
#include "task_priv.h"
#include "timer.h"

/**
 * @file kernel.c
 * @brief RTOS Kernel Core Implementation
 *
 * This file contains the main kernel initialization and core functions.
 */

/* Global kernel control block */
rtos_kernel_cb_t g_kernel = {.state               = RTOS_KERNEL_STATE_INACTIVE,
                             .tick_count          = 0,
                             .current_task        = NULL,
                             .next_task           = NULL,
                             .scheduler_suspended = 0};

/**
 * @brief Initialize the RTOS system
 */
rtos_status_t rtos_init(void)
{
    rtos_status_t status;

    /* Check if already initialized */
    if (g_kernel.state != RTOS_KERNEL_STATE_INACTIVE)
    {
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Initialize kernel control block */
    g_kernel.state               = RTOS_KERNEL_STATE_INACTIVE;
    g_kernel.tick_count          = 0;
    g_kernel.current_task        = NULL;
    g_kernel.next_task           = NULL;
    g_kernel.scheduler_suspended = 0;

    /* Initialize task management system */
    status = rtos_task_init_system();
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    /* Initialize scheduler subsystem */
    status = rtos_scheduler_init(RTOS_SCHEDULER_TYPE);
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    /* Initialize porting layer */
    status = rtos_port_init();
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    /* Create idle task */
    rtos_task_handle_t idle_task;
    status = rtos_task_create(rtos_task_idle_function, "IDLE", 0, NULL, RTOS_IDLE_TASK_PRIORITY, &idle_task);
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    g_kernel.state = RTOS_KERNEL_STATE_READY;
    return RTOS_SUCCESS;
}

/**
 * @brief Start the RTOS scheduler
 */
rtos_status_t rtos_start_scheduler(void)
{
    if (g_kernel.state != RTOS_KERNEL_STATE_READY)
    {
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Find the first task to run using scheduler */
    g_kernel.next_task = rtos_scheduler_get_next_task();
    if (g_kernel.next_task == NULL)
    {
        return RTOS_ERROR_GENERAL;
    }

    g_kernel.current_task        = g_kernel.next_task;
    g_kernel.current_task->state = RTOS_TASK_STATE_RUNNING;

    /* Remove from ready list using scheduler */
    rtos_scheduler_remove_from_ready_list(g_kernel.current_task);

    g_kernel.state = RTOS_KERNEL_STATE_RUNNING;

    rtos_port_start_systick();
    rtos_port_start_first_task();

    /* Should never return */
    return RTOS_ERROR_GENERAL;
}

/**
 * @brief Get current tick count
 */
rtos_tick_t rtos_get_tick_count(void)
{
    return g_kernel.tick_count;
}

/**
 * @brief Delay current task for specified ticks
 */
void rtos_delay_ticks(rtos_tick_t ticks)
{
    if (ticks == 0)
        return;

    rtos_port_enter_critical();

    if (g_kernel.current_task == NULL)
    {
        log_error("Delay called with no current task!");
        rtos_port_exit_critical();
        return;
    }

    /* Block current task */
    g_kernel.current_task->state = RTOS_TASK_STATE_BLOCKED;

    /* Use scheduler-specific delayed list management */
    rtos_scheduler_add_to_delayed_list(g_kernel.current_task, ticks);

    rtos_port_exit_critical();
    rtos_yield();
}

/**
 * @brief Delay current task for specified milliseconds
 */
void rtos_delay_ms(uint32_t ms)
{
    rtos_tick_t ticks = (ms * RTOS_TICK_RATE_HZ) / 1000U;
    if (ticks == 0)
    {
        ticks = 1; /* Minimum delay of 1 tick */
    }
    rtos_delay_ticks(ticks);
}

/**
 * @brief Force task yield
 */
void rtos_yield(void)
{
    rtos_port_yield();
}

/**
 * @brief System tick handler (called by port layer)
 */
void rtos_kernel_tick_handler(void)
{
    RTOS_SYS_PROFILE_START(tick);
    g_kernel.tick_count++;

    /* Process Software Timers */
    rtos_timer_tick();

    if (g_scheduler_instance.initialized)
    {
        rtos_port_enter_critical();

        /* Update delayed tasks using scheduler */
        rtos_scheduler_update_delayed_tasks();

        /* Get next task from scheduler */
        rtos_task_handle_t next_task = rtos_scheduler_get_next_task();

        /* Check if preemption is needed */
        if (rtos_scheduler_should_preempt(next_task))
        {
            rtos_port_exit_critical();
            RTOS_SYS_PROFILE_END(tick, &g_prof_tick);
            rtos_yield();
            return;
        }

        rtos_port_exit_critical();
    }
    RTOS_SYS_PROFILE_END(tick, &g_prof_tick);
}

/**
 * @brief Context switch handler (called by scheduler)
 */
void rtos_kernel_switch_context(void)
{
    if (g_kernel.scheduler_suspended > 0)
        return;

    RTOS_SYS_PROFILE_START(ctx_switch);

    rtos_port_enter_critical();

    /* Handle current task state */
    if (g_kernel.current_task != NULL)
    {
        /* Only add to ready list if NOT blocked/suspended */
        if (g_kernel.current_task->state != RTOS_TASK_STATE_BLOCKED &&
            g_kernel.current_task->state != RTOS_TASK_STATE_SUSPENDED)
        {
            g_kernel.current_task->state = RTOS_TASK_STATE_READY;

            /* Use scheduler-specific ready list management */
            rtos_scheduler_add_to_ready_list(g_kernel.current_task);
        }

        /* Notify scheduler about task completion/yield */
        rtos_scheduler_task_completed(g_kernel.current_task);
    }

    /* Select next task using scheduler */
    g_kernel.next_task = rtos_scheduler_get_next_task();

    /* Handle next task activation */
    if (g_kernel.next_task != NULL)
    {
        /* Remove from ready list using scheduler */
        rtos_scheduler_remove_from_ready_list(g_kernel.next_task);

        g_kernel.next_task->state = RTOS_TASK_STATE_RUNNING;
        g_kernel.current_task     = g_kernel.next_task;
    }
    else
    {
        /* Fallback to idle task */
        g_kernel.current_task = rtos_task_get_idle_task();
        if (g_kernel.current_task != NULL)
        {
            /* Remove idle task from ready list */
            rtos_scheduler_remove_from_ready_list(g_kernel.current_task);
            g_kernel.current_task->state = RTOS_TASK_STATE_RUNNING;
        }
    }

    rtos_port_exit_critical();

    RTOS_SYS_PROFILE_END(ctx_switch, &g_prof_context_switch);
}

/* =================== Task State Transition Helpers =================== */

/**
 * @brief Validate state transition and log/assert on invalid
 * @param task Task being transitioned
 * @param new_state Target state
 * @return true if transition is valid, false otherwise
 *
 * Valid transitions:
 *   READY    -> RUNNING, SUSPENDED
 *   RUNNING  -> READY, BLOCKED, SUSPENDED
 *   BLOCKED  -> READY, SUSPENDED
 *   SUSPENDED -> READY
 *   DELETED  -> (none)
 */
static bool rtos_kernel_validate_transition(rtos_task_handle_t task, rtos_task_state_t new_state)
{
    if (task == NULL)
    {
        return false;
    }

    rtos_task_state_t old_state = task->state;
    bool              valid     = false;

    switch (old_state)
    {
        case RTOS_TASK_STATE_READY:
            valid = (new_state == RTOS_TASK_STATE_RUNNING || new_state == RTOS_TASK_STATE_SUSPENDED);
            break;

        case RTOS_TASK_STATE_RUNNING:
            valid = (new_state == RTOS_TASK_STATE_READY || new_state == RTOS_TASK_STATE_BLOCKED ||
                     new_state == RTOS_TASK_STATE_SUSPENDED);
            break;

        case RTOS_TASK_STATE_BLOCKED:
            valid = (new_state == RTOS_TASK_STATE_READY || new_state == RTOS_TASK_STATE_SUSPENDED);
            break;

        case RTOS_TASK_STATE_SUSPENDED:
            valid = (new_state == RTOS_TASK_STATE_READY);
            break;

        case RTOS_TASK_STATE_DELETED:
            valid = false; /* Cannot transition from DELETED */
            break;

        default:
            valid = false;
            break;
    }

    if (!valid)
    {
        log_error("Invalid state transition for '%s': %d -> %d", task->name ? task->name : "unnamed", (int) old_state,
                  (int) new_state);
    }

    return valid;
}

/**
 * @brief Move task to ready state
 * @param task Task to make ready
 *
 * This helper function handles the state transition and list management
 * when a task becomes ready to run.
 */
void rtos_kernel_task_ready(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    rtos_port_enter_critical();

    /* Validate transition (BLOCKED/RUNNING -> READY is valid) */
    if (task->state != RTOS_TASK_STATE_BLOCKED && task->state != RTOS_TASK_STATE_RUNNING &&
        task->state != RTOS_TASK_STATE_SUSPENDED)
    {
        if (!rtos_kernel_validate_transition(task, RTOS_TASK_STATE_READY))
        {
            rtos_port_exit_critical();
            return;
        }
    }

    /* Update task state */
    task->state = RTOS_TASK_STATE_READY;

    /* Add to ready list using scheduler */
    rtos_scheduler_add_to_ready_list(task);

    /* Check if preemption is needed */
    if (g_kernel.state == RTOS_KERNEL_STATE_RUNNING)
    {
        if (rtos_scheduler_should_preempt(task))
        {
            rtos_port_exit_critical();
            rtos_yield();
            return;
        }
    }

    rtos_port_exit_critical();
}

/**
 * @brief Move task to blocked state
 * @param task Task to block
 * @param delay_ticks Delay in ticks (0 = indefinite block)
 *
 * This helper function handles the state transition and list management
 * when a task becomes blocked.
 */
void rtos_kernel_task_block(rtos_task_handle_t task, rtos_tick_t delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    rtos_port_enter_critical();

    /* Validate transition (only RUNNING/READY can block) */
    if (task->state != RTOS_TASK_STATE_RUNNING && task->state != RTOS_TASK_STATE_READY)
    {
        log_error("Cannot block task '%s' from state %d", task->name ? task->name : "unnamed", (int) task->state);
        rtos_port_exit_critical();
        return;
    }

    /* Update task state */
    task->state = RTOS_TASK_STATE_BLOCKED;

    /* Remove from ready list if it was there */
    rtos_scheduler_remove_from_ready_list(task);

    /* Add to delayed list if delay specified */
    if (delay_ticks > 0)
    {
        rtos_scheduler_add_to_delayed_list(task, delay_ticks);
    }

    /* If this is the current task, force context switch */
    if (task == g_kernel.current_task)
    {
        rtos_port_exit_critical();
        rtos_yield();
        return;
    }

    rtos_port_exit_critical();
}

/**
 * @brief Unblock a task
 * @param task Task to unblock
 *
 * This helper function moves a blocked task back to ready state.
 */
void rtos_kernel_task_unblock(rtos_task_handle_t task)
{
    if (task == NULL || task->state != RTOS_TASK_STATE_BLOCKED)
    {
        return;
    }

    rtos_port_enter_critical();

    /* Remove from delayed list if it was there */
    rtos_scheduler_remove_from_delayed_list(task);

    /* Move to ready state */
    rtos_kernel_task_ready(task);

    rtos_port_exit_critical();
}