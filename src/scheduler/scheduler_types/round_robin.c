/*******************************************************************************
 * File: src/scheduler/scheduler_types/round_robin.c
 * Description: Round Robin Scheduler with Time-Slice Preemption
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "round_robin.h"

#include "VRTOS.h"
#include "config.h"
#include "log.h"
#include "scheduler.h"
#include "task_priv.h"

#include <string.h>

/**
 * @file round_robin.c
 * @brief Round Robin Scheduler Implementation
 *
 * This implementation provides a preemptive round robin scheduler where
 * each task receives an equal time slice. Tasks are scheduled in FIFO
 * order and automatically preempted when their time slice expires.
 */

/* ============ Round Robin List Management Functions ============ */

/**
 * @brief Add task to round robin ready list (FIFO - add to tail)
 */
static void round_robin_add_to_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    /* Initialize task's list pointers */
    task->next = NULL;
    task->prev = NULL;

    if (g_round_robin_data.ready_list == NULL)
    {
        /* First task in ready list */
        g_round_robin_data.ready_list      = task;
        g_round_robin_data.ready_list_tail = task;
    }
    else
    {
        /* Add to tail for FIFO ordering */
        g_round_robin_data.ready_list_tail->next = task;
        task->prev                               = g_round_robin_data.ready_list_tail;
        g_round_robin_data.ready_list_tail       = task;
    }

    g_round_robin_data.ready_count++;

    log_debug("Round Robin: Added task '%s' to ready list (total ready: %d)", task->name ? task->name : "unnamed",
              g_round_robin_data.ready_count);
}

/**
 * @brief Remove task from round robin ready list
 */
static void round_robin_remove_from_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_round_robin_data.ready_list == NULL)
    {
        return;
    }

    /* Update previous node's next pointer */
    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        /* Task is at head - update head pointer */
        g_round_robin_data.ready_list = task->next;
    }

    /* Update next node's prev pointer */
    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }
    else
    {
        /* Task is at tail - update tail pointer */
        g_round_robin_data.ready_list_tail = task->prev;
    }

    /* Clear task's list pointers */
    task->next = NULL;
    task->prev = NULL;

    if (g_round_robin_data.ready_count > 0)
    {
        g_round_robin_data.ready_count--;
    }

    log_debug("Round Robin: Removed task '%s' from ready list (remaining: %d)", task->name ? task->name : "unnamed",
              g_round_robin_data.ready_count);
}

/**
 * @brief Add task to round robin delayed list (time-sorted)
 */
static void round_robin_add_to_delayed_list_internal(rtos_task_handle_t task, rtos_tick_t delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    /* Calculate wakeup time */
    task->delay_until = rtos_get_tick_count() + delay_ticks;

    /* Initialize task's list pointers */
    task->next = NULL;
    task->prev = NULL;

    rtos_tcb_t **list_head = &g_round_robin_data.delayed_list;

    if (*list_head == NULL)
    {
        /* Empty delayed list */
        *list_head                       = task;
        g_round_robin_data.delayed_count = 1;
        return;
    }

    /* Find insertion point (sorted by delay_until - earliest first) */
    rtos_tcb_t *current = *list_head;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && current->delay_until <= task->delay_until)
    {
        prev    = current;
        current = current->next;
    }

    /* Insert task at found position */
    task->next = current;
    task->prev = prev;

    if (prev == NULL)
    {
        /* Insert at head */
        *list_head = task;
    }
    else
    {
        prev->next = task;
    }

    if (current != NULL)
    {
        current->prev = task;
    }

    g_round_robin_data.delayed_count++;

    log_debug("Round Robin: Added task '%s' to delayed list, wakeup at tick %u (total delayed: %d)",
              task->name ? task->name : "unnamed", (unsigned int) task->delay_until, g_round_robin_data.delayed_count);
}

/**
 * @brief Remove task from round robin delayed list
 */
