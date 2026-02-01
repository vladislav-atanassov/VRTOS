/*******************************************************************************
 * File: include/VRTOS/config.h
 * Description: RTOS Configuration Header
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

/**
 * @file config.h
 * @brief RTOS Configuration Parameters
 *
 * This file contains all configurable parameters for the RTOS system.
 * Modify these values to customize the RTOS behavior for your application.
 */

/* System Configuration */
#define RTOS_SYSTEM_CLOCK_HZ (16000000U)                 /**< System clock frequency in Hz */
#define RTOS_TICK_RATE_HZ    (1000U)                     /**< System tick frequency in Hz (1ms tick) */
#define RTOS_TICK_PERIOD_MS  (1000U / RTOS_TICK_RATE_HZ) /**< Tick period in milliseconds */

/* Task Configuration */
#define RTOS_MAX_TASKS               (8U)    /**< Maximum number of tasks */
#define RTOS_MAX_TASK_PRIORITIES     (8U)    /**< Maximum number of priority levels */
#define RTOS_IDLE_TASK_PRIORITY      (0U)    /**< Idle task priority (lowest) */
#define RTOS_DEFAULT_TASK_STACK_SIZE (1024U) /**< Default task stack size in bytes */
#define RTOS_MINIMUM_TASK_STACK_SIZE (128U)  /**< Minimum allowed task stack size */

/* Scheduler Configuration */
#ifndef RTOS_SCHEDULER_TYPE
#define RTOS_SCHEDULER_TYPE RTOS_SCHEDULER_PREEMPTIVE_SP /**< Preemptive scheduler for sync demo */
#endif

/* Ensure the macro expands to the correct enum value */
#if !defined(RTOS_SCHEDULER_PREEMPTIVE_SP) || !defined(RTOS_SCHEDULER_COOPERATIVE) ||                                  \
    !defined(RTOS_SCHEDULER_ROUND_ROBIN)
#include "scheduler.h" // IWYU pragma: keep - Include to get enum definitions
#endif

/* Scheduler-specific configurations */
#if (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_PREEMPTIVE_SP)
/* Preemptive static priority-based Scheduler uses priority-based scheduling */
#define RTOS_USE_PRIORITY_SCHEDULING 1
#elif (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_COOPERATIVE)
/* Cooperative scheduling */
#define RTOS_USE_COOPERATIVE_SCHEDULING 1
#elif (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_ROUND_ROBIN)
/* Round robin scheduling with time slicing */
#define RTOS_USE_ROUND_ROBIN_SCHEDULING 1
#endif

/* Time slice configuration for round-robin within same priority */
#ifndef RTOS_TIME_SLICE_TICKS
#define RTOS_TIME_SLICE_TICKS 20 /**< Time slice in ticks */
#endif

/* Memory Configuration */
#define RTOS_TOTAL_HEAP_SIZE (16384U) /**< Total heap size for task stacks */
#define RTOS_STACK_ALIGNMENT (8U)     /**< Stack alignment requirement */

/* Debug Configuration */
#define RTOS_DEBUG_ENABLED  (1U) /**< Enable debug features */
#define RTOS_ASSERT_ENABLED (1U) /**< Enable assertions */

/**
 * @brief Interrupt Priority Configuration
 * 
 * Cortex-M4 uses 4-bit priority (16 levels) in upper bits of 8-bit field.
 * Lower numeric value = higher priority.
 */

/* Highest priority - never masked (time-critical hardware) */
#define RTOS_IRQ_PRIORITY_CRITICAL (0x00) /* DMA, critical timers */

/* High priority - can preempt RTOS operations */
#define RTOS_IRQ_PRIORITY_HIGH (0x40) /* UART RX, SPI, ADC */

/* RTOS kernel priority level - SysTick, PendSV run here */
#define RTOS_IRQ_PRIORITY_KERNEL (0x80) /* SysTick */
#define RTOS_IRQ_PRIORITY_PENDSV (0xF0) /* PendSV (lowest for late reschedule) */

/* Low priority - masked during critical sections */
#define RTOS_IRQ_PRIORITY_LOW (0xC0) /* Non-critical peripherals */

/* BASEPRI threshold - mask this priority and lower (numerically higher) */
#define RTOS_KERNEL_INTERRUPT_PRIORITY (RTOS_IRQ_PRIORITY_KERNEL)

#endif /* RTOS_CONFIG_H */
