/*******************************************************************************
 * File: src/sync/event_group/event_group.c
 * Description: Event Group (Event Flags) implementation
 ******************************************************************************/

#include "event_group.h"

#include "VRTOS.h"
#include "klog.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"

/* =================== Static Helpers =================== */

/**
 * @brief Check if the wait condition is met
 */
static bool eg_condition_met(uint32_t current_bits, uint32_t wait_bits, uint8_t wait_all)
{
    if (wait_all)
    {
        return (current_bits & wait_bits) == wait_bits;
    }
    else
    {
        return (current_bits & wait_bits) != 0;
    }
}

/**
 * @brief Insert a task into the event group's priority-ordered waiting list
 */
static void eg_add_to_waiting_list(rtos_event_group_t *eg, rtos_tcb_t *task, uint32_t bits_to_wait, uint8_t wait_all,
                                   uint8_t clear_on_exit)
{
    task->next_waiting      = NULL;
    task->blocked_on        = eg;
    task->blocked_on_type   = RTOS_SYNC_TYPE_EVENT_GROUP;
    task->event_wait_bits   = bits_to_wait;
    task->event_wait_all    = wait_all;
    task->event_clear_on_exit = clear_on_exit;

    if (eg->waiting_list == NULL)
    {
        eg->waiting_list = task;
        return;
    }

    /* Insert in priority order (highest priority at head) */
    rtos_tcb_t *current = eg->waiting_list;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && current->priority >= task->priority)
    {
        prev    = current;
        current = current->next_waiting;
    }

    if (prev == NULL)
    {
        task->next_waiting = eg->waiting_list;
        eg->waiting_list   = task;
    }
    else
    {
        task->next_waiting = current;
        prev->next_waiting = task;
    }
}

/**
 * @brief Remove a task from the event group's waiting list
 */
static void eg_remove_from_waiting_list(rtos_event_group_t *eg, rtos_tcb_t *task)
{
    if (eg->waiting_list == NULL || task == NULL)
    {
        return;
    }

    if (eg->waiting_list == task)
    {
        eg->waiting_list = task->next_waiting;
    }
    else
    {
        rtos_tcb_t *current = eg->waiting_list;
        while (current->next_waiting != NULL && current->next_waiting != task)
        {
            current = current->next_waiting;
        }
        if (current->next_waiting == task)
        {
            current->next_waiting = task->next_waiting;
        }
    }

    task->next_waiting    = NULL;
    task->blocked_on      = NULL;
    task->blocked_on_type = RTOS_SYNC_TYPE_NONE;
}

/**
 * @brief Core set_bits logic shared between task and ISR variants
 *
 * Must be called inside a critical section. Builds a wake list of tasks
 * whose conditions are satisfied, applies deferred clear-on-exit, and
 * returns the wake list head. Caller is responsible for calling
 * rtos_kernel_task_unblock() on each task in the wake list after
 * exiting the critical section.
 */
static rtos_tcb_t *eg_set_bits_internal(rtos_event_group_t *eg, uint32_t bits_to_set)
{
    eg->bits |= bits_to_set;

    KLOGD(KEVT_EG_SET, bits_to_set, eg->bits);

    rtos_tcb_t *wake_list     = NULL;
    uint32_t    bits_to_clear = 0;

    rtos_tcb_t *current = eg->waiting_list;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL)
    {
        rtos_tcb_t *next           = current->next_waiting;
        uint32_t    orig_wait_bits = current->event_wait_bits;

        if (eg_condition_met(eg->bits, orig_wait_bits, current->event_wait_all))
        {
            /* Save bits snapshot for bits_out return value */
            current->event_wait_bits = eg->bits;

            if (current->event_clear_on_exit)
            {
                bits_to_clear |= orig_wait_bits;
            }

            /* Remove from waiting list */
            if (prev == NULL)
            {
                eg->waiting_list = next;
            }
            else
            {
                prev->next_waiting = next;
            }

            current->blocked_on      = NULL;
            current->blocked_on_type = RTOS_SYNC_TYPE_NONE;

            /* Chain onto wake list */
            current->next_waiting = wake_list;
            wake_list             = current;

            KLOGD(KEVT_EG_WAKE, current->task_id, eg->bits);
            /* prev stays the same — we removed current */
        }
        else
        {
            prev = current;
        }

        current = next;
    }

    /* Deferred clear: apply after all waiters have been checked */
    eg->bits &= ~bits_to_clear;

    return wake_list;
}

/* =================== Public API =================== */

rtos_eg_status_t rtos_event_group_init(rtos_event_group_t *eg)
{
    if (eg == NULL)
    {
        return RTOS_EG_ERR_INVALID;
    }

    rtos_port_enter_critical();

    eg->bits         = 0;
    eg->waiting_list = NULL;

    rtos_port_exit_critical();

    KLOGD(KEVT_EG_INIT, 0, 0);

    return RTOS_EG_OK;
}

