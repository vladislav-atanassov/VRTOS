#include "semaphore.h"

#include "VRTOS.h"
#include "klog.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"

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

rtos_sem_status_t rtos_semaphore_init(rtos_semaphore_t *sem, uint32_t initial_count, uint32_t max_count)
{
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

    KLOGD(KEVT_SEM_INIT, initial_count, max_count);

    return RTOS_SEM_OK;
}

rtos_sem_status_t rtos_semaphore_wait(rtos_semaphore_t *sem, rtos_tick_t timeout_ticks)
{
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
        KLOGD(KEVT_SEM_ACQUIRE, sem->count, 0);
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
        KLOGE(KEVT_NO_CURRENT_TASK, 0, 0);
        return RTOS_SEM_ERR_INVALID;
    }

    sem_add_to_waiting_list(sem, current_task);

    KLOGD(KEVT_SEM_BLOCK, current_task->task_id, (uint32_t) timeout_ticks);

    if (timeout_ticks == RTOS_SEM_MAX_WAIT)
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

    /* Check if we were woken by signal (blocked_on cleared) or timeout */
    if (current_task->blocked_on == sem)
    {
        /* Still on waiting list = timeout occurred */
        sem_remove_from_waiting_list(sem, current_task);
        rtos_port_exit_critical();
        KLOGD(KEVT_SEM_TIMEOUT, current_task->task_id, 0);
        return RTOS_SEM_ERR_TIMEOUT;
    }

    rtos_port_exit_critical();
    KLOGD(KEVT_SEM_ACQUIRE, current_task->task_id, 1);
    return RTOS_SEM_OK;
}

rtos_sem_status_t rtos_semaphore_signal(rtos_semaphore_t *sem)
{
    if (sem == NULL)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    rtos_port_enter_critical();

    /* Check for waiting tasks first */
    rtos_tcb_t *waiter = sem_pop_highest_priority_waiter(sem);
    if (waiter != NULL)
    {
        /* Wake the highest priority waiter instead of incrementing count */
        KLOGD(KEVT_SEM_WAKE, waiter->task_id, 0);
        rtos_port_exit_critical();

        rtos_kernel_task_unblock(waiter);
        return RTOS_SEM_OK;
    }


    if (sem->max_count != 0 && sem->count >= sem->max_count)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_SEM_OVERFLOW, sem->count, sem->max_count);
        return RTOS_SEM_ERR_OVERFLOW;
    }

    sem->count++;
    rtos_port_exit_critical();

    KLOGD(KEVT_SEM_SIGNAL, sem->count, 0);
    return RTOS_SEM_OK;
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
