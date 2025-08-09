/*******************************************************************************
 * File: include/VRTOS/VRTOS.h
 * Description: Main RTOS API Header
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef VRTOS_H
#define VRTOS_H

#include "config.h"
#include "rtos_types.h"
#include "task.h"
#include "mutex.h"
#include "semaphore.h"
#include "queue.h"
#include "timer.h"

/**
 * @file VRTOS.h
 * @brief Main RTOS API Header
 *
 * This is the main header file that applications should include to use the RTOS.
 */

#ifdef __cplusplus
extern "C" {
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
 * @brief Force a task yield (context switch)
 *
 * This function forces the scheduler to run and potentially switch tasks.
 */
void rtos_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* VRTOS_H */