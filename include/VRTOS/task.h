/*******************************************************************************
 * File: include/VRTOS/task.h
 * Description: Task Management API
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_TASK_H
#define RTOS_TASK_H

#include "rtos_types.h"

/**
 * @file task.h
 * @brief Task Management API
 *
 * This file contains the API for creating, managing, and controlling tasks.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Create a new task
 *
 * @param task_function Pointer to the task function
 * @param name Task name string (for debugging)
 * @param stack_size Stack size in bytes (0 = use default)
 * @param parameter Parameter to pass to task function
 * @param priority Task priority (higher number = higher priority)
 * @param task_handle Pointer to store the created task handle
 *
 * @return RTOS_SUCCESS if task created successfully, error code otherwise
 */
rtos_status_t rtos_task_create(rtos_task_function_t task_function, const char *name,
                               rtos_stack_size_t stack_size, void *parameter,
                               rtos_priority_t priority, rtos_task_handle_t *task_handle);

/**
 * @brief Get the idle task's TCB (Task Control Block)
 *
 * @return Pointer to the idle task's TCB
 */
rtos_tcb_t *rtos_task_get_idle_task(void);

/**
 * @brief Get the current running task handle
 *
 * @return Handle of the currently running task, NULL if no task running
 */
rtos_task_handle_t rtos_task_get_current(void);

/**
 * @brief Get task state
 *
 * @param task_handle Task handle
 * @return Current state of the task
 */
rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task_handle);

/**
 * @brief Get task priority
 *
 * @param task_handle Task handle
 * @return Task priority
 */
rtos_priority_t rtos_task_get_priority(rtos_task_handle_t task_handle);

/**
 * @brief Suspend a task
 *
 * @param task_handle Task to suspend (NULL = suspend current task)
 * @return RTOS_SUCCESS on success, error code otherwise
 */
rtos_status_t rtos_task_suspend(rtos_task_handle_t task_handle);

/**
 * @brief Resume a suspended task
 *
 * @param task_handle Task to resume
 * @return RTOS_SUCCESS on success, error code otherwise
 */
rtos_status_t rtos_task_resume(rtos_task_handle_t task_handle);

/**
 * @brief Check if a task's stack has overflowed
 *
 * @param task_handle Task to check (NULL = check all tasks)
 * @return true if overflow detected, false if stack OK
 */
bool rtos_task_check_stack(rtos_task_handle_t task_handle);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TASK_H */