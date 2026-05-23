#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

/* Board-specific overrides (resolved via -I config/<board>/) */
#include "rtos_config.h" // IWYU pragma: keep

/* ======================== System Configuration ========================== */

#ifndef RTOS_SYSTEM_CLOCK_HZ
#define RTOS_SYSTEM_CLOCK_HZ (16000000U) /**< System clock frequency in Hz */
#endif

#ifndef RTOS_TICK_RATE_HZ
#define RTOS_TICK_RATE_HZ (1000U) /**< System tick frequency in Hz (1ms tick) */
#endif

#ifndef RTOS_TICK_PERIOD_MS
#define RTOS_TICK_PERIOD_MS (1000U / RTOS_TICK_RATE_HZ) /**< Tick period in ms */
#endif

/* Enable Tickless Idle mode for low power consumption */
#ifndef RTOS_CONFIG_USE_TICKLESS_IDLE
#define RTOS_CONFIG_USE_TICKLESS_IDLE (0U)
#endif

/* Minimum number of ticks the system must be idle before entering tickless sleep.
   This prevents entering low-power modes for very short durations where the 
   overhead of reprogramming timers exceeds the power saved. */
#ifndef RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP
#define RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP (5U)
#endif

/* ======================== Task Configuration ============================ */

#ifndef RTOS_MAX_TASKS
#define RTOS_MAX_TASKS (8U) /**< Maximum number of tasks */
#endif

#ifndef RTOS_MAX_TASK_PRIORITIES
#define RTOS_MAX_TASK_PRIORITIES (8U) /**< Maximum priority levels */
#endif

#ifndef RTOS_IDLE_TASK_PRIORITY
#define RTOS_IDLE_TASK_PRIORITY (0U) /**< Idle task priority (lowest) */
#endif

#ifndef RTOS_DEFAULT_TASK_STACK_SIZE
#define RTOS_DEFAULT_TASK_STACK_SIZE (1024U) /**< Default task stack size in bytes */
#endif

#ifndef RTOS_MINIMUM_TASK_STACK_SIZE
#define RTOS_MINIMUM_TASK_STACK_SIZE (128U) /**< Minimum allowed task stack size */
#endif

/* ======================== Scheduler Configuration ======================= */

#ifndef RTOS_SCHEDULER_TYPE
#define RTOS_SCHEDULER_TYPE RTOS_SCHEDULER_PREEMPTIVE_SP
#endif

/* Ensure enum values are available */
#if !defined(RTOS_SCHEDULER_PREEMPTIVE_SP) || !defined(RTOS_SCHEDULER_COOPERATIVE) ||                                  \
    !defined(RTOS_SCHEDULER_ROUND_ROBIN)
#include "scheduler.h" // IWYU pragma: keep
#endif

/* Scheduler-specific flags */
#if (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_PREEMPTIVE_SP)
#define RTOS_USE_PRIORITY_SCHEDULING 1
#elif (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_COOPERATIVE)
#define RTOS_USE_COOPERATIVE_SCHEDULING 1
#elif (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_ROUND_ROBIN)
#define RTOS_USE_ROUND_ROBIN_SCHEDULING 1
#endif

#ifndef RTOS_TIME_SLICE_TICKS
#define RTOS_TIME_SLICE_TICKS 1 /**< Time slice in ticks */
#endif

/* ======================== Memory Configuration ========================== */

#ifndef RTOS_TOTAL_HEAP_SIZE
#define RTOS_TOTAL_HEAP_SIZE (16384U) /**< Total heap size for task stacks */
#endif

/* Minimum block size for the dual-ended heap allocator. A block split that
 * would leave a remainder smaller than this threshold is suppressed (the
 * whole free block is handed out instead). Must be >= sizeof(heap header)
 * + a useful payload. 16 bytes = 8-byte header + 8-byte minimum payload. */
#ifndef RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE
#define RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE (16U)
#endif

/* ======================== Logging Configuration ========================= */

/* UART baud rate for logging output. Must match the serial monitor speed. */
#ifndef RTOS_UART_BAUD_RATE
#define RTOS_UART_BAUD_RATE (921600U) /**< UART baud rate for logging */
#endif

/* ======================== Debug Configuration =========================== */

#ifndef RTOS_ASSERT_ENABLED
#define RTOS_ASSERT_ENABLED (1U) /**< Enable assertions */
#endif

#ifndef RTOS_ENABLE_STACK_OVERFLOW_CHECK
#define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)
#endif

/* Verify the per-task stack canary on every context switch. Adds one
 * load + compare on the outgoing task before it is re-readied; on mismatch
 * the application stack-overflow hook (rtos_hooks.h) fires with the offending
 * task handle. Requires RTOS_ENABLE_STACK_OVERFLOW_CHECK to do anything. */
#ifndef RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH
#if RTOS_ENABLE_STACK_OVERFLOW_CHECK
#define RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH (1U)
#else
#define RTOS_CONFIG_AUTO_STACK_CHECK_ON_SWITCH (0U)
#endif
#endif

#endif /* RTOS_CONFIG_H */
