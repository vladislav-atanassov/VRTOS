#ifndef RTOS_COOPERATIVE_H
#define RTOS_COOPERATIVE_H

#include "scheduler.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern const rtos_scheduler_t cooperative_scheduler;

typedef struct
{
    rtos_tcb_t *ready_list;    /**< FIFO ready list */
    rtos_tcb_t *delayed_list;  /**< Time-sorted delayed list */
    uint8_t     ready_count;   /**< Number of ready tasks */
    uint8_t     delayed_count; /**< Number of delayed tasks */
} cooperative_private_data_t;

/* Private data instance — defined in cooperative.c */
extern cooperative_private_data_t g_cooperative_data;

#ifdef __cplusplus
}
#endif

#endif /* RTOS_COOPERATIVE_H */