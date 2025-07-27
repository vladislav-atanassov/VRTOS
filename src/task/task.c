/*******************************************************************************
 * File: src/task/task.c
 * Description: Task Management Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS/task.h"
#include "VRTOS/VRTOS.h"
#include "core/kernel_priv.h"
#include "log.h"
#include "rtos_port.h"
#include "task_priv.h"
#include <string.h>

/**
 * @file task.c
 * @brief Task Management Implementation
 *
 * This file contains the implementation of task creation, scheduling,
 * and management functions.
 */

/* Task management global variables */
rtos_tcb_t  g_task_pool[RTOS_MAX_TASKS];            /**< Pool of task control blocks */
rtos_tcb_t *g_ready_list[RTOS_MAX_TASK_PRIORITIES]; /**< Ready task lists per priority */
rtos_tcb_t *g_delayed_task_list = NULL;             /**< List of delayed tasks */
uint8_t     g_task_count = 0;                       /**< Current number of tasks */

/* Static memory for task stacks */
static uint8_t  g_task_stack_memory[RTOS_TOTAL_HEAP_SIZE];
static uint32_t g_stack_memory_index = 0;

/* Static function prototypes */
static rtos_tcb_t *rtos_task_allocate_tcb(void);
static uint32_t   *rtos_task_allocate_stack(rtos_stack_size_t size);
static void        rtos_task_list_insert_sorted(rtos_tcb_t **list, rtos_tcb_t *task);
static void        rtos_task_list_remove(rtos_tcb_t **list, rtos_tcb_t *task);

/**
 * @brief Initialize the task management system
 */
rtos_status_t rtos_task_init_system(void) {
    /* Initialize task pool */
    memset(g_task_pool, 0, sizeof(g_task_pool));

    /* Initialize ready lists */
    for (uint8_t i = 0; i < RTOS_MAX_TASK_PRIORITIES; i++) {
        g_ready_list[i] = NULL;
    }

    /* Initialize delayed task list */
    g_delayed_task_list = NULL;

    /* Reset counters */
    g_task_count = 0;
    g_stack_memory_index = 0;

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
    log_info("ENTERING for task with name %s", name);

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
    stack_size = (stack_size + RTOS_STACK_ALIGNMENT - 1) & ~(RTOS_STACK_ALIGNMENT - 1);

    log_info("ENTERING rtos_port_enter_critical()");

    rtos_port_enter_critical();

    log_info("ENTERING rtos_task_allocate_tcb()");

    /* Allocate TCB */
    rtos_tcb_t *new_task = rtos_task_allocate_tcb();
    if (new_task == NULL) {
        rtos_port_exit_critical();
        return RTOS_ERROR_NO_MEMORY;
    }

    log_info("ENTERING rtos_task_allocate_stack()");

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
    new_task->stack_top = stack_memory + (stack_size / sizeof(uint32_t));
    new_task->delay_until = 0;
    new_task->time_slice_remaining = RTOS_TIME_SLICE_TICKS;
    new_task->next = NULL;
    new_task->prev = NULL;

    log_debug("new_task->name: %s, expected: %s", new_task->name, name);

    log_info("ENTERING rtos_port_init_task_stack()");

    /* Initialize task stack */
    new_task->stack_pointer = rtos_port_init_task_stack(new_task->stack_top, task_function, parameter);

    log_info("ENTERING rtos_task_add_to_ready_list()");

    // TODO: Inspect rtos_task_add_to_ready_list()
    /* Add to ready list */
    rtos_task_add_to_ready_list(new_task);

    g_task_count++;
    *task_handle = new_task;

    log_info("ENTERING rtos_port_exit_critical()");

    rtos_port_exit_critical();

    log_info("EXITING rtos_task_create()");

    return RTOS_SUCCESS;
}

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
 * @brief Add task to ready list
 */
void rtos_task_add_to_ready_list(rtos_tcb_t *task) {
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES) {
        return;
    }

    /* Add to end of ready list for round-robin */
    rtos_tcb_t **list_head = &g_ready_list[task->priority];

    if (*list_head == NULL) {
        /* First task in this priority level */
        *list_head = task;
        task->next = task;
        task->prev = task;
    } else {
        /* Insert at end of circular list */
        rtos_tcb_t *last = (*list_head)->prev;
        task->next = *list_head;
        task->prev = last;
        last->next = task;
        (*list_head)->prev = task;
    }
}

