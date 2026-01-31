/*******************************************************************************
 * File: src/scheduler/cooperative.c
 * Description: Cooperative Scheduler with Integrated List Management
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "cooperative.h"

#include "VRTOS.h"
#include "log.h"
#include "scheduler.h"
#include "task_priv.h"

#include <string.h>

/**
 * @file cooperative.c
 * @brief Cooperative Scheduler Implementation with List Management
 * This implementation provides a non-preemptive cooperative scheduler
 * where tasks run until they voluntarily yield control.
 */

/* ============ Cooperative List Management Functions ============ */

/**
 * @brief Add task to cooperative ready list (FIFO order)
 */
static void cooperative_add_to_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    /* Add to end of FIFO ready list */
    task->next = NULL;
    task->prev = NULL;

    if (g_cooperative_data.ready_list == NULL)
    {
        /* First task in ready list */
        g_cooperative_data.ready_list = task;
    }
    else
    {
        /* Find tail and append */
        rtos_tcb_t *current = g_cooperative_data.ready_list;
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = task;
        task->prev    = current;
    }

    g_cooperative_data.ready_count++;

    log_debug("Cooperative: Added task '%s' to ready list (total ready: %d)", task->name ? task->name : "unnamed",
              g_cooperative_data.ready_count);
}

/**
 * @brief Remove task from cooperative ready list
 */
static void cooperative_remove_from_ready_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_cooperative_data.ready_list == NULL)
    {
        return;
    }

    /* Remove from linked list */
    if (task->prev != NULL)
    {
        task->prev->next = task->next;
    }
    else
    {
        /* Task is at head */
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

    log_debug("Cooperative: Removed task '%s' from ready list (remaining: %d)", task->name ? task->name : "unnamed",
              g_cooperative_data.ready_count);
}

/**
 * @brief Add task to cooperative delayed list (time-sorted)
 */
