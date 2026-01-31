/*******************************************************************************
 * File: include/VRTOS/queue.h
 * Description: Queue API Header (Updated)
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef QUEUE_H
#define QUEUE_H

#include "rtos_types.h"

#include <stdbool.h>

/**
 * @file queue.h
 * @brief Queue API for inter-task communication
 * 
 * Provides thread-safe FIFO queues with:
 * - Blocking send/receive with timeout
 * - Priority-ordered wait lists
 * - Circular buffer implementation
 */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Queue handle type
 */
typedef struct rtos_queue *rtos_queue_handle_t;

/* =================== Queue Creation =================== */

/**
 * @brief Create a new queue
 *
 * @param[out] queue_handle Pointer to store the created queue handle
 * @param[in] item_count Maximum number of items the queue can hold
 * @param[in] item_size Size of each item in bytes
 * @return rtos_status_t RTOS_SUCCESS on success, error code otherwise
 */
rtos_status_t rtos_queue_create(rtos_queue_handle_t *queue_handle, uint32_t item_count, uint32_t item_size);

/* =================== Queue Operations =================== */

/**
 * @brief Send an item to the queue (back of queue)
 *
 * Blocks if queue is full until space becomes available or timeout expires.
 *
 * @param[in] queue_handle Handle of the queue
 * @param[in] item_ptr Pointer to the item to send
 * @param[in] timeout_ticks Maximum time to wait if queue is full
 *                          - 0: Non-blocking (return immediately if full)
 *                          - RTOS_MAX_DELAY: Wait forever
 *                          - Other: Wait up to specified ticks
 * @return rtos_status_t 
 *        @subsection RTOS_SUCCESS: Item sent successfully
 *        @subsection RTOS_ERROR_TIMEOUT: Timeout expired
 *        @subsection RTOS_ERROR_FULL: Queue full (timeout_ticks = 0)
 *        @subsection RTOS_ERROR_INVALID_PARAM: Invalid parameters
 */
rtos_status_t rtos_queue_send(rtos_queue_handle_t queue_handle, const void *item_ptr, rtos_tick_t timeout_ticks);

/**
 * @brief Receive an item from the queue (front of queue)
 *
 * Blocks if queue is empty until data becomes available or timeout expires.
 *
 * @param[in] queue_handle Handle of the queue
 * @param[out] buffer Pointer to buffer to store received item
 * @param[in] timeout_ticks Maximum time to wait if queue is empty
 *                          - 0: Non-blocking (return immediately if empty)
 *                          - RTOS_MAX_DELAY: Wait forever
 *                          - Other: Wait up to specified ticks
 * @return rtos_status_t 
 *         - RTOS_SUCCESS: Item received successfully
 *         - RTOS_ERROR_TIMEOUT: Timeout expired
 *         - RTOS_ERROR_EMPTY: Queue empty (timeout_ticks = 0)
 *         - RTOS_ERROR_INVALID_PARAM: Invalid parameters
 */
rtos_status_t rtos_queue_receive(rtos_queue_handle_t queue_handle, void *buffer, rtos_tick_t timeout_ticks);

/* =================== Queue Query Functions =================== */

/**
 * @brief Get number of items currently in the queue
 *
 * @param[in] queue_handle Handle of the queue
 * @return uint32_t Number of items (0 if invalid handle)
 */
uint32_t rtos_queue_messages_waiting(rtos_queue_handle_t queue_handle);

/**
 * @brief Get number of free spaces in the queue
 *
 * @param[in] queue_handle Handle of the queue
 * @return uint32_t Number of free spaces (0 if invalid handle)
 */
uint32_t rtos_queue_spaces_available(rtos_queue_handle_t queue_handle);

/**
 * @brief Check if queue is full
 *
 * @param[in] queue_handle Handle of the queue
 * @return bool True if queue is full, false otherwise
 */
bool rtos_queue_is_full(rtos_queue_handle_t queue_handle);

/**
 * @brief Check if queue is empty
 *
 * @param[in] queue_handle Handle of the queue
 * @return bool True if queue is empty, false otherwise
 */
bool rtos_queue_is_empty(rtos_queue_handle_t queue_handle);

/**
 * @brief Reset queue to empty state
 * 
 * @warning: This does not wake waiting tasks. Use with caution.
 * All data in the queue is discarded.
 *
 * @param[in] queue_handle Handle of the queue
 * @return rtos_status_t RTOS_SUCCESS on success, error code otherwise
 */
rtos_status_t rtos_queue_reset(rtos_queue_handle_t queue_handle);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */