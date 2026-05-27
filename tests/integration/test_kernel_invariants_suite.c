/**
 * @file test_kernel_invariants_suite.c
 * @brief Kernel-level invariants under deterministic adversarial conditions.
 *
 * Invariant prefix: INV-KERN-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 *
 * These are regression tests for three audit findings, all now fixed. Each
 * case reproduced its bug as a hard FAIL before the fix and PASSes after.
 *
 * Cases:
 *   1. timed_wait_wake_during_block_gap_sem
 *      The sync-primitive block path used to be non-atomic:
 *        add_to_wait_list → exit_critical → rtos_kernel_task_block
 *      Between exit_critical and the BLOCKED transition the task was on the
 *      wait list with blocked_on set but state still RUNNING. A signal in
 *      that window popped the waiter and called rtos_kernel_task_unblock,
 *      which no-ops on a non-BLOCKED task, so the wake was lost and the
 *      waiter slept the full timeout. Fixed by rtos_kernel_task_block_locked,
 *      which performs the BLOCKED transition inside the SAME critical section
 *      as the wait-list insert. The pre-block hook now fires at the realistic
 *      post-commit point (state already BLOCKED) and the wake takes effect.
 *   2. timed_wait_wake_during_block_gap_notify — same path via notify_take.
 *   3. suspend_resume_preserves_timeout
 *      rtos_task_suspend pulls a BLOCKED-with-timeout task off the delayed
 *      list; resume now re-arms it (delay_is_timeout + preserved delay_until)
 *      instead of dropping the timeout.
 *
 * Hook usage: each waiter arms g_kernel_test_pre_block_cb inside its OWN
 * task body, immediately before the blocking API under test, so the hook
 * fires for the waiter's block — not the runner's. The hook is one-shot;
 * it auto-clears on fire and cannot leak into a later block call.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "kernel_priv.h"
#include "semaphore.h"
#include "task.h"
#include "task_priv.h"

#include <stdbool.h>
#include <stdint.h>

#define PRIO_LOW  2U
#define PRIO_MID  3U
#define STK       RTOS_DEFAULT_TASK_STACK_SIZE

static rtos_semaphore_t   g_sem;
static rtos_task_handle_t g_waiter;
static volatile uint32_t  g_wait_start_tick;
static volatile uint32_t  g_wait_end_tick;
static volatile int       g_wait_status;
static test_signal_t      g_case_done;

static void kern_before(void)
{
    rtos_semaphore_init(&g_sem, 0U, 1U);
    test_signal_init(&g_case_done);
    g_waiter           = NULL;
    g_wait_start_tick  = 0U;
    g_wait_end_tick    = 0U;
    g_wait_status      = -1;

    while (g_kernel.scheduler_suspended > 0U)
    {
        rtos_scheduler_resume();
    }
    g_kernel.yield_pending = false;

    g_kernel_test_pre_block_cb   = NULL;
    g_kernel_test_pre_block_user = NULL;
}

/* ── Hook callbacks ─────────────────────────────────────────────────────── */

/* Producer-side wake for the semaphore race. Runs in the waiter's context,
 * synchronously, between the waiter's wait-list insert and the kernel's
 * state→BLOCKED transition. */
static void hook_signal_sem(rtos_task_handle_t task, rtos_tick_t delay, void *user)
{
    (void) task;
    (void) delay;
    rtos_semaphore_signal((rtos_semaphore_t *) user);
}

static void hook_notify_task(rtos_task_handle_t task, rtos_tick_t delay, void *user)
{
    (void) delay;
    (void) user;
    rtos_task_notify(task, 0U, RTOS_NOTIFY_ACTION_INCREMENT);
}

/* ───────────────────────────────────────────────────────────────────────── */
/* Case 1                                                                    */
/* ───────────────────────────────────────────────────────────────────────── */

