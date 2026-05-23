#ifndef QUEUE_H
#define QUEUE_H

#include "rtos_types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Queue control block. Public for static allocation via rtos_queue_create_static().
 * Treat fields as opaque — use the rtos_queue_* API instead of accessing directly. */
typedef struct rtos_queue
{
    void    *buffer;    /**< Pointer to the start of the queue storage */
    void    *read_ptr;  /**< Pointer to the next item to read */
    void    *write_ptr; /**< Pointer to the next empty slot to write */
    uint32_t item_size; /**< Size of each item in bytes */
    uint32_t length;    /**< Queue length (number of items) */
    uint32_t count;     /**< Current number of items in the queue */

    rtos_tcb_t *sender_wait_list;   /**< List of tasks waiting to send (queue full) */
    rtos_tcb_t *receiver_wait_list; /**< List of tasks waiting to receive (queue empty) */

    bool buffer_is_static; /**< True if `buffer` is caller-owned (do not free on delete). */
    bool cb_is_static;     /**< True if the queue struct itself is caller-owned. */
} rtos_queue_t;

typedef rtos_queue_t *rtos_queue_handle_t;

rtos_status_t rtos_queue_create(rtos_queue_handle_t *queue_handle, uint32_t item_count, uint32_t item_size);

/**
 * @brief Create a queue using caller-provided storage for both the control block
 *        and the item buffer (no heap allocation).
 * @param queue_buffer   Caller-owned rtos_queue_t storage. Must remain valid for queue lifetime.
 * @param item_storage   Caller-owned item buffer of size >= item_count * item_size.
 *                       Must remain valid for queue lifetime.
 * @param item_count     Number of items the queue can hold.
 * @param item_size      Size of each item in bytes.
 * @param queue_handle   Output handle (will equal queue_buffer on success).
 * @return RTOS_SUCCESS or an error code.
 *
 * @note rtos_queue_delete() on a statically-created queue does not free storage.
 */
rtos_status_t rtos_queue_create_static(rtos_queue_t *queue_buffer, void *item_storage, uint32_t item_count,
                                       uint32_t item_size, rtos_queue_handle_t *queue_handle);

/**
 * @brief Delete a queue, freeing heap storage if it was created dynamically.
 *        Wakes all waiting senders and receivers with their current operation
 *        aborted (they will observe the queue having been invalidated via
 *        timeout-like behaviour). The handle is invalid after this call.
 * @param queue_handle Handle of the queue to delete.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_queue_delete(rtos_queue_handle_t queue_handle);

/* timeout_ticks: 0=non-blocking, RTOS_MAX_DELAY=forever */
rtos_status_t rtos_queue_send(rtos_queue_handle_t queue_handle, const void *item_ptr, rtos_tick_t timeout_ticks);

/* timeout_ticks: 0=non-blocking, RTOS_MAX_DELAY=forever */
rtos_status_t rtos_queue_receive(rtos_queue_handle_t queue_handle, void *buffer, rtos_tick_t timeout_ticks);

/**
 * @brief ISR-context counterpart of rtos_queue_send(). Never blocks; returns
 *        RTOS_ERROR_FULL if no slot is available. Wakes the highest-priority
 *        waiting receiver via the ISR-safe unblock path.
 *
 * Must only be called from interrupt context.
 *
 * @param queue_handle Handle of the queue to send to.
 * @param item_ptr     Pointer to the item to copy into the queue (item_size bytes).
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_queue_send_from_isr(rtos_queue_handle_t queue_handle, const void *item_ptr);

/**
 * @brief ISR-context counterpart of rtos_queue_receive(). Never blocks;
 *        returns RTOS_ERROR_EMPTY if the queue has no items. Wakes the
 *        highest-priority waiting sender via the ISR-safe unblock path.
 *
 * Must only be called from interrupt context.
 *
 * @param queue_handle Handle of the queue to receive from.
 * @param buffer       Output buffer of at least item_size bytes.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_queue_receive_from_isr(rtos_queue_handle_t queue_handle, void *buffer);

uint32_t rtos_queue_messages_waiting(rtos_queue_handle_t queue_handle);
uint32_t rtos_queue_spaces_available(rtos_queue_handle_t queue_handle);
bool     rtos_queue_is_full(rtos_queue_handle_t queue_handle);
bool     rtos_queue_is_empty(rtos_queue_handle_t queue_handle);

/* Warning: does not wake waiting receivers. All data is discarded. */
rtos_status_t rtos_queue_reset(rtos_queue_handle_t queue_handle);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */