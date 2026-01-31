/*******************************************************************************
 * File: src/scheduler/scheduler.h
 * Description: Enhanced Scheduler Interface with List Management
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_SCHEDULER_H
#define RTOS_SCHEDULER_H

#include "rtos_types.h"

/**
 * @file scheduler.h
 * @brief Enhanced Scheduler Interface Definition
 *
 * This file defines the vtable interface that all schedulers must implement.
 * This version includes scheduler-specific list management operations for
 * optimal performance and flexibility.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/* Forward declarations */
typedef struct rtos_scheduler          rtos_scheduler_t;
typedef struct rtos_scheduler_instance rtos_scheduler_instance_t;

/* Scheduler Types */
typedef enum
{
    RTOS_SCHEDULER_PREEMPTIVE_SP = 0, /**< Preemptive static priority-based Scheduler */
    RTOS_SCHEDULER_COOPERATIVE   = 1, /**< Cooperative Scheduler */
    RTOS_SCHEDULER_ROUND_ROBIN   = 2  /**< Round Robin Scheduler */
} rtos_scheduler_type_t;

/* Deadline Types for EDF */
typedef uint32_t rtos_deadline_t; /**< Task deadline in ticks */
typedef uint32_t rtos_period_t;   /**< Task period in ticks */

/**
 * @brief Enhanced scheduler interface vtable
 *
 * All scheduler implementations must provide these function pointers.
 * This version includes list management operations that are scheduler-specific.
 */
struct rtos_scheduler
{
    /* =================== Core Scheduling Functions =================== */

    /**
     * @brief Initialize the scheduler
     * @param instance Scheduler instance
     * @return RTOS_SUCCESS if successful
     */
    rtos_status_t (*init)(rtos_scheduler_instance_t *instance);

    /**
     * @brief Get next task to run
     * @param instance Scheduler instance
     * @return Next task to run, NULL if none
     */
    rtos_task_handle_t (*get_next_task)(rtos_scheduler_instance_t *instance);

    /**
     * @brief Check if preemption is needed
     * @param instance Scheduler instance
     * @param new_task Newly ready task
     * @return True if preemption needed
     */
    bool (*should_preempt)(rtos_scheduler_instance_t *instance, rtos_task_handle_t new_task);

    /**
     * @brief Handle task completion/yield
     * @param instance Scheduler instance
     * @param completed_task Task that completed
     */
    void (*task_completed)(rtos_scheduler_instance_t *instance, rtos_task_handle_t completed_task);

    /* =================== List Management Operations =================== */

    /**
     * @brief Add task to scheduler's ready list
     * @param instance Scheduler instance
     * @param task_handle Task to add
     *
     * This function should add the task to the scheduler's ready list
     * using the appropriate ordering (priority for Preemptive static priority-based Scheduler,
     * deadline for EDF).
     */
    void (*add_to_ready_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);

    /**
     * @brief Remove task from scheduler's ready list
     * @param instance Scheduler instance
     * @param task_handle Task to remove
     */
    void (*remove_from_ready_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);

    /**
     * @brief Add task to scheduler's delayed/blocked list
     * @param instance Scheduler instance
     * @param task_handle Task to add
     * @param delay_ticks Delay in ticks (0 = add to blocked list)
     *
     * This function handles both delayed tasks (delay > 0) and blocked tasks (delay = 0).
     * The scheduler can use different lists for these as needed.
     */
    void (*add_to_delayed_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle,
                                rtos_tick_t delay_ticks);

    /**
     * @brief Remove task from scheduler's delayed/blocked list
     * @param instance Scheduler instance
     * @param task_handle Task to remove
     */
    void (*remove_from_delayed_list)(rtos_scheduler_instance_t *instance, rtos_task_handle_t task_handle);

    /**
     * @brief Update delayed tasks (called from tick handler)
     * @param instance Scheduler instance
     *
     * This function should check for delayed tasks that are ready to run
     * and move them from delayed list to ready list.
     */
    void (*update_delayed_tasks)(rtos_scheduler_instance_t *instance);

    /* =================== Optional Debug/Statistics =================== */

    /**
     * @brief Get scheduler statistics (optional)
     * @param instance Scheduler instance
     * @param stats_buffer Buffer to fill with statistics
     * @param buffer_size Size of statistics buffer
     * @return Number of bytes written, 0 if not supported
     */
    size_t (*get_statistics)(rtos_scheduler_instance_t *instance, void *stats_buffer, size_t buffer_size);
};

/**
 * @brief Scheduler instance structure
 *
 * Contains the vtable and instance-specific data
 */
struct rtos_scheduler_instance
{
    const rtos_scheduler_t *vtable;       /**< Function pointer table */
    rtos_scheduler_type_t   type;         /**< Scheduler type */
    void                   *private_data; /**< Scheduler-specific data */
    bool                    initialized;  /**< Initialization flag */
};

/* =================== Public Scheduler Manager API =================== */

extern rtos_scheduler_instance_t g_scheduler_instance;

/**
 * @brief Initialize the scheduler subsystem
 * @param scheduler_type Type of scheduler to use
 * @return RTOS_SUCCESS if successful
 */
rtos_status_t rtos_scheduler_init(rtos_scheduler_type_t scheduler_type);

/**
 * @brief Get the current scheduler type
 * @return Current scheduler type
 */
rtos_scheduler_type_t rtos_scheduler_get_type(void);

/* =================== Core Scheduling Operations =================== */

/**
 * @brief Get the next task to run
 * @return Next task handle, NULL if none
 */
rtos_task_handle_t rtos_scheduler_get_next_task(void);

/**
 * @brief Check if preemption is needed
 * @param new_task Newly ready task
 * @return True if preemption needed
 */
bool rtos_scheduler_should_preempt(rtos_task_handle_t new_task);

/**
 * @brief Handle task completion/yield
 * @param completed_task Task that completed
 */
void rtos_scheduler_task_completed(rtos_task_handle_t completed_task);

/* =================== List Management Operations =================== */

/**
 * @brief Add task to ready list via scheduler
 * @param task_handle Task to add
 */
void rtos_scheduler_add_to_ready_list(rtos_task_handle_t task_handle);

/**
 * @brief Remove task from ready list via scheduler
 * @param task_handle Task to remove
 */
void rtos_scheduler_remove_from_ready_list(rtos_task_handle_t task_handle);

/**
 * @brief Add task to delayed list via scheduler
 * @param task_handle Task to add
 * @param delay_ticks Delay in ticks
 */
void rtos_scheduler_add_to_delayed_list(rtos_task_handle_t task_handle, rtos_tick_t delay_ticks);

/**
 * @brief Remove task from delayed list via scheduler
 * @param task_handle Task to remove
 */
void rtos_scheduler_remove_from_delayed_list(rtos_task_handle_t task_handle);

/**
 * @brief Update delayed tasks via scheduler
 */
void rtos_scheduler_update_delayed_tasks(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_SCHEDULER_H */