#ifndef RTOS_TASK_H
#define RTOS_TASK_H

#include "rtos_types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Create a new task and add it to the scheduler's ready list.
 * @param task_function Task entry point.
 * @param name Task name string (not copied; must remain valid for the task lifetime).
 * @param stack_size Stack size in bytes. 0 uses the default; values below the minimum are clamped.
 * @param parameter Argument passed to task_function.
 * @param priority Task priority (0 = lowest/idle, higher value = higher priority).
 * @param task_handle Output handle for the created task.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_task_create(rtos_task_function_t task_function, const char *name, rtos_stack_size_t stack_size,
                               void *parameter, rtos_priority_t priority, rtos_task_handle_t *task_handle);

/**
 * @brief Create a task using a caller-provided stack buffer (no heap allocation for stack).
 *        The TCB itself still comes from the static g_task_pool, bounded by RTOS_MAX_TASKS.
 * @param task_function Task entry point.
 * @param name          Task name string (not copied; must remain valid for the task lifetime).
 * @param stack_buffer  Caller-owned stack memory. Must remain valid for the task lifetime.
 *                      Must be 8-byte aligned.
 * @param stack_size    Size of stack_buffer in bytes. Must be at least RTOS_MINIMUM_TASK_STACK_SIZE.
 * @param parameter     Argument passed to task_function.
 * @param priority      Task priority.
 * @param task_handle   Output handle for the created task.
 * @return RTOS_SUCCESS or an error code.
 *
 * @note rtos_task_delete() on a statically-created task does not free stack memory.
 */
rtos_status_t rtos_task_create_static(rtos_task_function_t task_function, const char *name, uint32_t *stack_buffer,
                                      rtos_stack_size_t stack_size, void *parameter, rtos_priority_t priority,
                                      rtos_task_handle_t *task_handle);

/**
 * @brief Return the idle task TCB.
 * @return Pointer to the idle task TCB, or NULL if the idle task has not been created yet.
 */
rtos_tcb_t *rtos_task_get_idle_task(void);

/**
 * @brief Return the currently running task handle.
 * @return Handle of the current task, or NULL before the scheduler starts.
 */
rtos_task_handle_t rtos_task_get_current(void);

/**
 * @brief Return the state of a task.
 * @param task_handle Task to query. NULL returns RTOS_TASK_STATE_DELETED.
 * @return Current task state.
 */
rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task_handle);

/**
 * @brief Return the effective priority of a task (may be boosted by priority inheritance).
 * @param task_handle Task to query. NULL returns 0.
 * @return Current task priority.
 */
rtos_priority_t rtos_task_get_priority(rtos_task_handle_t task_handle);

/**
 * @brief Return the name of the task with the given ID.
 * @param task_id Task ID.
 * @return Task name string, or "?" if not found.
 */
const char *rtos_task_get_name(rtos_task_id_t task_id);

/**
 * @brief Return the name of the currently running task.
 * @return Task name string, or "none" if no task is running.
 */
const char *rtos_get_current_task_name(void);

/**
 * @brief Suspend a task, removing it from the ready and delayed lists.
 * @param task_handle Task to suspend, or NULL to suspend the calling task.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_task_suspend(rtos_task_handle_t task_handle);

/**
 * @brief Resume a previously suspended task.
 * @param task_handle Task to resume (must not be NULL).
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_task_resume(rtos_task_handle_t task_handle);

/**
 * @brief Delete a task. Pass NULL to delete the calling task (self-delete, never returns).
 *
 * The TCB slot is returned to the pool (task_function cleared to NULL). If the task was
 * created with rtos_task_create(), its stack memory is freed back to the HIGH heap;
 * for self-delete the free is deferred to the idle task (the deleting task is still
 * running on it). Tasks created with rtos_task_create_static() keep their stack
 * (caller-owned). The idle task cannot be deleted. If the deleted task held mutexes
 * they are released to the highest-priority waiter (or marked free).
 *
 * @param task_handle Task to delete, or NULL for the calling task.
 * @return RTOS_SUCCESS, or an error code if the task cannot be deleted.
 */
rtos_status_t rtos_task_delete(rtos_task_handle_t task_handle);

