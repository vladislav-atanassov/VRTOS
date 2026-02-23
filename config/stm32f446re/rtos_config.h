/*******************************************************************************
 * File: config/stm32f446re/rtos_config.h
 * Description: STM32F446RE Board-Specific RTOS Configuration
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_CONFIG_STM32F446RE_H
#define RTOS_CONFIG_STM32F446RE_H

/**
 * @file rtos_config.h
 * @brief STM32F446RE board overrides.
 *
 * Values defined here take precedence over the defaults in config.h.
 * Only define what differs from the defaults.
 */

#include "clock_config.h" // IWYU pragma: keep
#include "memory_map.h"   // IWYU pragma: keep

/* System clock — Nucleo-F446RE runs at 84 MHz */
#define RTOS_SYSTEM_CLOCK_HZ (84000000U)

/* Task limits */
#define RTOS_MAX_TASKS               (10U)
#define RTOS_DEFAULT_TASK_STACK_SIZE (768U)
#define RTOS_MINIMUM_TASK_STACK_SIZE (256U)

/* Heap */
#define RTOS_TOTAL_HEAP_SIZE (8192U)

/* Features */
#define RTOS_USE_FAST_INTERRUPTS         (1U)
#define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)

#endif /* RTOS_CONFIG_STM32F446RE_H */
