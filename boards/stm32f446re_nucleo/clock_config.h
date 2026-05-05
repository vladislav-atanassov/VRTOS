#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

/**
 * @file clock_config.h
 * @brief Clock-derived constants for the STM32F446RE.
 *
 * RTOS_SYSTEM_CLOCK_HZ is defined in rtos_config.h. Clock aliases
 * that depend on it are defined here.
 */

#define RTOS_SYSTICK_CLOCK_HZ  RTOS_SYSTEM_CLOCK_HZ
#define RTOS_CPU_CLOCK_HZ      RTOS_SYSTEM_CLOCK_HZ

/** Number of SysTick counter cycles in one OS tick period. */
#define RTOS_CYCLES_PER_TICK   (RTOS_SYSTICK_CLOCK_HZ / RTOS_TICK_RATE_HZ)

#endif /* CLOCK_CONFIG_H */
