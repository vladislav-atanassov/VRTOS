#ifndef QUEUE_PRIV_H
#define QUEUE_PRIV_H

#include "kernel_priv.h"
#include "queue.h"
#include "task.h"

/**
 * @brief Queue Control Block structure
 */
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
} rtos_queue_t;

#endif /* QUEUE_PRIV_H */
