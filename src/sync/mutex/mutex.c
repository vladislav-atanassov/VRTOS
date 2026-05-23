#include "mutex.h"

#include "KARTOS.h"
#include "config.h"      /* RTOS_MAX_TASKS — bound for chain-walk loops */
#include "kernel_priv.h"
#include "klog.h"
#include "rtos_assert.h"
#include "rtos_hooks.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task.h"
#include "task_priv.h"
#include "test_hooks_priv.h"

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

/* Walk the wait-for graph starting at `m->owner` and return true if the chain
 * leads back to `waiter` — i.e. granting `waiter -> m` would close a cycle.
 *
 * Pre-condition (maintained by this very check rejecting cycle-closing locks):
 * the graph before adding the new edge is acyclic, so the walk is bounded by
 * the number of tasks. RTOS_MAX_TASKS+1 is a defensive cap: a correct kernel
 * never hits it; if a bug introduced an undetected cycle earlier, this still
 * terminates and reports deadlock instead of hanging the caller. */
static bool mutex_would_deadlock(const rtos_mutex_t *m, const rtos_tcb_t *waiter)
{
    const rtos_tcb_t *cur   = m->owner;
    uint32_t          guard = 0U;

    while (cur != NULL && guard <= RTOS_MAX_TASKS)
    {
        if (cur == waiter)
        {
            return true;
        }
        if (cur->state != RTOS_TASK_STATE_BLOCKED ||
            cur->blocked_on_type != RTOS_SYNC_TYPE_MUTEX ||
            cur->blocked_on == NULL)
        {
            return false;
        }
        cur = ((const rtos_mutex_t *) cur->blocked_on)->owner;
        guard++;
    }
    /* Guard tripped — treat as suspected cycle. */
    return cur != NULL;
}

static void mutex_apply_priority_inheritance(rtos_mutex_t *m, rtos_tcb_t *waiter)
{
    /* Transitive priority inheritance: walk the chain of mutex owners and boost
     * each one whose effective priority is below the waiter's. The walk is
     * bounded by the cycle-free invariant; RTOS_MAX_TASKS is a belt-and-
     * suspenders cap so a corrupted graph never spins forever here. */
    rtos_tcb_t     *target_task = m->owner;
    rtos_priority_t boost_prio  = waiter->priority;
    uint32_t        guard       = 0U;

    while (target_task != NULL && guard <= RTOS_MAX_TASKS)
    {
        if (target_task->priority < boost_prio)
        {
            KLOGD("Mutex", "MutexBoost id=%u prio=%u", target_task->task_id, boost_prio);

            /* If the boosted task is currently in the READY list it is stored
             * in the bucket indexed by its old priority. Changing the priority
             * field alone would leave the task in the wrong bucket, causing the
             * scheduler to either miss it or to corrupt the list on the next
             * remove. Re-insert it at the new priority level. */
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
            RTOS_TEST_HOOK_FIRE(RTOS_HOOK_MUTEX_BOOST, {
                _ctx_.u.mutex_boost.owner    = target_task;
                _ctx_.u.mutex_boost.new_prio = boost_prio;
            });
        }
        else if (target_task->priority > boost_prio)
        {
            /* Carry the higher effective priority forward so we do not
             * under-boost further owners in the chain. */
            boost_prio = target_task->priority;
        }

        if (target_task->state == RTOS_TASK_STATE_BLOCKED && target_task->blocked_on_type == RTOS_SYNC_TYPE_MUTEX &&
            target_task->blocked_on != NULL)
        {
            target_task = ((rtos_mutex_t *) target_task->blocked_on)->owner;
        }
        else
        {
            break;
        }

        guard++;
    }
}

static void mutex_remove_from_held_list(rtos_tcb_t *task, rtos_mutex_t *m)
{
    if (task->held_mutex_list == m)
    {
        task->held_mutex_list = m->next_held;
    }
    else
    {
        rtos_mutex_t *cur = task->held_mutex_list;
        while (cur != NULL && cur->next_held != m)
        {
            cur = cur->next_held;
        }
        if (cur != NULL)
        {
            cur->next_held = m->next_held;
        }
    }
    m->next_held = NULL;
}

static void mutex_restore_priority(rtos_tcb_t *task)
{
    if (task == NULL)
    {
        return;
    }

    rtos_priority_t max_prio = task->base_priority;

    for (rtos_mutex_t *m = task->held_mutex_list; m != NULL; m = m->next_held)
    {
        if (m->waiting_list != NULL && m->waiting_list->priority > max_prio)
        {
            max_prio = m->waiting_list->priority;
        }
    }

    if (task->priority != max_prio)
    {
        KLOGD("Mutex", "MutexRestore id=%u prio=%u", task->task_id, max_prio);
        task->priority = max_prio;
        RTOS_TEST_HOOK_FIRE(RTOS_HOOK_MUTEX_RESTORE, {
            _ctx_.u.mutex_restore.owner         = task;
            _ctx_.u.mutex_restore.restored_prio = max_prio;
        });
    }
}

rtos_mutex_status_t rtos_mutex_init(rtos_mutex_t *m)
{
    if (m == NULL)
    {
        return RTOS_MUTEX_ERR_INVALID;
    }

    rtos_port_enter_critical();

    m->owner        = NULL;
    m->waiting_list = NULL;
    m->next_held    = NULL;
    m->lock_count   = 0;

    rtos_port_exit_critical();

    KLOGD("Mutex", "MutexInit");
    return RTOS_MUTEX_OK;
}

