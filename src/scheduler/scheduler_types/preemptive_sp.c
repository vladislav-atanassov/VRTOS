/*******************************************************************************
 * File: src/scheduler/preemptive_sp.c
 * Description: Preemptive static priority-based Scheduler with Integrated List Management
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "preemptive_sp.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "log.h"
#include "scheduler.h"
#include "task_priv.h"

#include <string.h>

/**
 * @file preemptive_sp.c
 * @brief Preemptive static priority-based Scheduler Implementation with List Management
 *
 * This implementation includes scheduler-specific list management operations
 * optimized for Preemptive static priority-based Scheduler scheduling.
 */

/* =================== Preemptive static priority-based Scheduler List Management Functions =================== */

/**
 * @brief Add task to Preemptive static priority-based Scheduler ready list (priority-based)
 */
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
        /* First task in this priority level */
        *list_head = task;
        g_preemptive_sp_data.ready_priorities |= (1U << priority);
    }
    else
    {
        /* Find tail and append */
        rtos_tcb_t *current = *list_head;
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = task;
        task->prev    = current;
    }

    log_debug("Preemptive static priority-based: Added task '%s' (prio=%d) to ready list",
              task->name ? task->name : "unnamed", priority);
}

/**
 * @brief Remove task from Preemptive static priority-based Scheduler ready list
 */
static void preemptive_sp_remove_from_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        return;
    }

    rtos_priority_t priority  = task->priority;
    rtos_tcb_t    **list_head = &g_preemptive_sp_data.ready_lists[priority];

    /* Remove from linked list */
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

    /* Clear priority bit if no more tasks at this priority */
    if (*list_head == NULL)
    {
        g_preemptive_sp_data.ready_priorities &= ~(1U << priority);
    }

    task->next = NULL;
    task->prev = NULL;

    log_debug("Preemptive static priority-based: Removed task '%s' (prio=%d) from ready list",
              task->name ? task->name : "unnamed", priority);
}

/**
 * @brief Add task to Preemptive static priority-based Scheduler delayed list (time-sorted)
 */
static void preemptive_sp_add_to_delayed_list_internal(rtos_task_handle_t task,
                                                       rtos_tick_t        delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    /* Calculate wakeup time */
    task->delay_until = rtos_get_tick_count() + delay_ticks;

    /* Insert in time-sorted order */
    rtos_tcb_t **list_head = &g_preemptive_sp_data.delayed_list;

    task->next = NULL;
    task->prev = NULL;

    if (*list_head == NULL)
    {
        /* Empty delayed list */
        *list_head = task;
        return;
    }

    /* Find insertion point (sorted by delay_until) */
    rtos_tcb_t *current = *list_head;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && current->delay_until <= task->delay_until)
    {
        prev    = current;
        current = current->next;
    }

    /* Insert task */
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

    log_debug(
        "Preemptive static priority-based: Added task '%s' to delayed list, wakeup at tick %lu",
        task->name ? task->name : "unnamed", task->delay_until);
}

/**
 * @brief Remove task from Preemptive static priority-based Scheduler delayed list
 */
static void preemptive_sp_remove_from_delayed_list_internal(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    rtos_tcb_t **list_head = &g_preemptive_sp_data.delayed_list;

    /* Remove from linked list */
    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else if (*list_head == task)
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

    log_debug("Preemptive static priority-based: Removed task '%s' from delayed list",
              task->name ? task->name : "unnamed");
}

/**
 * @brief Update delayed tasks for Preemptive static priority-based Scheduler
 */
