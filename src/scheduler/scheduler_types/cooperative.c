#include "cooperative.h"

#include "VRTOS.h"
#include "klog.h"
#include "scheduler.h"
#include "task_priv.h"

#include <string.h>

cooperative_private_data_t g_cooperative_data = {
    .ready_list = NULL, .delayed_list = NULL, .ready_count = 0, .delayed_count = 0};

static void cooperative_add_to_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    task->next = NULL;
    task->prev = NULL;

    if (g_cooperative_data.ready_list == NULL)
    {
        g_cooperative_data.ready_list = task;
    }
    else
    {
        rtos_tcb_t *current = g_cooperative_data.ready_list;
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = task;
        task->prev    = current;
    }

    g_cooperative_data.ready_count++;

    KLOGT(KEVT_SCHED_TASK_READY, task->task_id, g_cooperative_data.ready_count);
}

static void cooperative_remove_from_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_cooperative_data.ready_list == NULL)
    {
        return;
    }

    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        g_cooperative_data.ready_list = task->next;
    }

    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;

    if (g_cooperative_data.ready_count > 0)
    {
        g_cooperative_data.ready_count--;
    }

    KLOGT(KEVT_SCHED_TASK_READY, task->task_id, g_cooperative_data.ready_count);
}

static void cooperative_add_to_delayed_list_internal(rtos_task_handle_t task, rtos_tick_t delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    task->delay_until = rtos_get_tick_count() + delay_ticks;

    rtos_tcb_t **list_head = &g_cooperative_data.delayed_list;

    task->next = NULL;
    task->prev = NULL;

    if (*list_head == NULL)
    {
        *list_head                       = task;
        g_cooperative_data.delayed_count = 1;
        return;
    }

    /* Find insertion point (sorted by delay_until) */
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

    g_cooperative_data.delayed_count++;

    KLOGT(KEVT_SCHED_TASK_DELAYED, task->task_id, (uint32_t) task->delay_until);
}

static void cooperative_remove_from_delayed_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_cooperative_data.delayed_list == NULL)
    {
        return;
    }

    rtos_tcb_t **list_head = &g_cooperative_data.delayed_list;

    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        *list_head = task->next;
    }

    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;

    if (g_cooperative_data.delayed_count > 0)
    {
        g_cooperative_data.delayed_count--;
    }

    KLOGT(KEVT_SCHED_TASK_DELAYED, task->task_id, g_cooperative_data.delayed_count);
}

static void cooperative_update_delayed_tasks_internal(void)
{
    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tcb_t *task         = g_cooperative_data.delayed_list;

    /* Check delayed tasks (list is time-sorted, so we can break early) */
    while (task != NULL)
    {
        rtos_tcb_t *next_task = task->next;

        if ((int32_t)(current_tick - task->delay_until) >= 0)
        {
            cooperative_remove_from_delayed_list_internal(task);
            task->state = RTOS_TASK_STATE_READY;

#if RTOS_PROFILING_SYSTEM_ENABLED
            if (task->priority > 0)
            {
                task->ready_timestamp = rtos_profiling_get_cycles();
            }
#endif

            cooperative_add_to_ready_list_internal(task);

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

static rtos_task_handle_t cooperative_get_next_ready(void)
{
    return g_cooperative_data.ready_list;
}

static rtos_status_t cooperative_init(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    g_cooperative_data.ready_list    = NULL;
    g_cooperative_data.delayed_list  = NULL;
    g_cooperative_data.ready_count   = 0;
    g_cooperative_data.delayed_count = 0;

    instance->private_data = &g_cooperative_data;

    KLOGT(KEVT_SCHEDULER_INIT, 0, 0);
    return RTOS_SUCCESS;
}

static rtos_task_handle_t cooperative_get_next_task(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return NULL;
    }

    return cooperative_get_next_ready();
}

/* Cooperative scheduler is non-preemptive: tasks run until they voluntarily yield or block */
static bool cooperative_should_preempt(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task)
{
    return false;
}

static void cooperative_task_completed(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task)
{
    if (instance == NULL || completed_task == NULL)
    {
        return;
    }

    /* no-op: cooperative tasks yield voluntarily */
    if (completed_task->state == RTOS_TASK_STATE_READY)
    {
        /* Remove from current position and add to end for round-robin */
        cooperative_remove_from_ready_list_internal(completed_task);
        cooperative_add_to_ready_list_internal(completed_task);

        KLOGT(KEVT_SCHED_ROTATE, completed_task->task_id, 0);
    }
}

/* ========= Cooperative List Management Interface Implementation ========= */

static void cooperative_add_to_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_add_to_ready_list_internal(task_handle);
}

static void cooperative_remove_from_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_remove_from_ready_list_internal(task_handle);
}

static void cooperative_add_to_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle,
                                            rtos_tick_t delay_ticks)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_add_to_delayed_list_internal(task_handle, delay_ticks);
}

static void cooperative_remove_from_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_remove_from_delayed_list_internal(task_handle);
}

static void cooperative_update_delayed_tasks(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    cooperative_update_delayed_tasks_internal();
}

static size_t cooperative_get_statistics(rtos_scheduler_instance_t *instance, void *stats_buffer, size_t buffer_size)
{
    if (instance == NULL || stats_buffer == NULL || buffer_size == 0)
    {
        return 0;
    }

    /* Simple statistics structure for cooperative scheduler */
    typedef struct
    {
        uint8_t     ready_count;
        uint8_t     delayed_count;
        rtos_tick_t current_tick;
        rtos_tcb_t *current_ready_task;
    } cooperative_stats_t;

    if (buffer_size < sizeof(cooperative_stats_t))
    {
        return 0;
    }

    cooperative_stats_t *stats = (cooperative_stats_t *) stats_buffer;

    stats->ready_count        = g_cooperative_data.ready_count;
    stats->delayed_count      = g_cooperative_data.delayed_count;
    stats->current_tick       = rtos_get_tick_count();
    stats->current_ready_task = g_cooperative_data.ready_list;

    return sizeof(cooperative_stats_t);
}

const rtos_scheduler_t cooperative_scheduler = {
    .init           = cooperative_init,
    .get_next_task  = cooperative_get_next_task,
    .should_preempt = cooperative_should_preempt,
    .task_completed = cooperative_task_completed,

    .add_to_ready_list        = cooperative_add_to_ready_list,
    .remove_from_ready_list   = cooperative_remove_from_ready_list,
    .add_to_delayed_list      = cooperative_add_to_delayed_list,
    .remove_from_delayed_list = cooperative_remove_from_delayed_list,
    .update_delayed_tasks     = cooperative_update_delayed_tasks,

    .get_statistics = cooperative_get_statistics};

/* =================== Public Helper Functions =================== */

/**
 * @brief Get next ready task (public interface for kernel)
 * This function is used by the kernel when it needs direct access
 * to the next ready task without going through the scheduler interface.
 */
rtos_tcb_t *rtos_task_get_next_ready_cooperative(void)
{
    return cooperative_get_next_ready();
}
