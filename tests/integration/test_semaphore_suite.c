/**
 * @file test_semaphore_suite.c
 * @brief Semaphore test suite — 5 focused cases replacing test_semaphore_state.c.
 *
 * Invariant prefix: INV-SEM-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
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

static rtos_semaphore_t g_sem;

static test_signal_t g_case_done;
static test_signal_t g_waiter_ready;

static rtos_task_handle_t g_low_task;
static rtos_task_handle_t g_mid_task;

/* Shared: which waiter acquired first (set by winning task) */
static volatile uint32_t g_acquire_seq;
static volatile uint32_t g_mid_acquire_seq;
static volatile uint32_t g_low_acquire_seq;

#if RTOS_TEST_HOOKS_ENABLED
static test_signal_t            g_give_observed;
static test_signal_t            g_take_observed;
static volatile void           *g_observed_prim;
static volatile rtos_task_handle_t g_observed_caller;
static int                      g_hook_id;
#endif

static void sem_before(void)
{
    /* Binary semaphore, starts at 0. */
    rtos_semaphore_init(&g_sem, 0U, 1U);
    test_signal_init(&g_case_done);
    test_signal_init(&g_waiter_ready);
    g_acquire_seq     = 0U;
    g_mid_acquire_seq = 0U;
    g_low_acquire_seq = 0U;
    g_low_task        = NULL;
    g_mid_task        = NULL;
#if RTOS_TEST_HOOKS_ENABLED
    test_signal_init(&g_give_observed);
    test_signal_init(&g_take_observed);
    g_observed_prim   = NULL;
    g_observed_caller = NULL;
    g_hook_id         = -1;
#endif
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

static void task_sem_wait_and_done(void *_)
{
    (void) _;
    rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

static void task_record_sem(void *arg)
{
    volatile uint32_t *seq_out = (volatile uint32_t *) arg;
    rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);
    *seq_out = ++g_acquire_seq;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* ── Case 1: waiter_blocks_on_zero ───────────────────────────────────────── */
TEST_CASE(sem, waiter_blocks_on_zero)
{
    TEST_INV_DECLARE("INV-SEM-BLOCK", 1);

    TEST_ASSERT(rtos_task_create(task_sem_wait_and_done, "TSW", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-SEM-BLOCK");

    /* Give waiter time to reach semaphore_wait and block. */
    TEST_AWAIT_PHASE("waiter-blocking", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_low_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-SEM-BLOCK");

    /* Unblock and clean up. */
    rtos_semaphore_signal(&g_sem);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: signal_wakes_highest_priority ───────────────────────────────── */
TEST_CASE(sem, signal_wakes_highest_priority)
{
    TEST_INV_DECLARE("INV-SEM-PRIO-ORDER",    1);
    TEST_INV_DECLARE("INV-SEM-COUNT-ZERO",    1);

    /* Create two waiters at different priorities. */
    TEST_ASSERT(rtos_task_create(task_record_sem, "TLo",  STK,
                                 (void *) &g_low_acquire_seq,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-SEM-PRIO-ORDER");
    TEST_ASSERT(rtos_task_create(task_record_sem, "TMid", STK,
                                 (void *) &g_mid_acquire_seq,
                                 PRIO_MID, &g_mid_task) == RTOS_SUCCESS,
                "INV-SEM-PRIO-ORDER");

    /* Give both tasks time to block on the semaphore. */
    TEST_AWAIT_PHASE("waiters-blocking", 100, { rtos_delay_ms(5); });

    /* Signal once: only the highest-priority waiter (Mid) should acquire. */
    rtos_semaphore_signal(&g_sem);

    /* After signal, count should be 0 (went to the waiter, not the counter). */
    TEST_EXPECT(rtos_semaphore_get_count(&g_sem) == 0U, "INV-SEM-COUNT-ZERO");

    /* Wait for Mid to complete. */
    TEST_AWAIT_PHASE("mid-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    /* Signal again to release Low. */
    rtos_semaphore_signal(&g_sem);
    TEST_AWAIT_PHASE("low-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_mid_acquire_seq < g_low_acquire_seq, "INV-SEM-PRIO-ORDER");
}

/* ── Case 3: count_never_exceeds_max ─────────────────────────────────────── */
TEST_CASE(sem, count_never_exceeds_max)
{
    TEST_INV_DECLARE("INV-SEM-MAX-COUNT", 3);

    /* Reset g_sem as counting semaphore with max = 3. */
    rtos_semaphore_init(&g_sem, 0U, 3U);

    for (uint32_t i = 0U; i < 5U; i++)
    {
        rtos_semaphore_signal(&g_sem); /* last 2 calls should overflow-protect */
        TEST_EXPECT(rtos_semaphore_get_count(&g_sem) <= 3U,
                    "INV-SEM-MAX-COUNT");
    }
}

/* ── Case 4: timeout_returns_error ───────────────────────────────────────── */
TEST_CASE(sem, timeout_returns_error)
{
    TEST_INV_DECLARE("INV-SEM-TIMEOUT", 1);

    rtos_sem_status_t ret = rtos_semaphore_wait(&g_sem, 10U);
    TEST_EXPECT(ret == RTOS_SEM_ERR_TIMEOUT, "INV-SEM-TIMEOUT");
}

/* ── Case 5: counting_sem_accumulates ────────────────────────────────────── */
TEST_CASE(sem, counting_sem_accumulates)
{
    TEST_INV_DECLARE("INV-SEM-COUNTING", 3);

    /* Reinit as counting semaphore (max = 255). */
    rtos_semaphore_init(&g_sem, 0U, 255U);

    /* Signal 3 times before any waiter — count should accumulate. */
    rtos_semaphore_signal(&g_sem);
    rtos_semaphore_signal(&g_sem);
    rtos_semaphore_signal(&g_sem);

    /* Each wait should return immediately (count > 0). */
    TEST_EXPECT(rtos_semaphore_wait(&g_sem, RTOS_SEM_NO_WAIT) == RTOS_SEM_OK,
                "INV-SEM-COUNTING");
    TEST_EXPECT(rtos_semaphore_wait(&g_sem, RTOS_SEM_NO_WAIT) == RTOS_SEM_OK,
                "INV-SEM-COUNTING");
    TEST_EXPECT(rtos_semaphore_wait(&g_sem, RTOS_SEM_NO_WAIT) == RTOS_SEM_OK,
                "INV-SEM-COUNTING");
    /* Fourth try must fail (count is now 0). */
    TEST_EXPECT(rtos_semaphore_try_wait(&g_sem) != RTOS_SEM_OK,
                "INV-SEM-COUNTING");
}

/* ── Case 6: signal_to_waiter_fires_give_hook ────────────────────────────── */
/*
 * Hook observability: SEM_GIVE only fires when signal routes to wake a
 * waiter (not when it just bumps the count). Block a worker on the empty
 * sem, then signal from the runner. Verify the hook fired with the
 * runner's TCB as the caller and our sem as the primitive — proves the
 * kernel took the wake-a-waiter path, not the count-increment fast path.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_sem_give(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.prim.primitive == &g_sem)
    {
        g_observed_prim   = ctx->u.prim.primitive;
        g_observed_caller = ctx->u.prim.caller;
        test_signal_post(&g_give_observed);
    }
}
#endif

TEST_CASE(sem, signal_to_waiter_fires_give_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-SEM-GIVE-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-SEM-GIVE-HOOK-PAYLOAD", 2);

    /* Drain stale SEM_GIVE events from prior cases before registering.
     * Same global g_sem pointer, so a stale event would pass our filter. */
    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_SEM_GIVE, on_sem_give,
                                        NULL, &g_hook_id) == 0,
                "INV-SEM-GIVE-HOOK-FIRES");

    TEST_ASSERT(rtos_task_create(task_sem_wait_and_done, "TSW", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-SEM-GIVE-HOOK-FIRES");

    /* Let the worker reach semaphore_wait and block. */
    TEST_AWAIT_PHASE("waiter-blocking", 100, { rtos_delay_ms(5); });

    rtos_task_handle_t runner = rtos_task_get_current();

    /* Signal — kernel must take the wake-a-waiter path and fire SEM_GIVE. */
    rtos_semaphore_signal(&g_sem);

    TEST_AWAIT_PHASE("hook-fired", 500, {
        test_signal_wait(&g_give_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-SEM-GIVE-HOOK-FIRES");
    TEST_EXPECT(g_observed_prim == &g_sem,     "INV-SEM-GIVE-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_caller == runner,   "INV-SEM-GIVE-HOOK-PAYLOAD");

    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-SEM-GIVE-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 7: take_when_available_fires_take_hook ─────────────────────────── */
/*
 * Hook observability: SEM_TAKE only fires on the fast path — when wait()
 * finds count > 0 and decrements without blocking. The block-then-wake
 * path does not fire this event (it fires SEM_GIVE on the signaler side).
 * Runner pre-initialises the sem with count=1 then calls wait(); the hook
 * must fire with this sem and the runner as caller.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_sem_take(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.prim.primitive == &g_sem)
    {
        g_observed_prim   = ctx->u.prim.primitive;
        g_observed_caller = ctx->u.prim.caller;
        test_signal_post(&g_take_observed);
    }
}
#endif

TEST_CASE(sem, take_when_available_fires_take_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-SEM-TAKE-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-SEM-TAKE-HOOK-PAYLOAD", 2);

    /* Binary sem starting at 1: fast path is taken. */
    rtos_semaphore_init(&g_sem, 1U, 1U);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_SEM_TAKE, on_sem_take,
                                        NULL, &g_hook_id) == 0,
                "INV-SEM-TAKE-HOOK-FIRES");

    rtos_task_handle_t runner = rtos_task_get_current();

    TEST_ASSERT(rtos_semaphore_wait(&g_sem, RTOS_SEM_NO_WAIT) == RTOS_SEM_OK,
                "INV-SEM-TAKE-HOOK-FIRES");

    TEST_AWAIT_PHASE("take-hook", 500, {
        test_signal_wait(&g_take_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-SEM-TAKE-HOOK-FIRES");
    TEST_EXPECT(g_observed_prim   == &g_sem, "INV-SEM-TAKE-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_caller == runner, "INV-SEM-TAKE-HOOK-PAYLOAD");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-SEM-TAKE-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case: null_input_asserts ────────────────────────────────────────────── */
/*
 * Semaphore init/wait/signal call RTOS_ASSERT_PARAM(sem != NULL) before the
 * legacy ERR_INVALID return. The framework's KASSERT catcher proves the
 * assert fires; the error-return remains as a release-build fallback.
 */
TEST_CASE(sem, null_input_asserts)
{
#if RTOS_ASSERT_ENABLED
    TEST_INV_DECLARE("INV-SEM-NULL-ASSERT-INIT",   1);
    TEST_INV_DECLARE("INV-SEM-NULL-ASSERT-WAIT",   1);
    TEST_INV_DECLARE("INV-SEM-NULL-ASSERT-SIGNAL", 1);

    TEST_ASSERT_KASSERT_FIRES(rtos_semaphore_init(NULL, 0U, 1U),
                              "INV-SEM-NULL-ASSERT-INIT");
    TEST_ASSERT_KASSERT_FIRES(rtos_semaphore_wait(NULL, RTOS_SEM_NO_WAIT),
                              "INV-SEM-NULL-ASSERT-WAIT");
    TEST_ASSERT_KASSERT_FIRES(rtos_semaphore_signal(NULL),
                              "INV-SEM-NULL-ASSERT-SIGNAL");
#else
    TEST_ASSUME(false, "null_input_asserts requires RTOS_ASSERT_ENABLED");
#endif
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_sem_cases[] = {
    &TEST_CASE_REF(sem, waiter_blocks_on_zero),
    &TEST_CASE_REF(sem, signal_wakes_highest_priority),
    &TEST_CASE_REF(sem, count_never_exceeds_max),
    &TEST_CASE_REF(sem, timeout_returns_error),
    &TEST_CASE_REF(sem, counting_sem_accumulates),
    &TEST_CASE_REF(sem, signal_to_waiter_fires_give_hook),
    &TEST_CASE_REF(sem, take_when_available_fires_take_hook),
    &TEST_CASE_REF(sem, null_input_asserts),
};

TEST_SUITE_DEFINE(sem, g_sem_cases,
                  .before = sem_before);

int main(void)
{
    return test_runtime_main(&test_suite_sem);
}
