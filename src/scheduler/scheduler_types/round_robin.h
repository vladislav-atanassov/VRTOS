#ifndef RTOS_ROUND_ROBIN_H
#define RTOS_ROUND_ROBIN_H

#include "config.h"
#include "scheduler.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern const rtos_scheduler_t round_robin_scheduler;

typedef struct
{
    rtos_tcb_t *ready_lists[RTOS_MAX_TASK_PRIORITIES];      /**< Ready list head per priority */
    rtos_tcb_t *ready_lists_tail[RTOS_MAX_TASK_PRIORITIES]; /**< Ready list tail per priority (O(1) append) */
    rtos_tcb_t *delayed_list;                               /**< Time-sorted delayed list */
    rtos_tcb_t *current_task;                               /**< Currently running task (for rotation) */
    rtos_tick_t slice_remaining;                            /**< Remaining ticks in current time slice */
    uint8_t     ready_priorities;                           /**< Bitmask of priorities with ready tasks */
    uint8_t     ready_count;                                /**< Number of ready tasks (across all priorities) */
    uint8_t     delayed_count;                              /**< Number of delayed tasks */
} round_robin_private_data_t;

/* Private data instance — defined in round_robin.c */
extern round_robin_private_data_t g_round_robin_data;

#ifdef __cplusplus
}
#endif

#endif /* RTOS_ROUND_ROBIN_H */
