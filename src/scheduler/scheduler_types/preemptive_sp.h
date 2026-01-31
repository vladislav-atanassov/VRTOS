/*******************************************************************************
 * File: src/scheduler/preemptive_sp.h
 * Description: Preemptive static priority-based Scheduler (Preemptive static priority-based)
 * Interface Author: Student Date: 2025
 ******************************************************************************/

#ifndef RTOS_PREEMPTIVE_SP_H
#define RTOS_PREEMPTIVE_SP_H

#include "config.h"
#include "scheduler.h"

/**
 * @file preemptive_sp.h
 * @brief Preemptive static priority-based Scheduler Interface
 *
 * This file contains the interface for the Preemptive static priority-based Scheduler
 * Scheduler implementation.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Preemptive static priority-based Scheduler vtable interface
 * 
 * Exposed interface for the Preemptive static priority-based Scheduler.
 * This allows the scheduler manager to access Preemptive static priority-based Scheduler
 * functionality.
 */
extern const rtos_scheduler_t preemptive_sp_scheduler;

/**
 * @brief Preemptive static priority-based Scheduler-specific private data
 *
 * Preemptive static priority-based Scheduler uses priority-based ready lists and a single
 * time-sorted delayed list
 */
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