/**
 * @brief Remove task from ready list
 */
void rtos_task_remove_from_ready_list(rtos_tcb_t *task) {
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES) {
        return;
    }

    rtos_tcb_t **list_head = &g_ready_list[task->priority];

    if (*list_head == NULL) {
        return; /* List is empty */
    }

    if (task->next == task) {
        /* Only task in list */
        *list_head = NULL;
    } else {
        /* Remove from circular list */
        task->prev->next = task->next;
        task->next->prev = task->prev;

        /* Update list head if necessary */
        if (*list_head == task) {
            *list_head = task->next;
        }
    }

    task->next = NULL;
    task->prev = NULL;
}

/**
 * @brief Add task to delayed list
 */
void rtos_task_add_to_delayed_list(rtos_tcb_t *task, rtos_tick_t delay_ticks) {
    if (task == NULL) {
        return;
    }

    task->delay_until = g_kernel.tick_count + delay_ticks;
    rtos_task_list_insert_sorted(&g_delayed_task_list, task);
}

/**
 * @brief Update delayed tasks (called from tick handler)
 */
void rtos_task_update_delayed_tasks(void) {
    rtos_tcb_t *task = g_delayed_task_list;
    rtos_tick_t current_tick = g_kernel.tick_count;

    /* Check all delayed tasks */
    while (task != NULL) {
        rtos_tcb_t *next_task = task->next;

        if (current_tick >= task->delay_until) {
            /* Task delay expired */
            rtos_task_list_remove(&g_delayed_task_list, task);
            task->state = RTOS_TASK_STATE_READY;
            rtos_task_add_to_ready_list(task);
        } else {
            /* Tasks are sorted, so we can break here */
            break;
        }

        task = next_task;
    }
}

/**
 * @brief Get highest priority ready task
 */
rtos_tcb_t *rtos_task_get_highest_priority_ready(void) {
    /* Find highest priority with ready tasks */
    for (int8_t priority = RTOS_MAX_TASK_PRIORITIES - 1; priority >= 0; priority--) {
        if (g_ready_list[priority] != NULL) {
            rtos_tcb_t *task = g_ready_list[priority];

            /* Move to next task for round-robin */
            g_ready_list[priority] = task->next;

            return task;
        }
    }

    return NULL; /* No ready tasks */
}

/**
 * @brief Idle task function
 */
void rtos_task_idle_function(void *param) {
    (void)param; /* Unused parameter */

    while (1) {
        __asm volatile("wfi"); /* Wait for interrupt */
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
    // Ensure size is multiple of 8
    size = (size + 7) & ~0x7;
    
    // Align memory index to 8 bytes
    g_stack_memory_index = (g_stack_memory_index + 7) & ~0x7;
    
    if (g_stack_memory_index + size > RTOS_TOTAL_HEAP_SIZE) {
        return NULL;
    }
    
    uint8_t *stack_base = &g_task_stack_memory[g_stack_memory_index];
    g_stack_memory_index += size;

    log_info("Allocated stack top: 0x%08X", (uint32_t)(stack_base + size));
    
    // Return aligned top of stack
    return (uint32_t *)((uint32_t)(stack_base + size) & ~0x7);
}

/**
 * @brief Insert task into sorted list by delay_until
 */
static void rtos_task_list_insert_sorted(rtos_tcb_t **list, rtos_tcb_t *task) {
    if (*list == NULL) {
        /* Empty list */
        *list = task;
        task->next = NULL;
        task->prev = NULL;
        return;
    }

    rtos_tcb_t *current = *list;
    rtos_tcb_t *prev = NULL;

    /* Find insertion point */
    while (current != NULL && current->delay_until <= task->delay_until) {
        prev = current;
        current = current->next;
    }

    /* Insert task */
    task->next = current;
    task->prev = prev;

    if (prev == NULL) {
        /* Insert at head */
        *list = task;
    } else {
        prev->next = task;
    }

    if (current != NULL) {
        current->prev = task;
    }
}

/**
 * @brief Remove task from list
 */
static void rtos_task_list_remove(rtos_tcb_t **list, rtos_tcb_t *task) {
    if (task->prev != NULL) {
        task->prev->next = task->next;
    } else {
        *list = task->next;
    }

    if (task->next != NULL) {
        task->next->prev = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;
}
