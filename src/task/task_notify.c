/*******************************************************************************
 * File: src/task/task_notify.c
 * Description: Task Notification Implementation (Direct-to-Task Signaling)
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "klog.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"

/**
 * @file task_notify.c
 * @brief Task Notification Implementation
 *
 * Implements direct-to-task signaling embedded in the TCB.
 * No separate kernel object — uses the task's own pointer as the
 * blocked_on sentinel: task->blocked_on = task.
 *
 * Blocking pattern mirrors semaphore.c (lines 197-224):
 * - Infinite wait:   set state = BLOCKED, yield
 * - Timed wait:      rtos_kernel_task_block(task, timeout)
 * - Timeout detect:  if blocked_on still == sentinel after resume
 */

/* =================== Send-side API (ISR-safe) =================== */

/**
 * @brief Send a notification to a task with a specific action.
 */
rtos_notify_status_t rtos_task_notify(rtos_task_handle_t task, uint32_t value, rtos_notify_action_t action)
{
    if (task == NULL)
    {
        return RTOS_NOTIFY_ERR_INVALID;
    }

    rtos_port_enter_critical();

    /* Apply action to notification_value */
    switch (action)
    {
        case RTOS_NOTIFY_ACTION_NONE:
            /* Just set pending, don't modify value */
            break;

        case RTOS_NOTIFY_ACTION_SET_BITS:
            task->notification_value |= value;
            break;

        case RTOS_NOTIFY_ACTION_INCREMENT:
            task->notification_value++;
            break;

        case RTOS_NOTIFY_ACTION_OVERWRITE:
            task->notification_value = value;
            break;

        default:
            rtos_port_exit_critical();
            return RTOS_NOTIFY_ERR_INVALID;
    }

    task->notification_pending = 1;

    KLOGD(KEVT_NOTIFY_SEND, task->task_id, (uint32_t) action);

    /* If target task is blocked waiting for a notification, wake it */
    if (task->state == RTOS_TASK_STATE_BLOCKED && task->blocked_on_type == RTOS_SYNC_TYPE_NOTIFICATION)
    {
        /* Clear the blocking sentinel */
        task->blocked_on      = NULL;
        task->blocked_on_type = RTOS_SYNC_TYPE_NONE;

        KLOGD(KEVT_NOTIFY_WAKE, task->task_id, task->notification_value);

        rtos_port_exit_critical();
        rtos_kernel_task_unblock(task);
        return RTOS_NOTIFY_OK;
    }

    rtos_port_exit_critical();
    return RTOS_NOTIFY_OK;
}

/**
 * @brief Simplified notify: increment the target's notification value.
 */
rtos_notify_status_t rtos_task_notify_give(rtos_task_handle_t task)
{
    return rtos_task_notify(task, 0, RTOS_NOTIFY_ACTION_INCREMENT);
}

/* =================== Receive-side API =================== */

/**
 * @brief Wait for a notification with bit-level control.
 */
