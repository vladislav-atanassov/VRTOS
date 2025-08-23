/*******************************************************************************
 * File: list.h
 * Description: List management interface for RTOS task control blocks (TCBs)
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef LIST_H
#define LIST_H

#include "rtos_types.h"
#include "task_priv.h"

/**
 * @file list.h
 * @brief Function prototypes and type definitions for RTOS linked list handling.
 *
 * This module provides APIs for adding, removing, and sorting tasks in ready
 * and delayed lists. It operates directly on task control blocks (rtos_tcb_t)
 * and is used internally by the scheduler for RMS and delay management.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Basic list manipulation ------------------------ */

/**
 * @brief Add task to end of list (FIFO order)
 * @param list_head Pointer to the head of the list
 * @param task Task to add
 */
void rtos_list_add_tail(rtos_tcb_t **list_head, rtos_tcb_t *task);

/**
 * @brief Add task to list in sorted order by delay_until
 * @param list_head Pointer to the head of the list
 * @param task Task to add (must have delay_until set)
 */
void rtos_list_add_sorted(rtos_tcb_t **list_head, rtos_tcb_t *task);

/**
 * @brief Remove task from any list
 * @param list_head Pointer to the head of the list
 * @param task Task to remove
 */
void rtos_task_list_remove(rtos_tcb_t **list_head, rtos_tcb_t *task);

/* ----------------------- Higher-level task list ops ----------------------- */

/**
 * @brief Add task to ready list
 */
void rtos_task_add_to_ready_list(rtos_tcb_t *task);

/**
 * @brief Remove task from ready list
 */
void rtos_task_remove_from_ready_list(rtos_tcb_t *task);

/**
 * @brief Add task to delayed list
 */
void rtos_task_add_to_delayed_list(rtos_tcb_t *task, rtos_tick_t delay_ticks);

/**
 * @brief Remove task from delayed list
 */
void rtos_task_remove_from_delayed_list(rtos_tcb_t *task);

#ifdef __cplusplus
}
#endif

#endif /* LIST_H */
