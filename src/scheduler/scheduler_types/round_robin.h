#ifndef RTOS_ROUND_ROBIN_H
#define RTOS_ROUND_ROBIN_H

#include "scheduler.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern const rtos_scheduler_t round_robin_scheduler;

typedef struct
{
    rtos_tcb_t *ready_list;      /**< Circular FIFO ready list (head) */
    rtos_tcb_t *ready_list_tail; /**< Tail pointer for O(1) insertion */
    rtos_tcb_t *delayed_list;    /**< Time-sorted delayed list */
    rtos_tcb_t *current_task;    /**< Currently running task (for rotation) */
    rtos_tick_t slice_remaining; /**< Remaining ticks in current time slice */
    uint8_t     ready_count;     /**< Number of ready tasks */
    uint8_t     delayed_count;   /**< Number of delayed tasks */
} round_robin_private_data_t;

/* Private data instance — defined in round_robin.c */
extern round_robin_private_data_t g_round_robin_data;

#ifdef __cplusplus
}
#endif

#endif /* RTOS_ROUND_ROBIN_H */
