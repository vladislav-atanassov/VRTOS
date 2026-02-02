/*******************************************************************************
 * File: src/utils/hardware_env.h
 * Description: Hardware Environment - Shared hardware initialization for tests
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef HARDWARE_ENV_H
#define HARDWARE_ENV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** 
 * @brief Initialize hardware environment (clock, GPIO, LED)
 *
 * Call this at the start of main() before RTOS initialization.
 * Sets up system clock, enables GPIO, and configures LED pin.
 */
void hardware_env_config(void);

/**
 * @brief Toggle LED state
 */
void led_toggle(void);

/**
 * @brief Set LED to specific state
 * @param on true = LED on, false = LED off
 */
void led_set(bool on);

/**
 * @brief Indicate system failure by rapid LED blinking
 *
 * This function never returns. Used for fatal errors
 * during initialization or runtime.
 */
__attribute__((__noreturn__)) void indicate_system_failure(void);

/**
 * @brief HAL error handler
 *
 * Called by HAL functions on error. Never returns.
 */
__attribute__((__noreturn__)) void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* HARDWARE_ENV_H */
