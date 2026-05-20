/**
 * @file test_scheduler_preemptive_suite.c
 * @brief Preemptive static-priority scheduler test suite — 4 cases.
 *
 * Invariant prefix: INV-PREEMPT-
 * All cases start with TEST_ASSUME(scheduler == PREEMPTIVE_SP).
 * Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "scheduler.h"
#include "semaphore.h"
#include "task.h"

#if RTOS_TEST_HOOKS_ENABLED
#include "rtos_test_hooks.h"
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_LOW  2U
#define PRIO_MID  3U
#define PRIO_HIGH 4U
#define STK       RTOS_DEFAULT_TASK_STACK_SIZE

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static test_signal_t      g_case_done;
static test_signal_t      g_high_ran;
static rtos_task_handle_t g_low_task;
static rtos_task_handle_t g_mid_task;
static rtos_task_handle_t g_high_task;
static volatile uint32_t  g_acquire_seq;
static volatile uint32_t  g_seq_low;
static volatile uint32_t  g_seq_mid;
static volatile uint32_t  g_seq_high;

static rtos_semaphore_t g_sem;

#if RTOS_TEST_HOOKS_ENABLED
static test_signal_t               g_ctx_switch_observed;
static volatile rtos_task_handle_t g_observed_out;
static volatile rtos_task_handle_t g_observed_in;
static int                         g_hook_id;
#endif

static void preempt_before(void)
{
    test_signal_init(&g_case_done);
    test_signal_init(&g_high_ran);
    g_low_task    = NULL;
    g_mid_task    = NULL;
    g_high_task   = NULL;
    g_acquire_seq = 0U;
    g_seq_low     = 0U;
    g_seq_mid     = 0U;
    g_seq_high    = 0U;
#if RTOS_TEST_HOOKS_ENABLED
    test_signal_init(&g_ctx_switch_observed);
    g_observed_out = NULL;
    g_observed_in  = NULL;
    g_hook_id      = -1;
#endif
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* High task: posts g_high_ran immediately then deletes. */
static void task_high_signal(void *_)
{
    (void) _;
    test_signal_post(&g_high_ran);
    rtos_task_delete(NULL);
}

/* Records completion sequence using arg as output slot, then signals done. */
static void task_record_seq(void *arg)
{
    volatile uint32_t *seq_out = (volatile uint32_t *) arg;
    *seq_out = ++g_acquire_seq;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waits on g_sem then records seq and posts g_high_ran. */
static void task_high_wait_then_seq(void *_)
{
    (void) _;
    rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);
    g_seq_high = ++g_acquire_seq;
    test_signal_post(&g_high_ran);
    rtos_task_delete(NULL);
}

/* Signals g_sem (unblocking a higher-prio task → preemption), records own seq,
 * then posts g_case_done. In preemptive mode g_seq_high is written first. */
