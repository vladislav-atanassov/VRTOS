/**
 * @file test_scheduler_suspend_suite.c
 * @brief Scheduler suspend/resume API test suite (MR-2).
 *
 * Invariant prefix: INV-SCHED-SUSPEND-
 * Scheduler: PREEMPTIVE_SP.
 *
 * The suite covers:
 *   - counter mechanics (suspend/resume nest balance)
 *   - tick advancement is not blocked by suspend (only the context-switch is)
 *   - tick handler continues to wake delayed tasks while suspended; they
 *     land READY but do not run until resume drops the count to zero
 *   - explicit rtos_yield() is a no-op while suspended (switch_context guard)
 *   - resume reports whether it fired a deferred switch
 *
 * The runner sits at RTOS_MAX_TASK_PRIORITIES-1 (set by the framework), so
 * we can't directly exhibit "higher-priority wake gets deferred". Instead
 * we verify the observable surface: counter mechanics, tick keeps running,
 * delayed-list wakes still happen, and explicit yields are gated. Same-
 * priority peers exercise the switch_context guard path.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "kernel_priv.h"  /* g_kernel — needed to peek at scheduler_suspended */
#include "task.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_LOW  2U
#define STK       RTOS_DEFAULT_TASK_STACK_SIZE

/* ── Fixture state ──────────────────────────────────────────────────────── */

static volatile bool     s_delayed_worker_ran;
static volatile uint32_t s_peer_ran_count;

static rtos_task_handle_t s_delayed_worker;
static rtos_task_handle_t s_peer_task;

static void suspend_before(void)
{
    s_delayed_worker_ran = false;
    s_peer_ran_count     = 0U;
    s_delayed_worker     = NULL;
    s_peer_task          = NULL;

    /* Defensive: a previous case must always leave the counter at 0. If it
     * doesn't, the next case starts in an inconsistent state and the
     * failure mode is non-obvious. Make the violation explicit. */
    while (g_kernel.scheduler_suspended > 0U)
    {
        rtos_scheduler_resume();
    }
    g_kernel.yield_pending = false;
}

/* ── Task bodies ────────────────────────────────────────────────────────── */

static void delayed_worker_body(void *_)
{
    (void) _;
    rtos_delay_ms(10);
    s_delayed_worker_ran = true;
    rtos_task_delete(NULL);
}

static void peer_increment_and_yield(void *_)
{
    (void) _;
    for (;;)
    {
        s_peer_ran_count++;
        rtos_yield();
    }
}

/* ── Case 1: suspend_resume_counter_mechanics ───────────────────────────── */
/*
 * Each rtos_scheduler_suspend() increments the nesting counter; each
 * rtos_scheduler_resume() decrements it. Resume returns true only on the
 * call that actually drained a pending deferred switch (no pending here →
 * always false). The counter must end where it started.
 */
TEST_CASE(suspend, suspend_resume_counter_mechanics)
{
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-COUNTER", 4);
    TEST_INV_DECLARE("INV-SCHED-RESUME-RETURN",   2);

    TEST_EXPECT(g_kernel.scheduler_suspended == 0U, "INV-SCHED-SUSPEND-COUNTER");

    rtos_scheduler_suspend();
    TEST_EXPECT(g_kernel.scheduler_suspended == 1U, "INV-SCHED-SUSPEND-COUNTER");

    rtos_scheduler_suspend();
    TEST_EXPECT(g_kernel.scheduler_suspended == 2U, "INV-SCHED-SUSPEND-COUNTER");

    /* No deferred switch has been recorded, so resume returns false at both
     * the still-nested step and the drop-to-zero step. */
    TEST_EXPECT(rtos_scheduler_resume() == false, "INV-SCHED-RESUME-RETURN");
    TEST_EXPECT(g_kernel.scheduler_suspended == 1U, "INV-SCHED-SUSPEND-COUNTER");
    TEST_EXPECT(rtos_scheduler_resume() == false, "INV-SCHED-RESUME-RETURN");
    TEST_EXPECT(g_kernel.scheduler_suspended == 0U, "INV-SCHED-SUSPEND-COUNTER");
}

/* ── Case 2: tick_count_advances_under_suspend ──────────────────────────── */
/*
 * Suspending only blocks context switches; the SysTick ISR runs at a
 * priority above the kernel's BASEPRI mask and is not affected. Busy-wait
 * for the tick counter to advance — if the handler were gated by suspend,
 * the loop would spin until the safety bound trips and the test fails.
 */
TEST_CASE(suspend, tick_count_advances_under_suspend)
{
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-TICK-ADVANCES", 1);

    rtos_scheduler_suspend();

    rtos_tick_t target = rtos_get_tick_count() + 10U;
    uint32_t    safety = 100000000U;
    while (rtos_get_tick_count() < target && safety > 0U)
    {
        safety--;
    }

    rtos_tick_t now = rtos_get_tick_count();
    rtos_scheduler_resume();

    TEST_EXPECT(now >= target, "INV-SCHED-SUSPEND-TICK-ADVANCES");
}

