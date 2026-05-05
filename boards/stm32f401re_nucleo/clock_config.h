#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

/*
 * Clock-derived constants for the STM32F401RE Nucleo.
 * RTOS_SYSTEM_CLOCK_HZ is defined in rtos_config.h (84 MHz).
 */

#define RTOS_SYSTICK_CLOCK_HZ  RTOS_SYSTEM_CLOCK_HZ
#define RTOS_CPU_CLOCK_HZ      RTOS_SYSTEM_CLOCK_HZ

#define RTOS_CYCLES_PER_TICK   (RTOS_SYSTICK_CLOCK_HZ / RTOS_TICK_RATE_HZ)

#endif /* CLOCK_CONFIG_H */
