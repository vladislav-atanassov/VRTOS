#ifndef RTOS_CONFIG_STM32F446RE_H
#define RTOS_CONFIG_STM32F446RE_H

/* Values defined here take precedence over the defaults in config.h. */

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

#define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)

#endif /* RTOS_CONFIG_STM32F446RE_H */
