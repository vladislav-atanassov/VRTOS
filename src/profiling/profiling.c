#include "profiling.h"

#include "rtos_port.h"
#include "ulog.h"

#include <stddef.h>

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
    /* Avoid division by zero and handle potential overflow */
    if (SystemCoreClock == 0)
    {
        return 0;
    }
    /* cycles * 1000000 / SystemCoreClock, but avoid overflow by dividing first */
    /* For 16MHz clock: 1 cycle = 0.0625 us, so cycles / 16 = us */
    return (cycles / (SystemCoreClock / 1000000U));
}

void rtos_profiling_snapshot(const rtos_profile_stat_t *stat, rtos_profile_snapshot_t *out)
{
    if (stat == NULL || out == NULL || stat->count == 0)
    {
        return;
    }

    rtos_port_enter_critical();

    uint32_t min_cycles   = stat->min_cycles;
    uint32_t max_cycles   = stat->max_cycles;
    uint32_t total_cycles = stat->total_cycles;
    uint32_t count        = stat->count;

    rtos_port_exit_critical();

    uint32_t avg_cycles = total_cycles / count;

    out->min_cycles = min_cycles;
    out->max_cycles = max_cycles;
    out->avg_cycles = avg_cycles;
    out->count      = count;
    out->min_us     = cycles_to_us(min_cycles);
    out->max_us     = cycles_to_us(max_cycles);
    out->avg_us     = cycles_to_us(avg_cycles);
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

    ulog_info("[%s]: Min=%lu cyc (%lu us), Max=%lu cyc (%lu us), "
              "Avg=%lu cyc (%lu us), Cnt=%lu",
              name, (unsigned long) min_cycles, (unsigned long) min_us, (unsigned long) max_cycles,
              (unsigned long) max_us, (unsigned long) avg_cycles, (unsigned long) avg_us, (unsigned long) count);
}

#if RTOS_PROFILING_SYSTEM_ENABLED

void rtos_profiling_report_system_stats(void)
{
    ulog_info("=== RTOS System Profiling Report ===");
    rtos_profiling_print_stat(&g_prof_context_switch);
    rtos_profiling_print_stat(&g_prof_pendsv_full);
    rtos_profiling_print_stat(&g_prof_scheduler);
    rtos_profiling_print_stat(&g_prof_tick);
    rtos_profiling_print_stat(&g_prof_tick_jitter);
    rtos_profiling_print_stat(&g_prof_scheduling_latency);
}

void rtos_profiling_reset_system_stats(void)
{
    rtos_profiling_reset_stat(&g_prof_context_switch, "ContextSwitch");
    rtos_profiling_reset_stat(&g_prof_pendsv_full, "PendSV_Full");
    rtos_profiling_reset_stat(&g_prof_scheduler, "Scheduler");
    rtos_profiling_reset_stat(&g_prof_tick, "TickHandler");
    rtos_profiling_reset_stat(&g_prof_tick_jitter, "TickJitter");
    rtos_profiling_reset_stat(&g_prof_scheduling_latency, "SchedLatency");
}

rtos_profile_stat_t g_prof_context_switch     = {UINT32_MAX, 0, 0, 0, "ContextSwitch"};
rtos_profile_stat_t g_prof_scheduler          = {UINT32_MAX, 0, 0, 0, "Scheduler"};
rtos_profile_stat_t g_prof_tick               = {UINT32_MAX, 0, 0, 0, "TickHandler"};
rtos_profile_stat_t g_prof_pendsv_full        = {UINT32_MAX, 0, 0, 0, "PendSV_Full"};
rtos_profile_stat_t g_prof_tick_jitter        = {UINT32_MAX, 0, 0, 0, "TickJitter"};
rtos_profile_stat_t g_prof_scheduling_latency = {UINT32_MAX, 0, 0, 0, "SchedLatency"};

volatile uint32_t g_pendsv_cycles       = 0;
volatile uint32_t g_pendsv_start_cycles = 0;

#else /* RTOS_PROFILING_SYSTEM_ENABLED == 0 */

void rtos_profiling_report_system_stats(void)
{
    ulog_info("System profiling disabled (RTOS_PROFILING_SYSTEM_ENABLED=0)");
}

void rtos_profiling_reset_system_stats(void)
{
    /* No-op when profiling is disabled */
}

#endif /* RTOS_PROFILING_SYSTEM_ENABLED */
