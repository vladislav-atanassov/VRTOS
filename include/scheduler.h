#ifndef RTOS_SCHEDULER_H
#define RTOS_SCHEDULER_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct rtos_scheduler          rtos_scheduler_t;
typedef struct rtos_scheduler_instance rtos_scheduler_instance_t;

typedef enum
{
    RTOS_SCHEDULER_PREEMPTIVE_SP = 0,
    RTOS_SCHEDULER_COOPERATIVE   = 1,
    RTOS_SCHEDULER_ROUND_ROBIN   = 2
} rtos_scheduler_type_t;

struct rtos_scheduler
{
    /** @brief Initialize scheduler private state and bind it to the instance. */
    rtos_status_t (*init)(rtos_scheduler_instance_t *instance);
    /** @brief Return the next task to run, or NULL if no task is ready. */
    rtos_task_handle_t (*get_next_task)(rtos_scheduler_instance_t *instance);
    /** @brief Return true if new_task should preempt the currently running task. */
    bool (*should_preempt)(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task);
    /** @brief Notify the scheduler that completed_task has finished its time slice. */
    void (*task_completed)(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task);

    /** @brief Add task_handle to the ready list. */
    void (*add_to_ready_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);
    /** @brief Remove task_handle from the ready list. */
    void (*remove_from_ready_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);
    /** @brief Insert task_handle into the time-ordered delayed list to wake after delay_ticks. */
    void (*add_to_delayed_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle,
                                rtos_tick_t delay_ticks);
    /** @brief Remove task_handle from the delayed list (e.g. on timeout cancellation). */
    void (*remove_from_delayed_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);
    /** @brief Move all expired delayed tasks to the ready list. Called each tick. */
    void (*update_delayed_tasks)(rtos_scheduler_instance_t *instance);
    /** @brief Return ticks until the next delayed task wakes, or 0xFFFFFFFF if none. */
    uint32_t (*get_expected_idle_ticks)(rtos_scheduler_instance_t *instance);

    /** @brief Fill stats_buffer with scheduler-specific statistics. Returns bytes written. */
    size_t (*get_statistics)(rtos_scheduler_instance_t *instance, void *stats_buffer, size_t buffer_size);
};

struct rtos_scheduler_instance
{
    const rtos_scheduler_t *vtable;
    rtos_scheduler_type_t   type;
    void                   *private_data;
    bool                    initialized;
};

extern rtos_scheduler_instance_t g_scheduler_instance;

/**
 * @brief Initialize the scheduler for the given type. Must be called once before the kernel starts.
 * @param scheduler_type Scheduler algorithm to use.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_scheduler_init(rtos_scheduler_type_t scheduler_type);

/**
 * @brief Return the type of the active scheduler.
 * @return Active scheduler type.
 */
rtos_scheduler_type_t rtos_scheduler_get_type(void);

/**
 * @brief Ask the scheduler for the next task to run.
 * @return Handle of the selected task, or NULL if no task is ready.
 */
rtos_task_handle_t rtos_scheduler_get_next_task(void);

/**
 * @brief Ask the scheduler whether new_task should preempt the current task.
 * @param new_task Candidate task that just became ready.
 * @return true if a context switch should occur.
 */
bool rtos_scheduler_should_preempt(rtos_task_handle_t new_task);

/**
 * @brief Notify the scheduler that completed_task has finished its current run slice.
 * @param completed_task Task that was just switched out.
 */
void rtos_scheduler_task_completed(rtos_task_handle_t completed_task);

/**
 * @brief Add a task to the scheduler's ready list.
 * @param task_handle Task to add.
 */
void rtos_scheduler_add_to_ready_list(rtos_task_handle_t task_handle);

/**
 * @brief Remove a task from the scheduler's ready list.
 * @param task_handle Task to remove.
 */
void rtos_scheduler_remove_from_ready_list(rtos_task_handle_t task_handle);

/**
 * @brief Add a task to the scheduler's time-ordered delayed list.
 * @param task_handle Task to delay.
 * @param delay_ticks Number of ticks until the task should wake.
 */
void rtos_scheduler_add_to_delayed_list(rtos_task_handle_t task_handle, rtos_tick_t delay_ticks);

/**
 * @brief Remove a task from the scheduler's delayed list.
 * @param task_handle Task to remove.
 */
void rtos_scheduler_remove_from_delayed_list(rtos_task_handle_t task_handle);

/**
 * @brief Move all expired delayed tasks to the ready list. Called from the tick handler.
 */
void rtos_scheduler_update_delayed_tasks(void);

/**
 * @brief Return the number of ticks until the next delayed task wakes.
 * @return Ticks until next wake, or 0xFFFFFFFF if no tasks are delayed.
 */
uint32_t rtos_scheduler_get_expected_idle_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_SCHEDULER_H */
