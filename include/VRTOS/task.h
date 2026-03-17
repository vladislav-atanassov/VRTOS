#ifndef RTOS_TASK_H
#define RTOS_TASK_H

#include "rtos_types.h"

#include <stdbool.h>

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
rtos_status_t rtos_task_create(rtos_task_function_t task_function, const char *name, rtos_stack_size_t stack_size,
                               void *parameter, rtos_priority_t priority, rtos_task_handle_t *task_handle);

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
 * @brief Get task name by task ID
 *
 * @param task_id Task identifier
 * @return Task name string, or "?" if invalid
 */
const char *rtos_task_get_name(rtos_task_id_t task_id);


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
 * @brief Delete a task and remove it from all scheduler and sync-object lists.
 *
 * Pass NULL to delete the calling task (self-delete). Self-delete triggers a
 * yield and never returns. The TCB slot is permanently consumed (bump allocator
 * cannot free stack RAM). The idle task cannot be deleted.
 *
 * Note: if the deleted task held a mutex, that mutex remains locked.
 *
 * @param task_handle Task to delete, or NULL to delete the current task.
 * @return RTOS_SUCCESS, RTOS_ERROR_INVALID_PARAM, or RTOS_ERROR_INVALID_STATE
 */
rtos_status_t rtos_task_delete(rtos_task_handle_t task_handle);

/**
 * @brief Check if a task's stack has overflowed
 *
 * @param task_handle Task to check (NULL = check all tasks)
 * @return true if overflow detected, false if stack OK
 */
bool rtos_task_check_stack(rtos_task_handle_t task_handle);

typedef enum
{
    RTOS_NOTIFY_ACTION_NONE = 0,  /**< Just set pending, don't modify value */
    RTOS_NOTIFY_ACTION_SET_BITS,  /**< OR value into notification_value */
    RTOS_NOTIFY_ACTION_INCREMENT, /**< notification_value++ */
    RTOS_NOTIFY_ACTION_OVERWRITE  /**< Replace notification_value */
} rtos_notify_action_t;

typedef enum
{
    RTOS_NOTIFY_OK          = RTOS_SUCCESS,
    RTOS_NOTIFY_ERR_INVALID = RTOS_ERROR_INVALID_PARAM,
    RTOS_NOTIFY_ERR_TIMEOUT = RTOS_ERROR_TIMEOUT
} rtos_notify_status_t;

#define RTOS_NOTIFY_MAX_WAIT ((rtos_tick_t) 0xFFFFFFFFU)
#define RTOS_NOTIFY_NO_WAIT  ((rtos_tick_t) 0U)

/* Bitmask helpers for entry_clear_bits / exit_clear_bits parameters */
#define RTOS_NOTIFY_CLEAR_NONE ((uint32_t) 0x00000000U) /**< Clear no bits */
#define RTOS_NOTIFY_CLEAR_ALL  ((uint32_t) 0xFFFFFFFFU) /**< Clear all bits */

/**
 * @brief Send a notification to a task with a specific action.
 *
 * Can be called from ISR context. Never blocks the caller.
 *
 * @param task    Handle to the target task
 * @param value   Value to apply (meaning depends on action)
 * @param action  How to modify the target task's notification value
 * @return RTOS_NOTIFY_OK on success, RTOS_NOTIFY_ERR_INVALID if task is NULL
 */
rtos_notify_status_t rtos_task_notify(rtos_task_handle_t task, uint32_t value, rtos_notify_action_t action);

/**
 * @brief Simplified notify: increment the target's notification value.
 *
 * Lightweight binary/counting semaphore replacement.
 * Equivalent to rtos_task_notify(task, 0, RTOS_NOTIFY_ACTION_INCREMENT).
 *
 * @param task  Handle to the target task
 * @return RTOS_NOTIFY_OK on success
 */
rtos_notify_status_t rtos_task_notify_give(rtos_task_handle_t task);

/**
 * @brief Wait for a notification with bit-level control.
 *
 * @param entry_clear_bits  Bits to clear BEFORE checking pending state
 * @param exit_clear_bits   Bits to clear AFTER successful receive
 * @param value_out         Receives notification value before exit clear (can be NULL)
 * @param timeout_ticks     How long to wait (0 = no wait, MAX = forever)
 * @return RTOS_NOTIFY_OK on success, RTOS_NOTIFY_ERR_TIMEOUT if timed out
 */
rtos_notify_status_t rtos_task_notify_wait(uint32_t entry_clear_bits, uint32_t exit_clear_bits, uint32_t *value_out,
                                           rtos_tick_t timeout_ticks);

/**
 * @brief Take a notification (counting semaphore pattern).
 *
 * Blocks if notification_value == 0.
 *
 * @param clear_on_exit   true = reset value to 0, false = decrement by 1
 * @param timeout_ticks   How long to wait (0 = no wait, MAX = forever)
 * @return RTOS_NOTIFY_OK on success, RTOS_NOTIFY_ERR_TIMEOUT if timed out
 */
rtos_notify_status_t rtos_task_notify_take(bool clear_on_exit, rtos_tick_t timeout_ticks);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TASK_H */
