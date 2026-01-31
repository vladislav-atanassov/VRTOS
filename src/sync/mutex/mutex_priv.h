#ifndef MUTEX_PRIV_H
#define MUTEX_PRIV_H

#include "rtos_types.h"

typedef struct rtos_mutex
{
    rtos_task_handle_t owner;             /**< Current mutex owner */
    rtos_priority_t    original_priority; /**< Owner's original priority */
    rtos_tcb_t        *waiting_list;      /**< List of tasks waiting for mutex */
    uint8_t            lock_count;        /**< Recursive lock count */
} rtos_mutex_t;

#endif /* MUTEX_PRIV_H */
