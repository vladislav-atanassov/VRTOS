#include "mutex.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "klog.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"

#include <string.h>

static void mutex_add_to_waiting_list(rtos_mutex_t *m, rtos_tcb_t *task)
{
    task->next_waiting    = NULL;
    task->blocked_on      = m;
    task->blocked_on_type = RTOS_SYNC_TYPE_MUTEX;

    if (m->waiting_list == NULL)
    {
        m->waiting_list = task;
        return;
    }

    /* Insert in priority order (highest priority at head) */
    rtos_tcb_t *current = m->waiting_list;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && current->priority >= task->priority)
    {
        prev    = current;
        current = current->next_waiting;
    }

    if (prev == NULL)
    {
        task->next_waiting = m->waiting_list;
        m->waiting_list    = task;
    }
    else
    {
        task->next_waiting = current;
        prev->next_waiting = task;
    }
}

static void mutex_remove_from_waiting_list(rtos_mutex_t *m, rtos_tcb_t *task)
{
    if (m->waiting_list == NULL || task == NULL)
    {
        return;
    }

    if (m->waiting_list == task)
    {
        m->waiting_list = task->next_waiting;
    }
    else
    {
        rtos_tcb_t *current = m->waiting_list;
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

static rtos_tcb_t *mutex_pop_highest_priority_waiter(rtos_mutex_t *m)
{
    if (m->waiting_list == NULL)
    {
        return NULL;
    }

    /* Head is always highest priority due to ordered insertion */
    rtos_tcb_t *task      = m->waiting_list;
    m->waiting_list       = task->next_waiting;
    task->next_waiting    = NULL;
    task->blocked_on      = NULL;
    task->blocked_on_type = RTOS_SYNC_TYPE_NONE;

    return task;
}

static void mutex_apply_priority_inheritance(rtos_mutex_t *m, rtos_tcb_t *waiter)
{
    /**
     * Transitive Priority Inheritance:
     * Walk the chain of mutex owners and boost them if necessary.
     */
    rtos_tcb_t     *current_task = waiter;
    rtos_tcb_t     *target_task  = m->owner;
    rtos_priority_t boost_prio   = waiter->priority;

    /* Safety counter to prevent infinite loops (deadlocks) */
    uint32_t       safety_ctr = 0;
    const uint32_t max_depth  = 16;

    while (target_task != NULL && safety_ctr < max_depth)
    {
        /* If target has lower priority than current boost priority, boost it */
        if (target_task->priority < boost_prio)
        {
            KLOGD(KEVT_MUTEX_PIP_BOOST, target_task->task_id, boost_prio);

            /*
             * If the boosted task is currently in the READY list it is stored
             * in the bucket indexed by its old priority.  Changing the priority
             * field alone would leave the task in the wrong bucket, causing the
             * scheduler to either miss it or to corrupt the list on the next
             * remove.  Re-insert it at the new priority level.
             */
            if (target_task->state == RTOS_TASK_STATE_READY)
            {
                rtos_scheduler_remove_from_ready_list(target_task);
                target_task->priority = boost_prio;
                rtos_scheduler_add_to_ready_list(target_task);
            }
            else
            {
                target_task->priority = boost_prio;
            }
        }
        else
        {
            /* Target already at/above boost_prio. Carry its effective priority
             * forward so we do not under-boost further owners in the chain. */
            if (target_task->priority > boost_prio)
            {
                boost_prio = target_task->priority;
            }
        }

        /* Check if target is blocked on another MUTEX */
        if (target_task->state == RTOS_TASK_STATE_BLOCKED && target_task->blocked_on_type == RTOS_SYNC_TYPE_MUTEX &&
            target_task->blocked_on != NULL)
        {
            rtos_mutex_t *next_mutex = (rtos_mutex_t *) target_task->blocked_on;
            current_task             = target_task;
            target_task              = next_mutex->owner;
        }
        else
        {
            /* Not blocked on a mutex, end of chain */
            break;
        }

        safety_ctr++;
    }

    if (safety_ctr >= max_depth)
    {
        KLOGE(KEVT_MUTEX_DEADLOCK, safety_ctr, max_depth);
    }
}

static void mutex_restore_priority(rtos_tcb_t *task)
{
    if (task == NULL)
    {
        return;
    }

    if (task->priority != task->base_priority)
    {
        KLOGD(KEVT_MUTEX_PIP_RESTORE, task->task_id, task->base_priority);
        task->priority = task->base_priority;
    }
}

/**
 * @brief Initialize a mutex
 */
rtos_mutex_status_t rtos_mutex_init(rtos_mutex_t *m)
{
    if (m == NULL)
    {
        return RTOS_MUTEX_ERR_INVALID;
    }

    rtos_port_enter_critical();

    m->owner        = NULL;
    m->waiting_list = NULL;
    m->lock_count   = 0;

    rtos_port_exit_critical();

    KLOGD(KEVT_MUTEX_INIT, 0, 0);
    return RTOS_MUTEX_OK;
}

/**
 * @brief Lock/acquire a mutex
 */
rtos_mutex_status_t rtos_mutex_lock(rtos_mutex_t *m, rtos_tick_t timeout_ticks)
{
    if (m == NULL)
    {
        return RTOS_MUTEX_ERR_INVALID;
    }

    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_NO_CURRENT_TASK, 0, 0);
        return RTOS_MUTEX_ERR_INVALID;
    }

    /* Fast path: mutex is free */
    if (m->owner == NULL)
    {
        m->owner      = current_task;
        m->lock_count = 1;
        rtos_port_exit_critical();
        KLOGD(KEVT_MUTEX_LOCK, current_task->task_id, 0);
        return RTOS_MUTEX_OK;
    }

    if (m->owner == current_task)
    {
        if (m->lock_count < 255)
        {
            m->lock_count++;
            rtos_port_exit_critical();
            KLOGD(KEVT_MUTEX_RECURSIVE, current_task->task_id, m->lock_count);
            return RTOS_MUTEX_OK;
        }
        else
        {
            rtos_port_exit_critical();
            KLOGE(KEVT_MUTEX_MAX_RECURSION, current_task->task_id, 0);
            return RTOS_MUTEX_ERR_GENERAL;
        }
    }

    if (timeout_ticks == RTOS_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_MUTEX_ERR_TIMEOUT;
    }

    mutex_apply_priority_inheritance(m, current_task);
    mutex_add_to_waiting_list(m, current_task);

    KLOGD(KEVT_MUTEX_BLOCK, current_task->task_id, (uint32_t) timeout_ticks);

    if (timeout_ticks == RTOS_MAX_WAIT)
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

    /* Check if we were woken by unlock (blocked_on cleared) or timeout */
    if (current_task->blocked_on == m)
    {
        /* Still on waiting list = timeout occurred */
        mutex_remove_from_waiting_list(m, current_task);
        rtos_port_exit_critical();
        KLOGD(KEVT_MUTEX_TIMEOUT, current_task->task_id, 0);
        return RTOS_MUTEX_ERR_TIMEOUT;
    }

    rtos_port_exit_critical();
    KLOGD(KEVT_MUTEX_LOCK, current_task->task_id, 1);
    return RTOS_MUTEX_OK;
}

