#include "KARTOS.h"
#include "kernel_priv.h"   /* rtos_kernel_task_unblock_from_isr */
#include "klog.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task.h"
#include "task_priv.h"
#include "test_hooks_priv.h"

/* Caller MUST hold a critical section. */
static bool task_notify_locked(rtos_task_handle_t task, uint32_t value, rtos_notify_action_t action,
                                rtos_notify_status_t *status_out)
{
    switch (action)
    {
        case RTOS_NOTIFY_ACTION_NONE:
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
            *status_out = RTOS_NOTIFY_ERR_INVALID;
            return false;
    }

    task->notification_pending = 1;

    RTOS_TEST_HOOK_FIRE(RTOS_HOOK_NOTIFY_SEND, {
        _ctx_.u.notify.task  = task;
        _ctx_.u.notify.value = task->notification_value;
    });

    *status_out = RTOS_NOTIFY_OK;

    if (task->state == RTOS_TASK_STATE_BLOCKED && task->blocked_on_type == RTOS_SYNC_TYPE_NOTIFICATION)
    {
        task->blocked_on      = NULL;
        task->blocked_on_type = RTOS_SYNC_TYPE_NONE;
        return true;
    }
    return false;
}

rtos_notify_status_t rtos_task_notify(rtos_task_handle_t task, uint32_t value, rtos_notify_action_t action)
{
    if (task == NULL)
    {
        return RTOS_NOTIFY_ERR_INVALID;
    }

    rtos_port_enter_critical();

    rtos_notify_status_t status;
    bool                 needs_wake = task_notify_locked(task, value, action, &status);

    if (status != RTOS_NOTIFY_OK)
    {
        rtos_port_exit_critical();
        return status;
    }

    KLOGD("Notify", "NotifySend id=%u act=%u", task->task_id, (uint32_t) action);

    if (needs_wake)
    {
        KLOGD("Notify", "NotifyWake id=%u val=%u", task->task_id, task->notification_value);
        rtos_port_exit_critical();
        rtos_kernel_task_unblock(task);
        return RTOS_NOTIFY_OK;
    }

    rtos_port_exit_critical();
    return RTOS_NOTIFY_OK;
}

rtos_notify_status_t rtos_task_notify_give(rtos_task_handle_t task)
{
    return rtos_task_notify(task, 0, RTOS_NOTIFY_ACTION_INCREMENT);
}

rtos_notify_status_t rtos_task_notify_from_isr(rtos_task_handle_t task, uint32_t value, rtos_notify_action_t action)
{
    if (task == NULL)
    {
        return RTOS_NOTIFY_ERR_INVALID;
    }

    uint32_t             saved      = rtos_port_enter_critical_from_isr();
    rtos_notify_status_t status;
    bool                 needs_wake = task_notify_locked(task, value, action, &status);

    if (status == RTOS_NOTIFY_OK && needs_wake)
    {
        rtos_kernel_task_unblock_from_isr(task);
    }

    rtos_port_exit_critical_from_isr(saved);
    return status;
}

rtos_notify_status_t rtos_task_notify_give_from_isr(rtos_task_handle_t task)
{
    return rtos_task_notify_from_isr(task, 0, RTOS_NOTIFY_ACTION_INCREMENT);
}

rtos_notify_status_t rtos_task_notify_wait(uint32_t entry_clear_bits, uint32_t exit_clear_bits, uint32_t *value_out,
                                           rtos_tick_t timeout_ticks)
{
    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE("Notify", "NoCurrentTask");
        return RTOS_NOTIFY_ERR_INVALID;
    }

    if (!current_task->notification_pending)
    {
        current_task->notification_value &= ~entry_clear_bits;
    }

    KLOGD("Notify", "NotifyWait id=%u val=%u", current_task->task_id, current_task->notification_value);

    if (current_task->notification_pending)
    {
        if (value_out != NULL)
        {
            *value_out = current_task->notification_value;
        }
        current_task->notification_value &= ~exit_clear_bits;
        current_task->notification_pending = 0;
        RTOS_TEST_HOOK_FIRE(RTOS_HOOK_NOTIFY_RECEIVE, {
            _ctx_.u.notify.task  = current_task;
            _ctx_.u.notify.value = current_task->notification_value;
        });
        rtos_port_exit_critical();
        return RTOS_NOTIFY_OK;
    }

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

    KLOGD("Notify", "NotifyBlock id=%u timeout=%u", current_task->task_id, (uint32_t) timeout_ticks);

    /* Block atomically with the self-sentinel set above: a notify racing us
     * can never see the sentinel while we are still RUNNING and drop the
     * wake. delay 0 = infinite block. */
    rtos_kernel_task_block_locked(current_task, (timeout_ticks == RTOS_NOTIFY_MAX_WAIT) ? 0U : timeout_ticks);
    rtos_port_exit_critical();
    rtos_yield();

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    /* Check if we were woken by notify (blocked_on cleared) or timeout */
    if (current_task->blocked_on == current_task)
    {
        /* Still has sentinel = timeout occurred */
        current_task->blocked_on      = NULL;
        current_task->blocked_on_type = RTOS_SYNC_TYPE_NONE;
        rtos_port_exit_critical();
        KLOGD("Notify", "NotifyTimeout id=%u", current_task->task_id);
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

rtos_notify_status_t rtos_task_notify_take(bool clear_on_exit, rtos_tick_t timeout_ticks)
{
    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE("Notify", "NoCurrentTask");
        return RTOS_NOTIFY_ERR_INVALID;
    }

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

    if (timeout_ticks == RTOS_NOTIFY_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_NOTIFY_ERR_TIMEOUT;
    }

    current_task->blocked_on      = current_task;
    current_task->blocked_on_type = RTOS_SYNC_TYPE_NOTIFICATION;

    KLOGD("Notify", "NotifyBlock id=%u timeout=%u", current_task->task_id, (uint32_t) timeout_ticks);

    /* Block atomically with the self-sentinel set above: a notify racing us
     * can never see the sentinel while we are still RUNNING and drop the
     * wake. delay 0 = infinite block. */
    rtos_kernel_task_block_locked(current_task, (timeout_ticks == RTOS_NOTIFY_MAX_WAIT) ? 0U : timeout_ticks);
    rtos_port_exit_critical();
    rtos_yield();

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    if (current_task->blocked_on == current_task)
    {
        current_task->blocked_on      = NULL;
        current_task->blocked_on_type = RTOS_SYNC_TYPE_NONE;
        rtos_port_exit_critical();
        KLOGD("Notify", "NotifyTimeout id=%u", current_task->task_id);
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