static void round_robin_remove_from_delayed_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_round_robin_data.delayed_list == NULL)
    {
        return;
    }

    rtos_tcb_t **list_head = &g_round_robin_data.delayed_list;

    /* Update previous node's next pointer */
    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        /* Task is at head */
        *list_head = task->next;
    }

    /* Update next node's prev pointer */
    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }

    /* Clear task's list pointers */
    task->next = NULL;
    task->prev = NULL;

    if (g_round_robin_data.delayed_count > 0)
    {
        g_round_robin_data.delayed_count--;
    }

    log_debug("Round Robin: Removed task '%s' from delayed list (remaining: %d)", task->name ? task->name : "unnamed",
              g_round_robin_data.delayed_count);
}

/**
 * @brief Update delayed tasks - move expired tasks to ready list
 */
static void round_robin_update_delayed_tasks_internal(void)
{
    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tcb_t *task         = g_round_robin_data.delayed_list;

    /* Check delayed tasks (list is time-sorted, so we can break early) */
    while (task != NULL)
    {
        rtos_tcb_t *next_task = task->next;

        if (current_tick >= task->delay_until)
        {
            /* Task delay expired - move to ready list */
            round_robin_remove_from_delayed_list_internal(task);
            task->state = RTOS_TASK_STATE_READY;

#if RTOS_PROFILING_SYSTEM_ENABLED
            task->ready_timestamp = rtos_profiling_get_cycles();
#endif

            round_robin_add_to_ready_list_internal(task);

            log_debug("Round Robin: Task '%s' delay expired, moved to ready list", task->name ? task->name : "unnamed");
        }
        else
        {
            /* Tasks are sorted by delay_until, so we can break */
            break;
        }

        task = next_task;
    }
}

/**
 * @brief Get next ready task (FIFO - return head of queue)
 */
static rtos_task_handle_t round_robin_get_next_ready(void)
{
    return g_round_robin_data.ready_list;
}

/* ============ Round Robin Scheduler Interface Implementation ============ */

/**
 * @brief Initialize round robin scheduler
 */
static rtos_status_t round_robin_init(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    /* Initialize round robin data structures */
    g_round_robin_data.ready_list      = NULL;
    g_round_robin_data.ready_list_tail = NULL;
    g_round_robin_data.delayed_list    = NULL;
    g_round_robin_data.current_task    = NULL;
    g_round_robin_data.slice_remaining = RTOS_TIME_SLICE_TICKS;
    g_round_robin_data.ready_count     = 0;
    g_round_robin_data.delayed_count   = 0;

    /* Set up private data pointer */
    instance->private_data = &g_round_robin_data;

    log_debug("Round robin scheduler initialized (time slice: %d ticks)", RTOS_TIME_SLICE_TICKS);
    return RTOS_SUCCESS;
}

/**
 * @brief Get next task using round robin algorithm (FIFO)
 */
static rtos_task_handle_t round_robin_get_next_task(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return NULL;
    }

    rtos_task_handle_t next = round_robin_get_next_ready();

    if (next != NULL)
    {
        g_round_robin_data.current_task = next;
    }

    return next;
}

/**
 * @brief Check if preemption is needed (time slice expired)
 *
 * For round robin, preemption occurs when:
 * 1. A new task becomes ready (basic check)
 * 2. The current task's time slice has expired (handled via tick handler)
 */
static bool round_robin_should_preempt(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task)
{
    if (instance == NULL)
    {
        return false;
    }

    /* Decrement time slice counter */
    if (g_round_robin_data.slice_remaining > 0)
    {
        g_round_robin_data.slice_remaining--;
    }

    /* Preempt if time slice expired and there are other ready tasks */
    if (g_round_robin_data.slice_remaining == 0 && g_round_robin_data.ready_count > 1)
    {
        log_debug("Round Robin: Time slice expired, preemption needed");
        return true;
    }

    return false;
}

/**
 * @brief Handle task completion/yield for round robin scheduler
 *
 * When a task yields or its time slice expires, move it to the end
 * of the ready queue to give other tasks a turn.
 */