rtos_mutex_status_t rtos_mutex_lock(rtos_mutex_t *m, rtos_tick_t timeout_ticks)
{
    RTOS_ASSERT_PARAM(m != NULL);
    if (m == NULL)
    {
        return RTOS_MUTEX_ERR_INVALID;
    }

    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE("Mutex", "NoCurrentTask");
        return RTOS_MUTEX_ERR_INVALID;
    }

    if (m->owner == NULL)
    {
        m->owner                      = current_task;
        m->lock_count                 = 1;
        m->next_held                  = current_task->held_mutex_list;
        current_task->held_mutex_list = m;
        rtos_port_exit_critical();
        KLOGD("Mutex", "MutexLock id=%u", current_task->task_id);
        RTOS_TEST_HOOK_FIRE(RTOS_HOOK_MUTEX_LOCK_EXIT, {
            _ctx_.u.mutex_lock.mutex  = m;
            _ctx_.u.mutex_lock.caller = current_task;
            _ctx_.u.mutex_lock.status = RTOS_MUTEX_OK;
        });
        return RTOS_MUTEX_OK;
    }

    if (m->owner == current_task)
    {
        if (m->lock_count < 255)
        {
            m->lock_count++;
            rtos_port_exit_critical();
            KLOGD("Mutex", "MutexRecursive id=%u count=%u", current_task->task_id, m->lock_count);
            return RTOS_MUTEX_OK;
        }
        else
        {
            rtos_port_exit_critical();
            KLOGE("Mutex", "MutexMaxRecursion id=%u", current_task->task_id);
            return RTOS_MUTEX_ERR_RECURSION;
        }
    }

    if (timeout_ticks == RTOS_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_MUTEX_ERR_TIMEOUT;
    }

    /* Reject locks that would close a cycle before we either boost owners or
     * add ourselves to the wait list — neither operation is safe on a cyclic
     * graph, and the caller would otherwise hang here forever. */
    if (mutex_would_deadlock(m, current_task))
    {
        rtos_port_exit_critical();
        KLOGE("Mutex", "MutexDeadlock id=%u", current_task->task_id);
        rtos_application_deadlock_hook(current_task, m);
        return RTOS_MUTEX_ERR_DEADLOCK;
    }

    mutex_apply_priority_inheritance(m, current_task);
    mutex_add_to_waiting_list(m, current_task);

    KLOGD("Mutex", "MutexBlock id=%u timeout=%u", current_task->task_id, (uint32_t) timeout_ticks);

    if (timeout_ticks == RTOS_MAX_WAIT)
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

    /* Check if we were woken by unlock (blocked_on cleared) or timeout */
    if (current_task->blocked_on == m)
    {
        /* Still on waiting list = timeout occurred */
        mutex_remove_from_waiting_list(m, current_task);
        rtos_port_exit_critical();
        KLOGD("Mutex", "MutexTimeout id=%u", current_task->task_id);
        return RTOS_MUTEX_ERR_TIMEOUT;
    }

    rtos_port_exit_critical();
    KLOGD("Mutex", "MutexLock id=%u", current_task->task_id);
    RTOS_TEST_HOOK_FIRE(RTOS_HOOK_MUTEX_LOCK_EXIT, {
        _ctx_.u.mutex_lock.mutex  = m;
        _ctx_.u.mutex_lock.caller = current_task;
        _ctx_.u.mutex_lock.status = RTOS_MUTEX_OK;
    });
    return RTOS_MUTEX_OK;
}

void rtos_mutex_remove_task_from_wait(void *mutex_ptr, rtos_tcb_t *task)
{
    mutex_remove_from_waiting_list((rtos_mutex_t *) mutex_ptr, task);
}

rtos_mutex_status_t rtos_mutex_unlock(rtos_mutex_t *m)
{
    RTOS_ASSERT_PARAM(m != NULL);
    if (m == NULL)
    {
        return RTOS_MUTEX_ERR_INVALID;
    }

    rtos_port_enter_critical();

    rtos_tcb_t *current_task = rtos_task_get_current();

    if (m->owner != current_task)
    {
        rtos_port_exit_critical();
        KLOGE("Mutex", "MutexUnlockErr owner=%u caller=%u", m->owner ? m->owner->task_id : 0xFF,
              current_task ? current_task->task_id : 0xFF);
        return RTOS_MUTEX_ERR_INVALID;
    }

    if (m->lock_count > 1)
    {
        m->lock_count--;
        rtos_port_exit_critical();
        KLOGD("Mutex", "MutexRecursive id=%u count=%u", current_task->task_id, m->lock_count);
        return RTOS_MUTEX_OK;
    }

    RTOS_TEST_HOOK_FIRE(RTOS_HOOK_MUTEX_UNLOCK, {
        _ctx_.u.mutex_unlock.mutex  = m;
        _ctx_.u.mutex_unlock.caller = current_task;
    });
    mutex_remove_from_held_list(current_task, m);
    mutex_restore_priority(current_task);

    if (current_task->state == RTOS_TASK_STATE_READY)
    {
        rtos_scheduler_remove_from_ready_list(current_task);
        rtos_scheduler_add_to_ready_list(current_task);
    }

    rtos_tcb_t *waiter = mutex_pop_highest_priority_waiter(m);
    if (waiter != NULL)
    {
        m->owner                = waiter;
        m->lock_count           = 1;
        m->next_held            = waiter->held_mutex_list;
        waiter->held_mutex_list = m;

        KLOGD("Mutex", "MutexUnlock->id=%u", waiter->task_id);

        rtos_kernel_task_unblock(waiter);
        rtos_port_exit_critical();
        return RTOS_MUTEX_OK;
    }

    m->owner      = NULL;
    m->lock_count = 0;

    rtos_port_exit_critical();

    KLOGD("Mutex", "MutexUnlock id=%u", current_task->task_id);
    return RTOS_MUTEX_OK;
}
