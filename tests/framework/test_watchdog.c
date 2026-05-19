#include "test_watchdog.h"

#include "KARTOS.h"  /* rtos_get_tick_count */
#include "config.h"  /* RTOS_TICK_RATE_HZ */
#include "timer.h"

#include <stdio.h>
#include <string.h>

/* ── Public state ─────────────────────────────────────────────────────────── */

volatile bool        g_test_aborted          = false;
volatile rtos_tick_t g_test_watchdog_deadline = 0;

/* ── Module-private state ─────────────────────────────────────────────────── */

static rtos_timer_handle_t g_watchdog_timer    = NULL;

/*
 * Buffer holding "INV-WATCHDOG:<phase_name>" for the currently armed phase.
 * Stored by pointer in the invariant table, so it must be module-static (not
 * stack-allocated). Only one watchdog runs at a time, so one buffer suffices.
 */
static char g_watchdog_inv_id[64];

/* Prevents double-recording if disarm() is called more than once per arm(). */
static bool g_watchdog_recorded = false;

/* ── Timer ISR callback ───────────────────────────────────────────────────── */

static void watchdog_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    /* Called from SysTick ISR. Only set the flag; no blocking APIs. */
    g_test_aborted = true;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void test_watchdog_arm(const char *phase_name, uint32_t budget_ms)
{
    g_test_aborted      = false;
    g_watchdog_recorded = false;

    snprintf(g_watchdog_inv_id, sizeof(g_watchdog_inv_id),
             "INV-WATCHDOG:%s", phase_name ? phase_name : "?");

    rtos_tick_t budget_ticks = (rtos_tick_t)(budget_ms * RTOS_TICK_RATE_HZ / 1000U);
    if (budget_ticks == 0U)
    {
        budget_ticks = 1U;
    }

    g_test_watchdog_deadline = rtos_get_tick_count() + budget_ticks;

    if (g_watchdog_timer == NULL)
    {
        /* Lazy creation — requires the scheduler to be running. */
        rtos_timer_create("Watchdog", budget_ticks, RTOS_TIMER_ONE_SHOT,
                          watchdog_callback, NULL, &g_watchdog_timer);
    }
    else
    {
        rtos_timer_stop(g_watchdog_timer);
        rtos_timer_change_period(g_watchdog_timer, budget_ticks);
    }

    if (g_watchdog_timer != NULL)
    {
        rtos_timer_start(g_watchdog_timer);
    }
}

void test_watchdog_disarm(void)
{
    g_test_watchdog_deadline = 0U;

    if (g_watchdog_timer != NULL)
    {
        rtos_timer_stop(g_watchdog_timer);
    }

    if (g_test_aborted && !g_watchdog_recorded)
    {
        g_watchdog_recorded = true;
        /* g_watchdog_inv_id is module-static — pointer remains valid. */
        test_inv_fail(g_watchdog_inv_id, "phase watchdog expired");
    }

    /* Reset for next phase or case. */
    g_test_aborted = false;
}
