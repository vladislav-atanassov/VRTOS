/*******************************************************************************
 * File: src/task/task.c
 * Description: Simplified Task Management (No List Operations)
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "task.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "log.h"
#include "memory.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task_priv.h"
#include "utils.h"

#include <string.h>

/**
 * @file task.c
 * @brief Simplified Task Management Implementation
 *
 * This file contains only the core task creation and management functions.
 * All list management operations are now handled by the scheduler implementations.
 */

/* Task management global variables */
rtos_tcb_t g_task_pool[RTOS_MAX_TASKS] = {0}; /**< Pool of task control blocks */
uint8_t    g_task_count                = 0;   /**< Current number of tasks */

/* Static function prototypes */
static rtos_tcb_t *rtos_task_allocate_tcb(void);
static uint32_t   *rtos_task_allocate_stack(rtos_stack_size_t size);

/**
 * @brief Initialize the task management system
 */
rtos_status_t rtos_task_init_system(void)
{
    /* Initialize task pool */
    memset(g_task_pool, 0, sizeof(g_task_pool));
    g_task_count = 0;

    /* Stack usage stats relative to global pool are no longer valid with dynamic allocation */
    log_printf(LOG_LEVEL_DEBUG, "DEBUG", "Stack memory: (Dynamic Heap Managed)");

    /* Stack alignment is now handled by rtos_malloc in memory.c */

    log_debug("Task management system initialized");
    log_debug("Task pool: %d tasks, Stack memory: %d bytes", RTOS_MAX_TASKS, RTOS_TOTAL_HEAP_SIZE);

    return RTOS_SUCCESS;
}

/**
 * @brief Create a new task
 */
