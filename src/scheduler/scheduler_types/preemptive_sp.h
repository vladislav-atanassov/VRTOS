#ifndef RTOS_PREEMPTIVE_SP_H
#define RTOS_PREEMPTIVE_SP_H

#include "config.h"
#include "scheduler.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern const rtos_scheduler_t preemptive_sp_scheduler;

typedef struct
{
    rtos_tcb_t *ready_lists[RTOS_MAX_TASK_PRIORITIES]; /**< Ready lists per priority */
    rtos_tcb_t *delayed_list;                          /**< Time-sorted delayed list */
    uint8_t     ready_priorities; /**< Bitmask of priorities with ready tasks */
} preemptive_sp_private_data_t;

/* Static private data instance */
__attribute__((used)) static preemptive_sp_private_data_t g_preemptive_sp_data = {
    .ready_lists = {NULL}, .delayed_list = NULL, .ready_priorities = 0};

#ifdef __cplusplus
}
#endif

#endif /* RTOS_PREEMPTIVE_SP_H */
