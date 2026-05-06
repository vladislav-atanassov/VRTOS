/**
 * @file hardware_env.h
 * @brief Example bootstrap for the STM32F446RE Nucleo board.
 *
 * Programs the system clock, GPIO, and LED so the demos under
 * src/examples/ run with no hardware setup boilerplate. NOT required
 * to use KARTOS as a kernel. A user using KARTOS in their own
 * application is expected to set RTOS_SYSTEM_CLOCK_HZ in their
 * board's rtos_config.h and configure the hardware themselves.
 */

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
 * @brief Get the active CPU clock frequency in Hz.
 *
 * Returns the CMSIS SystemCoreClock global, which hardware_env_config()
 * syncs to RTOS_SYSTEM_CLOCK_HZ via SystemCoreClockUpdate(). Before
 * hardware_env_config() runs, returns the CMSIS reset default (HSI).
 *
 * @return CPU clock frequency in Hz.
 */
uint32_t hardware_env_cpu_clock_hz(void);

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
