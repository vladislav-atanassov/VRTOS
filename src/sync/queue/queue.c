#include "queue.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "klog.h"
#include "memory.h"
#include "queue_priv.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task.h"
#include "task_priv.h"

#include <string.h>

static void queue_add_to_waiting_list(rtos_tcb_t **list_head, rtos_tcb_t *task, void *queue)
{
    task->next_waiting    = NULL;
    task->blocked_on      = queue;
    task->blocked_on_type = RTOS_SYNC_TYPE_QUEUE;

    if (*list_head == NULL)
    {
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
        task->next_waiting = *list_head;
        *list_head         = task;
    }
    else
    {
        task->next_waiting = current;
        prev->next_waiting = task;
    }
}

static void queue_remove_from_waiting_list(rtos_tcb_t **list_head, rtos_tcb_t *task)
{
    if (*list_head == NULL || task == NULL)
    {
        return;
    }

    if (*list_head == task)
    {
        *list_head = task->next_waiting;
    }
    else
    {
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

void rtos_queue_remove_task_from_wait(void *queue_ptr, rtos_tcb_t *task)
{
    rtos_queue_t *q = (rtos_queue_t *) queue_ptr;
    queue_remove_from_waiting_list(&q->sender_wait_list, task);
    queue_remove_from_waiting_list(&q->receiver_wait_list, task);
}

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

static rtos_status_t rtos_queue_init(rtos_queue_t *queue, uint32_t item_count, uint32_t item_size)
{
    if (queue == NULL || item_count == 0 || item_size == 0)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    queue->buffer = rtos_malloc(item_count * item_size);
    if (queue->buffer == NULL)
    {
        KLOGE(KEVT_ALLOC_FAIL, 0, 0);
        return RTOS_ERROR_NO_MEMORY;
    }

    queue->item_size          = item_size;
    queue->length             = item_count;
    queue->count              = 0;
    queue->read_ptr           = queue->buffer;
    queue->write_ptr          = queue->buffer;
    queue->sender_wait_list   = NULL;
    queue->receiver_wait_list = NULL;

    KLOGD(KEVT_QUEUE_INIT, item_count, item_size);

    return RTOS_SUCCESS;
}

rtos_status_t rtos_queue_create(rtos_queue_handle_t *queue_handle, uint32_t item_count, uint32_t item_size)
{
    if (queue_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_queue_t *queue = (rtos_queue_t *) rtos_malloc(sizeof(rtos_queue_t));
    if (queue == NULL)
    {
        KLOGE(KEVT_ALLOC_FAIL, 0, 0);
        return RTOS_ERROR_NO_MEMORY;
    }

    rtos_status_t status = rtos_queue_init(queue, item_count, item_size);
    if (status != RTOS_SUCCESS)
    {
        rtos_free(queue);
        return status;
    }

    *queue_handle = queue;
    KLOGI(KEVT_QUEUE_CREATE, item_count, item_size);

    return RTOS_SUCCESS;
}

rtos_status_t rtos_queue_send(rtos_queue_handle_t queue_handle, const void *item_ptr, rtos_tick_t timeout_ticks)
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

    if (timeout_ticks == 0)
    {
        rtos_port_exit_critical();
        KLOGD(KEVT_QUEUE_SEND_FULL, 0, 0);
        return RTOS_ERROR_FULL;
    }

    rtos_tcb_t *current_task = g_kernel.current_task;
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_NO_CURRENT_TASK, 0, 0);
        return RTOS_ERROR_INVALID_STATE;
    }

    queue_add_to_waiting_list(&queue->sender_wait_list, current_task, queue);

    KLOGD(KEVT_QUEUE_SEND_BLOCK, current_task->task_id, (uint32_t) timeout_ticks);

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
        /* Still blocked on queue = timeout occurred */
        queue_remove_from_waiting_list(&queue->sender_wait_list, current_task);
        rtos_port_exit_critical();

        KLOGD(KEVT_QUEUE_SEND_TIMEOUT, current_task->task_id, 0);
        return RTOS_ERROR_TIMEOUT;
    }

    /* Check again if queue has space (should always be true here) */
    if (queue->count >= queue->length)
    {
        /* This shouldn't happen - defensive programming */
        rtos_port_exit_critical();
        KLOGE(KEVT_QUEUE_SEND_FULL, queue->count, queue->length);
        return RTOS_ERROR_FULL;
    }

