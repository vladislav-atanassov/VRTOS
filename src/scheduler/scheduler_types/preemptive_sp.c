#include "preemptive_sp.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "klog.h"
#include "scheduler.h"
#include "task_priv.h"

#include <string.h>

preemptive_sp_private_data_t g_preemptive_sp_data = {
    .ready_lists = {NULL}, .delayed_list = NULL, .ready_priorities = 0};

static void preemptive_sp_add_to_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        return;
    }

    rtos_priority_t priority  = task->priority;
    rtos_tcb_t    **list_head = &g_preemptive_sp_data.ready_lists[priority];

    /* Add to end of priority list (FIFO within same priority) */
    task->next = NULL;
    task->prev = NULL;

    if (*list_head == NULL)
    {
        *list_head = task;
        g_preemptive_sp_data.ready_priorities |= (1U << priority);
    }
    else
    {
        rtos_tcb_t *current = *list_head;
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = task;
        task->prev    = current;
    }

    KLOGT(KEVT_SCHED_TASK_READY, task->task_id, priority);
}

static void preemptive_sp_remove_from_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        return;
    }

    rtos_priority_t priority  = task->priority;
    rtos_tcb_t    **list_head = &g_preemptive_sp_data.ready_lists[priority];

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

    /* Clear priority bit if no more tasks at this priority */
    if (*list_head == NULL)
    {
        g_preemptive_sp_data.ready_priorities &= ~(1U << priority);
    }

    task->next = NULL;
    task->prev = NULL;

    KLOGT(KEVT_SCHED_TASK_READY, task->task_id, priority);
}

static void preemptive_sp_add_to_delayed_list_internal(rtos_task_handle_t task, rtos_tick_t delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    task->delay_until      = rtos_get_tick_count() + delay_ticks;
    rtos_tcb_t **list_head = &g_preemptive_sp_data.delayed_list;

    task->next = NULL;
    task->prev = NULL;

    if (*list_head == NULL)
    {
        *list_head = task;
        return;
    }

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

    KLOGT(KEVT_SCHED_TASK_DELAYED, task->task_id, (uint32_t) task->delay_until);
}

static void preemptive_sp_remove_from_delayed_list_internal(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    rtos_tcb_t **list_head = &g_preemptive_sp_data.delayed_list;

    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else if (*list_head == task)
    {
        *list_head = task->next;
    }

    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;

    KLOGT(KEVT_SCHED_TASK_DELAYED, task->task_id, 0);
}

static void preemptive_sp_update_delayed_tasks_internal(void)
{
    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tcb_t *task         = g_preemptive_sp_data.delayed_list;

    /* Check delayed tasks (list is time-sorted, so we can break early) */
    while (task != NULL)
    {
        rtos_tcb_t *next_task = task->next;

        if ((int32_t)(current_tick - task->delay_until) >= 0)
        {
            preemptive_sp_remove_from_delayed_list_internal(task);
            task->state = RTOS_TASK_STATE_READY;

#if RTOS_PROFILING_SYSTEM_ENABLED
            if (task->priority > 0)
            {
                task->ready_timestamp = rtos_profiling_get_cycles();
            }
#endif

            preemptive_sp_add_to_ready_list_internal(task);

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

static rtos_task_handle_t preemptive_sp_get_highest_priority_ready(void)
{
    /* Use bitmask to quickly find highest priority with ready tasks */
    if (g_preemptive_sp_data.ready_priorities == 0)
    {
        return NULL; /* No ready tasks */
    }

    for (int8_t priority = RTOS_MAX_TASK_PRIORITIES - 1; priority >= 0; priority--)
    {
        if (g_preemptive_sp_data.ready_priorities & (1U << priority))
        {
            return g_preemptive_sp_data.ready_lists[priority];
        }
    }

    return NULL; /* Shouldn't reach here if bitmask is correct */
}

static rtos_status_t preemptive_sp_init(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    memset(g_preemptive_sp_data.ready_lists, 0, sizeof(g_preemptive_sp_data.ready_lists));
    g_preemptive_sp_data.delayed_list     = NULL;
    g_preemptive_sp_data.ready_priorities = 0;

    instance->private_data = &g_preemptive_sp_data;

    KLOGT(KEVT_SCHEDULER_INIT, 0, 0);
    return RTOS_SUCCESS;
}

static rtos_task_handle_t preemptive_sp_get_next_task(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return NULL;
    }

    return preemptive_sp_get_highest_priority_ready();
}

static bool preemptive_sp_should_preempt(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task)
{
    if (instance == NULL || new_task == NULL || g_kernel.current_task == NULL)
    {
        return false;
    }

    /* Preempt if new task has higher priority */
    return (new_task != g_kernel.current_task && new_task->priority > g_kernel.current_task->priority);
}

static void preemptive_sp_task_completed(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task)
{
    if (instance == NULL || completed_task == NULL)
    {
        return;
    }

    /* no-op: state management is handled by the kernel */
}

static void preemptive_sp_add_to_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_add_to_ready_list_internal(task_handle);
}

static void preemptive_sp_remove_from_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_remove_from_ready_list_internal(task_handle);
}

