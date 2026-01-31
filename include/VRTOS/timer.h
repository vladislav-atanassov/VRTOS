/*******************************************************************************
 * File: include/VRTOS/timer.h
 * Description: Software Timer API
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_TIMER_H
#define RTOS_TIMER_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Timer modes
 */
typedef enum
{
    RTOS_TIMER_ONE_SHOT    = 0, /**< Run once and stop */
    RTOS_TIMER_AUTO_RELOAD = 1  /**< Run periodically */
} rtos_timer_mode_t;

/**
 * @brief Timer callback function type
 * @param timer_handle Handle of the timer that expired
 * @param parameter User parameter provided at creation
 */
typedef void (*rtos_timer_callback_t)(void *timer_handle, void *parameter);

/**
 * @brief Timer structure instantiation (opaque in public API)
 * Note: Actual definition is in timer_priv.h
 */
typedef struct rtos_timer rtos_timer_t;
typedef rtos_timer_t     *rtos_timer_handle_t;

/**
 * @brief Create a new software timer
 *
 * @param name Timer name (for debugging)
 * @param period_ticks Timer period in ticks
 * @param mode One-shot or Auto-reload
 * @param callback Function to call when timer expires
 * @param parameter Parameter to pass to callback
 * @param timer_handle Pointer to store returned handle
 * @return RTOS_SUCCESS on success
 */
rtos_status_t rtos_timer_create(const char *name, rtos_tick_t period_ticks, rtos_timer_mode_t mode,
                                rtos_timer_callback_t callback, void *parameter, rtos_timer_handle_t *timer_handle);

/**
 * @brief Start a timer
 *
 * @param timer_handle Timer to start
 * @return RTOS_SUCCESS or error
 */
rtos_status_t rtos_timer_start(rtos_timer_handle_t timer_handle);

/**
 * @brief Stop a timer
 *
 * @param timer_handle Timer to stop
 * @return RTOS_SUCCESS or error
 */
rtos_status_t rtos_timer_stop(rtos_timer_handle_t timer_handle);

/**
 * @brief Change timer period
 *
 * @param timer_handle Timer to modify
 * @param new_period_ticks New period
 * @return RTOS_SUCCESS or error
 */
rtos_status_t rtos_timer_change_period(rtos_timer_handle_t timer_handle, rtos_tick_t new_period_ticks);

/**
 * @brief Delete a timer and free resources
 *
 * @param timer_handle Timer to delete
 * @return RTOS_SUCCESS or error
 */
rtos_status_t rtos_timer_delete(rtos_timer_handle_t timer_handle);

/**
 * @brief Process timer ticks (Called by Kernel)
 */
void rtos_timer_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TIMER_H */