static void waiter_sem_body(void *arg)
{
    (void) arg;
    /* Arm the hook inside the waiter so it fires for THIS task's call to
     * rtos_kernel_task_block, not for the runner's earlier blocks. */
    g_kernel_test_pre_block_user = &g_sem;
    g_kernel_test_pre_block_cb   = hook_signal_sem;

    g_wait_start_tick = rtos_get_tick_count();
    g_wait_status     = (int) rtos_semaphore_wait(&g_sem, 200U /* ticks */);
    g_wait_end_tick   = rtos_get_tick_count();

    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

TEST_CASE(kern, timed_wait_wake_during_block_gap_sem)
{
    TEST_INV_DECLARE("INV-KERN-TIMED-WAIT-SEM-OK",     1);
    TEST_INV_DECLARE("INV-KERN-TIMED-WAIT-SEM-PROMPT", 1);

    TEST_ASSERT(rtos_task_create(waiter_sem_body, "WaitSem", STK, NULL,
                                 PRIO_LOW, &g_waiter) == RTOS_SUCCESS,
                "INV-KERN-TIMED-WAIT-SEM-OK");

    TEST_AWAIT_PHASE("sem-race", 1000, {
        test_signal_wait(&g_case_done, 1000U);
    });

    /* RTOS_SEM_OK == 0. The wait MUST report success — the signal happened
     * before the timeout. */
    TEST_EXPECT(g_wait_status == 0, "INV-KERN-TIMED-WAIT-SEM-OK");

    uint32_t elapsed = g_wait_end_tick - g_wait_start_tick;
    /* Prompt wake = elapsed must be tiny. If the race silently dropped the
     * wake, elapsed will be ~200 ticks. */
    TEST_EXPECT(elapsed < 30U, "INV-KERN-TIMED-WAIT-SEM-PROMPT");
}

/* ───────────────────────────────────────────────────────────────────────── */
/* Case 2                                                                    */
/* ───────────────────────────────────────────────────────────────────────── */

static void waiter_notify_body(void *arg)
{
    (void) arg;
    g_kernel_test_pre_block_user = NULL;
    g_kernel_test_pre_block_cb   = hook_notify_task;

    g_wait_start_tick = rtos_get_tick_count();
    g_wait_status     = (int) rtos_task_notify_take(true, 200U /* ticks */);
    g_wait_end_tick   = rtos_get_tick_count();

    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

TEST_CASE(kern, timed_wait_wake_during_block_gap_notify)
{
    TEST_INV_DECLARE("INV-KERN-TIMED-WAIT-NOTIFY-OK",     1);
    TEST_INV_DECLARE("INV-KERN-TIMED-WAIT-NOTIFY-PROMPT", 1);

    TEST_ASSERT(rtos_task_create(waiter_notify_body, "WaitNtfy", STK, NULL,
                                 PRIO_LOW, &g_waiter) == RTOS_SUCCESS,
                "INV-KERN-TIMED-WAIT-NOTIFY-OK");

    TEST_AWAIT_PHASE("notify-race", 1000, {
        test_signal_wait(&g_case_done, 1000U);
    });

    /* RTOS_NOTIFY_OK == 0. */
    TEST_EXPECT(g_wait_status == 0, "INV-KERN-TIMED-WAIT-NOTIFY-OK");

    uint32_t elapsed = g_wait_end_tick - g_wait_start_tick;
    TEST_EXPECT(elapsed < 30U, "INV-KERN-TIMED-WAIT-NOTIFY-PROMPT");
}

/* ───────────────────────────────────────────────────────────────────────── */
/* Case 3: suspend_resume_preserves_timeout                                  */
/* ───────────────────────────────────────────────────────────────────────── */

static void waiter_no_signal_body(void *arg)
{
    (void) arg;
    g_wait_start_tick = rtos_get_tick_count();
    g_wait_status     = (int) rtos_semaphore_wait(&g_sem, 100U /* ticks */);
    g_wait_end_tick   = rtos_get_tick_count();
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

TEST_CASE(kern, suspend_resume_preserves_timeout)
{
    TEST_INV_DECLARE("INV-KERN-SUSPEND-TIMEOUT-TIMEOUT",  1);
    TEST_INV_DECLARE("INV-KERN-SUSPEND-TIMEOUT-DEADLINE", 1);

    TEST_ASSERT(rtos_task_create(waiter_no_signal_body, "WaitNoSig", STK, NULL,
                                 PRIO_LOW, &g_waiter) == RTOS_SUCCESS,
                "INV-KERN-SUSPEND-TIMEOUT-TIMEOUT");

    /* Let the waiter reach the BLOCKED + delayed-list state. */
    TEST_AWAIT_PHASE("waiter-blocking", 50, {
        while (rtos_task_get_state(g_waiter) != RTOS_TASK_STATE_BLOCKED &&
               !g_test_aborted)
        {
            rtos_delay_ticks(1U);
        }
    });

    /* Suspend then immediately resume — the delayed-list entry was removed
     * by suspend; resume must restore it or the timeout is lost forever. */
    rtos_task_suspend(g_waiter);
    rtos_task_resume(g_waiter);

    /* Budget = 400 ticks; the 100-tick timeout must fire within this. If
     * the timer was dropped, the case-done signal never posts and the
     * watchdog records INV-WATCHDOG:await-timeout. */
    TEST_AWAIT_PHASE("await-timeout", 400, {
        test_signal_wait(&g_case_done, 400U);
    });

    TEST_EXPECT(g_wait_status != 0, "INV-KERN-SUSPEND-TIMEOUT-TIMEOUT");

    uint32_t elapsed = g_wait_end_tick - g_wait_start_tick;
    TEST_EXPECT(elapsed >= 80U && elapsed <= 200U,
                "INV-KERN-SUSPEND-TIMEOUT-DEADLINE");
}

/* Finding #4 (unbalanced rtos_scheduler_resume() wrapping scheduler_suspended
 * to 255) is hardened in the kernel with a clamp, but is NOT exercised here:
 * in an asserts-enabled build the guard RTOS_ASSERT fires from inside the
 * resume's critical section, and the framework's KASSERT catcher cannot
 * unwind that safely (it restores PRIMASK but not BASEPRI/nesting), so the
 * case wedges. The clamp is the fallback for asserts-disabled builds. */

/* ── Suite descriptor ────────────────────────────────────────────────────── */

/* Order matters: suspend_resume_preserves_timeout leaves a permanently
 * blocked waiter (its lost-timeout bug is the whole point), so it runs
 * LAST so its hang cannot starve preceding case verdicts. */
static const test_case_t *const kern_cases[] = {
    &TEST_CASE_REF(kern, timed_wait_wake_during_block_gap_sem),
    &TEST_CASE_REF(kern, timed_wait_wake_during_block_gap_notify),
    &TEST_CASE_REF(kern, suspend_resume_preserves_timeout),
};

TEST_SUITE_DEFINE(kern, kern_cases,
    .before = kern_before);

int main(void)
{
    extern const test_suite_t test_suite_kern;
    return test_runtime_main(&test_suite_kern);
}
