#include "profiling.h"

#include "rtos_port.h"
#include "ulog.h"

#include <stddef.h>

#include "config.h"    // IWYU pragma: keep
#include "stm32f4xx.h" // IWYU pragma: keep

void rtos_profiling_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t rtos_profiling_get_cycles(void)
{
    return DWT->CYCCNT;
}

void rtos_profiling_reset_stat(rtos_profile_stat_t *stat, const char *name)
{
    if (stat == NULL)
    {
        return;
    }

    rtos_port_enter_critical();

    stat->min_cycles   = UINT32_MAX;
    stat->max_cycles   = 0;
    stat->total_cycles = 0;
    stat->count        = 0;
    stat->name         = name;

    rtos_port_exit_critical();
}

/* uint32_t subtraction naturally handles DWT_CYCCNT wraparound for
 * measurements under ~268s. total_cycles may overflow after many
 * samples — avg becomes inaccurate, but min/max remain valid. */
void rtos_profiling_record(rtos_profile_stat_t *stat, uint32_t cycles)
{
    if (stat == NULL)
    {
        return;
    }

    rtos_port_enter_critical();

    if (cycles < stat->min_cycles)
    {
        stat->min_cycles = cycles;
    }
    if (cycles > stat->max_cycles)
    {
        stat->max_cycles = cycles;
    }

    stat->total_cycles += cycles;
    stat->count++;

    rtos_port_exit_critical();
}

static uint32_t cycles_to_us(uint32_t cycles)
{
    return cycles / (RTOS_CPU_CLOCK_HZ / 1000000U);
}

void rtos_profiling_print_stat(rtos_profile_stat_t *stat)
{
    if (stat == NULL || stat->count == 0)
    {
        return;
    }

    rtos_port_enter_critical();

    const char *name         = stat->name != NULL ? stat->name : "unnamed";
    uint32_t    min_cycles   = stat->min_cycles;
    uint32_t    max_cycles   = stat->max_cycles;
    uint32_t    total_cycles = stat->total_cycles;
    uint32_t    count        = stat->count;

    rtos_port_exit_critical();

    uint32_t avg_cycles = total_cycles / count;

    uint32_t min_us = cycles_to_us(min_cycles);
    uint32_t max_us = cycles_to_us(max_cycles);
    uint32_t avg_us = cycles_to_us(avg_cycles);

    ulog_info("[%s]: Min=%lu cyc (%lu us), Max=%lu cyc (%lu us), "
              "Avg=%lu cyc (%lu us), Cnt=%lu",
              name, (unsigned long) min_cycles, (unsigned long) min_us, (unsigned long) max_cycles,
              (unsigned long) max_us, (unsigned long) avg_cycles, (unsigned long) avg_us, (unsigned long) count);
}