static void preemptive_sp_add_to_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle,
                                              rtos_tick_t delay_ticks)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_add_to_delayed_list_internal(task_handle, delay_ticks);
}

static void preemptive_sp_remove_from_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_remove_from_delayed_list_internal(task_handle);
}

static void preemptive_sp_update_delayed_tasks(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    preemptive_sp_update_delayed_tasks_internal();
}

static size_t preemptive_sp_get_statistics(rtos_scheduler_instance_t *instance, void *stats_buffer, size_t buffer_size)
{
    if (instance == NULL || stats_buffer == NULL || buffer_size == 0)
    {
        return 0;
    }

    typedef struct
    {
        uint8_t     ready_priorities_mask;
        uint8_t     num_ready_tasks;
        uint8_t     num_delayed_tasks;
        rtos_tick_t current_tick;
    } preemptive_sp_stats_t;

    if (buffer_size < sizeof(preemptive_sp_stats_t))
    {
        return 0;
    }

    preemptive_sp_stats_t *stats = (preemptive_sp_stats_t *) stats_buffer;

    stats->ready_priorities_mask = g_preemptive_sp_data.ready_priorities;
    stats->current_tick          = rtos_get_tick_count();

    uint8_t ready_count = 0;
    for (uint8_t i = 0; i < RTOS_MAX_TASK_PRIORITIES; i++)
    {
        rtos_tcb_t *task = g_preemptive_sp_data.ready_lists[i];
        while (task != NULL)
        {
            ready_count++;
            task = task->next;
        }
    }
    stats->num_ready_tasks = ready_count;

    uint8_t     delayed_count = 0;
    rtos_tcb_t *task          = g_preemptive_sp_data.delayed_list;
    while (task != NULL)
    {
        delayed_count++;
        task = task->next;
    }
    stats->num_delayed_tasks = delayed_count;

    return sizeof(preemptive_sp_stats_t);
}

const rtos_scheduler_t preemptive_sp_scheduler = {
    /* Core scheduling functions */
    .init           = preemptive_sp_init,
    .get_next_task  = preemptive_sp_get_next_task,
    .should_preempt = preemptive_sp_should_preempt,
    .task_completed = preemptive_sp_task_completed,

    /* List management operations */
    .add_to_ready_list        = preemptive_sp_add_to_ready_list,
    .remove_from_ready_list   = preemptive_sp_remove_from_ready_list,
    .add_to_delayed_list      = preemptive_sp_add_to_delayed_list,
    .remove_from_delayed_list = preemptive_sp_remove_from_delayed_list,
    .update_delayed_tasks     = preemptive_sp_update_delayed_tasks,

    /* Optional statistics */
    .get_statistics = preemptive_sp_get_statistics};

rtos_tcb_t *rtos_task_get_highest_priority_ready(void)
{
    return preemptive_sp_get_highest_priority_ready();
}