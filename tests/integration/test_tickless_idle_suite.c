/**
 * @file test_tickless_idle_suite.c
 * @brief Tickless idle test suite — exercises rtos_port_suppress_ticks_and_sleep.
 *
 * Invariant prefix: INV-TICKLESS-
 *
 * Tickless idle has historically been fragile (see project memory:
 * project-tickless-idle-bug, project-tickless-suppress-hang). The two known
 * failure modes both surface the same way at the system level:
 *   1. SysTick is left disabled / mis-loaded after wake → tick_count freezes
 *      → every subsequent rtos_delay_ms() hangs forever.
 *   2. The wake path mis-counts cycles → tick_count drifts away from real time.
 *
 * Every case below sets up a scenario that drives the idle task through
 * rtos_port_suppress_ticks_and_sleep at least once, then asserts either that
 * elapsed kernel ticks track real time or that the hardware SysTick state is
 * the standard 1ms configuration on the way out.
 *
 * This variant force-defines RTOS_CONFIG_USE_TICKLESS_IDLE=1 (via the CMake
 * variant's EXTRA_DEFINES), so the suite is meaningful regardless of the
 * board default. TEST_ASSUME on the same macro is kept as belt-and-suspenders
 * in case someone copies this file into another build context.
 *
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "clock_config.h"  /* RTOS_CYCLES_PER_TICK */
#include "config.h"
#include "rtos_port.h"     /* rtos_port_get_uptime_us */
#include "stm32f4xx.h"     /* SysTick */
#include "task.h"
#include "ulog.h"          /* diagnostic logging of register state */

/* ── Tunables ────────────────────────────────────────────────────────────── */

/* Long enough to comfortably exceed RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP
 * (5 ticks) so the idle task is guaranteed to take the tickless suppress
 * path, not bare WFI. */
#define LONG_SLEEP_MS 50U

/* Tolerance for "delay completed in approximately N ticks". Tickless wake
 * rounds cycles_slept down to whole ticks, so under-counting by 1 tick is
 * possible; jitter from log-flush / hook-drain wake-ups can add a few more.
 * Keep the window generous enough that healthy hardware always passes. */
#define DELAY_TOLERANCE_LOW_TICKS   2U
#define DELAY_TOLERANCE_HIGH_TICKS  20U

/* Soak duration for the regression test. The "freeze after ~2.6s" memory
 * said the bug surfaced after a few seconds; 3s covers that and stays
 * inside reasonable host-side stall budgets. */
#define SOAK_ITERATIONS 30U
#define SOAK_STEP_MS    100U

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/**
 * Drive the idle task through at least one suppress cycle.
 *
 * Because the suite runner is the highest-priority application task and the
 * only readier tasks are log_flush (P0) and (optionally) hook_drain (P1), the
 * scheduler must run the idle task while the runner is blocked. With an
 * expected idle horizon of LONG_SLEEP_MS ticks (>> 5), idle takes the
 * suppress path on every iteration.
 */
static rtos_tick_t do_tickless_sleep(uint32_t ms)
{
    rtos_tick_t before = rtos_get_tick_count();
    rtos_delay_ms(ms);
    return rtos_get_tick_count() - before;
}

/* Acceptable LOAD window for the 1ms-tick mode. Two known-good values exist:
 *   - cycles_per_tick - 1: what rtos_port_suppress_ticks_and_sleep restores
 *                          after a tickless cycle (port.c).
 *   - cycles_per_tick - 2: what SysTick_Config(cycles_per_tick - 1) leaves at
 *                          boot (CMSIS subtracts another 1 internally).
 * Anything else is either the long-sleep reload value left by suppress (huge)
 * or a corrupted value — both real bugs. */
#define LOAD_LOW_ACCEPT  (RTOS_CYCLES_PER_TICK - 2U)
#define LOAD_HIGH_ACCEPT (RTOS_CYCLES_PER_TICK - 1U)

static bool systick_in_running_1ms_mode(uint32_t *out_ctrl, uint32_t *out_load)
{
    /* Read CTRL once. This clears COUNTFLAG as a side effect, which is fine
     * because nothing in the suite depends on the flag's value here. */
    uint32_t ctrl = SysTick->CTRL;
    uint32_t load = SysTick->LOAD;

    if (out_ctrl) *out_ctrl = ctrl;
    if (out_load) *out_load = load;

    const uint32_t required = SysTick_CTRL_CLKSOURCE_Msk
                            | SysTick_CTRL_TICKINT_Msk
                            | SysTick_CTRL_ENABLE_Msk;

    if ((ctrl & required) != required)
    {
        return false;
    }
    if (load < LOAD_LOW_ACCEPT || load > LOAD_HIGH_ACCEPT)
    {
        return false;
    }
    return true;
}

/* ── Case 1: single long delay completes in bounded time ─────────────────── */
/*
 * The absolute minimum tickless contract: one rtos_delay_ms(50) must wake the
 * runner within ~50 ticks. A frozen SysTick (the headline failure mode) makes
 * this case hang until the watchdog fires.
 */
