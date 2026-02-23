/*******************************************************************************
 * File: src/task/task_priv.h
 * Description: Private Task Definitions
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef TASK_PRIV_H
#define TASK_PRIV_H

#include "config.h"
#include "rtos_assert.h"
#include "rtos_types.h"

/**
 * @file task_priv.h
 * @brief Private Task Management Definitions
 *
 * This file contains internal task structures and definitions.
 */

/* Task Control Block */
typedef struct rtos_task_control_block
{
    /* Stack management */
    uint32_t         *stack_pointer; /**< Current stack pointer */
    uint32_t         *stack_base;    /**< Base of task stack */
    uint32_t         *stack_top;     /**< Top of task stack */
    rtos_stack_size_t stack_size;    /**< Stack size in bytes */

    /* Task identification */
    rtos_task_id_t task_id; /**< Unique task identifier */
    const char    *name;    /**< Task name for debugging */

    /* Task execution */
    rtos_task_function_t task_function; /**< Task function pointer */
    void                *parameter;     /**< Parameter for task function */

    /* Task state */
    rtos_task_state_t state;         /**< Current task state */
    rtos_priority_t   priority;      /**< Task priority (may be boosted) */
    rtos_priority_t   base_priority; /**< Original priority (for priority inheritance) */

    /* Scheduling */
    rtos_tick_t delay_until;          /**< Tick count until task ready */
    rtos_tick_t time_slice_remaining; /**< Remaining time slice */

    /* List management */
    struct rtos_task_control_block *next; /**< Next task in list */
    struct rtos_task_control_block *prev; /**< Previous task in list */

    /* Synchronization support */
    struct rtos_task_control_block *next_waiting;    /**< Next task in sync wait queue */
    void                           *blocked_on;      /**< Sync object task is waiting on */
    rtos_sync_type_t                blocked_on_type; /**< Type of sync object */
} rtos_tcb_t;
RTOS_STATIC_ASSERT(offsetof(rtos_tcb_t, stack_pointer) == 0, "stack_pointer must be first in TCB");

// TODO: To be used by rtos_task_get_memory_stats
/**
 * @brief Task memory usage statistics
 */
typedef struct
{
    uint32_t total_stack_memory;                    /**< Total stack memory available */
    uint32_t used_stack_memory;                     /**< Stack memory currently used */
    uint32_t free_stack_memory;                     /**< Stack memory available */
    uint8_t  total_task_slots;                      /**< Total task slots */
    uint8_t  used_task_slots;                       /**< Task slots currently used */
    uint8_t  free_task_slots;                       /**< Task slots available */
    uint32_t per_task_stack_size[RTOS_MAX_TASKS];   /**< Stack size per task */
    uint32_t per_task_stack_unused[RTOS_MAX_TASKS]; /**< Unused stack per task */
} rtos_task_memory_stats_t;

/* Task management variables */
extern rtos_tcb_t g_task_pool[RTOS_MAX_TASKS]; /**< Pool of task control blocks */
extern uint8_t    g_task_count;                /**< Current number of tasks */

/* Internal task functions */
rtos_status_t rtos_task_init_system(void);
rtos_tcb_t   *rtos_task_get_idle_task(void);
void          rtos_task_idle_function(void *param);

/* Extended task management functions */
rtos_task_handle_t rtos_task_get_by_id(rtos_task_id_t task_id);
rtos_task_handle_t rtos_task_get_by_name(const char *name);
uint8_t            rtos_task_get_count(void);
void               rtos_task_get_memory_stats(void); // TODO: Implement using rtos_task_memory_stats_t
void               rtos_task_debug_print_all(void);

/* Kernel helper functions for task state transitions */
void rtos_kernel_task_ready(rtos_task_handle_t task);
void rtos_kernel_task_block(rtos_task_handle_t task, rtos_tick_t delay_ticks);
void rtos_kernel_task_unblock(rtos_task_handle_t task);

#endif /* TASK_PRIV_H */