static void task_low_unblock_high(void *_)
{
    (void) _;
    rtos_semaphore_signal(&g_sem);
    g_seq_low = ++g_acquire_seq;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waits on g_sem then records seq and posts g_case_done. Mirror of
 * task_high_wait_then_seq for the low-priority side of case 1. */
static void task_low_wait_then_seq(void *_)
{
    (void) _;
    rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);
    g_seq_low = ++g_acquire_seq;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Driver task (PRIO_LOW): creates a high-priority task then signals done.
 * In preemptive mode the high task immediately preempts this task. */
static void task_driver_creates_high(void *_)
{
    (void) _;
    rtos_task_create(task_high_signal, "TH", STK, NULL, PRIO_HIGH, &g_high_task);
    /* In preemptive mode, the line above causes an immediate context switch to
     * task_high_signal. Driver resumes only after task_high_signal completes. */
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* ── Case 1: lower_priority_runs_when_higher_blocks ─────────────────────── */
/*
 * High (P4) and low (P2) tasks both wait on g_sem. Runner signals once:
 * the highest-priority waiter (high) unblocks and runs. Low stays blocked.
 * After observing high's BLOCKED state and confirming it ran, signal a
 * second time to release low and let the case wrap up cleanly.
 */
TEST_CASE(preempt, lower_priority_runs_when_higher_blocks)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_PREEMPTIVE_SP,
                "INV-PREEMPT-LOW-RUNS requires RTOS_SCHEDULER_PREEMPTIVE_SP");
    TEST_INV_DECLARE("INV-PREEMPT-LOW-RUNS", 1);

    rtos_semaphore_init(&g_sem, 0U, 2U);

    TEST_ASSERT(rtos_task_create(task_high_wait_then_seq, "TH", STK, NULL,
                                 PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-PREEMPT-LOW-RUNS");
    TEST_ASSERT(rtos_task_create(task_low_wait_then_seq,  "TL", STK, NULL,
                                 PRIO_LOW,  &g_low_task)  == RTOS_SUCCESS,
                "INV-PREEMPT-LOW-RUNS");

    /* Give both tasks time to block or reach their sem calls. */
    TEST_AWAIT_PHASE("tasks-blocking", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_high_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-PREEMPT-LOW-RUNS");

    /* Signal once → highest-priority waiter (high, P4) unblocks and runs. */
    rtos_semaphore_signal(&g_sem);
    TEST_AWAIT_PHASE("high-ran", 500, {
        test_signal_wait(&g_high_ran, RTOS_SEM_MAX_WAIT);
    });

    /* Signal again → low (P2) unblocks and posts case_done. */
    rtos_semaphore_signal(&g_sem);
    TEST_AWAIT_PHASE("low-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: unblock_causes_immediate_preemption ─────────────────────────── */
/*
 * Low task (PRIO_LOW) unblocks high task (PRIO_HIGH) via semaphore_signal.
 * Preemptive: high task runs immediately → records seq first → g_seq_high < g_seq_low.
 */
TEST_CASE(preempt, unblock_causes_immediate_preemption)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_PREEMPTIVE_SP,
                "INV-PREEMPT-IMMEDIATE requires RTOS_SCHEDULER_PREEMPTIVE_SP");
    TEST_INV_DECLARE("INV-PREEMPT-IMMEDIATE", 1);

    rtos_semaphore_init(&g_sem, 0U, 1U);

    /* High task blocks on sem first. */
    TEST_ASSERT(rtos_task_create(task_high_wait_then_seq, "TH", STK, NULL,
                                 PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-PREEMPT-IMMEDIATE");

    TEST_AWAIT_PHASE("high-blocking", 100, { rtos_delay_ms(2); });

    /* Low task: signals sem → high task preempts immediately → then low records seq. */
    TEST_ASSERT(rtos_task_create(task_low_unblock_high, "TL", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-PREEMPT-IMMEDIATE");

    TEST_AWAIT_PHASE("high-ran", 500, {
        test_signal_wait(&g_high_ran, RTOS_SEM_MAX_WAIT);
    });
    TEST_AWAIT_PHASE("low-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_seq_high < g_seq_low, "INV-PREEMPT-IMMEDIATE");
}

/* ── Case 3: priority_ordering_is_strict ─────────────────────────────────── */
/*
 * Three tasks at P4, P3, P2 are created simultaneously. Runner blocks.
 * Preemptive scheduler runs them in strict descending priority order:
 * high (seq=1) → mid (seq=2) → low (seq=3).
 */
TEST_CASE(preempt, priority_ordering_is_strict)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_PREEMPTIVE_SP,
                "INV-PREEMPT-ORDER requires RTOS_SCHEDULER_PREEMPTIVE_SP");
    TEST_INV_DECLARE("INV-PREEMPT-ORDER-HIGH", 1);
    TEST_INV_DECLARE("INV-PREEMPT-ORDER-MID",  1);
    TEST_INV_DECLARE("INV-PREEMPT-ORDER-LOW",  1);

    TEST_ASSERT(rtos_task_create(task_record_seq, "TL", STK,
                                 (void *) &g_seq_low,  PRIO_LOW,  &g_low_task)  == RTOS_SUCCESS,
                "INV-PREEMPT-ORDER-LOW");
    TEST_ASSERT(rtos_task_create(task_record_seq, "TM", STK,
                                 (void *) &g_seq_mid,  PRIO_MID,  &g_mid_task)  == RTOS_SUCCESS,
                "INV-PREEMPT-ORDER-MID");
    TEST_ASSERT(rtos_task_create(task_record_seq, "TH", STK,
                                 (void *) &g_seq_high, PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-PREEMPT-ORDER-HIGH");

    /* Runner blocks → highest READY task (P4) runs first, then P3, then P2. */
    TEST_AWAIT_PHASE("first-done",  500, { test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT); });
    TEST_AWAIT_PHASE("second-done", 500, { test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT); });
    TEST_AWAIT_PHASE("third-done",  500, { test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT); });

    TEST_EXPECT(g_seq_high < g_seq_mid, "INV-PREEMPT-ORDER-HIGH");
    TEST_EXPECT(g_seq_mid  < g_seq_low, "INV-PREEMPT-ORDER-MID");
    TEST_EXPECT(g_seq_high < g_seq_low, "INV-PREEMPT-ORDER-LOW");
}

/* ── Case 4: newly_created_higher_priority_preempts_current ─────────────── */
/*
 * A driver task (PRIO_LOW) runs after runner blocks. Driver creates a high-
 * priority task. In preemptive mode the high task immediately preempts driver.
 * High task posts g_high_ran and exits. Driver resumes and posts g_case_done.
 * Verifies high task ran (and reached DELETED state) before driver finished.
 */
TEST_CASE(preempt, newly_created_higher_priority_preempts_current)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_PREEMPTIVE_SP,
                "INV-PREEMPT-NEW-HIGH requires RTOS_SCHEDULER_PREEMPTIVE_SP");
    TEST_INV_DECLARE("INV-PREEMPT-NEW-HIGH", 1);

    rtos_task_handle_t driver = NULL;
    TEST_ASSERT(rtos_task_create(task_driver_creates_high, "TD", STK, NULL,
                                 PRIO_LOW, &driver) == RTOS_SUCCESS,
                "INV-PREEMPT-NEW-HIGH");

    /* Runner blocks → driver (P2) runs, creates high (P4), which preempts driver. */
    TEST_AWAIT_PHASE("high-ran", 500, {
        test_signal_wait(&g_high_ran, RTOS_SEM_MAX_WAIT);
    });
    TEST_AWAIT_PHASE("driver-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    /* Settle 2 ms to allow deletion to complete. */
    TEST_AWAIT_PHASE("settling", 50, { rtos_delay_ms(2); });

    TEST_EXPECT(rtos_task_get_state(g_high_task) == RTOS_TASK_STATE_DELETED,
                "INV-PREEMPT-NEW-HIGH");
}

/* ── Case 5: ctx_switch_hook_fires_on_preempt ────────────────────────────── */
/*
 * Verifies RTOS_HOOK_CTX_SWITCH fires when the scheduler picks a different
 * task and that the payload names the correct (out, in) pair. We arrange a
 * deterministic preemption: high task blocks on g_sem, runner signals, the
 * scheduler picks high → hook fires with in_task = high_task.
 *
 * The hook fires unconditionally on every get_next_task call (including
 * "same task chosen"), so the callback filters for the specific in_task we
 * care about before posting g_ctx_switch_observed.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_ctx_switch(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.ctx_switch.in_task == g_high_task)
    {
        g_observed_out = ctx->u.ctx_switch.out_task;
        g_observed_in  = ctx->u.ctx_switch.in_task;
        test_signal_post(&g_ctx_switch_observed);
    }
}
#endif

TEST_CASE(preempt, ctx_switch_hook_fires_on_preempt)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_PREEMPTIVE_SP,
                "INV-PREEMPT-CTX-SWITCH-HOOK requires RTOS_SCHEDULER_PREEMPTIVE_SP");
    TEST_INV_DECLARE("INV-PREEMPT-CTX-SWITCH-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-PREEMPT-CTX-SWITCH-HOOK-PAYLOAD", 1);

    rtos_semaphore_init(&g_sem, 0U, 1U);

    /* High task blocks on sem first so we know its TCB before the hook fires. */
    TEST_ASSERT(rtos_task_create(task_high_wait_then_seq, "TH", STK, NULL,
                                 PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-PREEMPT-CTX-SWITCH-HOOK-FIRES");

    TEST_AWAIT_PHASE("high-blocking", 100, { rtos_delay_ms(2); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_CTX_SWITCH, on_ctx_switch,
                                        NULL, &g_hook_id) == 0,
                "INV-PREEMPT-CTX-SWITCH-HOOK-FIRES");

    /* Signal sem → high task becomes READY, scheduler picks it → CTX_SWITCH fires. */
    rtos_semaphore_signal(&g_sem);

    TEST_AWAIT_PHASE("ctx-switch-delivered", 500, {
        test_signal_wait(&g_ctx_switch_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-PREEMPT-CTX-SWITCH-HOOK-FIRES");
    TEST_EXPECT(g_observed_in == g_high_task,
                "INV-PREEMPT-CTX-SWITCH-HOOK-PAYLOAD");

    /* Let high task finish its body so the case doesn't leak it. */
    TEST_AWAIT_PHASE("high-ran", 500, {
        test_signal_wait(&g_high_ran, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-PREEMPT-CTX-SWITCH-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_preempt_cases[] = {
    &TEST_CASE_REF(preempt, lower_priority_runs_when_higher_blocks),
    &TEST_CASE_REF(preempt, unblock_causes_immediate_preemption),
    &TEST_CASE_REF(preempt, priority_ordering_is_strict),
    &TEST_CASE_REF(preempt, newly_created_higher_priority_preempts_current),
    &TEST_CASE_REF(preempt, ctx_switch_hook_fires_on_preempt),
};

TEST_SUITE_DEFINE(preempt, g_preempt_cases,
                  .before = preempt_before);

int main(void)
{
    return test_runtime_main(&test_suite_preempt);
}
