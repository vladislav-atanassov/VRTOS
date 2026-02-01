/*******************************************************************************
 * File: src/sync/queue/queue.c
 * Description: Fixed Queue Implementation with Proper Timeout Handling
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "queue.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "log.h"
#include "memory.h"
#include "queue_priv.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task.h"
#include "task_priv.h"

#include <string.h>


/**
 * @file queue.c
 * @brief Fixed Queue Implementation
 *
 * This implementation provides:
 * - Proper timeout detection and cleanup
 * - Priority-ordered wait lists
 * - Clean scheduler integration
 * - Robust error handling
 */

/* =================== Internal Helper Functions =================== */

/**
 * @brief Add task to queue wait list (priority-ordered, highest first)
 * @param list_head Pointer to list head pointer
 * @param task Task to add
 */
static void queue_add_to_waiting_list(rtos_tcb_t **list_head, rtos_tcb_t *task, void *queue)
{
    task->next_waiting    = NULL;
    task->blocked_on      = queue;
    task->blocked_on_type = RTOS_SYNC_TYPE_QUEUE;

    if (*list_head == NULL)
    {
        /* First waiter */
        *list_head = task;
        return;
    }

    /* Insert in priority order (highest priority at head) */
    rtos_tcb_t *current = *list_head;
    rtos_tcb_t *prev    = NULL;

    while (current != NULL && current->priority >= task->priority)
    {
        prev    = current;
        current = current->next_waiting;
    }

    if (prev == NULL)
    {
        /* Insert at head */
        task->next_waiting = *list_head;
        *list_head         = task;
    }
    else
    {
        /* Insert after prev */
        task->next_waiting = current;
        prev->next_waiting = task;
    }
}

/**
 * @brief Remove task from queue wait list
 * @param list_head Pointer to list head pointer
 * @param task Task to remove
 */
