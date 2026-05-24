#ifndef RTOS_CONFIG_BOARD_H
#define RTOS_CONFIG_BOARD_H

/* Copy to boards/<your_board>/rtos_config.h and uncomment values to override.
 * Defaults are in include/config.h.
 * See boards/stm32f446re_nucleo/rtos_config.h for a real example. */

/* Board-specific headers — REQUIRED (not optional), both must exist in boards/<your_board>/) */
// #include "clock_config.h"
// #include "memory_map.h"

/* ======================== System ======================================== */
// #define RTOS_SYSTEM_CLOCK_HZ        (84000000U)
// #define RTOS_TICK_RATE_HZ           (1000U)

/* ======================== Tasks ========================================= */
// #define RTOS_MAX_TASKS              (10U)
// #define RTOS_MAX_TASK_PRIORITIES    (8U)
// #define RTOS_DEFAULT_TASK_STACK_SIZE (768U)
// #define RTOS_MINIMUM_TASK_STACK_SIZE (256U)
// #define LOG_FLUSH_TASK_STACK_SIZE    (2048U)

/* ======================== Scheduler ===================================== */
// #define RTOS_SCHEDULER_TYPE RTOS_SCHEDULER_PREEMPTIVE_SP
// #define RTOS_TIME_SLICE_TICKS       (20)

/* ======================== Power Management ============================== */
/* Tickless idle: stop the SysTick during long idle windows, sleep with WFI,
 * and reconcile the tick count on wake. Off by default. */
// #define RTOS_CONFIG_USE_TICKLESS_IDLE               (1U)
/* Idle windows shorter than this (in ticks) skip tickless sleep — the
 * SysTick reprogramming overhead is not worth it. */
// #define RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP (5U)

/* ======================== Memory ======================================== */
// #define RTOS_TOTAL_HEAP_SIZE        (8192U)

/* ======================== Logging ======================================= */
// #define RTOS_UART_BAUD_RATE         (921600U)
// #define RTOS_KLOG_ENABLED           (1U)              /* Kernel auto-inits klog */
// #define RTOS_KLOG_MIN_LEVEL         (3U)              /* 0=FAULT … 5=TRACE */
// #define RTOS_ULOG_ENABLED           (1U)              /* Kernel auto-inits ulog */
// #define RTOS_ULOG_MIN_LEVEL         (3U)              /* 0=NONE 1=ERROR … 4=DEBUG */
// #define LOG_FLUSH_TASK_STACK_SIZE   (2048U)

/* ======================== Features ====================================== */
// #define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)

/* ======================== Debug ========================================= */
// #define RTOS_ASSERT_ENABLED (1U)

#endif /* RTOS_CONFIG_BOARD_H */
