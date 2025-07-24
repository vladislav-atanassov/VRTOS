/*******************************************************************************
 * File: src/core/kernel_priv.h
 * Description: Private Kernel Definitions
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef KERNEL_PRIV_H
#define KERNEL_PRIV_H

#include "../include/VRTOS/config.h"
#include "../include/VRTOS/rtos_types.h"

/**
 * @file kernel_priv.h
 * @brief Private Kernel Definitions
 *
 * This file contains internal kernel structures and definitions.
 * Should not be included by application code.
 */

/* Kernel States */
typedef enum {
    RTOS_KERNEL_STATE_INACTIVE = 0, /**< Kernel not initialized */
    RTOS_KERNEL_STATE_READY,        /**< Kernel initialized but not started */
    RTOS_KERNEL_STATE_RUNNING,      /**< Kernel running */
    RTOS_KERNEL_STATE_SUSPENDED     /**< Kernel suspended */
} rtos_kernel_state_t;

/* Kernel Control Block */
typedef struct {
    rtos_kernel_state_t state;               /**< Current kernel state */
    rtos_tick_t         tick_count;          /**< System tick counter */
    rtos_task_handle_t  current_task;        /**< Currently running task */
    rtos_task_handle_t  next_task;           /**< Next task to run */
    uint8_t             scheduler_suspended; /**< Scheduler suspension counter */
} rtos_kernel_cb_t;

/* Global kernel control block */
extern rtos_kernel_cb_t g_kernel;

/* Internal kernel functions */
void rtos_kernel_tick_handler(void);
void rtos_kernel_switch_context(void);

#endif /* KERNEL_PRIV_H */
