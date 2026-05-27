#include "semaphore.h"

#include "KARTOS.h"
#include "kernel_priv.h"   /* rtos_kernel_task_unblock_from_isr */
#include "klog.h"
#include "rtos_assert.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"
#include "test_hooks_priv.h"

#include <string.h>

static void sem_add_to_waiting_list(rtos_semaphore_t *sem, rtos_tcb_t *task)
{
    task->next_waiting    = NULL;
    task->blocked_on      = sem;
    task->blocked_on_type = RTOS_SYNC_TYPE_SEMAPHORE;

    if (sem->waiting_list == NULL)
    {
        sem->waiting_list = task;
        return;
    }

    /* Insert in priority order (highest priority at head) */
    rtos_tcb_t *current = sem->waiting_list;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && current->priority >= task->priority)
    {
        prev    = current;
        current = current->next_waiting;
    }

    if (prev == NULL)
    {
        task->next_waiting = sem->waiting_list;
        sem->waiting_list  = task;
    }
    else
    {
        task->next_waiting = current;
        prev->next_waiting = task;
    }
}

static void sem_remove_from_waiting_list(rtos_semaphore_t *sem, rtos_tcb_t *task)
{
    if (sem->waiting_list == NULL || task == NULL)
    {
        return;
    }

    if (sem->waiting_list == task)
    {
        sem->waiting_list = task->next_waiting;
    }
    else
    {
        rtos_tcb_t *current = sem->waiting_list;
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

static rtos_tcb_t *sem_pop_highest_priority_waiter(rtos_semaphore_t *sem)
{
    if (sem->waiting_list == NULL)
    {
        return NULL;
    }

    /* Head is always highest priority due to ordered insertion */
    rtos_tcb_t *task      = sem->waiting_list;
    sem->waiting_list     = task->next_waiting;
    task->next_waiting    = NULL;
    task->blocked_on      = NULL;
    task->blocked_on_type = RTOS_SYNC_TYPE_NONE;

    return task;
}

void rtos_sem_remove_task_from_wait(void *sem_ptr, rtos_tcb_t *task)
{
    sem_remove_from_waiting_list((rtos_semaphore_t *) sem_ptr, task);
}

rtos_sem_status_t rtos_semaphore_init(rtos_semaphore_t *sem, uint32_t initial_count, uint32_t max_count)
{
    RTOS_ASSERT_PARAM(sem != NULL);
    if (sem == NULL)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    if (max_count != 0 && initial_count > max_count)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    rtos_port_enter_critical();

    sem->count        = initial_count;
    sem->max_count    = max_count;
    sem->waiting_list = NULL;

    rtos_port_exit_critical();

    KLOGD("Semaphore", "SemInit count=%u max=%u", initial_count, max_count);

    return RTOS_SEM_OK;
}

rtos_sem_status_t rtos_semaphore_wait(rtos_semaphore_t *sem, rtos_tick_t timeout_ticks)
{
    RTOS_ASSERT_PARAM(sem != NULL);
    if (sem == NULL)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    rtos_port_enter_critical();

    /* Fast path: semaphore available */
    if (sem->count > 0)
    {
        sem->count--;
        rtos_port_exit_critical();
        KLOGD("Semaphore", "SemAcquire count=%u", sem->count);
        RTOS_TEST_HOOK_FIRE(RTOS_HOOK_SEM_TAKE, {
            _ctx_.u.prim.primitive = sem;
            _ctx_.u.prim.caller    = rtos_task_get_current();
        });
        return RTOS_SEM_OK;
    }

    if (timeout_ticks == RTOS_SEM_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_SEM_ERR_TIMEOUT;
    }

    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE("Semaphore", "NoCurrentTask");
        return RTOS_SEM_ERR_INVALID;
    }

    sem_add_to_waiting_list(sem, current_task);

    KLOGD("Semaphore", "SemBlock id=%u timeout=%u", current_task->task_id, (uint32_t) timeout_ticks);

    /* Block atomically with the wait-list insert above: a signal racing us
     * can never observe us listed-but-RUNNING and drop the wake. delay 0 =
     * infinite block. */
    rtos_kernel_task_block_locked(current_task, (timeout_ticks == RTOS_SEM_MAX_WAIT) ? 0U : timeout_ticks);
    rtos_port_exit_critical();
    rtos_yield();

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    /* Check if we were woken by signal (blocked_on cleared) or timeout */
    if (current_task->blocked_on == sem)
    {
        /* Still on waiting list = timeout occurred */
        sem_remove_from_waiting_list(sem, current_task);
        rtos_port_exit_critical();
        KLOGD("Semaphore", "SemTimeout id=%u", current_task->task_id);
        return RTOS_SEM_ERR_TIMEOUT;
    }

    rtos_port_exit_critical();
    KLOGD("Semaphore", "SemAcquire id=%u", current_task->task_id);
    return RTOS_SEM_OK;
}

/* Caller MUST hold a critical section. */
static rtos_tcb_t *sem_signal_locked(rtos_semaphore_t *sem, rtos_sem_status_t *status_out)
{
    rtos_tcb_t *waiter = sem_pop_highest_priority_waiter(sem);
    if (waiter != NULL)
    {
        *status_out = RTOS_SEM_OK;
        return waiter;
    }

    if (sem->max_count != 0 && sem->count >= sem->max_count)
    {
        *status_out = RTOS_SEM_ERR_OVERFLOW;
        return NULL;
    }

    sem->count++;
    *status_out = RTOS_SEM_OK;
    return NULL;
}

rtos_sem_status_t rtos_semaphore_signal(rtos_semaphore_t *sem)
{
    RTOS_ASSERT_PARAM(sem != NULL);
    if (sem == NULL)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    rtos_port_enter_critical();

    rtos_sem_status_t status;
    rtos_tcb_t       *waiter = sem_signal_locked(sem, &status);
    if (waiter != NULL)
    {
        KLOGD("Semaphore", "SemWake id=%u", waiter->task_id);
        RTOS_TEST_HOOK_FIRE(RTOS_HOOK_SEM_GIVE, {
            _ctx_.u.prim.primitive = sem;
            _ctx_.u.prim.caller    = rtos_task_get_current();
        });
        rtos_kernel_task_unblock(waiter);
        rtos_port_exit_critical();
        return status;
    }

    rtos_port_exit_critical();

    if (status == RTOS_SEM_ERR_OVERFLOW)
    {
        KLOGE("Semaphore", "SemOverflow count=%u max=%u", sem->count, sem->max_count);
    }
    else
    {
        KLOGD("Semaphore", "SemSignal count=%u", sem->count);
    }
    return status;
}

rtos_sem_status_t rtos_semaphore_signal_from_isr(rtos_semaphore_t *sem)
{
    if (sem == NULL)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    uint32_t          saved  = rtos_port_enter_critical_from_isr();
    rtos_sem_status_t status;
    rtos_tcb_t       *waiter = sem_signal_locked(sem, &status);
    if (waiter != NULL)
    {
        RTOS_TEST_HOOK_FIRE(RTOS_HOOK_SEM_GIVE, {
            _ctx_.u.prim.primitive = sem;
            _ctx_.u.prim.caller    = NULL; /* ISR has no caller TCB */
        });
        rtos_kernel_task_unblock_from_isr(waiter);
    }
    rtos_port_exit_critical_from_isr(saved);

    /* klog_write is documented ISR-safe (see klog.h). */
    if (status == RTOS_SEM_ERR_OVERFLOW)
    {
        KLOGE("Semaphore", "SemOverflowISR");
    }
    return status;
}

rtos_sem_status_t rtos_semaphore_try_wait(rtos_semaphore_t *sem)
{
    return rtos_semaphore_wait(sem, RTOS_SEM_NO_WAIT);
}

uint32_t rtos_semaphore_get_count(rtos_semaphore_t *sem)
{
    if (sem == NULL)
    {
        return 0;
    }

    rtos_port_enter_critical();
    uint32_t count = sem->count;
    rtos_port_exit_critical();

    return count;
}