rtos_notify_status_t rtos_task_notify_wait(uint32_t entry_clear_bits, uint32_t exit_clear_bits, uint32_t *value_out,
                                           rtos_tick_t timeout_ticks)
{
    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_NO_CURRENT_TASK, 0, 0);
        return RTOS_NOTIFY_ERR_INVALID;
    }

    /* Clear entry bits before checking pending state */
    current_task->notification_value &= ~entry_clear_bits;

    KLOGD(KEVT_NOTIFY_WAIT, current_task->task_id, current_task->notification_value);

    /* Fast path: notification already pending */
    if (current_task->notification_pending)
    {
        if (value_out != NULL)
        {
            *value_out = current_task->notification_value;
        }
        current_task->notification_value &= ~exit_clear_bits;
        current_task->notification_pending = 0;
        rtos_port_exit_critical();
        return RTOS_NOTIFY_OK;
    }

    /* No notification pending — check if we should wait */
    if (timeout_ticks == RTOS_NOTIFY_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_NOTIFY_ERR_TIMEOUT;
    }

    /*
     * Block: use self-pointer as blocked_on sentinel.
     * No separate kernel object exists, so the task's own address
     * serves as the "object" we're blocked on.
     */
    current_task->blocked_on      = current_task;
    current_task->blocked_on_type = RTOS_SYNC_TYPE_NOTIFICATION;

    KLOGD(KEVT_NOTIFY_BLOCK, current_task->task_id, (uint32_t) timeout_ticks);

    /* Block the task with timeout (same pattern as semaphore.c) */
    if (timeout_ticks == RTOS_NOTIFY_MAX_WAIT)
    {
        /* Infinite wait - block without delay timeout */
        current_task->state = RTOS_TASK_STATE_BLOCKED;
        rtos_port_exit_critical();
        rtos_yield();
    }
    else
    {
        /* Timed wait - use kernel block with delay */
        rtos_port_exit_critical();
        rtos_kernel_task_block(current_task, timeout_ticks);
    }

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    /* Check if we were woken by notify (blocked_on cleared) or timeout */
    if (current_task->blocked_on == current_task)
    {
        /* Still has sentinel = timeout occurred */
        current_task->blocked_on      = NULL;
        current_task->blocked_on_type = RTOS_SYNC_TYPE_NONE;
        rtos_port_exit_critical();
        KLOGD(KEVT_NOTIFY_TIMEOUT, current_task->task_id, 0);
        return RTOS_NOTIFY_ERR_TIMEOUT;
    }

    /* Woken by notification — read value, apply exit clear */
    if (value_out != NULL)
    {
        *value_out = current_task->notification_value;
    }
    current_task->notification_value &= ~exit_clear_bits;
    current_task->notification_pending = 0;

    rtos_port_exit_critical();
    return RTOS_NOTIFY_OK;
}

/**
 * @brief Take a notification (counting semaphore pattern).
 */
rtos_notify_status_t rtos_task_notify_take(bool clear_on_exit, rtos_tick_t timeout_ticks)
{
    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_NO_CURRENT_TASK, 0, 0);
        return RTOS_NOTIFY_ERR_INVALID;
    }

    /* Fast path: notification value > 0 */
    if (current_task->notification_value > 0)
    {
        if (clear_on_exit)
        {
            current_task->notification_value = 0;
        }
        else
        {
            current_task->notification_value--;
        }
        current_task->notification_pending = 0;
        rtos_port_exit_critical();
        return RTOS_NOTIFY_OK;
    }

    /* Value is 0 — check if we should wait */
    if (timeout_ticks == RTOS_NOTIFY_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_NOTIFY_ERR_TIMEOUT;
    }

    /* Block: self-pointer sentinel */
    current_task->blocked_on      = current_task;
    current_task->blocked_on_type = RTOS_SYNC_TYPE_NOTIFICATION;

    KLOGD(KEVT_NOTIFY_BLOCK, current_task->task_id, (uint32_t) timeout_ticks);

    /* Block the task (same pattern as semaphore.c) */
    if (timeout_ticks == RTOS_NOTIFY_MAX_WAIT)
    {
        current_task->state = RTOS_TASK_STATE_BLOCKED;
        rtos_port_exit_critical();
        rtos_yield();
    }
    else
    {
        rtos_port_exit_critical();
        rtos_kernel_task_block(current_task, timeout_ticks);
    }

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    /* Timeout check */
    if (current_task->blocked_on == current_task)
    {
        current_task->blocked_on      = NULL;
        current_task->blocked_on_type = RTOS_SYNC_TYPE_NONE;
        rtos_port_exit_critical();
        KLOGD(KEVT_NOTIFY_TIMEOUT, current_task->task_id, 0);
        return RTOS_NOTIFY_ERR_TIMEOUT;
    }

    /* Woken by notification — consume value */
    if (clear_on_exit)
    {
        current_task->notification_value = 0;
    }
    else
    {
        current_task->notification_value--;
    }
    current_task->notification_pending = 0;

    rtos_port_exit_critical();
    return RTOS_NOTIFY_OK;
}