static void preemptive_sp_update_delayed_tasks_internal(void)
{
    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tcb_t *task         = g_preemptive_sp_data.delayed_list;

    /* Check delayed tasks (list is time-sorted, so we can break early) */
    while (task != NULL)
    {
        rtos_tcb_t *next_task = task->next;

        if (current_tick >= task->delay_until)
        {
            /* Task delay expired - move to ready list */
            preemptive_sp_remove_from_delayed_list_internal(task);
            task->state = RTOS_TASK_STATE_READY;
            preemptive_sp_add_to_ready_list_internal(task);

            log_debug(
                "Preemptive static priority-based: Task '%s' delay expired, moved to ready list",
                task->name ? task->name : "unnamed");
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
 * @brief Get highest priority ready task for Preemptive static priority-based Scheduler
 */
static rtos_task_handle_t preemptive_sp_get_highest_priority_ready(void)
{
    /* Use bitmask to quickly find highest priority with ready tasks */
    if (g_preemptive_sp_data.ready_priorities == 0)
    {
        return NULL; /* No ready tasks */
    }

    /* Find highest set bit (highest priority) */
    for (int8_t priority = RTOS_MAX_TASK_PRIORITIES - 1; priority >= 0; priority--)
    {
        if (g_preemptive_sp_data.ready_priorities & (1U << priority))
        {
            return g_preemptive_sp_data.ready_lists[priority];
        }
    }

    return NULL; /* Shouldn't reach here if bitmask is correct */
}

/* =================== Preemptive static priority-based Scheduler Interface Implementation =================== */

/**
 * @brief Initialize Preemptive static priority-based Scheduler
 */
static rtos_status_t preemptive_sp_init(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    /*Initialize Preemptive static priority-based Scheduler data structures */
    memset(g_preemptive_sp_data.ready_lists, 0, sizeof(g_preemptive_sp_data.ready_lists));
    g_preemptive_sp_data.delayed_list     = NULL;
    g_preemptive_sp_data.ready_priorities = 0;

    /* Set up private data */
    instance->private_data = &g_preemptive_sp_data;

    log_debug("Preemptive static priority-based scheduler initialized");
    return RTOS_SUCCESS;
}

/**
 * @brief Get next task using Preemptive static priority-based Scheduler algorithm (highest
 * priority)
 */
static rtos_task_handle_t preemptive_sp_get_next_task(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return NULL;
    }

    return preemptive_sp_get_highest_priority_ready();
}

/**
 * @brief Check if Preemptive static priority-based Scheduler preemption is needed
 */
static bool preemptive_sp_should_preempt(rtos_scheduler_instance_t *instance,
                                         rtos_task_handle_t         new_task)
{
    if (instance == NULL || new_task == NULL || g_kernel.current_task == NULL)
    {
        return false;
    }

    /* Preempt if new task has higher priority */
    return (new_task != g_kernel.current_task &&
            new_task->priority > g_kernel.current_task->priority);
}

/**
 * @brief Handle task completion for Preemptive static priority-based Scheduler
 */
static void preemptive_sp_task_completed(rtos_scheduler_instance_t *instance,
                                         rtos_task_handle_t         completed_task)
{
    if (instance == NULL || completed_task == NULL)
    {
        return;
    }

    /* Preemptive static priority-based Scheduler doesn't need special handling for task completion */
    /* Task state management is handled by the kernel and list operations */
}

/* ============= Preemptive static priority-based Scheduler List Management Interface Implementation ============== */

/**
 * @brief Add task to ready list (Preemptive static priority-based interface)
 */
static void preemptive_sp_add_to_ready_list(rtos_scheduler_instance_t *instance,
                                            rtos_task_handle_t         task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_add_to_ready_list_internal(task_handle);
}

/**
 * @brief Remove task from ready list (Preemptive static priority-based interface)
 */
static void preemptive_sp_remove_from_ready_list(rtos_scheduler_instance_t *instance,
                                                 rtos_task_handle_t         task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_remove_from_ready_list_internal(task_handle);
}

/**
 * @brief Add task to delayed list (Preemptive static priority-based interface)
 */
static void preemptive_sp_add_to_delayed_list(rtos_scheduler_instance_t *instance,
                                              rtos_task_handle_t         task_handle,
                                              rtos_tick_t                delay_ticks)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_add_to_delayed_list_internal(task_handle, delay_ticks);
}

/**
 * @brief Remove task from delayed list (Preemptive static priority-based interface)
 */
static void preemptive_sp_remove_from_delayed_list(rtos_scheduler_instance_t *instance,
                                                   rtos_task_handle_t         task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    preemptive_sp_remove_from_delayed_list_internal(task_handle);
}

/**
 * @brief Update delayed tasks (Preemptive static priority-based interface)
 */
static void preemptive_sp_update_delayed_tasks(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    preemptive_sp_update_delayed_tasks_internal();
}

/**
 * @brief Get Preemptive static priority-based Scheduler statistics (optional)
 */
static size_t preemptive_sp_get_statistics(rtos_scheduler_instance_t *instance, void *stats_buffer,
                                           size_t buffer_size)
{
    if (instance == NULL || stats_buffer == NULL || buffer_size == 0)
    {
        return 0;
    }

    /* Simple statistics structure for Preemptive static priority-based Scheduler */
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

    /* Count ready tasks */
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

    /* Count delayed tasks */
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

/* =================== Preemptive static priority-based Scheduler Vtable =================== */

/**
 * @brief Preemptive static priority-based Scheduler vtable interface
 *
 * This vtable provides the complete interface implementation for the Preemptive static
 * priority-based Scheduler scheduler, including the new list management operations.
 */
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

/* =================== Public Helper Functions =================== */

/**
 * @brief Get highest priority ready task (public interface for kernel)
 *
 * This function is used by the kernel when it needs direct access
 * to the highest priority ready task without going through the scheduler
 * interface.
 */
rtos_tcb_t *rtos_task_get_highest_priority_ready(void)
{
    return preemptive_sp_get_highest_priority_ready();
}