/*******************************************************************************
 * File: src/task/task.c
 * Description: Task Management Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "task.h"
#include "VRTOS.h"
#include "kernel_priv.h"
#include "list.h"
#include "log.h"
#include "rtos_port.h"
#include "task_priv.h"
#include "utils.h"
#include <string.h>

/**
 * @file task.c
 * @brief Task Management Implementation
 *
 * This file contains the implementation of task creation, scheduling,
 * and management functions.
 */

/* Task management global variables */
rtos_tcb_t  g_task_pool[RTOS_MAX_TASKS] = {0};            /**< Pool of task control blocks */
rtos_tcb_t *g_ready_list[RTOS_MAX_TASK_PRIORITIES] = {0}; /**< Ready task lists per priority */
rtos_tcb_t *g_delayed_task_list = NULL;                   /**< List of delayed tasks */
uint8_t     g_task_count = 0;                             /**< Current number of tasks */

/* Static memory for task stacks */
__attribute__((aligned(RTOS_STACK_ALIGNMENT))) static uint8_t g_task_stack_memory[RTOS_TOTAL_HEAP_SIZE] = {0};
static uint32_t g_stack_memory_index = 0;

/* Static function prototypes */
static rtos_tcb_t *rtos_task_allocate_tcb(void);
static uint32_t   *rtos_task_allocate_stack(rtos_stack_size_t size);

/**
 * @brief Initialize the task management system
 */
rtos_status_t rtos_task_init_system(void) {
    memset(g_task_pool, 0, sizeof(g_task_pool));
    memset(g_ready_list, 0, sizeof(g_ready_list));
    g_delayed_task_list = NULL;
    g_task_count = 0;

    /* Initialize stack memory to known pattern */
    memset(g_task_stack_memory, 0xAA, sizeof(g_task_stack_memory));
    g_stack_memory_index = 0;

    if (((uint32_t)g_task_stack_memory & (RTOS_STACK_ALIGNMENT - 1U)) != 0) {
        log_error("Stack memory not aligned! 0x%08X", (uint32_t)g_task_stack_memory);
    }

    return RTOS_SUCCESS;
}

/**
 * @brief Create a new task
 */
rtos_status_t rtos_task_create(rtos_task_function_t task_function,
                               const char          *name,
                               rtos_stack_size_t    stack_size,
                               void                *parameter,
                               rtos_priority_t      priority,
                               rtos_task_handle_t  *task_handle) {
    /* Validate parameters */
    if (task_function == NULL || task_handle == NULL) {
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (priority >= RTOS_MAX_TASK_PRIORITIES) {
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (g_task_count >= RTOS_MAX_TASKS) {
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Use default stack size if not specified */
    if (stack_size == 0) {
        stack_size = RTOS_DEFAULT_TASK_STACK_SIZE;
    }

    /* Ensure minimum stack size */
    if (stack_size < RTOS_MINIMUM_TASK_STACK_SIZE) {
        stack_size = RTOS_MINIMUM_TASK_STACK_SIZE;
    }

    /* Align stack size */
    ALIGN8_UP(stack_size);

    rtos_port_enter_critical();

    /* Allocate TCB */
    rtos_tcb_t *new_task = rtos_task_allocate_tcb();
    if (new_task == NULL) {
        rtos_port_exit_critical();
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Allocate stack */
    uint32_t *stack_memory = rtos_task_allocate_stack(stack_size);
    if (stack_memory == NULL) {
        rtos_port_exit_critical();
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Initialize TCB */
    new_task->task_id = g_task_count;
    new_task->name = name;
    new_task->task_function = task_function;
    new_task->parameter = parameter;
    new_task->state = RTOS_TASK_STATE_READY;
    new_task->priority = priority;
    new_task->stack_base = stack_memory;
    new_task->stack_size = stack_size;
    new_task->stack_top = stack_memory;
    new_task->delay_until = 0;
    new_task->time_slice_remaining = RTOS_TIME_SLICE_TICKS;
    new_task->next = NULL;
    new_task->prev = NULL;

    /* Initialize task stack */
    new_task->stack_pointer = rtos_port_init_task_stack(new_task->stack_top, task_function, parameter);

    rtos_task_add_to_ready_list(new_task);

    g_task_count++;
    *task_handle = new_task;

    rtos_port_exit_critical();

    return RTOS_SUCCESS;
}

/**
 * @brief Get the idle task
 */
rtos_tcb_t *rtos_task_get_idle_task(void) { return &g_task_pool[RTOS_IDLE_TASK_PRIORITY]; }

/**
 * @brief Get current running task
 */
rtos_task_handle_t rtos_task_get_current(void) { return g_kernel.current_task; }

/**
 * @brief Get task state
 */
rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task_handle) {
    if (task_handle == NULL) {
        return RTOS_TASK_STATE_DELETED;
    }
    return task_handle->state;
}

/**
 * @brief Get task priority
 */
rtos_priority_t rtos_task_get_priority(rtos_task_handle_t task_handle) {
    if (task_handle == NULL) {
        return 0;
    }
    return task_handle->priority;
}

/**
 * @brief Update delayed tasks (called from tick handler)
 */
void rtos_task_update_delayed_tasks(void) {
    rtos_port_enter_critical();

    rtos_tcb_t *task = g_delayed_task_list;
    rtos_tick_t current_tick = rtos_get_tick_count();

    /* Check all delayed tasks */
    while (task != NULL) {
        rtos_tcb_t *next_task = task->next;

        if (current_tick >= task->delay_until) {
            /* Task delay expired */
            rtos_task_remove_from_delayed_list(task);
            task->state = RTOS_TASK_STATE_READY;
            rtos_task_add_to_ready_list(task);
        } else {
            /* Tasks are sorted, so we can break here */
            break;
        }

        task = next_task;
    }

    rtos_port_exit_critical();
}

/**
 * @brief Get highest priority ready task
 */
rtos_tcb_t *rtos_task_get_highest_priority_ready(void) {
    for (int8_t priority = RTOS_MAX_TASK_PRIORITIES - 1; priority >= 0; priority--) {
        if (g_ready_list[priority] != NULL) {
            return g_ready_list[priority];
        }
    }

    return NULL; /* No ready tasks */
}

/**
 * @brief Idle task function
 */
__attribute__((__noreturn__))
void rtos_task_idle_function(void *param) {
    (void)param; /* Unused parameter */

    while (1) {
        __WFI(); /* Wait for interrupt */
    }
}

/**
 * @brief Allocate a TCB from the pool
 */
static rtos_tcb_t *rtos_task_allocate_tcb(void) {
    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++) {
        if (g_task_pool[i].task_function == NULL) {
            return &g_task_pool[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate stack memory
 */
static uint32_t *rtos_task_allocate_stack(rtos_stack_size_t size) {
    uint32_t aligned_index = ALIGN8_UP_VALUE(g_stack_memory_index);

    if (aligned_index + size > sizeof(g_task_stack_memory)) {
        return NULL;
    }

    uint8_t *stack_base = &g_task_stack_memory[aligned_index];
    uint8_t *stack_top = stack_base + size;
    g_stack_memory_index = aligned_index + size;

    return (uint32_t *)ALIGN8_DOWN_VALUE((uint32_t)stack_top);
}