rtos_status_t rtos_task_create(rtos_task_function_t task_function, const char *name,
                               rtos_stack_size_t stack_size, void *parameter,
                               rtos_priority_t priority, rtos_task_handle_t *task_handle)
{
    /* Validate parameters */
    if (task_function == NULL || task_handle == NULL)
    {
        log_error("Invalid parameters: task_function=%p, task_handle=%p", task_function,
                  task_handle);
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        log_error("Invalid priority: %d (max: %d)", priority, RTOS_MAX_TASK_PRIORITIES - 1);
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (g_task_count >= RTOS_MAX_TASKS)
    {
        log_error("Maximum number of tasks reached: %d", RTOS_MAX_TASKS);
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Use default stack size if not specified */
    if (stack_size == 0)
    {
        stack_size = RTOS_DEFAULT_TASK_STACK_SIZE;
    }

    /* Ensure minimum stack size */
    if (stack_size < RTOS_MINIMUM_TASK_STACK_SIZE)
    {
        stack_size = RTOS_MINIMUM_TASK_STACK_SIZE;
    }

    /* Align stack size */
    ALIGN8_UP(stack_size);

    rtos_port_enter_critical();

    /* Allocate TCB */
    rtos_tcb_t *new_task = rtos_task_allocate_tcb();
    if (new_task == NULL)
    {
        rtos_port_exit_critical();
        log_error("Failed to allocate TCB");
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Allocate stack */
    uint32_t *stack_memory = rtos_task_allocate_stack(stack_size);
    if (stack_memory == NULL)
    {
        /* Free the TCB */
        new_task->task_function = NULL;
        rtos_port_exit_critical();
        log_error("Failed to allocate stack of size %d", stack_size);
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Write stack canary at bottom of stack for overflow detection */
    *stack_memory = RTOS_STACK_CANARY_VALUE;

    /* Initialize TCB */
    new_task->task_id              = g_task_count;
    new_task->name                 = name;
    new_task->task_function        = task_function;
    new_task->parameter            = parameter;
    new_task->state                = RTOS_TASK_STATE_READY;
    new_task->priority             = priority;
    new_task->base_priority        = priority; /* Store original priority for inheritance */
    new_task->stack_base           = stack_memory;
    new_task->stack_size           = stack_size;
    new_task->stack_top            = stack_memory;
    new_task->delay_until          = 0;
    new_task->time_slice_remaining = RTOS_TIME_SLICE_TICKS;

    /* Initialize list pointers */
    new_task->next = NULL;
    new_task->prev = NULL;

    /* Initialize synchronization support */
    new_task->next_waiting    = NULL;
    new_task->blocked_on      = NULL;
    new_task->blocked_on_type = RTOS_SYNC_TYPE_NONE;

    /* Initialize task stack */
    new_task->stack_pointer =
        rtos_port_init_task_stack(new_task->stack_top, task_function, parameter);

    /* Add to ready list via scheduler */
    rtos_scheduler_add_to_ready_list(new_task);

    g_task_count++;
    *task_handle = new_task;

    rtos_port_exit_critical();

    log_info("Created task '%s' (ID=%d, prio=%d, stack=%d bytes)", name ? name : "unnamed",
             new_task->task_id, priority, stack_size);

    return RTOS_SUCCESS;
}

/**
 * @brief Get the idle task
 */
rtos_tcb_t *rtos_task_get_idle_task(void)
{
    /* Find the idle task (priority 0) */
    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        if (g_task_pool[i].task_function != NULL &&
            g_task_pool[i].priority == RTOS_IDLE_TASK_PRIORITY)
        {
            return &g_task_pool[i];
        }
    }
    return NULL;
}

/**
 * @brief Get current running task
 */
rtos_task_handle_t rtos_task_get_current(void)
{
    return g_kernel.current_task;
}

/**
 * @brief Get task state
 */
rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task_handle)
{
    if (task_handle == NULL)
    {
        return RTOS_TASK_STATE_DELETED;
    }
    return task_handle->state;
}

/**
 * @brief Get task priority
 */
rtos_priority_t rtos_task_get_priority(rtos_task_handle_t task_handle)
{
    if (task_handle == NULL)
    {
        return 0;
    }
    return task_handle->priority;
}

/**
 * @brief Get task by ID
 */
rtos_task_handle_t rtos_task_get_by_id(rtos_task_id_t task_id)
{
    if (task_id >= RTOS_MAX_TASKS)
    {
        return NULL;
    }

    rtos_tcb_t *task = &g_task_pool[task_id];
    if (task->task_function == NULL)
    {
        return NULL; /* Task slot is empty */
    }

    return task;
}

/**
 * @brief Get task by name
 */
rtos_task_handle_t rtos_task_get_by_name(const char *name)
{
    if (name == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        rtos_tcb_t *task = &g_task_pool[i];
        if (task->task_function != NULL && task->name != NULL)
        {
            if (strcmp(task->name, name) == 0)
            {
                return task;
            }
        }
    }

    return NULL;
}

/**
 * @brief Get total number of created tasks
 */
uint8_t rtos_task_get_count(void)
{
    return g_task_count;
}

/**
 * @brief Print task information for debugging
 */
void rtos_task_debug_print_all(void)
{
    log_debug("=== Task Debug Information ===");
    log_debug("Total tasks: %d/%d", g_task_count, RTOS_MAX_TASKS);
    log_debug("Stack memory: (Dynamic Heap Managed)");

    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        rtos_tcb_t *task = &g_task_pool[i];
        if (task->task_function != NULL)
        {
            const char *state_str;
            switch (task->state)
            {
                case RTOS_TASK_STATE_READY:
                    state_str = "READY";
                    break;
                case RTOS_TASK_STATE_RUNNING:
                    state_str = "RUNNING";
                    break;
                case RTOS_TASK_STATE_BLOCKED:
                    state_str = "BLOCKED";
                    break;
                case RTOS_TASK_STATE_SUSPENDED:
                    state_str = "SUSPENDED";
                    break;
                case RTOS_TASK_STATE_DELETED:
                    state_str = "DELETED";
                    break;
                default:
                    state_str = "UNKNOWN";
                    break;
            }

            log_debug("Task[%d]: '%s' prio=%d state=%s stack=%d SP=0x%08lX", task->task_id,
                      task->name ? task->name : "unnamed", task->priority, state_str,
                      task->stack_size, (unsigned long) task->stack_pointer);
        }
    }
    log_debug("==============================");
}

/**
 * @brief Idle task function
 */
__attribute__((__noreturn__)) void rtos_task_idle_function(void *param)
{
    (void) param; /* Unused parameter */

    log_debug("Idle task started");

    while (1)
    {
        __asm volatile("wfi"); /* Wait for interrupt */

#if (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_COOPERATIVE)
        rtos_yield();
#endif
    }
}

/* ==================== Stack & State Management ==================== */

/**
 * @brief Check if a task's stack has overflowed
 * @param task_handle Task to check (NULL = check all tasks)
 * @return true if overflow detected, false if stack OK
 */
bool rtos_task_check_stack(rtos_task_handle_t task_handle)
{
    if (task_handle != NULL)
    {
        /* Check single task */
        if (task_handle->stack_base != NULL)
        {
            if (*task_handle->stack_base != RTOS_STACK_CANARY_VALUE)
            {
                log_error("STACK OVERFLOW detected in task '%s' (ID=%d)",
                          task_handle->name ? task_handle->name : "unnamed", task_handle->task_id);
                return true;
            }
        }
        return false;
    }

    /* Check all tasks */
    bool overflow_found = false;
    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        rtos_tcb_t *task = &g_task_pool[i];
        if (task->task_function != NULL && task->stack_base != NULL)
        {
            if (*task->stack_base != RTOS_STACK_CANARY_VALUE)
            {
                log_error("STACK OVERFLOW detected in task '%s' (ID=%d)",
                          task->name ? task->name : "unnamed", task->task_id);
                overflow_found = true;
            }
        }
    }
    return overflow_found;
}