TEST_CASE(tickless, single_long_delay_completes)
{
    TEST_ASSUME(RTOS_CONFIG_USE_TICKLESS_IDLE == 1U,
                "INV-TICKLESS-ENABLED requires RTOS_CONFIG_USE_TICKLESS_IDLE=1");
    TEST_INV_DECLARE("INV-TICKLESS-WAKES",      1);
    TEST_INV_DECLARE("INV-TICKLESS-NO-FREEZE",  1);

    rtos_tick_t elapsed = 0U;
    TEST_AWAIT_PHASE("long-sleep", LONG_SLEEP_MS + 200U, {
        elapsed = do_tickless_sleep(LONG_SLEEP_MS);
    });

    /* Reaching this assertion at all proves SysTick did not freeze. */
    TEST_EXPECT(elapsed > 0U, "INV-TICKLESS-NO-FREEZE");

    /* And the delay finished in approximately the requested time. */
    TEST_EXPECT(elapsed >= (LONG_SLEEP_MS - DELAY_TOLERANCE_LOW_TICKS),
                "INV-TICKLESS-WAKES");
    TEST_EXPECT(elapsed <= (LONG_SLEEP_MS + DELAY_TOLERANCE_HIGH_TICKS),
                "INV-TICKLESS-WAKES");
}

/* ── Case 2: SysTick hardware healthy after wake ─────────────────────────── */
/*
 * Direct probe of the failure mode described in project-tickless-suppress-hang
 * (SysTick exits with ENABLE=0 and LOAD still set to the sleep-period reload
 * value). After any tickless sleep, CTRL must have ENABLE|TICKINT|CLKSOURCE
 * set and LOAD must be back to the standard 1ms reload.
 */
TEST_CASE(tickless, systick_state_healthy_after_wake)
{
    TEST_ASSUME(RTOS_CONFIG_USE_TICKLESS_IDLE == 1U,
                "INV-TICKLESS-HW-STATE requires RTOS_CONFIG_USE_TICKLESS_IDLE=1");
    TEST_INV_DECLARE("INV-TICKLESS-HW-STATE", 1);

    uint32_t ctrl = 0U, load = 0U;

    /* Force at least one suppress cycle. */
    TEST_AWAIT_PHASE("provoke-suppress", LONG_SLEEP_MS + 200U, {
        (void) do_tickless_sleep(LONG_SLEEP_MS);
    });

    bool ok = systick_in_running_1ms_mode(&ctrl, &load);
    ulog_info("tickless: post-50ms wake CTRL=0x%08x LOAD=%u (expect [%u..%u])",
              (unsigned) ctrl, (unsigned) load,
              (unsigned) LOAD_LOW_ACCEPT, (unsigned) LOAD_HIGH_ACCEPT);
    TEST_EXPECT(ok, "INV-TICKLESS-HW-STATE");

    /* Do another short delay (which still triggers suppress since 10 >= 5)
     * and re-check — a single restore could pass by luck. */
    TEST_AWAIT_PHASE("provoke-suppress-again", 200U, {
        (void) do_tickless_sleep(10U);
    });

    ok = systick_in_running_1ms_mode(&ctrl, &load);
    ulog_info("tickless: post-10ms wake CTRL=0x%08x LOAD=%u",
              (unsigned) ctrl, (unsigned) load);
    TEST_EXPECT(ok, "INV-TICKLESS-HW-STATE");
}

/* ── Case 3: repeated long delays don't accumulate drift ─────────────────── */
/*
 * Twenty back-to-back tickless cycles must complete in close to 20 *
 * LONG_SLEEP_MS ticks. A per-cycle off-by-one in cycles_slept would
 * compound here into hundreds of ms of drift.
 */
TEST_CASE(tickless, repeated_long_delays_total_correct_time)
{
    TEST_ASSUME(RTOS_CONFIG_USE_TICKLESS_IDLE == 1U,
                "INV-TICKLESS-DRIFT requires RTOS_CONFIG_USE_TICKLESS_IDLE=1");
    TEST_INV_DECLARE("INV-TICKLESS-DRIFT", 1);

    const uint32_t iters    = 20U;
    const uint32_t step_ms  = 20U;
    const uint32_t expected = iters * step_ms;

    rtos_tick_t before = 0U;
    rtos_tick_t after  = 0U;

    TEST_AWAIT_PHASE("drift-loop", expected + 500U, {
        before = rtos_get_tick_count();
        for (uint32_t i = 0; i < iters; i++)
        {
            rtos_delay_ms(step_ms);
        }
        after = rtos_get_tick_count();
    });

    rtos_tick_t elapsed = after - before;

    /* Accept a small per-cycle truncation + a few jitter ticks. */
    TEST_EXPECT(elapsed >= (expected - (DELAY_TOLERANCE_LOW_TICKS * iters)),
                "INV-TICKLESS-DRIFT");
    TEST_EXPECT(elapsed <= (expected + DELAY_TOLERANCE_HIGH_TICKS + iters),
                "INV-TICKLESS-DRIFT");
}