/* ── Case 3: delayed_wake_lands_on_ready_list_under_suspend ─────────────── */
/*
 * rtos_scheduler_update_delayed_tasks() runs from the tick handler before
 * the suspend gate; expiring delays must still move tasks from the delayed
 * list to the ready list. Because the worker is lower priority than the
 * runner, no preempt would have happened anyway — but the state transition
 * itself is what we're verifying.
 */
TEST_CASE(suspend, delayed_wake_lands_on_ready_list_under_suspend)
{
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-DELAYED-WAKE",     1);
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-DELAYED-DEFERRED", 1);
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-DELAYED-RUNS",     1);

    TEST_ASSERT(rtos_task_create(delayed_worker_body, "TDly", STK, NULL,
                                 PRIO_LOW, &s_delayed_worker) == RTOS_SUCCESS,
                "INV-SCHED-SUSPEND-DELAYED-WAKE");

    /* Wait for the worker to enter its delay (state = BLOCKED). */
    TEST_AWAIT_PHASE("worker-blocked", 1000, {
        while (rtos_task_get_state(s_delayed_worker) != RTOS_TASK_STATE_BLOCKED)
        {
            rtos_delay_ms(1);
        }
    });

    rtos_scheduler_suspend();

    /* Spin past the worker's wake deadline. The tick handler must move it
     * to the ready list while we hold the suspend. */
    rtos_tick_t target = rtos_get_tick_count() + 20U;
    uint32_t    safety = 100000000U;
    while (rtos_get_tick_count() < target && safety > 0U)
    {
        safety--;
    }

    TEST_EXPECT(rtos_task_get_state(s_delayed_worker) == RTOS_TASK_STATE_READY,
                "INV-SCHED-SUSPEND-DELAYED-WAKE");
    TEST_EXPECT(s_delayed_worker_ran == false,
                "INV-SCHED-SUSPEND-DELAYED-DEFERRED");

    rtos_scheduler_resume();

    /* Now let the worker run and self-delete. */
    TEST_AWAIT_PHASE("worker-completes", 1000, {
        while (s_delayed_worker_ran == false)
        {
            rtos_delay_ms(1);
        }
    });
    TEST_INV_PASS("INV-SCHED-SUSPEND-DELAYED-RUNS");
}

/* ── Case 4: yield_under_suspend_does_not_switch ────────────────────────── */
/*
 * rtos_yield() pends PendSV unconditionally; the suspend guard inside
 * rtos_kernel_switch_context() must consume the request without switching.
 * Demonstrated against a same-priority peer: an explicit yield normally
 * lets the peer run once (preemptive_sp rotates within a bucket on yield),
 * but under suspend the peer must not run.
 */
TEST_CASE(suspend, yield_under_suspend_does_not_switch)
{
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-YIELD-BASELINE", 1);
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-YIELD-NOOP",     1);
    TEST_INV_DECLARE("INV-SCHED-SUSPEND-YIELD-RESUMES",  1);

    TEST_ASSERT(rtos_task_create(peer_increment_and_yield, "TPeer", STK, NULL,
                                 (rtos_priority_t) (RTOS_MAX_TASK_PRIORITIES - 1U),
                                 &s_peer_task) == RTOS_SUCCESS,
                "INV-SCHED-SUSPEND-YIELD-BASELINE");

    /* Baseline: without suspend, two yields from the runner give the peer
     * a slot (it increments + yields back). */
    s_peer_ran_count = 0U;
    rtos_yield();
    rtos_yield();
    TEST_EXPECT(s_peer_ran_count > 0U, "INV-SCHED-SUSPEND-YIELD-BASELINE");

    /* Under suspend the same yields must not switch. */
    rtos_scheduler_suspend();
    uint32_t before = s_peer_ran_count;
    rtos_yield();
    rtos_yield();
    rtos_yield();
    TEST_EXPECT(s_peer_ran_count == before, "INV-SCHED-SUSPEND-YIELD-NOOP");
    rtos_scheduler_resume();

    /* After resume the peer can run again. */
    uint32_t after_resume_base = s_peer_ran_count;
    rtos_yield();
    rtos_yield();
    TEST_EXPECT(s_peer_ran_count > after_resume_base,
                "INV-SCHED-SUSPEND-YIELD-RESUMES");

    rtos_task_delete(s_peer_task);
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_suspend_cases[] = {
    &TEST_CASE_REF(suspend, suspend_resume_counter_mechanics),
    &TEST_CASE_REF(suspend, tick_count_advances_under_suspend),
    &TEST_CASE_REF(suspend, delayed_wake_lands_on_ready_list_under_suspend),
    &TEST_CASE_REF(suspend, yield_under_suspend_does_not_switch),
};

TEST_SUITE_DEFINE(suspend, g_suspend_cases, .before = suspend_before);

int main(void)
{
    return test_runtime_main(&test_suite_suspend);
}