static void cooperative_add_to_delayed_list_internal(rtos_task_handle_t task, rtos_tick_t delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    /* Calculate wakeup time */
    task->delay_until = rtos_get_tick_count() + delay_ticks;

    /* Insert in time-sorted order */
    rtos_tcb_t **list_head = &g_cooperative_data.delayed_list;

    task->next = NULL;
    task->prev = NULL;

    if (*list_head == NULL)
    {
        /* Empty delayed list */
        *list_head                       = task;
        g_cooperative_data.delayed_count = 1;
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

    g_cooperative_data.delayed_count++;

    log_debug("Cooperative: Added task '%s' to delayed list, wakeup at tick "
              "%lu (total delayed: %d)",
              task->name ? task->name : "unnamed", (unsigned long) task->delay_until, g_cooperative_data.delayed_count);
}

/**
 * @brief Remove task from cooperative delayed list
 */
static void cooperative_remove_from_delayed_list_internal(rtos_task_handle_t task)
{
    if (task == NULL || g_cooperative_data.delayed_list == NULL)
    {
        return;
    }

    rtos_tcb_t **list_head = &g_cooperative_data.delayed_list;

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

    task->next = NULL;
    task->prev = NULL;

    if (g_cooperative_data.delayed_count > 0)
    {
        g_cooperative_data.delayed_count--;
    }

    log_debug("Cooperative: Removed task '%s' from delayed list (remaining: %d)", task->name ? task->name : "unnamed",
              g_cooperative_data.delayed_count);
}

/**
 * @brief Update delayed tasks for cooperative scheduler
 */
static void cooperative_update_delayed_tasks_internal(void)
{
    rtos_tick_t current_tick = rtos_get_tick_count();
    rtos_tcb_t *task         = g_cooperative_data.delayed_list;

    /* Check delayed tasks (list is time-sorted, so we can break early) */
    while (task != NULL)
    {
        rtos_tcb_t *next_task = task->next;

        if (current_tick >= task->delay_until)
        {
            /* Task delay expired - move to ready list */
            cooperative_remove_from_delayed_list_internal(task);
            task->state = RTOS_TASK_STATE_READY;
            cooperative_add_to_ready_list_internal(task);

            log_debug("Cooperative: Task '%s' delay expired, moved to ready list", task->name ? task->name : "unnamed");
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
 * @brief Get next ready task for cooperative scheduler (FIFO)
 */
static rtos_task_handle_t cooperative_get_next_ready(void)
{
    /* Return the first task in the FIFO ready list */
    return g_cooperative_data.ready_list;
}

/* ============ Cooperative Scheduler Interface Implementation ============ */

/**
 * @brief Initialize cooperative scheduler
 */
static rtos_status_t cooperative_init(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    /* Initialize cooperative data structures */
    g_cooperative_data.ready_list    = NULL;
    g_cooperative_data.delayed_list  = NULL;
    g_cooperative_data.ready_count   = 0;
    g_cooperative_data.delayed_count = 0;

    /* Set up private data */
    instance->private_data = &g_cooperative_data;

    log_debug("Cooperative scheduler initialized");
    return RTOS_SUCCESS;
}

/**
 * @brief Get next task using cooperative algorithm (FIFO)
 */
static rtos_task_handle_t cooperative_get_next_task(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return NULL;
    }

    return cooperative_get_next_ready();
}

/**
 * @brief Check if cooperative preemption is needed (always false for
 * cooperative)
 */
static bool cooperative_should_preempt(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task)
{
    /* Cooperative scheduler is non-preemptive */
    /* Tasks run until they voluntarily yield or block */
    return false;
}

/**
 * @brief Handle task completion for cooperative scheduler
 */
static void cooperative_task_completed(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task)
{
    if (instance == NULL || completed_task == NULL)
    {
        return;
    }

    /* For cooperative scheduling, when a task completes (yields), */
    /* we can optionally move it to the end of the ready list for round-robin */
    /* behavior */
    if (completed_task->state == RTOS_TASK_STATE_READY)
    {
        /* Remove from current position and add to end for round-robin */
        cooperative_remove_from_ready_list_internal(completed_task);
        cooperative_add_to_ready_list_internal(completed_task);

        log_debug("Cooperative: Task '%s' yielded, moved to end of ready list",
                  completed_task->name ? completed_task->name : "unnamed");
    }
}

/* ========= Cooperative List Management Interface Implementation ========= */

/**
 * @brief Add task to ready list (cooperative interface)
 */
static void cooperative_add_to_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_add_to_ready_list_internal(task_handle);
}

/**
 * @brief Remove task from ready list (cooperative interface)
 */
static void cooperative_remove_from_ready_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_remove_from_ready_list_internal(task_handle);
}

/**
 * @brief Add task to delayed list (cooperative interface)
 */
static void cooperative_add_to_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle,
                                            rtos_tick_t delay_ticks)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_add_to_delayed_list_internal(task_handle, delay_ticks);
}

/**
 * @brief Remove task from delayed list (cooperative interface)
 */
static void cooperative_remove_from_delayed_list(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle)
{
    if (instance == NULL || task_handle == NULL)
    {
        return;
    }

    cooperative_remove_from_delayed_list_internal(task_handle);
}

/**
 * @brief Update delayed tasks (cooperative interface)
 */
static void cooperative_update_delayed_tasks(rtos_scheduler_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    cooperative_update_delayed_tasks_internal();
}

/**
 * @brief Get cooperative statistics
 */
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

/* =================== Cooperative Scheduler Vtable =================== */

/**
 * @brief Cooperative scheduler vtable interface
 *
 * This vtable provides the complete interface implementation for the
 * cooperative scheduler.
 */
const rtos_scheduler_t cooperative_scheduler = {
    /* Core scheduling functions */
    .init           = cooperative_init,
    .get_next_task  = cooperative_get_next_task,
    .should_preempt = cooperative_should_preempt,
    .task_completed = cooperative_task_completed,

    /* List management operations */
    .add_to_ready_list        = cooperative_add_to_ready_list,
    .remove_from_ready_list   = cooperative_remove_from_ready_list,
    .add_to_delayed_list      = cooperative_add_to_delayed_list,
    .remove_from_delayed_list = cooperative_remove_from_delayed_list,
    .update_delayed_tasks     = cooperative_update_delayed_tasks,

    /* Optional statistics */
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