/**
 * @brief Unlock/release a mutex
 */
void rtos_mutex_remove_task_from_wait(void *mutex_ptr, rtos_tcb_t *task)
{
    mutex_remove_from_waiting_list((rtos_mutex_t *) mutex_ptr, task);
}

rtos_mutex_status_t rtos_mutex_unlock(rtos_mutex_t *m)
{
    if (m == NULL)
    {
        return RTOS_MUTEX_ERR_INVALID;
    }

    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();

    /* Only owner can unlock */
    if (m->owner != current_task)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_MUTEX_UNLOCK, m->owner ? m->owner->task_id : 0xFF, current_task ? current_task->task_id : 0xFF);
        return RTOS_MUTEX_ERR_INVALID;
    }

    if (m->lock_count > 1)
    {
        m->lock_count--;
        rtos_port_exit_critical();
        KLOGD(KEVT_MUTEX_RECURSIVE, current_task->task_id, m->lock_count);
        return RTOS_MUTEX_OK;
    }

    /* Full unlock - restore priority first */
    mutex_restore_priority(current_task);

    /* Transfer ownership to highest priority waiter */
    rtos_tcb_t *waiter = mutex_pop_highest_priority_waiter(m);
    if (waiter != NULL)
    {
        m->owner      = waiter;
        m->lock_count = 1;

        KLOGD(KEVT_MUTEX_UNLOCK, waiter->task_id, 0);

        rtos_port_exit_critical();

        rtos_kernel_task_unblock(waiter);
        return RTOS_MUTEX_OK;
    }

    m->owner      = NULL;
    m->lock_count = 0;

    rtos_port_exit_critical();

    KLOGD(KEVT_MUTEX_UNLOCK, current_task->task_id, 0);
    return RTOS_MUTEX_OK;
}