/**
 * @brief Check for stack overflow via the canary pattern at the stack base.
 * @param task_handle Task to check, or NULL to check all tasks.
 * @return true if an overflow was detected in any checked task.
 */
bool rtos_task_check_stack(rtos_task_handle_t task_handle);

typedef enum
{
    RTOS_NOTIFY_ACTION_NONE = 0,  /* just set pending, don't modify value */
    RTOS_NOTIFY_ACTION_SET_BITS,  /* OR value into notification_value */
    RTOS_NOTIFY_ACTION_INCREMENT, /* notification_value++ */
    RTOS_NOTIFY_ACTION_OVERWRITE  /* replace notification_value */
} rtos_notify_action_t;

typedef enum
{
    RTOS_NOTIFY_OK          = RTOS_SUCCESS,
    RTOS_NOTIFY_ERR_INVALID = RTOS_ERROR_INVALID_PARAM,
    RTOS_NOTIFY_ERR_TIMEOUT = RTOS_ERROR_TIMEOUT
} rtos_notify_status_t;

#define RTOS_NOTIFY_MAX_WAIT ((rtos_tick_t) 0xFFFFFFFFU)
#define RTOS_NOTIFY_NO_WAIT  ((rtos_tick_t) 0U)

#define RTOS_NOTIFY_CLEAR_NONE ((uint32_t) 0x00000000U)
#define RTOS_NOTIFY_CLEAR_ALL  ((uint32_t) 0xFFFFFFFFU)

/**
 * @brief Send a notification to a task. Task-context only — uses the task-mode
 *        critical section and may pend a context switch via the unblock path.
 *        For ISR contexts use rtos_task_notify_from_isr().
 * @param task Target task handle.
 * @param value Notification value (interpretation depends on action).
 * @param action How the value is applied to the target's notification_value.
 * @return RTOS_NOTIFY_OK or RTOS_NOTIFY_ERR_INVALID.
 */
rtos_notify_status_t rtos_task_notify(rtos_task_handle_t task, uint32_t value, rtos_notify_action_t action);

/**
 * @brief Increment the target task's notification value. Lightweight binary/counting semaphore replacement.
 *        Equivalent to rtos_task_notify(task, 0, RTOS_NOTIFY_ACTION_INCREMENT).
 * @param task Target task handle.
 * @return RTOS_NOTIFY_OK or an error code.
 */
rtos_notify_status_t rtos_task_notify_give(rtos_task_handle_t task);

/**
 * @brief ISR-context counterpart of rtos_task_notify(). Uses an ISR-local
 *        critical section and pends PendSV directly so a higher-priority
 *        wake happens after the ISR tail-chains out.
 *
 * Must only be called from interrupt context.
 */
rtos_notify_status_t rtos_task_notify_from_isr(rtos_task_handle_t task, uint32_t value, rtos_notify_action_t action);

/**
 * @brief ISR-context counterpart of rtos_task_notify_give(). See rtos_task_notify_from_isr().
 */
rtos_notify_status_t rtos_task_notify_give_from_isr(rtos_task_handle_t task);

/**
 * @brief Wait for a notification, optionally clearing bits on entry and exit.
 * @param entry_clear_bits Bits cleared in notification_value before checking pending state.
 * @param exit_clear_bits  Bits cleared in notification_value after successful receive.
 * @param value_out        Output: notification value at time of wake (can be NULL).
 * @param timeout_ticks    Maximum ticks to wait. RTOS_NOTIFY_NO_WAIT returns immediately.
 * @return RTOS_NOTIFY_OK on receipt, RTOS_NOTIFY_ERR_TIMEOUT on timeout.
 */
rtos_notify_status_t rtos_task_notify_wait(uint32_t entry_clear_bits, uint32_t exit_clear_bits, uint32_t *value_out,
                                           rtos_tick_t timeout_ticks);

/**
 * @brief Block until notification_value > 0, then consume it.
 * @param clear_on_exit true = reset notification_value to 0, false = decrement by 1.
 * @param timeout_ticks Maximum ticks to wait.
 * @return RTOS_NOTIFY_OK on receipt, RTOS_NOTIFY_ERR_TIMEOUT on timeout.
 */
rtos_notify_status_t rtos_task_notify_take(bool clear_on_exit, rtos_tick_t timeout_ticks);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TASK_H */
