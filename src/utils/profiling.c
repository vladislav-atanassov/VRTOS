/*******************************************************************************
 * File: src/utils/profiling.c
 * Description: Profiling Implementation (DWT Cycle Counter)
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "profiling.h"

#include "log.h"
#include "rtos_port.h"

#include <stddef.h>

/* CMSIS Core for DWT, CoreDebug, and SystemCoreClock */
#include "stm32f4xx.h" // IWYU pragma: keep

void rtos_profiling_init(void)
{
    /* Enable TRCENA in DEMCR to allow DWT usage */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* Reset Cycle Counter */
    DWT->CYCCNT = 0;

    /* Enable Cycle Counter in DWT_CTRL */
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

/**
 * @brief Record a profiling measurement with thread-safe updates
 *
 * Uses critical sections to prevent data corruption when called from
 * multiple task contexts or ISRs.
 *
 * @note The cycles value is expected to come from RTOS_PROFILE_END macro,
 *       which computes (end - start) as uint32_t. This subtraction naturally
 *       handles DWT_CYCCNT wraparound correctly for measurements shorter than
 *       ~268 seconds (at 16MHz).
 *
 * @note total_cycles may overflow after many measurements. This is a known
 *       limitation. The average calculation will be inaccurate once overflow
 *       occurs, but min/max values remain valid.
 */
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

/**
 * @brief Convert cycles to microseconds using SystemCoreClock
 * @param cycles Number of CPU cycles
 * @return Time in microseconds
 */
static uint32_t cycles_to_us(uint32_t cycles)
{
    /* Avoid division by zero and handle potential overflow */
    if (SystemCoreClock == 0)
    {
        return 0;
    }
    /* cycles * 1000000 / SystemCoreClock, but avoid overflow by dividing first */
    /* For 16MHz clock: 1 cycle = 0.0625 us, so cycles / 16 = us */
    return (cycles / (SystemCoreClock / 1000000U));
}

void rtos_profiling_print_stat(rtos_profile_stat_t *stat)
{
    if (stat == NULL || stat->count == 0)
    {
        return;
    }

    /* Capture values atomically for consistent snapshot */
    rtos_port_enter_critical();

    const char *name         = stat->name != NULL ? stat->name : "unnamed";
    uint32_t    min_cycles   = stat->min_cycles;
    uint32_t    max_cycles   = stat->max_cycles;
    uint32_t    total_cycles = stat->total_cycles;
    uint32_t    count        = stat->count;

    rtos_port_exit_critical();

    uint32_t avg_cycles = total_cycles / count;

    /* Convert to microseconds for human-readable output */
    uint32_t min_us = cycles_to_us(min_cycles);
    uint32_t max_us = cycles_to_us(max_cycles);
    uint32_t avg_us = cycles_to_us(avg_cycles);

    log_info("PROFILE [%s]: Min=%lu cycles (%lu us), Max=%lu cycles (%lu us), "
             "Avg=%lu cycles (%lu us), Cnt=%lu",
             name, (unsigned long) min_cycles, (unsigned long) min_us, (unsigned long) max_cycles,
             (unsigned long) max_us, (unsigned long) avg_cycles, (unsigned long) avg_us, (unsigned long) count);
}

/* =============================================================================
 * SYSTEM PROFILING (Conditional Compilation)
 * ============================================================================= */

#if RTOS_PROFILING_SYSTEM_ENABLED

void rtos_profiling_report_system_stats(void)
{
    log_info("=== RTOS System Profiling Report ===");
    rtos_profiling_print_stat(&g_prof_context_switch);
    rtos_profiling_print_stat(&g_prof_scheduler);
    rtos_profiling_print_stat(&g_prof_tick);
}

/* System profiling statistics - only compiled when enabled */
rtos_profile_stat_t g_prof_context_switch = {UINT32_MAX, 0, 0, 0, "ContextSwitch"};
rtos_profile_stat_t g_prof_scheduler      = {UINT32_MAX, 0, 0, 0, "Scheduler"};
rtos_profile_stat_t g_prof_tick           = {UINT32_MAX, 0, 0, 0, "TickHandler"};

#else /* RTOS_PROFILING_SYSTEM_ENABLED == 0 */

void rtos_profiling_report_system_stats(void)
{
    log_info("System profiling disabled (RTOS_PROFILING_SYSTEM_ENABLED=0)");
}

#endif /* RTOS_PROFILING_SYSTEM_ENABLED */
