/*******************************************************************************
 * File: src/sync/semaphore/semaphore.c
 * Description: Counting Semaphore Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "semaphore.h"

#include "VRTOS.h"
#include "log.h"
#include "rtos_port.h"
#include "task.h"
#include "task_priv.h"

#include <string.h>

/**
 * @file semaphore.c
 * @brief Counting Semaphore Implementation
 *
 * Implements counting semaphores with:
 * - Priority-ordered wait queue (highest priority task wakes first)
 * - Timeout support
 * - Binary semaphore support (max_count = 1)
 */

/* =================== Internal Helper Functions =================== */

/**
 * @brief Add task to semaphore wait queue (priority-ordered, highest first)
 */
static void sem_add_to_waiting_list(rtos_semaphore_t *sem, rtos_tcb_t *task)
{
    task->next_waiting    = NULL;
    task->blocked_on      = sem;
    task->blocked_on_type = RTOS_SYNC_TYPE_SEMAPHORE;

    if (sem->waiting_list == NULL)
    {
        /* First waiter */
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
        /* Insert at head */
        task->next_waiting = sem->waiting_list;
        sem->waiting_list  = task;
    }
    else
    {
        /* Insert after prev */
        task->next_waiting = current;
        prev->next_waiting = task;
    }
}

/**
 * @brief Remove task from semaphore wait queue
 */
static void sem_remove_from_waiting_list(rtos_semaphore_t *sem, rtos_tcb_t *task)
{
    if (sem->waiting_list == NULL || task == NULL)
    {
        return;
    }

    if (sem->waiting_list == task)
    {
        /* Task is at head */
        sem->waiting_list = task->next_waiting;
    }
    else
    {
        /* Find and remove task */
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

/**
 * @brief Get and remove highest priority waiter
 */
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

/* =================== Public API Implementation =================== */

/**
 * @brief Initialize a semaphore
 */
rtos_sem_status_t rtos_semaphore_init(rtos_semaphore_t *sem, uint32_t initial_count,
                                      uint32_t max_count)
{
    if (sem == NULL)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    /* Validate initial count against max */
    if (max_count != 0 && initial_count > max_count)
    {
        return RTOS_SEM_ERR_INVALID;
    }

    rtos_port_enter_critical();

    sem->count        = initial_count;
    sem->max_count    = max_count;
    sem->waiting_list = NULL;

    rtos_port_exit_critical();

    log_debug("Semaphore initialized: count=%lu, max=%lu", (unsigned long) initial_count,
              (unsigned long) max_count);

    return RTOS_SEM_OK;
}

/**
 * @brief Wait on a semaphore
 */
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
        log_debug("Semaphore acquired (count now %lu)", (unsigned long) sem->count);
        return RTOS_SEM_OK;
    }

    /* Semaphore not available - check if we should wait */
    if (timeout_ticks == RTOS_SEM_NO_WAIT)
    {
        rtos_port_exit_critical();
        return RTOS_SEM_ERR_TIMEOUT;
    }

    /* Get current task and block it */
    rtos_tcb_t *current_task = rtos_task_get_current();
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        log_error("Semaphore wait called with no current task!");
        return RTOS_SEM_ERR_INVALID;
    }

    /* Add to waiting list (priority-ordered) */
    sem_add_to_waiting_list(sem, current_task);

    log_debug("Task '%s' blocking on semaphore (timeout=%lu)",
              current_task->name ? current_task->name : "unnamed", (unsigned long) timeout_ticks);

    /* Block the task with timeout */
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
        log_debug("Task '%s' semaphore wait timed out",
                  current_task->name ? current_task->name : "unnamed");
        return RTOS_SEM_ERR_TIMEOUT;
    }

    rtos_port_exit_critical();
    log_debug("Task '%s' semaphore acquired after wait",
              current_task->name ? current_task->name : "unnamed");
    return RTOS_SEM_OK;
}

/**
 * @brief Signal a semaphore
 */
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
        log_debug("Semaphore signal waking task '%s'", waiter->name ? waiter->name : "unnamed");
        rtos_port_exit_critical();

        /* Unblock the waiting task */
        rtos_kernel_task_unblock(waiter);
        return RTOS_SEM_OK;
    }

    /* No waiters - increment count */
    if (sem->max_count != 0 && sem->count >= sem->max_count)
    {
        rtos_port_exit_critical();
        log_error("Semaphore overflow! count=%lu, max=%lu", (unsigned long) sem->count,
                  (unsigned long) sem->max_count);
        return RTOS_SEM_ERR_OVERFLOW;
    }

    sem->count++;
    rtos_port_exit_critical();

    log_debug("Semaphore signaled (count now %lu)", (unsigned long) sem->count);
    return RTOS_SEM_OK;
}

/**
 * @brief Try to acquire semaphore without blocking
 */
rtos_sem_status_t rtos_semaphore_try_wait(rtos_semaphore_t *sem)
{
    return rtos_semaphore_wait(sem, RTOS_SEM_NO_WAIT);
}

/**
 * @brief Get current semaphore count
 */
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
