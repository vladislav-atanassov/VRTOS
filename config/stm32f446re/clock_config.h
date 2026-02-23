/*******************************************************************************
 * File: config/stm32f446re/clock_config.h
 * Description: STM32F446RE Clock Configuration
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

/**
 * @file clock_config.h
 * @brief Clock-derived constants for the STM32F446RE.
 *
 * RTOS_SYSTEM_CLOCK_HZ is defined in rtos_config.h. Clock aliases
 * that depend on it are defined here.
 */

#define RTOS_SYSTICK_CLOCK_HZ RTOS_SYSTEM_CLOCK_HZ
#define RTOS_CPU_CLOCK_HZ     RTOS_SYSTEM_CLOCK_HZ

#endif /* CLOCK_CONFIG_H */
