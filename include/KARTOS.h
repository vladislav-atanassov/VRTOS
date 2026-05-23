#ifndef KARTOS_H
#define KARTOS_H

#include "rtos_types.h"

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
 * @brief Block the calling task for the specified number of ticks.
 * @param ticks Number of ticks to delay. 0 returns immediately.
 */
void rtos_delay_ticks(rtos_tick_t ticks);

/**
 * @brief Block the calling task for approximately the given number of milliseconds.
 * @param ms Milliseconds to delay (rounded up to the nearest tick).
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
 * @brief Yield the CPU to allow the scheduler to select the next task.
 */
void rtos_yield(void);

/**
 * @brief Suspend task switching. Nestable: pair every call with
 *        rtos_scheduler_resume(). Tick counting, timer expiry, and
 *        delayed-list wake-ups continue to run; only the context-switch
 *        side-effect is deferred until the matching resume.
 *
 * The currently running task keeps the CPU until it voluntarily exits this
 * region. Do NOT call blocking RTOS APIs (rtos_delay_*, rtos_mutex_lock with
 * non-zero timeout, etc.) while suspended — nothing else can run to wake the
 * caller, and the system will hang.
 */
void rtos_scheduler_suspend(void);

/**
 * @brief Pair a prior rtos_scheduler_suspend(). When the nesting count drops
 *        to zero, any context switch that was deferred during the suspended
 *        region runs now.
 * @return true if a deferred switch was fired by this call, false otherwise
 *         (still nested, or no switch was pending).
 */
bool rtos_scheduler_resume(void);

#ifdef __cplusplus
}
#endif

#endif /* KARTOS_H */
