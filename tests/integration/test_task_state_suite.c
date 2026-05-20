/**
 * @file test_task_state_suite.c
 * @brief Task-state test suite — 3 cases covering delay, suspend/resume, and deletion.
 *
 * Invariant prefix: INV-TASK-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "task.h"

#if RTOS_TEST_HOOKS_ENABLED
#include "rtos_test_hooks.h"
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_WORKER 2U
#define STK         RTOS_DEFAULT_TASK_STACK_SIZE

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static test_signal_t      g_case_done;
static rtos_task_handle_t g_worker_task;

#if RTOS_TEST_HOOKS_ENABLED
static test_signal_t               g_hook_observed;
static volatile rtos_task_handle_t g_observed_task;
static volatile uint8_t            g_observed_old_state;
static volatile uint8_t            g_observed_new_state;
static volatile uint32_t           g_observed_tick_count;
static volatile uint32_t           g_last_tick_val;
static volatile bool               g_tick_monotonic;
static int                         g_hook_id;
#endif

static void task_state_before(void)
{
    test_signal_init(&g_case_done);
    g_worker_task = NULL;
#if RTOS_TEST_HOOKS_ENABLED
    test_signal_init(&g_hook_observed);
    g_observed_task       = NULL;
    g_observed_old_state  = 0U;
    g_observed_new_state  = 0U;
    g_observed_tick_count = 0U;
    g_last_tick_val       = 0U;
    g_tick_monotonic      = true;
    g_hook_id             = -1;
#endif
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* Delays 50 ms then signals done. */
static void task_delay_and_done(void *_)
{
    (void) _;
    rtos_delay_ms(50U);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Signals done then deletes self. */
static void task_signal_then_delete(void *_)
{
    (void) _;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waits for a semaphore (never posted) — used as a blocking body.
 * Deleted externally via the handle. */
static rtos_semaphore_t g_block_sem;

static void task_block_forever(void *_)
{
    (void) _;
    rtos_semaphore_wait(&g_block_sem, RTOS_SEM_MAX_WAIT);
    rtos_task_delete(NULL);
}

/* ── Case 1: delay_blocks_task ───────────────────────────────────────────── */
TEST_CASE(task_state, delay_blocks_task)
{
    TEST_INV_DECLARE("INV-TASK-DELAY-BLOCKED", 1);

    TEST_ASSERT(rtos_task_create(task_delay_and_done, "TW", STK, NULL,
                                 PRIO_WORKER, &g_worker_task) == RTOS_SUCCESS,
                "INV-TASK-DELAY-BLOCKED");

    /* Give worker time to enter rtos_delay_ms and become BLOCKED. */
    TEST_AWAIT_PHASE("worker-blocking", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-TASK-DELAY-BLOCKED");

    /* Wait for the 50 ms delay to expire and worker to self-complete. */
    TEST_AWAIT_PHASE("worker-done", 1000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: suspend_blocks_execution ────────────────────────────────────── */
TEST_CASE(task_state, suspend_blocks_execution)
{
    TEST_INV_DECLARE("INV-TASK-SUSPENDED",  1);
    TEST_INV_DECLARE("INV-TASK-RESUMED",    1);

    /* Worker blocks on a semaphore so it stays alive long enough to suspend. */
    rtos_semaphore_init(&g_block_sem, 0U, 1U);
    TEST_ASSERT(rtos_task_create(task_block_forever, "TW", STK, NULL,
                                 PRIO_WORKER, &g_worker_task) == RTOS_SUCCESS,
                "INV-TASK-SUSPENDED");

    /* Let worker reach semaphore_wait and block. */
    TEST_AWAIT_PHASE("worker-blocking", 100, { rtos_delay_ms(5); });

    /* Suspend the task. */
    rtos_task_suspend(g_worker_task);
    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_SUSPENDED,
                "INV-TASK-SUSPENDED");

    /* Give it time — while suspended it must remain suspended. */
    TEST_AWAIT_PHASE("still-suspended", 100, { rtos_delay_ms(10); });
    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_SUSPENDED,
                "INV-TASK-SUSPENDED");

    /* Resume — worker returns to BLOCKED (still waiting on sem). */
    rtos_task_resume(g_worker_task);
    TEST_AWAIT_PHASE("resumed", 100, { rtos_delay_ms(2); });
    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-TASK-RESUMED");

    /* Clean up: signal the semaphore so worker can exit. */
    rtos_semaphore_signal(&g_block_sem);
}

/* ── Case 3: delete_transitions_to_deleted ───────────────────────────────── */
TEST_CASE(task_state, delete_transitions_to_deleted)
{
    TEST_INV_DECLARE("INV-TASK-DELETED", 1);

    TEST_ASSERT(rtos_task_create(task_signal_then_delete, "TW", STK, NULL,
                                 PRIO_WORKER, &g_worker_task) == RTOS_SUCCESS,
                "INV-TASK-DELETED");

    /* Worker signals done then deletes itself. */
    TEST_AWAIT_PHASE("worker-done-signal", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    /* Worker may have been preempted by the runner before deleting itself.
     * A brief delay lets the delete complete. */
    TEST_AWAIT_PHASE("delete-settling", 50, { rtos_delay_ms(2); });

    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_DELETED,
                "INV-TASK-DELETED");
}

/* ── Hook callbacks ──────────────────────────────────────────────────────── */
#if RTOS_TEST_HOOKS_ENABLED
/* Records the first BLOCKED→READY transition observed for g_worker_task. */
static void on_task_state(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.task_state.task != g_worker_task)
    {
        return;
    }
    /* We only want one observation — the wake from BLOCKED back to READY. */
    if (ctx->u.task_state.old_state == (uint8_t) RTOS_TASK_STATE_BLOCKED &&
        ctx->u.task_state.new_state == (uint8_t) RTOS_TASK_STATE_READY)
    {
        g_observed_task      = ctx->u.task_state.task;
        g_observed_old_state = ctx->u.task_state.old_state;
        g_observed_new_state = ctx->u.task_state.new_state;
        test_signal_post(&g_hook_observed);
    }
}

/* Counts every TICK hook and checks that tick_val is strictly increasing. */
static void on_tick(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (g_observed_tick_count > 0U && ctx->u.tick_val <= g_last_tick_val)
    {
        g_tick_monotonic = false;
    }
    g_last_tick_val = ctx->u.tick_val;
    g_observed_tick_count++;
}
#endif

/* Worker that waits on a semaphore (driven by the case) and then deletes. */
static rtos_semaphore_t g_state_hook_sem;

static void task_block_on_sem(void *_)
{
    (void) _;
    rtos_semaphore_wait(&g_state_hook_sem, RTOS_SEM_MAX_WAIT);
    rtos_task_delete(NULL);
}

/* ── Case 4: null_input_asserts ──────────────────────────────────────────── */
/*
 * task_create requires a non-NULL function and out-handle; task_resume
 * requires a specific target (no "self" semantic — a SUSPENDED task can't be
 * running). Both call RTOS_ASSERT_PARAM before the legacy ERR_INVALID_PARAM
 * return; the catcher proves each assert fires.
 *
 * Not tested here: rtos_task_suspend/delete with NULL handle — these are
 * documented to mean "current task" and must NOT assert.
 */
TEST_CASE(task_state, null_input_asserts)
{
#if RTOS_ASSERT_ENABLED
    TEST_INV_DECLARE("INV-TASK-NULL-ASSERT-CREATE-FN",  1);
    TEST_INV_DECLARE("INV-TASK-NULL-ASSERT-CREATE-OUT", 1);
    TEST_INV_DECLARE("INV-TASK-NULL-ASSERT-RESUME",     1);

    rtos_task_handle_t dummy_handle = NULL;

    TEST_ASSERT_KASSERT_FIRES(
        rtos_task_create(NULL, "Bad", STK, NULL, PRIO_WORKER, &dummy_handle),
        "INV-TASK-NULL-ASSERT-CREATE-FN");

    TEST_ASSERT_KASSERT_FIRES(
        rtos_task_create(task_signal_then_delete, "Bad", STK, NULL, PRIO_WORKER, NULL),
        "INV-TASK-NULL-ASSERT-CREATE-OUT");

    TEST_ASSERT_KASSERT_FIRES(rtos_task_resume(NULL),
                              "INV-TASK-NULL-ASSERT-RESUME");
#else
    TEST_ASSUME(false, "null_input_asserts requires RTOS_ASSERT_ENABLED");
#endif
}

/* ── Case 5: task_state_hook_fires_on_block_and_wake ─────────────────────── */
/*
 * Verifies RTOS_HOOK_TASK_STATE is delivered for the BLOCKED→READY transition
 * when a worker waiting on a semaphore is signalled. The hook is dispatched
 * by the HookDrain task (P1) after the firing site runs in kernel context, so
 * the runner waits on a signal posted from the callback rather than polling.
 */
TEST_CASE(task_state, task_state_hook_fires_on_wake)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-TASK-STATE-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-TASK-STATE-HOOK-PAYLOAD", 3);

    rtos_semaphore_init(&g_state_hook_sem, 0U, 1U);

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_TASK_STATE, on_task_state,
                                        NULL, &g_hook_id) == 0,
                "INV-TASK-STATE-HOOK-FIRES");

    TEST_ASSERT(rtos_task_create(task_block_on_sem, "TW", STK, NULL,
                                 PRIO_WORKER, &g_worker_task) == RTOS_SUCCESS,
                "INV-TASK-STATE-HOOK-FIRES");

    /* Let worker reach sem_wait and block. */
    TEST_AWAIT_PHASE("worker-blocking", 100, { rtos_delay_ms(5); });

    /* Signal → worker transitions BLOCKED→READY → hook fires → drain dispatches. */
    rtos_semaphore_signal(&g_state_hook_sem);

    TEST_AWAIT_PHASE("hook-delivered", 500, {
        test_signal_wait(&g_hook_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-TASK-STATE-HOOK-FIRES");
    TEST_EXPECT(g_observed_task      == g_worker_task,
                "INV-TASK-STATE-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_old_state == (uint8_t) RTOS_TASK_STATE_BLOCKED,
                "INV-TASK-STATE-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_new_state == (uint8_t) RTOS_TASK_STATE_READY,
                "INV-TASK-STATE-HOOK-PAYLOAD");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-TASK-STATE-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 6: tick_hook_fires_monotonically ───────────────────────────────── */
/*
 * Verifies RTOS_HOOK_TICK fires roughly once per SysTick and that tick_val
 * is strictly increasing across the observed sequence. The runner blocks for
 * a known duration so the drain task can deliver hooks between ticks; the
 * lower bound on the count tolerates drain backlog (ring is 64 entries, P1
 * drain may briefly fall behind under load).
 */
TEST_CASE(task_state, tick_hook_fires_monotonically)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-TICK-HOOK-FIRES",      1);
    TEST_INV_DECLARE("INV-TICK-HOOK-MONOTONIC",  1);

    const uint32_t window_ms = 20U;

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_TICK, on_tick,
                                        NULL, &g_hook_id) == 0,
                "INV-TICK-HOOK-FIRES");

    TEST_AWAIT_PHASE("tick-window", window_ms + 200U, {
        rtos_delay_ms(window_ms);
    });

    /* Give the drain task a brief window to deliver any in-flight ticks. */
    TEST_AWAIT_PHASE("drain-settle", 50, { rtos_delay_ms(5); });

    /* Lower bound is loose to tolerate drain backlog; upper bound catches
     * runaway fire (e.g. tickless wake firing multiple TICK hooks per ms). */
    TEST_EXPECT(g_observed_tick_count >= (window_ms / 2U),
                "INV-TICK-HOOK-FIRES");
    TEST_EXPECT(g_observed_tick_count <= (window_ms * 2U + 10U),
                "INV-TICK-HOOK-FIRES");
    TEST_EXPECT(g_tick_monotonic, "INV-TICK-HOOK-MONOTONIC");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-TICK-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_task_state_cases[] = {
    &TEST_CASE_REF(task_state, delay_blocks_task),
    &TEST_CASE_REF(task_state, suspend_blocks_execution),
    &TEST_CASE_REF(task_state, delete_transitions_to_deleted),
    &TEST_CASE_REF(task_state, null_input_asserts),
    &TEST_CASE_REF(task_state, task_state_hook_fires_on_wake),
    &TEST_CASE_REF(task_state, tick_hook_fires_monotonically),
};

TEST_SUITE_DEFINE(task_state, g_task_state_cases,
                  .before = task_state_before);

int main(void)
{
    return test_runtime_main(&test_suite_task_state);
}
