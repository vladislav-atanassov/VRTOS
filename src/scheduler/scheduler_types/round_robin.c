#include "round_robin.h"

#include "KARTOS.h"
#include "config.h"
#include "kernel_priv.h"
#include "klog.h"
#include "scheduler.h"
#include "task_priv.h"

#include <string.h>

round_robin_private_data_t g_round_robin_data = {.ready_lists      = {NULL},
                                                 .ready_lists_tail = {NULL},
                                                 .delayed_list     = NULL,
                                                 .current_task     = NULL,
                                                 .slice_remaining  = 0,
                                                 .ready_priorities = 0,
                                                 .ready_count      = 0,
                                                 .delayed_count    = 0};

static void round_robin_add_to_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        return;
    }

    rtos_priority_t priority = task->priority;
    rtos_tcb_t    **head     = &g_round_robin_data.ready_lists[priority];
    rtos_tcb_t    **tail     = &g_round_robin_data.ready_lists_tail[priority];

    task->next = NULL;
    task->prev = *tail; /* NULL when the bucket is empty */

    if (*head == NULL)
    {
        *head = task;
        g_round_robin_data.ready_priorities |= (uint8_t) (1U << priority);
    }
    else
    {
        (*tail)->next = task;
    }
    *tail = task;

    g_round_robin_data.ready_count++;

    KLOGT("Sched/RR", "Ready id=%u count=%u", task->task_id, g_round_robin_data.ready_count);
}

static void round_robin_remove_from_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        return;
    }

    rtos_priority_t priority = task->priority;
    rtos_tcb_t    **head     = &g_round_robin_data.ready_lists[priority];
    rtos_tcb_t    **tail     = &g_round_robin_data.ready_lists_tail[priority];

    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        *head = task->next;
    }

    if (task->next != NULL)
    {
        task->next->prev = task->prev;
    }
    else
    {
        /* Removing the tail; predecessor (or NULL if bucket now empty) becomes the new tail. */
        *tail = task->prev;
    }

    if (*head == NULL)
    {
        g_round_robin_data.ready_priorities &= (uint8_t) ~(1U << priority);
    }

    task->next = NULL;
    task->prev = NULL;

    if (g_round_robin_data.ready_count > 0)
    {
        g_round_robin_data.ready_count--;
    }

    KLOGT("Sched/RR", "ReadyRm id=%u count=%u", task->task_id, g_round_robin_data.ready_count);
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

    rtos_tcb_t *current = *list_head;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && (int32_t) (current->delay_until - task->delay_until) <= 0)
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

    KLOGT("Sched/RR", "Delayed id=%u until=%u", task->task_id, (uint32_t) task->delay_until);
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

    KLOGT("Sched/RR", "DelayedRm id=%u count=%u", task->task_id, g_round_robin_data.delayed_count);
}

static void round_robin_update_delayed_tasks_internal(void)
{
    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tcb_t *task         = g_round_robin_data.delayed_list;

    while (task != NULL)
    {
        rtos_tcb_t *next_task = task->next;

        if ((int32_t) (current_tick - task->delay_until) >= 0)
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

            KLOGT("Sched/RR", "DelayExpired id=%u", task->task_id);
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
    uint32_t mask = g_round_robin_data.ready_priorities;
    if (mask == 0U)
    {
        return NULL;
    }
    /* CLZ-based O(1) lookup of the highest-priority non-empty bucket. */
    uint32_t priority = 31U - (uint32_t) __builtin_clz(mask);
    return g_round_robin_data.ready_lists[priority];
}

static rtos_status_t round_robin_init(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    memset(g_round_robin_data.ready_lists, 0, sizeof(g_round_robin_data.ready_lists));
    memset(g_round_robin_data.ready_lists_tail, 0, sizeof(g_round_robin_data.ready_lists_tail));
    g_round_robin_data.delayed_list     = NULL;
    g_round_robin_data.current_task     = NULL;
    g_round_robin_data.slice_remaining  = RTOS_TIME_SLICE_TICKS;
    g_round_robin_data.ready_priorities = 0;
    g_round_robin_data.ready_count      = 0;
    g_round_robin_data.delayed_count    = 0;

    instance->private_data = &g_round_robin_data;

    KLOGT("Sched/RR", "RRInit slice=%u", RTOS_TIME_SLICE_TICKS);
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

static bool round_robin_should_preempt(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task)
{
    if (instance == NULL || new_task == NULL)
    {
        return false;
    }

    rtos_tcb_t *current = g_kernel.current_task;
    if (current == NULL)
    {
        return false;
    }

    /* Higher priority always wins — preempt the current task. */
    if (new_task->priority > current->priority)
    {
        return true;
    }

    /* Lower priority can never preempt. */
    if (new_task->priority < current->priority)
    {
        return false;
    }

    /* Equal priority: only advance the slice on the tick-driven path.
     * O(1) head lookup of the bucket the new_task lives in. */
    if (new_task != g_round_robin_data.ready_lists[new_task->priority])
    {
        return false;
    }

    if (g_round_robin_data.slice_remaining > 0)
    {
        g_round_robin_data.slice_remaining--;
    }

    if (g_round_robin_data.slice_remaining == 0)
    {
        KLOGT("Sched/RR", "TimeSlice");
        return true;
    }

    return false;
}

static void round_robin_task_completed(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task)
{
    if (instance == NULL || completed_task == NULL)
    {
        return;
    }

    if (completed_task->state == RTOS_TASK_STATE_READY)
    {
        /* Rotate within the task's priority bucket: pop from head, append at tail. */
        round_robin_remove_from_ready_list_internal(completed_task);
        round_robin_add_to_ready_list_internal(completed_task);

        g_round_robin_data.slice_remaining = RTOS_TIME_SLICE_TICKS;
        g_round_robin_data.current_task    = NULL;

        KLOGT("Sched/RR", "Rotate id=%u", completed_task->task_id);
    }
}

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

static uint32_t round_robin_get_expected_idle_ticks(rtos_scheduler_instance_t *instance)
{
    (void) instance;
    if (g_round_robin_data.delayed_list == NULL)
    {
        return 0xFFFFFFFF; /* Max wait */
    }

    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tick_t next_wake    = g_round_robin_data.delayed_list->delay_until;

    if ((int32_t) (next_wake - current_tick) <= 0)
    {
        return 0;
    }
    return (uint32_t) (next_wake - current_tick);
}

static size_t round_robin_get_statistics(rtos_scheduler_instance_t *instance, void *stats_buffer, size_t buffer_size)
{
    if (instance == NULL || stats_buffer == NULL || buffer_size == 0)
    {
        return 0;
    }

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

const rtos_scheduler_t round_robin_scheduler = {.init           = round_robin_init,
                                                .get_next_task  = round_robin_get_next_task,
                                                .should_preempt = round_robin_should_preempt,
                                                .task_completed = round_robin_task_completed,

                                                .add_to_ready_list        = round_robin_add_to_ready_list,
                                                .remove_from_ready_list   = round_robin_remove_from_ready_list,
                                                .add_to_delayed_list      = round_robin_add_to_delayed_list,
                                                .remove_from_delayed_list = round_robin_remove_from_delayed_list,
                                                .update_delayed_tasks     = round_robin_update_delayed_tasks,
                                                .get_expected_idle_ticks  = round_robin_get_expected_idle_ticks,

                                                .get_statistics = round_robin_get_statistics};
