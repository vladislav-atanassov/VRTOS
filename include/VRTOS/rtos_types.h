/*******************************************************************************
 * File: include/VRTOS/rtos_types.h
 * Description: RTOS Type Definitions
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_TYPES_H
#define RTOS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file rtos_types.h
 * @brief RTOS Type Definitions
 *
 * This file contains all type definitions used throughout the RTOS.
 */

/* Basic Types */
typedef uint32_t rtos_tick_t;       /**< System tick counter type */
typedef uint8_t  rtos_priority_t;   /**< Task priority type */
typedef uint16_t rtos_stack_size_t; /**< Stack size type */
typedef uint8_t  rtos_task_id_t;    /**< Task ID type */

/* Return Status Types */
typedef enum {
    RTOS_SUCCESS = 0,          /**< Operation successful */
    RTOS_ERROR_INVALID_PARAM,  /**< Invalid parameter */
    RTOS_ERROR_NO_MEMORY,      /**< No memory available */
    RTOS_ERROR_TASK_NOT_FOUND, /**< Task not found */
    RTOS_ERROR_INVALID_STATE,  /**< Invalid system state */
    RTOS_ERROR_TIMEOUT,        /**< Operation timed out */
    RTOS_ERROR_GENERAL         /**< General error */
} rtos_status_t;

/* Task States */
typedef enum {
    RTOS_TASK_STATE_READY = 0, /**< Task is ready to run */
    RTOS_TASK_STATE_RUNNING,   /**< Task is currently running */
    RTOS_TASK_STATE_BLOCKED,   /**< Task is blocked (waiting) */
    RTOS_TASK_STATE_SUSPENDED, /**< Task is suspended */
    RTOS_TASK_STATE_DELETED    /**< Task is deleted */
} rtos_task_state_t;

/* Task Function Type */
typedef void (*rtos_task_function_t)(void *param);

/* Forward Declarations */
struct rtos_task_control_block;
typedef struct rtos_task_control_block rtos_tcb_t;

/* Task Handle Type */
typedef rtos_tcb_t *rtos_task_handle_t;

#endif // RTOS_TYPES_H