copy_data:
    memcpy(queue->write_ptr, item_ptr, queue->item_size);

    /* Advance write pointer (circular buffer) */
    queue->write_ptr = (uint8_t *) queue->write_ptr + queue->item_size;
    if ((uint8_t *) queue->write_ptr >= (uint8_t *) queue->buffer + (queue->length * queue->item_size))
    {
        queue->write_ptr = queue->buffer; /* Wrap around */
    }

    queue->count++;

    KLOGD(KEVT_QUEUE_SEND, queue->count, 0);

    rtos_tcb_t *waiting_receiver = queue_pop_highest_priority_waiter(&queue->receiver_wait_list);
    if (waiting_receiver != NULL)
    {
        KLOGD(KEVT_QUEUE_WAKE_RECV, waiting_receiver->task_id, 0);

        rtos_kernel_task_unblock(waiting_receiver);
        rtos_port_exit_critical();
        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();
    return RTOS_SUCCESS;
}

rtos_status_t rtos_queue_receive(rtos_queue_handle_t queue_handle, void *buffer, rtos_tick_t timeout_ticks)
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

    if (timeout_ticks == 0)
    {
        rtos_port_exit_critical();
        KLOGD(KEVT_QUEUE_RECV_EMPTY, 0, 0);
        return RTOS_ERROR_EMPTY;
    }

    rtos_tcb_t *current_task = g_kernel.current_task;
    if (current_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_NO_CURRENT_TASK, 0, 0);
        return RTOS_ERROR_INVALID_STATE;
    }

    queue_add_to_waiting_list(&queue->receiver_wait_list, current_task, queue);

    KLOGD(KEVT_QUEUE_RECV_BLOCK, current_task->task_id, (uint32_t) timeout_ticks);

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
        /* Still blocked on queue = timeout occurred */
        queue_remove_from_waiting_list(&queue->receiver_wait_list, current_task);
        rtos_port_exit_critical();

        KLOGD(KEVT_QUEUE_RECV_TIMEOUT, current_task->task_id, 0);
        return RTOS_ERROR_TIMEOUT;
    }

    /* Check again if queue has data (should always be true here) */
    if (queue->count == 0)
    {
        /* This shouldn't happen - defensive programming */
        rtos_port_exit_critical();
        KLOGE(KEVT_QUEUE_RECV_EMPTY, queue->count, queue->length);
        return RTOS_ERROR_EMPTY;
    }

copy_data:
    memcpy(buffer, queue->read_ptr, queue->item_size);

    /* Advance read pointer (circular buffer) */
    queue->read_ptr = (uint8_t *) queue->read_ptr + queue->item_size;
    if ((uint8_t *) queue->read_ptr >= (uint8_t *) queue->buffer + (queue->length * queue->item_size))
    {
        queue->read_ptr = queue->buffer; /* Wrap around */
    }

    queue->count--;

    KLOGD(KEVT_QUEUE_RECV, queue->count, 0);

    rtos_tcb_t *waiting_sender = queue_pop_highest_priority_waiter(&queue->sender_wait_list);
    if (waiting_sender != NULL)
    {
        KLOGD(KEVT_QUEUE_WAKE_SEND, waiting_sender->task_id, 0);

        rtos_kernel_task_unblock(waiting_sender);
        rtos_port_exit_critical();
        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();
    return RTOS_SUCCESS;
}

uint32_t rtos_queue_messages_waiting(rtos_queue_handle_t queue_handle)
{
    if (queue_handle == NULL)
    {
        return 0;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();
    uint32_t count = queue->count;
    rtos_port_exit_critical();

    return count;
}

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

/* Warning: does not wake waiting receivers */
rtos_status_t rtos_queue_reset(rtos_queue_handle_t queue_handle)
{
    if (queue_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_queue_t *queue = (rtos_queue_t *) queue_handle;

    rtos_port_enter_critical();

    queue->count     = 0;
    queue->read_ptr  = queue->buffer;
    queue->write_ptr = queue->buffer;

    /* Wake all waiting senders since queue is now empty */
    while (queue->sender_wait_list != NULL)
    {
        rtos_tcb_t *sender = queue_pop_highest_priority_waiter(&queue->sender_wait_list);
        if (sender != NULL)
        {
            KLOGD(KEVT_QUEUE_WAKE_SEND, sender->task_id, 0);
            rtos_kernel_task_unblock(sender);
        }
    }

    /* Also wake all waiting receivers — queue data is gone, let them
     * re-evaluate or time out rather than blocking forever. */
    while (queue->receiver_wait_list != NULL)
    {
        rtos_tcb_t *receiver = queue_pop_highest_priority_waiter(&queue->receiver_wait_list);
        if (receiver != NULL)
        {
            KLOGD(KEVT_QUEUE_WAKE_RECV, receiver->task_id, 0);
            rtos_kernel_task_unblock(receiver);
        }
    }

    rtos_port_exit_critical();

    KLOGI(KEVT_QUEUE_RESET, 0, 0);
    return RTOS_SUCCESS;
}