#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

/* Copy to boards/<your_board>/clock_config.h and uncomment values to override.
 * See boards/stm32f446re_nucleo/clock_config.h for a real example. */

/* ======================== Clock Aliases ================================= */
/* SysTick source. For most Cortex-M4 boards this equals the CPU clock.
 * Override if SysTick runs from a divided reference (e.g. AHB/8). */
// #define RTOS_SYSTICK_CLOCK_HZ  RTOS_SYSTEM_CLOCK_HZ
// #define RTOS_CPU_CLOCK_HZ      RTOS_SYSTEM_CLOCK_HZ

/* ======================== Tick Period ==================================== */
/* SysTick reload value in cycles. Usually no need to change. */
// #define RTOS_CYCLES_PER_TICK   (RTOS_SYSTICK_CLOCK_HZ / RTOS_TICK_RATE_HZ)

#endif /* CLOCK_CONFIG_H */
