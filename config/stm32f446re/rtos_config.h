/*******************************************************************************
 * File: config/stm32f446re/rtos_config.h
 * Description: STM32F446RE Specific RTOS Configuration
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_CONFIG_STM32F446RE_H
#define RTOS_CONFIG_STM32F446RE_H

/**
 * @file rtos_config.h
 * @brief STM32F446RE Specific RTOS Configuration
 * 
 * This file contains RTOS configuration parameters specific to the
 * STM32F446RE microcontroller and Nucleo board.
 */

/* Include the generic RTOS configuration */
#include "/VRTOS/config.h"

/* STM32F446RE Specific Overrides */
#undef RTOS_SYSTEM_CLOCK_HZ
#define RTOS_SYSTEM_CLOCK_HZ                (84000000U)    /**< 84MHz system clock */

#undef RTOS_MAX_TASKS
#define RTOS_MAX_TASKS                      (10U)          /**< Increased for this target */

#undef RTOS_TOTAL_HEAP_SIZE
#define RTOS_TOTAL_HEAP_SIZE                (8192U)        /**< 8KB for task stacks */

/* Hardware specific settings */
#define RTOS_SYSTICK_CLOCK_HZ               RTOS_SYSTEM_CLOCK_HZ
#define RTOS_CPU_CLOCK_HZ                   RTOS_SYSTEM_CLOCK_HZ

/* Memory layout specific to STM32F446RE */
#define RTOS_FLASH_START                    (0x08000000U)
#define RTOS_FLASH_SIZE                     (512U * 1024U) /* 512KB Flash */
#define RTOS_RAM_START                      (0x20000000U)
#define RTOS_RAM_SIZE                       (128U * 1024U) /* 128KB RAM */

/* Stack sizes optimized for STM32F446RE */
#undef RTOS_DEFAULT_TASK_STACK_SIZE
#define RTOS_DEFAULT_TASK_STACK_SIZE        (768U)         /**< Larger default stack */

#undef RTOS_MINIMUM_TASK_STACK_SIZE
#define RTOS_MINIMUM_TASK_STACK_SIZE        (256U)         /**< Minimum stack size */

/* Performance optimizations */
#define RTOS_USE_FAST_INTERRUPTS            (1U)           /**< Enable fast interrupt handling */
#define RTOS_USE_TICKLESS_IDLE              (0U)           /**< Disable for MVP */

/* Debug and profiling features */
#define RTOS_ENABLE_STACK_OVERFLOW_CHECK    (1U)           /**< Enable stack overflow detection */
#define RTOS_ENABLE_RUNTIME_STATS           (0U)           /**< Disable for MVP */
#define RTOS_ENABLE_TRACE                   (0U)           /**< Disable for MVP */

#endif /* RTOS_CONFIG_STM32F446RE_H */