static void queue_remove_from_waiting_list(rtos_tcb_t **list_head, rtos_tcb_t *task)
{
    if (*list_head == NULL || task == NULL)
    {
        return;
    }

    if (*list_head == task)
    {
        /* Task is at head */
        *list_head = task->next_waiting;
    }
    else
    {
        /* Find and remove task */
        rtos_tcb_t *current = *list_head;
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
 * @param list_head Pointer to list head pointer
 * @return Highest priority waiting task, or NULL
 */
static rtos_tcb_t *queue_pop_highest_priority_waiter(rtos_tcb_t **list_head)
{
    if (*list_head == NULL)
    {
        return NULL;
    }

    /* Head is always highest priority due to ordered insertion */
    rtos_tcb_t *task      = *list_head;
    *list_head            = task->next_waiting;
    task->next_waiting    = NULL;
    task->blocked_on      = NULL;
    task->blocked_on_type = RTOS_SYNC_TYPE_NONE;

    return task;
}

/* =================== Queue Initialization =================== */

/**
 * @brief Initialize a queue structure
 */
static rtos_status_t rtos_queue_init(rtos_queue_t *queue, uint32_t item_count, uint32_t item_size)
{
    if (queue == NULL || item_count == 0 || item_size == 0)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    /* Allocate buffer for queue items */
    queue->buffer = rtos_malloc(item_count * item_size);
    if (queue->buffer == NULL)
    {
        log_error("Queue buffer allocation failed");
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Initialize queue control structure */
    queue->item_size          = item_size;
    queue->length             = item_count;
    queue->count              = 0;
    queue->read_ptr           = queue->buffer;
    queue->write_ptr          = queue->buffer;
    queue->sender_wait_list   = NULL;
    queue->receiver_wait_list = NULL;

    log_debug("Queue initialized: items=%lu, size=%lu bytes", (unsigned long) item_count,
              (unsigned long) item_size);

    return RTOS_SUCCESS;
}

/**
 * @brief Create a new queue
 */
rtos_status_t rtos_queue_create(rtos_queue_handle_t *queue_handle, uint32_t item_count,
                                uint32_t item_size)
{
    if (queue_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    /* Allocate queue control structure */
    rtos_queue_t *queue = (rtos_queue_t *) rtos_malloc(sizeof(rtos_queue_t));
    if (queue == NULL)
    {
        log_error("Queue structure allocation failed");
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Initialize the queue */
    rtos_status_t status = rtos_queue_init(queue, item_count, item_size);
    if (status != RTOS_SUCCESS)
    {
        rtos_free(queue);
        return status;
    }

    *queue_handle = queue;
    log_info("Queue created: handle=0x%p, capacity=%lu, item_size=%lu", queue,
             (unsigned long) item_count, (unsigned long) item_size);

    return RTOS_SUCCESS;
}

/* =================== Queue Send Operation =================== */

/**
 * @brief Send an item to the queue (with proper timeout handling)
 */
rtos_status_t rtos_queue_send(rtos_queue_handle_t queue_handle, const void *item_ptr,
                              rtos_tick_t timeout_ticks)
{
    if (queue_handle == NULL || item_ptr == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();

    /* Fast path: Queue has space */
    if (queue->count < queue->length)
    {
        goto copy_data;
    }

    /* Queue is full - handle blocking */
    if (timeout_ticks == 0)
    {
        /* Non-blocking mode - return immediately */
        rtos_port_exit_critical();
        log_debug("Queue send failed: queue full (non-blocking)");
        return RTOS_ERROR_FULL;
    }

    /* Block the current task */
    rtos_tcb_t *current_task = g_kernel.current_task;
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        log_error("Queue send called with no current task!");
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Add to sender wait list (priority-ordered) */
    queue_add_to_waiting_list(&queue->sender_wait_list, current_task, queue);

    log_debug("Task '%s' blocking on queue send (timeout=%lu)",
              current_task->name ? current_task->name : "unnamed", (unsigned long) timeout_ticks);

    /* Block the task using kernel helper */
    if (timeout_ticks == RTOS_MAX_DELAY)
    {
        /* Infinite wait */
        current_task->state = RTOS_TASK_STATE_BLOCKED;
        rtos_scheduler_remove_from_ready_list(current_task);
        rtos_port_exit_critical();
        rtos_yield();
    }
    else
    {
        /* Timed wait */
        rtos_port_exit_critical();
        rtos_kernel_task_block(current_task, timeout_ticks);
    }

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    /* Check if we were woken by receive (blocked_on cleared) or timeout */
    if (current_task->blocked_on == queue)
    {
        /**
         * Still blocked on queue = timeout occurred
         * We need to remove ourselves from the wait list
         */
        queue_remove_from_waiting_list(&queue->sender_wait_list, current_task);
        rtos_port_exit_critical();

        log_debug("Task '%s' queue send timed out",
                  current_task->name ? current_task->name : "unnamed");
        return RTOS_ERROR_TIMEOUT;
    }

    /* Check again if queue has space (should always be true here) */
    if (queue->count >= queue->length)
    {
        /* This shouldn't happen - defensive programming */
        rtos_port_exit_critical();
        log_error("Queue send: woken but queue still full!");
        return RTOS_ERROR_FULL;
    }

copy_data:
    /* Copy data to queue buffer */
    memcpy(queue->write_ptr, item_ptr, queue->item_size);

    /* Advance write pointer (circular buffer) */
    queue->write_ptr = (uint8_t *) queue->write_ptr + queue->item_size;
    if ((uint8_t *) queue->write_ptr >=
        (uint8_t *) queue->buffer + (queue->length * queue->item_size))
    {
        queue->write_ptr = queue->buffer; /* Wrap around */
    }

    queue->count++;

    log_debug("Queue item sent (count now %lu)", (unsigned long) queue->count);

    /* Wake up a waiting receiver if any */
    rtos_tcb_t *waiting_receiver = queue_pop_highest_priority_waiter(&queue->receiver_wait_list);
    if (waiting_receiver != NULL)
    {
        log_debug("Waking waiting receiver '%s'",
                  waiting_receiver->name ? waiting_receiver->name : "unnamed");

        rtos_port_exit_critical();

        /* Unblock the waiting task using kernel helper */
        rtos_kernel_task_unblock(waiting_receiver);

        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();
    return RTOS_SUCCESS;
}

/* =================== Queue Receive Operation =================== */

/**
 * @brief Receive an item from the queue (with proper timeout handling)
 */
rtos_status_t rtos_queue_receive(rtos_queue_handle_t queue_handle, void *buffer,
                                 rtos_tick_t timeout_ticks)
{
    if (queue_handle == NULL || buffer == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();

    /* Fast path: Queue has data */
    if (queue->count > 0)
    {
        goto copy_data;
    }

    /* Queue is empty - handle blocking */
    if (timeout_ticks == 0)
    {
        /* Non-blocking mode - return immediately */
        rtos_port_exit_critical();
        log_debug("Queue receive failed: queue empty (non-blocking)");
        return RTOS_ERROR_EMPTY;
    }

    /* Block the current task */
    rtos_tcb_t *current_task = g_kernel.current_task;
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        log_error("Queue receive called with no current task!");
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Add to receiver wait list (priority-ordered) */
    queue_add_to_waiting_list(&queue->receiver_wait_list, current_task, queue);

    log_debug("Task '%s' blocking on queue receive (timeout=%lu)",
              current_task->name ? current_task->name : "unnamed", (unsigned long) timeout_ticks);

    /* Block the task using kernel helper */
    if (timeout_ticks == RTOS_MAX_DELAY)
    {
        /* Infinite wait */
        current_task->state = RTOS_TASK_STATE_BLOCKED;
        rtos_scheduler_remove_from_ready_list(current_task);
        rtos_port_exit_critical();
        rtos_yield();
    }
    else
    {
        /* Timed wait */
        rtos_port_exit_critical();
        rtos_kernel_task_block(current_task, timeout_ticks);
    }

    /* --- Task resumes here after unblock or timeout --- */

    rtos_port_enter_critical();

    /* Check if we were woken by send (blocked_on cleared) or timeout */
    if (current_task->blocked_on == queue)
    {
        /**
         * Still blocked on queue = timeout occurred
         * We need to remove ourselves from the wait list
         */
        queue_remove_from_waiting_list(&queue->receiver_wait_list, current_task);
        rtos_port_exit_critical();

        log_debug("Task '%s' queue receive timed out",
                  current_task->name ? current_task->name : "unnamed");
        return RTOS_ERROR_TIMEOUT;
    }

    /* Check again if queue has data (should always be true here) */
    if (queue->count == 0)
    {
        /* This shouldn't happen - defensive programming */
        rtos_port_exit_critical();
        log_error("Queue receive: woken but queue still empty!");
        return RTOS_ERROR_EMPTY;
    }

copy_data:
    /* Copy data from queue buffer */
    memcpy(buffer, queue->read_ptr, queue->item_size);

    /* Advance read pointer (circular buffer) */
    queue->read_ptr = (uint8_t *) queue->read_ptr + queue->item_size;
    if ((uint8_t *) queue->read_ptr >=
        (uint8_t *) queue->buffer + (queue->length * queue->item_size))
    {
        queue->read_ptr = queue->buffer; /* Wrap around */
    }

    queue->count--;

    log_debug("Queue item received (count now %lu)", (unsigned long) queue->count);

    /* Wake up a waiting sender if any */
    rtos_tcb_t *waiting_sender = queue_pop_highest_priority_waiter(&queue->sender_wait_list);
    if (waiting_sender != NULL)
    {
        log_debug("Waking waiting sender '%s'",
                  waiting_sender->name ? waiting_sender->name : "unnamed");

        rtos_port_exit_critical();

        /* Unblock the waiting task using kernel helper */
        rtos_kernel_task_unblock(waiting_sender);

        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();
    return RTOS_SUCCESS;
}

/* =================== Queue Query Functions =================== */

/**
 * @brief Get number of items currently in the queue
 */
uint32_t rtos_queue_messages_waiting(rtos_queue_handle_t queue_handle)
{
    if (queue_handle == NULL)
    {
        return 0;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    /* Atomic read of count */
    rtos_port_enter_critical();
    uint32_t count = queue->count;
    rtos_port_exit_critical();

    return count;
}

/**
 * @brief Get number of free spaces in the queue
 */
uint32_t rtos_queue_spaces_available(rtos_queue_handle_t queue_handle)
{
    if (queue_handle == NULL)
    {
        return 0;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();
    uint32_t spaces = queue->length - queue->count;
    rtos_port_exit_critical();

    return spaces;
}

/**
 * @brief Check if queue is full
 */
bool rtos_queue_is_full(rtos_queue_handle_t queue_handle)
{
    if (queue_handle == NULL)
    {
        return false;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();
    bool is_full = (queue->count >= queue->length);
    rtos_port_exit_critical();

    return is_full;
}

/**
 * @brief Check if queue is empty
 */
bool rtos_queue_is_empty(rtos_queue_handle_t queue_handle)
{
    if (queue_handle == NULL)
    {
        return true;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();
    bool is_empty = (queue->count == 0);
    rtos_port_exit_critical();

    return is_empty;
}

/**
 * @brief Reset queue to empty state
 * @warning: Does not wake waiting tasks - use with caution
 */
rtos_status_t rtos_queue_reset(rtos_queue_handle_t queue_handle)
{
    if (queue_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();

    /* Reset queue state */
    queue->count     = 0;
    queue->read_ptr  = queue->buffer;
    queue->write_ptr = queue->buffer;

    /* Wake all waiting senders since queue is now empty */
    while (queue->sender_wait_list != NULL)
    {
        rtos_tcb_t *sender = queue_pop_highest_priority_waiter(&queue->sender_wait_list);
        if (sender != NULL)
        {
            log_debug("Queue reset: waking sender '%s'", sender->name ? sender->name : "unnamed");
            rtos_kernel_task_unblock(sender);
        }
    }

    rtos_port_exit_critical();

    log_info("Queue reset");
    return RTOS_SUCCESS;
}