static void round_robin_task_completed(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task)
{
    if (instance == NULL || completed_task == NULL)
    {
        return;
    }

    /* If task is still ready, rotate it to end of queue */
    if (completed_task->state == RTOS_TASK_STATE_READY)
    {
        /* Remove from current position */
        round_robin_remove_from_ready_list_internal(completed_task);
        /* Add to end of queue (FIFO rotation) */
        round_robin_add_to_ready_list_internal(completed_task);

        /* Reset time slice for next task */
        g_round_robin_data.slice_remaining = RTOS_TIME_SLICE_TICKS;
        g_round_robin_data.current_task    = NULL;

        log_debug("Round Robin: Task '%s' rotated to end of ready queue",
                  completed_task->name ? completed_task->name : "unnamed");
    }
}

/* ========= Round Robin List Management Interface Implementation ========= */

/**
 * @brief Add task to ready list (round robin interface)
 */
static void round_robin_add_to_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_add_to_ready_list_internal(task_handle);
}

/**
 * @brief Remove task from ready list (round robin interface)
 */
static void round_robin_remove_from_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_remove_from_ready_list_internal(task_handle);
}

/**
 * @brief Add task to delayed list (round robin interface)
 */
static void round_robin_add_to_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle,
                                            rtos_tick_t delay_ticks)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_add_to_delayed_list_internal(task_handle, delay_ticks);
}

/**
 * @brief Remove task from delayed list (round robin interface)
 */
static void round_robin_remove_from_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_remove_from_delayed_list_internal(task_handle);
}

/**
 * @brief Update delayed tasks (round robin interface)
 */
static void round_robin_update_delayed_tasks(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    round_robin_update_delayed_tasks_internal();
}

/**
 * @brief Get round robin statistics
 */
static size_t round_robin_get_statistics(rtos_scheduler_instance_t *instance, void *stats_buffer, size_t buffer_size)
{
    if (instance == NULL || stats_buffer == NULL || buffer_size == 0)
    {
        return 0;
    }

    /* Statistics structure for round robin scheduler */
    typedef struct
    {
        uint8_t     ready_count;
        uint8_t     delayed_count;
        rtos_tick_t slice_remaining;
        rtos_tick_t current_tick;
        rtos_tcb_t *current_task;
    } round_robin_stats_t;

    if (buffer_size < sizeof(round_robin_stats_t))
    {
        return 0;
    }

    round_robin_stats_t *stats = (round_robin_stats_t *) stats_buffer;

    stats->ready_count     = g_round_robin_data.ready_count;
    stats->delayed_count   = g_round_robin_data.delayed_count;
    stats->slice_remaining = g_round_robin_data.slice_remaining;
    stats->current_tick    = rtos_get_tick_count();
    stats->current_task    = g_round_robin_data.current_task;

    return sizeof(round_robin_stats_t);
}

/* =================== Round Robin Scheduler Vtable =================== */

/**
 * @brief Round robin scheduler vtable interface
 *
 * This vtable provides the complete interface implementation for the
 * round robin scheduler with time-slice preemption.
 */
const rtos_scheduler_t round_robin_scheduler = {
    /* Core scheduling functions */
    .init           = round_robin_init,
    .get_next_task  = round_robin_get_next_task,
    .should_preempt = round_robin_should_preempt,
    .task_completed = round_robin_task_completed,

    /* List management operations */
    .add_to_ready_list        = round_robin_add_to_ready_list,
    .remove_from_ready_list   = round_robin_remove_from_ready_list,
    .add_to_delayed_list      = round_robin_add_to_delayed_list,
    .remove_from_delayed_list = round_robin_remove_from_delayed_list,
    .update_delayed_tasks     = round_robin_update_delayed_tasks,

    /* Optional statistics */
    .get_statistics = round_robin_get_statistics};

/* =================== Public Helper Functions =================== */

/**
 * @brief Get next ready task (public interface for kernel)
 *
 * This function is used by the kernel when it needs direct access
 * to the next ready task without going through the scheduler interface.
 */
rtos_tcb_t *rtos_task_get_next_ready_round_robin(void)
{
    return round_robin_get_next_ready();
}
