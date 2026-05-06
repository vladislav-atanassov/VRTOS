#ifndef RTOS_CONFIG_STM32F446RE_H
#define RTOS_CONFIG_STM32F446RE_H

/* Values defined here take precedence over the defaults in config.h. */

#include "clock_config.h" // IWYU pragma: keep
#include "memory_map.h"   // IWYU pragma: keep

/* System clock — Nucleo-F446RE runs at 84 MHz */
#define RTOS_SYSTEM_CLOCK_HZ (84000000U)

/* UART logging baud rate. The serial monitor must match this value
 * (pass -b to `python -m kartos monitor/test` if you change it). */
#define RTOS_UART_BAUD_RATE (921600U)

/* Power Management: enable tickless idle (idle task suppresses SysTick
 * and sleeps with WFI when the next wake-up is far enough away). */
#define RTOS_CONFIG_USE_TICKLESS_IDLE               (1U)
#define RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP (5U)

/* Task limits */
#define RTOS_MAX_TASKS               (10U)
#define RTOS_DEFAULT_TASK_STACK_SIZE (768U)
#define RTOS_MINIMUM_TASK_STACK_SIZE (256U)
#define KLOG_FLUSH_TASK_STACK_SIZE   (2048U)

/* Heap */
#define RTOS_TOTAL_HEAP_SIZE (16384U)

#define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)

#endif /* RTOS_CONFIG_STM32F446RE_H */