/* ── Case 4: sustained idle for ~3 seconds keeps ticking ─────────────────── */
/*
 * Regression test for project-tickless-idle-bug: the original symptom was
 * tick_count freezing after roughly 2.6 seconds of runtime once a transient
 * cleared TICKINT and the RMW pattern kept perpetuating it. Drive the system
 * through 30 tickless cycles spanning ~3 s and assert tick_count advances on
 * every iteration plus matches real time within a generous band.
 */
TEST_CASE(tickless, sustained_idle_three_seconds_keeps_ticking)
{
    TEST_ASSUME(RTOS_CONFIG_USE_TICKLESS_IDLE == 1U,
                "INV-TICKLESS-SOAK requires RTOS_CONFIG_USE_TICKLESS_IDLE=1");
    TEST_INV_DECLARE("INV-TICKLESS-SOAK-MONOTONIC", SOAK_ITERATIONS);
    TEST_INV_DECLARE("INV-TICKLESS-SOAK-TOTAL",     1);

    const uint32_t expected_ms = SOAK_ITERATIONS * SOAK_STEP_MS;

    rtos_tick_t last  = rtos_get_tick_count();
    rtos_tick_t start = last;

    TEST_AWAIT_PHASE("soak-loop", expected_ms + 1000U, {
        for (uint32_t i = 0; i < SOAK_ITERATIONS; i++)
        {
            rtos_delay_ms(SOAK_STEP_MS);
            rtos_tick_t now = rtos_get_tick_count();
            /* Soft: tick must advance every iteration. A single frozen step
             * implicates the suppress path on that specific cycle. */
            TEST_EXPECT(now > last, "INV-TICKLESS-SOAK-MONOTONIC");
            last = now;
        }
    });

    rtos_tick_t elapsed = last - start;

    TEST_EXPECT(elapsed >= (expected_ms - (DELAY_TOLERANCE_LOW_TICKS * SOAK_ITERATIONS)),
                "INV-TICKLESS-SOAK-TOTAL");
    /* Upper bound is looser — log-flush and any background ticks can extend
     * each sleep by a few ms, but compounding shouldn't exceed ~10%. */
    TEST_EXPECT(elapsed <= (expected_ms + (expected_ms / 10U)),
                "INV-TICKLESS-SOAK-TOTAL");
}

/* ── Case 5: uptime_us stays monotonic across sleep ──────────────────────── */
/*
 * rtos_port_get_uptime_us combines tick_count with SysTick->VAL. If suppress
 * leaves SysTick LOADed for the long sleep period and VAL counts down from
 * there, the (load + 1 - val) cycles-into-tick computation produces nonsense
 * numbers (potentially > 1ms, clamped to 999us but still wrong direction).
 * Verify the wall-clock view is monotonic and roughly proportional to the
 * delay we requested.
 */
TEST_CASE(tickless, uptime_us_monotonic_across_sleep)
{
    TEST_ASSUME(RTOS_CONFIG_USE_TICKLESS_IDLE == 1U,
                "INV-TICKLESS-UPTIME requires RTOS_CONFIG_USE_TICKLESS_IDLE=1");
    TEST_INV_DECLARE("INV-TICKLESS-UPTIME-MONOTONIC", 1);
    TEST_INV_DECLARE("INV-TICKLESS-UPTIME-SCALE",     1);

    uint32_t us_before = rtos_port_get_uptime_us();

    TEST_AWAIT_PHASE("uptime-sleep", LONG_SLEEP_MS + 200U, {
        rtos_delay_ms(LONG_SLEEP_MS);
    });

    uint32_t us_after = rtos_port_get_uptime_us();

    TEST_EXPECT(us_after > us_before, "INV-TICKLESS-UPTIME-MONOTONIC");

    /* Convert delta to ms; require it tracks the delay we requested within
     * the same band the tick-based cases use. */
    uint32_t delta_us = us_after - us_before;
    uint32_t delta_ms = delta_us / 1000U;

    TEST_EXPECT(delta_ms >= (LONG_SLEEP_MS - DELAY_TOLERANCE_LOW_TICKS),
                "INV-TICKLESS-UPTIME-SCALE");
    TEST_EXPECT(delta_ms <= (LONG_SLEEP_MS + DELAY_TOLERANCE_HIGH_TICKS),
                "INV-TICKLESS-UPTIME-SCALE");
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_tickless_cases[] = {
    &TEST_CASE_REF(tickless, single_long_delay_completes),
    &TEST_CASE_REF(tickless, systick_state_healthy_after_wake),
    &TEST_CASE_REF(tickless, repeated_long_delays_total_correct_time),
    &TEST_CASE_REF(tickless, sustained_idle_three_seconds_keeps_ticking),
    &TEST_CASE_REF(tickless, uptime_us_monotonic_across_sleep),
};

TEST_SUITE_DEFINE(tickless, g_tickless_cases);

int main(void)
{
    return test_runtime_main(&test_suite_tickless);
}