/**
 * @brief Suspend a task
 * @param task_handle Task to suspend (NULL = suspend current task)
 * @return RTOS_SUCCESS on success, error code otherwise
 */
rtos_status_t rtos_task_suspend(rtos_task_handle_t task_handle)
{
    rtos_port_enter_critical();

    /* If NULL, suspend current task */
    rtos_tcb_t *task = (task_handle != NULL) ? task_handle : g_kernel.current_task;

    if (task == NULL)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_PARAM;
    }

    /* Cannot suspend deleted task */
    if (task->state == RTOS_TASK_STATE_DELETED)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Remove from ready list if currently ready */
    if (task->state == RTOS_TASK_STATE_READY)
    {
        rtos_scheduler_remove_from_ready_list(task);
    }

    /* Remove from delayed list if blocked with timeout */
    if (task->state == RTOS_TASK_STATE_BLOCKED)
    {
        rtos_scheduler_remove_from_delayed_list(task);
    }

    task->state = RTOS_TASK_STATE_SUSPENDED;

    log_debug("Task '%s' suspended", task->name ? task->name : "unnamed");

    /* If suspending current task, yield */
    if (task == g_kernel.current_task)
    {
        rtos_port_exit_critical();
        rtos_yield();
        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();
    return RTOS_SUCCESS;
}

/**
 * @brief Resume a suspended task
 * @param task_handle Task to resume
 * @return RTOS_SUCCESS on success, error code otherwise
 */
rtos_status_t rtos_task_resume(rtos_task_handle_t task_handle)
{
    if (task_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_port_enter_critical();

    /* Can only resume suspended tasks */
    if (task_handle->state != RTOS_TASK_STATE_SUSPENDED)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_STATE;
    }

    log_debug("Task '%s' resumed", task_handle->name ? task_handle->name : "unnamed");

    rtos_port_exit_critical();

    /* Move to ready state (this handles preemption check) */
    rtos_kernel_task_ready(task_handle);

    return RTOS_SUCCESS;
}

/* ==================== Static Helper Functions ==================== */

/**
 * @brief Allocate a TCB from the pool
 */
static rtos_tcb_t *rtos_task_allocate_tcb(void)
{
    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        if (g_task_pool[i].task_function == NULL)
        {
            return &g_task_pool[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate stack memory
 */
static uint32_t *rtos_task_allocate_stack(rtos_stack_size_t size)
{
    /* Use central memory manager */
    void *stack_block = rtos_malloc(size);

    if (stack_block == NULL)
    {
        log_error("Stack allocation failed: need %u bytes", (unsigned int) size);
        return NULL;
    }

    /*
     * Stack grows down on Cortex-M.
     * Return the TOP of the stack block (highest address).
     * rtos_malloc returns 8-byte aligned start address.
     * size is assumed to be 8-byte aligned by rtos_malloc (if not, it aligns up).
     * We just add size to get top.
     */
    uint8_t *stack_top = (uint8_t *) stack_block + size;

    /* Ensure 8-byte alignment of top (should be naturally aligned) */
    return (uint32_t *) ALIGN8_DOWN_VALUE((uint32_t) stack_top);
}
