#include "round_robin.h"

#include "VRTOS.h"
#include "config.h"
#include "kernel_priv.h"
#include "klog.h"
#include "scheduler.h"
#include "task_priv.h"

#include <string.h>

static void round_robin_add_to_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    task->next = NULL;
    task->prev = NULL;

    if (g_round_robin_data.ready_list == NULL)
    {
        g_round_robin_data.ready_list      = task;
        g_round_robin_data.ready_list_tail = task;
    }
    else
    {
        g_round_robin_data.ready_list_tail->next = task;
        task->prev                               = g_round_robin_data.ready_list_tail;
        g_round_robin_data.ready_list_tail       = task;
    }

    g_round_robin_data.ready_count++;

    KLOGT(KEVT_SCHED_TASK_READY, task->task_id, g_round_robin_data.ready_count);
}

static void round_robin_remove_from_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_round_robin_data.ready_list == NULL)
    {
        return;
    }

    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        /* Task is at head - update head pointer */
        g_round_robin_data.ready_list = task->next;
    }

    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }
    else
    {
        /* Task is at tail - update tail pointer */
        g_round_robin_data.ready_list_tail = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;

    if (g_round_robin_data.ready_count > 0)
    {
        g_round_robin_data.ready_count--;
    }

    KLOGT(KEVT_SCHED_TASK_READY, task->task_id, g_round_robin_data.ready_count);
}

static void round_robin_add_to_delayed_list_internal(rtos_task_handle_t task, rtos_tick_t delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    task->delay_until = rtos_get_tick_count() + delay_ticks;

    task->next = NULL;
    task->prev = NULL;

    rtos_tcb_t **list_head = &g_round_robin_data.delayed_list;

    if (*list_head == NULL)
    {
        *list_head                       = task;
        g_round_robin_data.delayed_count = 1;
        return;
    }

    /* Find insertion point (sorted by delay_until - earliest first) */
    rtos_tcb_t *current = *list_head;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && (int32_t)(current->delay_until - task->delay_until) <= 0)
    {
        prev    = current;
        current = current->next;
    }

    task->next = current;
    task->prev = prev;

    if (prev == NULL)
    {
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

    KLOGT(KEVT_SCHED_TASK_DELAYED, task->task_id, (uint32_t) task->delay_until);
}

static void round_robin_remove_from_delayed_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_round_robin_data.delayed_list == NULL)
    {
        return;
    }

    rtos_tcb_t **list_head = &g_round_robin_data.delayed_list;

    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        /* Task is at head */
        *list_head = task->next;
    }

    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;

    if (g_round_robin_data.delayed_count > 0)
    {
        g_round_robin_data.delayed_count--;
    }

    KLOGT(KEVT_SCHED_TASK_DELAYED, task->task_id, g_round_robin_data.delayed_count);
}

static void round_robin_update_delayed_tasks_internal(void)
{
    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tcb_t *task         = g_round_robin_data.delayed_list;

    /* Check delayed tasks (list is time-sorted, so we can break early) */
    while (task != NULL)
    {
        rtos_tcb_t *next_task = task->next;

        if ((int32_t)(current_tick - task->delay_until) >= 0)
        {
            round_robin_remove_from_delayed_list_internal(task);
            task->state = RTOS_TASK_STATE_READY;

#if RTOS_PROFILING_SYSTEM_ENABLED
            if (task->priority > 0)
            {
                task->ready_timestamp = rtos_profiling_get_cycles();
            }
#endif

            round_robin_add_to_ready_list_internal(task);

            KLOGT(KEVT_SCHED_TASK_DELAY_EXPIRED, task->task_id, 0);
        }
        else
        {
            /* Tasks are sorted by delay_until, so we can break */
            break;
        }

        task = next_task;
    }
}

static rtos_task_handle_t round_robin_get_next_ready(void)
{
    return g_round_robin_data.ready_list;
}

static rtos_status_t round_robin_init(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    g_round_robin_data.ready_list      = NULL;
    g_round_robin_data.ready_list_tail = NULL;
    g_round_robin_data.delayed_list    = NULL;
    g_round_robin_data.current_task    = NULL;
    g_round_robin_data.slice_remaining = RTOS_TIME_SLICE_TICKS;
    g_round_robin_data.ready_count     = 0;
    g_round_robin_data.delayed_count   = 0;

    instance->private_data = &g_round_robin_data;

    KLOGT(KEVT_SCHEDULER_INIT, RTOS_TIME_SLICE_TICKS, 0);
    return RTOS_SUCCESS;
}

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

    /* Only decrement the time slice on the tick path (current task still running).
     * When a new task becomes ready, new_task != current_task — skip decrement
     * to avoid depleting the slice faster than intended. */
    if (new_task == g_kernel.current_task)
    {
        if (g_round_robin_data.slice_remaining > 0)
        {
            g_round_robin_data.slice_remaining--;
        }

        if (g_round_robin_data.slice_remaining == 0 && g_round_robin_data.ready_count > 1)
        {
            KLOGT(KEVT_SCHED_TIME_SLICE, 0, 0);
            return true;
        }
    }

    return false;
}

/**
 * When a task yields or its time slice expires, rotate it to the end
 * of the ready queue to give other tasks a turn.
 */
static void round_robin_task_completed(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task)
{
    if (instance == NULL || completed_task == NULL)
    {
        return;
    }

    /* Rotate to next task */
    if (completed_task->state == RTOS_TASK_STATE_READY)
    {
        round_robin_remove_from_ready_list_internal(completed_task);
        round_robin_add_to_ready_list_internal(completed_task);

        /* Reset time slice for next task */
        g_round_robin_data.slice_remaining = RTOS_TIME_SLICE_TICKS;
        g_round_robin_data.current_task    = NULL;

        KLOGT(KEVT_SCHED_ROTATE, completed_task->task_id, 0);
    }
}

/* ========= Round Robin List Management Interface Implementation ========= */

static void round_robin_add_to_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_add_to_ready_list_internal(task_handle);
}

static void round_robin_remove_from_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_remove_from_ready_list_internal(task_handle);
}

static void round_robin_add_to_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle,
                                            rtos_tick_t delay_ticks)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_add_to_delayed_list_internal(task_handle, delay_ticks);
}

static void round_robin_remove_from_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    round_robin_remove_from_delayed_list_internal(task_handle);
}

static void round_robin_update_delayed_tasks(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    round_robin_update_delayed_tasks_internal();
}

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

const rtos_scheduler_t round_robin_scheduler = {
    .init           = round_robin_init,
    .get_next_task  = round_robin_get_next_task,
    .should_preempt = round_robin_should_preempt,
    .task_completed = round_robin_task_completed,

    .add_to_ready_list        = round_robin_add_to_ready_list,
    .remove_from_ready_list   = round_robin_remove_from_ready_list,
    .add_to_delayed_list      = round_robin_add_to_delayed_list,
    .remove_from_delayed_list = round_robin_remove_from_delayed_list,
    .update_delayed_tasks     = round_robin_update_delayed_tasks,

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
