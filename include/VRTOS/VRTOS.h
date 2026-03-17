#ifndef VRTOS_H
#define VRTOS_H

#include "rtos_types.h"

/**
 * @file VRTOS.h
 * @brief Main RTOS API Header
 *
 * This is the main header file that applications should include to use the RTOS.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Initialize the RTOS system
 *
 * This function initializes all RTOS components including the scheduler,
 * task management, and system tick. Must be called before any other RTOS functions.
 *
 * @return RTOS_SUCCESS if initialization successful, error code otherwise
 */
rtos_status_t rtos_init(void);

/**
 * @brief Start the RTOS scheduler
 *
 * This function starts the RTOS scheduler and begins task execution.
 * This function does not return if successful.
 *
 * @return Error code if scheduler fails to start
 */
rtos_status_t rtos_start_scheduler(void);

/**
 * @brief Get the current system tick count
 *
 * @return Current tick count since system start
 */
rtos_tick_t rtos_get_tick_count(void);

/**
 * @brief Delay the current task for specified number of ticks
 *
 * @param ticks Number of ticks to delay
 */
void rtos_delay_ticks(rtos_tick_t ticks);

/**
 * @brief Delay the current task for specified number of milliseconds
 *
 * @param ms Number of milliseconds to delay
 */
void rtos_delay_ms(uint32_t ms);

/**
 * @brief Delay a task until a specified time
 *
 * @param[in,out] prev_wake_time Pointer to a variable that holds the time at which the task was last unblocked.
 *                               The variable must be initialized with the current time prior to its first use.
 *                               Following this the variable is automatically updated within rtos_delay_until().
 * @param time_increment The cycle time period. The task will be unblocked at time *prev_wake_time + time_increment.
 */
void rtos_delay_until(rtos_tick_t *const prev_wake_time, rtos_tick_t time_increment);

/**
 * @brief Force a task yield (context switch)
 *
 * This function forces the scheduler to run and potentially switch tasks.
 */
void rtos_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* VRTOS_H */