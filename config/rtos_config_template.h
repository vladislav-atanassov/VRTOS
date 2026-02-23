/*******************************************************************************
 * File: config/rtos_config_template.h
 * Description: RTOS Configuration Template
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_CONFIG_BOARD_H
#define RTOS_CONFIG_BOARD_H

/**
 * @file rtos_config.h (template)
 * @brief Board-specific RTOS configuration template.
 *
 * Copy this file to config/<your_board>/rtos_config.h and uncomment
 * the values you need to override. Defaults are in include/VRTOS/config.h.
 */

/* Board-specific headers (create these for your board) */
// #include "memory_map.h"
// #include "clock_config.h"

/* ======================== System ======================================== */
// #define RTOS_SYSTEM_CLOCK_HZ        (84000000U)
// #define RTOS_TICK_RATE_HZ           (1000U)

/* ======================== Tasks ========================================= */
// #define RTOS_MAX_TASKS              (10U)
// #define RTOS_MAX_TASK_PRIORITIES    (8U)
// #define RTOS_DEFAULT_TASK_STACK_SIZE (768U)
// #define RTOS_MINIMUM_TASK_STACK_SIZE (256U)

/* ======================== Scheduler ===================================== */
// #define RTOS_SCHEDULER_TYPE RTOS_SCHEDULER_PREEMPTIVE_SP
// #define RTOS_TIME_SLICE_TICKS       (20)

/* ======================== Memory ======================================== */
// #define RTOS_TOTAL_HEAP_SIZE        (8192U)

/* ======================== Features ====================================== */
// #define RTOS_USE_FAST_INTERRUPTS         (1U)
// #define RTOS_USE_TICKLESS_IDLE           (0U)
// #define RTOS_ENABLE_STACK_OVERFLOW_CHECK (1U)
// #define RTOS_ENABLE_RUNTIME_STATS       (0U)
// #define RTOS_ENABLE_TRACE               (0U)

/* ======================== Debug ========================================= */
// #define RTOS_ASSERT_ENABLED (1U)

#endif /* RTOS_CONFIG_BOARD_H */
