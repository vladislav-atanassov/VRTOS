/*******************************************************************************
 * File: src/sync/mutex/mutex.c
 * Description: Mutex Implementation with Priority Inheritance
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "mutex.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "log.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"

#include <string.h>

/**
 * @file mutex.c
 * @brief Mutex Implementation with Priority Inheritance
 *
 * Implements mutexes with:
 * - Priority Inheritance Protocol (PIP) to prevent priority inversion
 * - Recursive locking (same task can lock multiple times)
 * - Timeout support
 * - Priority-ordered wait queue
 */

/* =================== Internal Helper Functions =================== */

/**
 * @brief Add task to mutex wait queue (priority-ordered, highest first)
 */
static void mutex_add_to_waiting_list(rtos_mutex_t *m, rtos_tcb_t *task)
{
    task->next_waiting    = NULL;
    task->blocked_on      = m;
    task->blocked_on_type = RTOS_SYNC_TYPE_MUTEX;

    if (m->waiting_list == NULL)
    {
        /* First waiter */
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
        /* Insert at head */
        task->next_waiting = m->waiting_list;
        m->waiting_list    = task;
    }
    else
    {
        /* Insert after prev */
        task->next_waiting = current;
        prev->next_waiting = task;
    }
}

/**
 * @brief Remove task from mutex wait queue
 */
static void mutex_remove_from_waiting_list(rtos_mutex_t *m, rtos_tcb_t *task)
{
    if (m->waiting_list == NULL || task == NULL)
    {
        return;
    }

    if (m->waiting_list == task)
    {
        /* Task is at head */
        m->waiting_list = task->next_waiting;
    }
    else
    {
        /* Find and remove task */
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

/**
 * @brief Get and remove highest priority waiter
 */
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

/**
 * @brief Apply priority inheritance - boost owner's priority if needed
 */
static void mutex_apply_priority_inheritance(rtos_mutex_t *m, rtos_tcb_t *waiter)
{
    /*
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
            log_debug("PIP: Boosting '%s' (%d->%d) due to waiter '%s'",
                      target_task->name ? target_task->name : "unnamed", target_task->priority,
                      boost_prio, current_task->name ? current_task->name : "unnamed");

            target_task->priority = boost_prio;
        }
        else
        {
            /* Target already has equal/higher priority, verify chain propagation might still be
               needed if we are just matching it, but usually we can stop if we didn't boost.
               However, if target is blocked on another mutex, we must check if THAT mutex's owner
               needs boosting to match our boost_prio. */
            if (target_task->priority > boost_prio)
            {
                /* Target is already higher than us, so we don't boost it.
                   But we shouldn't necessarily reduce our boost req.
                   Actually, if target is higher, it will run eventually (or is blocked).
                   If blocked, we should check if IT is blocked on something.
                   But if it's blocked, and its priority is HIGH, then the next owner needs to be at
                   least that HIGH. So we should continue with target's priority? Standard PIP:
                   Inherit the HIGHEST of waiting tasks. Our 'boost_prio' is the priority of the
                   task that just started waiting. If target is already higher, we don't change it.
                   But we should continue traversing if target is blocked, using target's priority?
                   Yes, effective priority. */
                boost_prio = target_task->priority;
            }
        }

        /* Check if target is blocked on another MUTEX */
        if (target_task->state == RTOS_TASK_STATE_BLOCKED &&
            target_task->blocked_on_type == RTOS_SYNC_TYPE_MUTEX && target_task->blocked_on != NULL)
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
        log_error("PIP: Max depth reached, potential deadlock detected!");
    }
}

/**
 * @brief Restore owner's original priority
 */
static void mutex_restore_priority(rtos_tcb_t *task)
{
    if (task == NULL)
    {
        return;
    }

    if (task->priority != task->base_priority)
    {
        log_debug("Priority restoration: restoring '%s' from %d to %d",
                  task->name ? task->name : "unnamed", task->priority, task->base_priority);
        task->priority = task->base_priority;
    }
}

/* =================== Public API Implementation =================== */

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

    log_debug("Mutex initialized");
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
        log_error("Mutex lock called with no current task!");
        return RTOS_MUTEX_ERR_INVALID;
    }

    /* Fast path: mutex is free */
    if (m->owner == NULL)
    {
        m->owner      = current_task;
        m->lock_count = 1;
        rtos_port_exit_critical();
        log_debug("Mutex acquired by '%s'", current_task->name ? current_task->name : "unnamed");
        return RTOS_MUTEX_OK;
    }

    /* Check for recursive lock (same task) */
    if (m->owner == current_task)
    {
        if (m->lock_count < 255)
        {
            m->lock_count++;
            rtos_port_exit_critical();
            log_debug("Mutex recursive lock by '%s' (count=%d)",
                      current_task->name ? current_task->name : "unnamed", m->lock_count);
            return RTOS_MUTEX_OK;
        }
        else
        {
            rtos_port_exit_critical();
            log_error("Mutex max recursion reached!");
            return RTOS_MUTEX_ERR_GENERAL;
        }
    }

    /* Mutex is held by another task - check if we should wait */
    if (timeout_ticks == RTOS_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_MUTEX_ERR_TIMEOUT;
    }

    /* Apply priority inheritance */
    mutex_apply_priority_inheritance(m, current_task);

    /* Add to waiting list (priority-ordered) */
    mutex_add_to_waiting_list(m, current_task);

    log_debug("Task '%s' blocking on mutex (timeout=%lu)",
              current_task->name ? current_task->name : "unnamed", (unsigned long) timeout_ticks);

    /* Block the task with timeout */
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
        log_debug("Task '%s' mutex lock timed out",
                  current_task->name ? current_task->name : "unnamed");
        return RTOS_MUTEX_ERR_TIMEOUT;
    }

    /* Successfully acquired mutex */
    rtos_port_exit_critical();
    log_debug("Task '%s' mutex acquired after wait",
              current_task->name ? current_task->name : "unnamed");
    return RTOS_MUTEX_OK;
}

/**
 * @brief Unlock/release a mutex
 */
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
        log_error("Mutex unlock by non-owner! owner='%s', caller='%s'",
                  m->owner ? (m->owner->name ? m->owner->name : "unnamed") : "NULL",
                  current_task ? (current_task->name ? current_task->name : "unnamed") : "NULL");
        return RTOS_MUTEX_ERR_INVALID;
    }

    /* Handle recursive unlock */
    if (m->lock_count > 1)
    {
        m->lock_count--;
        rtos_port_exit_critical();
        log_debug("Mutex recursive unlock by '%s' (count=%d)",
                  current_task->name ? current_task->name : "unnamed", m->lock_count);
        return RTOS_MUTEX_OK;
    }

    /* Full unlock - restore priority first */
    mutex_restore_priority(current_task);

    /* Transfer ownership to highest priority waiter */
    rtos_tcb_t *waiter = mutex_pop_highest_priority_waiter(m);
    if (waiter != NULL)
    {
        /* Transfer mutex to waiter */
        m->owner      = waiter;
        m->lock_count = 1;

        log_debug("Mutex transferred to '%s'", waiter->name ? waiter->name : "unnamed");

        rtos_port_exit_critical();

        /* Unblock the waiting task */
        rtos_kernel_task_unblock(waiter);
        return RTOS_MUTEX_OK;
    }

    /* No waiters - mark mutex as free */
    m->owner      = NULL;
    m->lock_count = 0;

    rtos_port_exit_critical();

    log_debug("Mutex released by '%s'", current_task->name ? current_task->name : "unnamed");
    return RTOS_MUTEX_OK;
}