rtos_eg_status_t rtos_event_group_wait_bits(rtos_event_group_t *eg, uint32_t bits_to_wait, bool wait_all,
                                            bool clear_on_exit, uint32_t *bits_out, rtos_tick_t timeout_ticks)
{
    if (eg == NULL || bits_to_wait == 0)
    {
        return RTOS_EG_ERR_INVALID;
    }

    rtos_port_enter_critical();

    KLOGD(KEVT_EG_WAIT, bits_to_wait, eg->bits);

    /* Fast path: condition already met */
    if (eg_condition_met(eg->bits, bits_to_wait, (uint8_t) wait_all))
    {
        if (bits_out != NULL)
        {
            *bits_out = eg->bits;
        }
        if (clear_on_exit)
        {
            eg->bits &= ~bits_to_wait;
        }
        rtos_port_exit_critical();
        return RTOS_EG_OK;
    }

    if (timeout_ticks == RTOS_EG_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_EG_ERR_TIMEOUT;
    }

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_NO_CURRENT_TASK, 0, 0);
        return RTOS_EG_ERR_INVALID;
    }

    eg_add_to_waiting_list(eg, current_task, bits_to_wait, (uint8_t) wait_all, (uint8_t) clear_on_exit);

    KLOGD(KEVT_EG_BLOCK, current_task->task_id, (uint32_t) timeout_ticks);

    if (timeout_ticks == RTOS_EG_MAX_WAIT)
    {
        /* Infinite wait — block without delay timeout */
        current_task->state = RTOS_TASK_STATE_BLOCKED;
        rtos_scheduler_remove_from_ready_list(current_task);
        rtos_port_exit_critical();
        rtos_yield();
    }
    else
    {
        /* Timed wait — use kernel block with delay */
        rtos_port_exit_critical();
        rtos_kernel_task_block(current_task, timeout_ticks);
    }

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    /* Check if we were woken by set_bits (blocked_on cleared) or timeout */
    if (current_task->blocked_on == eg)
    {
        /* Still on waiting list = timeout occurred */
        eg_remove_from_waiting_list(eg, current_task);
        rtos_port_exit_critical();
        KLOGD(KEVT_EG_TIMEOUT, current_task->task_id, 0);
        return RTOS_EG_ERR_TIMEOUT;
    }

    /* Woken by set_bits — event_wait_bits holds the bits snapshot */
    if (bits_out != NULL)
    {
        *bits_out = current_task->event_wait_bits;
    }

    rtos_port_exit_critical();
    return RTOS_EG_OK;
}

rtos_eg_status_t rtos_event_group_set_bits(rtos_event_group_t *eg, uint32_t bits_to_set)
{
    if (eg == NULL)
    {
        return RTOS_EG_ERR_INVALID;
    }

    rtos_port_enter_critical();

    rtos_tcb_t *wake_list = eg_set_bits_internal(eg, bits_to_set);

    rtos_port_exit_critical();

    /* Unblock all woken tasks outside critical section */
    while (wake_list != NULL)
    {
        rtos_tcb_t *task = wake_list;
        wake_list        = task->next_waiting;
        task->next_waiting = NULL;
        rtos_kernel_task_unblock(task);
    }

    return RTOS_EG_OK;
}

rtos_eg_status_t rtos_event_group_set_bits_from_isr(rtos_event_group_t *eg, uint32_t bits_to_set)
{
    if (eg == NULL)
    {
        return RTOS_EG_ERR_INVALID;
    }

    uint32_t saved = rtos_port_enter_critical_from_isr();

    rtos_tcb_t *wake_list = eg_set_bits_internal(eg, bits_to_set);

    rtos_port_exit_critical_from_isr(saved);

    /* Unblock all woken tasks outside ISR critical section */
    while (wake_list != NULL)
    {
        rtos_tcb_t *task = wake_list;
        wake_list        = task->next_waiting;
        task->next_waiting = NULL;
        rtos_kernel_task_unblock(task);
    }

    return RTOS_EG_OK;
}

rtos_eg_status_t rtos_event_group_clear_bits(rtos_event_group_t *eg, uint32_t bits_to_clear)
{
    if (eg == NULL)
    {
        return RTOS_EG_ERR_INVALID;
    }

    rtos_port_enter_critical();

    eg->bits &= ~bits_to_clear;

    KLOGD(KEVT_EG_CLEAR, bits_to_clear, eg->bits);

    rtos_port_exit_critical();

    return RTOS_EG_OK;
}

uint32_t rtos_event_group_get_bits(rtos_event_group_t *eg)
{
    if (eg == NULL)
    {
        return 0;
    }

    rtos_port_enter_critical();
    uint32_t bits = eg->bits;
    rtos_port_exit_critical();

    return bits;
}

/* =================== Task-Delete Cleanup =================== */

void rtos_event_group_remove_task_from_wait(void *eg_ptr, rtos_tcb_t *task)
{
    eg_remove_from_waiting_list((rtos_event_group_t *) eg_ptr, task);
}
