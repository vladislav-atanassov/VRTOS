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
#define RTOS_SYSTEM_CLOCK_HZ                (84000000U)    /**< System clock frequency in Hz */
#define RTOS_TICK_RATE_HZ                   (1000U)        /**< System tick frequency in Hz (1ms tick) */
#define RTOS_TICK_PERIOD_MS                 (1000U / RTOS_TICK_RATE_HZ)  /**< Tick period in milliseconds */

/* Task Configuration */
#define RTOS_MAX_TASKS                      (8U)           /**< Maximum number of tasks */
#define RTOS_MAX_TASK_PRIORITIES            (8U)           /**< Maximum number of priority levels */
#define RTOS_IDLE_TASK_PRIORITY             (0U)           /**< Idle task priority (lowest) */
#define RTOS_DEFAULT_TASK_STACK_SIZE        (512U)         /**< Default task stack size in bytes */
#define RTOS_MINIMUM_TASK_STACK_SIZE        (128U)         /**< Minimum allowed task stack size */

/* Scheduler Configuration */
#define RTOS_SCHEDULER_TYPE_ROUND_ROBIN     (1U)           /**< Round-robin scheduler enabled */
#define RTOS_TIME_SLICE_MS                  (10U)          /**< Time slice for round-robin in ms */
#define RTOS_TIME_SLICE_TICKS               (RTOS_TIME_SLICE_MS / RTOS_TICK_PERIOD_MS)

/* Memory Configuration */
#define RTOS_TOTAL_HEAP_SIZE                (4096U)        /**< Total heap size for task stacks */
#define RTOS_STACK_ALIGNMENT                (8U)           /**< Stack alignment requirement */

/* Debug Configuration */
#define RTOS_DEBUG_ENABLED                  (1U)           /**< Enable debug features */
#define RTOS_ASSERT_ENABLED                 (1U)           /**< Enable assertions */

/* Feature Flags */
#define RTOS_USE_MUTEXES                    (0U)           /**< Enable mutex support */
#define RTOS_USE_SEMAPHORES                 (0U)           /**< Enable semaphore support */
#define RTOS_USE_QUEUES                     (0U)           /**< Enable queue support */
#define RTOS_USE_TIMERS                     (0U)           /**< Enable software timer support */

#endif // RTOS_CONFIG_H
