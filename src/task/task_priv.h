
/*******************************************************************************
 * File: src/task/task_priv.h
 * Description: Private Task Definitions
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef TASK_PRIV_H
#define TASK_PRIV_H

#include "VRTOS/config.h"
#include "VRTOS/rtos_types.h"
#include "rtos_assert.h"

/**
 * @file task_priv.h
 * @brief Private Task Management Definitions
 *
 * This file contains internal task structures and definitions.
 */

/* Task Control Block */
struct rtos_task_control_block {
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
    rtos_task_state_t state;    /**< Current task state */
    rtos_priority_t   priority; /**< Task priority */

    /* Scheduling */
    rtos_tick_t delay_until;          /**< Tick count until task ready */
    rtos_tick_t time_slice_remaining; /**< Remaining time slice */

    /* List management */
    struct rtos_task_control_block *next; /**< Next task in list */
    struct rtos_task_control_block *prev; /**< Previous task in list */
};
RTOS_STATIC_ASSERT(offsetof(rtos_tcb_t, stack_pointer) == 0, "stack_pointer must be first in TCB");

/* Task management variables */
extern rtos_tcb_t  g_task_pool[RTOS_MAX_TASKS];
extern rtos_tcb_t *g_ready_list[RTOS_MAX_TASK_PRIORITIES];
extern rtos_tcb_t *g_delayed_task_list;
extern uint8_t     g_task_count;

/* Internal task functions */
rtos_status_t rtos_task_init_system(void);
void          rtos_task_add_to_ready_list(rtos_tcb_t *task);
void          rtos_task_remove_from_ready_list(rtos_tcb_t *task);
void          rtos_task_add_to_delayed_list(rtos_tcb_t *task, rtos_tick_t delay_ticks);
void          rtos_task_update_delayed_tasks(void);
rtos_tcb_t   *rtos_task_get_highest_priority_ready(void);
void          rtos_task_idle_function(void *param);
rtos_tcb_t   *rtos_task_get_idle_task(void);

#endif // TASK_PRIV_H
