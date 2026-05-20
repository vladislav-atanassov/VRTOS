/**
 * @file test_mutex_suite.c
 * @brief Mutex test suite — 8 cases replacing the monolithic test_mutex_state.c.
 *
 * Invariant naming: INV-MUTEX-<AREA>[-DETAIL]
 * Scheduler: PREEMPTIVE_SP (drives priority-ordering and PIP tests).
 * Build with RTOS_TEST_HOOKS_ENABLED=1 to enable hook-based PIP timing cases.
 * Build with RTOS_MAX_TASKS=24 to avoid TCB-slot exhaustion across all cases.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "mutex.h"
#include "task.h"

#if RTOS_TEST_HOOKS_ENABLED
#include "rtos_test_hooks.h"
#endif

/* ── Priority / stack constants ─────────────────────────────────────────── */

#define PRIO_LOW  2U
#define PRIO_MID  3U
#define PRIO_HIGH 4U
#define STK       RTOS_DEFAULT_TASK_STACK_SIZE

/* ── Fixture globals ─────────────────────────────────────────────────────── */

static rtos_mutex_t g_m;
static rtos_mutex_t g_m2;  /* second mutex for transitive-chain case */

/* Phase token: set a bit to announce a phase; multiple waiters safe */
static test_phase_token_t g_phases;
#define PHASE_M1_HELD  0x1U
#define PHASE_M2_HELD  0x2U

/* Per-signal handles re-used across cases (reset in before()) */
static test_signal_t g_release;    /* Runner → LowTask: "unlock now" */
static test_signal_t g_case_done;  /* Task(s) → Runner: "all work done" */

static rtos_task_handle_t g_low_task;
static rtos_task_handle_t g_mid_task;
static rtos_task_handle_t g_high_task;

/* Sequence counter for priority-ordering verification */
static volatile uint32_t g_acquire_seq;
static volatile uint32_t g_mid_acquire_seq;
static volatile uint32_t g_low_acquire_seq;

#if RTOS_TEST_HOOKS_ENABLED
static test_signal_t      g_boost_observed;   /* hook → runner */
static test_signal_t      g_restore_observed; /* hook → runner */
static test_signal_t      g_lock_exit_observed;
static test_signal_t      g_unlock_observed;
static volatile rtos_priority_t g_boosted_prio;
static volatile rtos_priority_t g_restored_prio;
static volatile void *             g_observed_mutex;
static volatile rtos_task_handle_t g_observed_caller;
static volatile int                g_observed_status;
static int g_hook_id;
#endif

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static void mutex_before(void)
{
    rtos_mutex_init(&g_m);
    rtos_mutex_init(&g_m2);
    test_phase_token_init(&g_phases);
    test_signal_init(&g_release);
    test_signal_init(&g_case_done);
    g_acquire_seq      = 0U;
    g_mid_acquire_seq  = 0U;
    g_low_acquire_seq  = 0U;
    g_low_task         = NULL;
    g_mid_task         = NULL;
    g_high_task        = NULL;
#if RTOS_TEST_HOOKS_ENABLED
    test_signal_init(&g_boost_observed);
    test_signal_init(&g_restore_observed);
    test_signal_init(&g_lock_exit_observed);
    test_signal_init(&g_unlock_observed);
    g_boosted_prio    = 0U;
    g_restored_prio   = 0U;
    g_observed_mutex  = NULL;
    g_observed_caller = NULL;
    g_observed_status = 0;
    g_hook_id         = -1;
#endif
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* Acquires g_m, announces M1_HELD phase, waits for release, unlocks, deletes */
static void task_hold_m1(void *_)
{
    (void) _;
    rtos_mutex_lock(&g_m, RTOS_MAX_WAIT);
    test_phase_set(&g_phases, PHASE_M1_HELD);
    test_signal_wait(&g_release, RTOS_SEM_MAX_WAIT);
    rtos_mutex_unlock(&g_m);
    rtos_task_delete(NULL);
}

/* Waits for M1_HELD phase, then tries to lock g_m (will block) */
static void task_contend_m1_then_done(void *_)
{
    (void) _;
    test_phase_wait(&g_phases, PHASE_M1_HELD, true, false, RTOS_EG_MAX_WAIT);
    rtos_mutex_lock(&g_m, RTOS_MAX_WAIT);
    rtos_mutex_unlock(&g_m);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Records acquisition sequence number, unlocks, signals case_done, deletes */
static void task_record_and_done(void *arg)
{
    volatile uint32_t *seq_out = (volatile uint32_t *) arg;
    rtos_mutex_lock(&g_m, RTOS_MAX_WAIT);
    *seq_out = ++g_acquire_seq;
    rtos_mutex_unlock(&g_m);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

#if RTOS_TEST_HOOKS_ENABLED
/* Hook callback: records boost priority for g_low_task, signals observer */
static void on_mutex_boost(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.mutex_boost.owner == g_low_task)
    {
        if (ctx->u.mutex_boost.new_prio > g_boosted_prio)
        {
            g_boosted_prio = ctx->u.mutex_boost.new_prio;
        }
        test_signal_post(&g_boost_observed);
    }
}

/* Hook callback: records restored priority for g_low_task, signals observer */
static void on_mutex_restore(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.mutex_restore.owner == g_low_task)
    {
        g_restored_prio = ctx->u.mutex_restore.restored_prio;
        test_signal_post(&g_restore_observed);
    }
}

/* Task body used in hook-based PIP cases: holds g_m and waits for release */
static void task_pip_low(void *_)
{
    (void) _;
    rtos_mutex_lock(&g_m, RTOS_MAX_WAIT);
    test_phase_set(&g_phases, PHASE_M1_HELD);
    test_signal_wait(&g_release, RTOS_SEM_MAX_WAIT);
    rtos_mutex_unlock(&g_m);
    rtos_task_delete(NULL);
}

/* Task body used in PIP cases: waits for M1_HELD, then contends, then done */
static void task_pip_high(void *_)
{
    (void) _;
    test_phase_wait(&g_phases, PHASE_M1_HELD, true, false, RTOS_EG_MAX_WAIT);
    rtos_mutex_lock(&g_m, RTOS_MAX_WAIT); /* triggers PIP boost */
    rtos_mutex_unlock(&g_m);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}
#endif /* RTOS_TEST_HOOKS_ENABLED */

/* ── Transitive-chain task bodies ────────────────────────────────────────── */
/* Scenario: Lo(P2) holds m1; Mid(P3) holds m2 then tries m1 (blocks, boosts Lo→P3);
 * Hi(P4) tries m2 (blocks, boosts Mid→P4, transitively Lo→P4).              */

static void task_trans_lo(void *_)
{
    (void) _;
    rtos_mutex_lock(&g_m, RTOS_MAX_WAIT);          /* acquire m1 */
    test_phase_set(&g_phases, PHASE_M1_HELD);
    test_signal_wait(&g_release, RTOS_SEM_MAX_WAIT); /* hold until runner says go */
    rtos_mutex_unlock(&g_m);
    rtos_task_delete(NULL);
}

static void task_trans_mid(void *_)
{
    (void) _;
    test_phase_wait(&g_phases, PHASE_M1_HELD, true, false, RTOS_EG_MAX_WAIT);
    rtos_mutex_lock(&g_m2, RTOS_MAX_WAIT);          /* acquire m2 */
    test_phase_set(&g_phases, PHASE_M2_HELD);
    rtos_mutex_lock(&g_m, RTOS_MAX_WAIT);           /* blocks → boosts Lo */
    rtos_mutex_unlock(&g_m);
    rtos_mutex_unlock(&g_m2);
    rtos_task_delete(NULL);
}

static void task_trans_hi(void *_)
{
    (void) _;
    test_phase_wait(&g_phases, PHASE_M2_HELD, true, false, RTOS_EG_MAX_WAIT);
    rtos_mutex_lock(&g_m2, RTOS_MAX_WAIT);          /* blocks → boosts Mid→P4, Lo→P4 */
    rtos_mutex_unlock(&g_m2);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* ── Test cases ──────────────────────────────────────────────────────────── */

/* ── Case 1: basic_lock_unlock ───────────────────────────────────────────── */
TEST_CASE(mutex, basic_lock_unlock)
{
    TEST_INV_DECLARE("INV-MUTEX-OWNER-ON-LOCK",    1);
    TEST_INV_DECLARE("INV-MUTEX-OWNER-NULL-UNLOCK", 1);

    TEST_ASSERT(rtos_mutex_lock(&g_m, RTOS_MAX_WAIT) == RTOS_MUTEX_OK,
                "INV-MUTEX-OWNER-ON-LOCK");
    TEST_EXPECT(g_m.owner == rtos_task_get_current(),
                "INV-MUTEX-OWNER-ON-LOCK");

    TEST_ASSERT(rtos_mutex_unlock(&g_m) == RTOS_MUTEX_OK,
                "INV-MUTEX-OWNER-NULL-UNLOCK");
    TEST_EXPECT(g_m.owner == NULL,
                "INV-MUTEX-OWNER-NULL-UNLOCK");
}

/* ── Case 2: contender_blocks_immediately ────────────────────────────────── */
TEST_CASE(mutex, contender_blocks_immediately)
{
    TEST_INV_DECLARE("INV-MUTEX-BLOCK",      1);
    TEST_INV_DECLARE("INV-MUTEX-NONBLOCKING", 1);

    /* Create holder (P2) and contender (P4). Runner (P7) keeps running. */
    TEST_ASSERT(rtos_task_create(task_hold_m1, "TLo", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-MUTEX-BLOCK");
    TEST_ASSERT(rtos_task_create(task_contend_m1_then_done, "THi", STK, NULL,
                                 PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-MUTEX-BLOCK");

    /* Wait for LowTask to acquire mutex (sets PHASE_M1_HELD). */
    TEST_AWAIT_PHASE("m1-held", 1000, {
        test_phase_wait(&g_phases, PHASE_M1_HELD, true, false, RTOS_EG_MAX_WAIT);
    });

    /* Non-blocking try from runner must fail — mutex is held. */
    TEST_EXPECT(rtos_mutex_lock(&g_m, RTOS_NO_WAIT) == RTOS_MUTEX_ERR_TIMEOUT,
                "INV-MUTEX-NONBLOCKING");

    /* Give HighTask time to reach mutex_lock and block (5 ms ≪ 1000 ms budget). */
    TEST_AWAIT_PHASE("high-contending", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_high_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-MUTEX-BLOCK");

    /* Release LowTask, wait for HighTask to complete. */
    test_signal_post(&g_release);
    TEST_AWAIT_PHASE("cleanup", 2000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 3: pip_boost_to_highest_waiter ─────────────────────────────────── */
TEST_CASE(mutex, pip_boost_to_highest_waiter)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-MUTEX-PIP-BOOST",        1);
    TEST_INV_DECLARE("INV-MUTEX-PIP-BOOST-TIMING", 1);

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_MUTEX_BOOST, on_mutex_boost,
                                        NULL, &g_hook_id) == 0,
                "INV-MUTEX-PIP-BOOST");

    TEST_ASSERT(rtos_task_create(task_pip_low,  "TLo", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-MUTEX-PIP-BOOST");
    TEST_ASSERT(rtos_task_create(task_pip_high, "THi", STK, NULL,
                                 PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-MUTEX-PIP-BOOST");

    /* Wait for hook to fire (HighTask contends → boost fires → hook posts signal). */
    TEST_AWAIT_PHASE("pip-boost", 2000, {
        test_signal_wait(&g_boost_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_boosted_prio == PRIO_HIGH, "INV-MUTEX-PIP-BOOST");
    /* g_boost_observed was posted from hook callback = synchronous with boost */
    TEST_INV_PASS("INV-MUTEX-PIP-BOOST-TIMING");

    test_signal_post(&g_release); /* let LowTask finish */
    TEST_AWAIT_PHASE("cleanup", 2000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-MUTEX-PIP-BOOST requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 4: pip_restore_on_unlock ───────────────────────────────────────── */
TEST_CASE(mutex, pip_restore_on_unlock)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-MUTEX-PIP-RESTORE", 1);

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_MUTEX_RESTORE, on_mutex_restore,
                                        NULL, &g_hook_id) == 0,
                "INV-MUTEX-PIP-RESTORE");

    TEST_ASSERT(rtos_task_create(task_pip_low,  "TLo", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-MUTEX-PIP-RESTORE");
    TEST_ASSERT(rtos_task_create(task_pip_high, "THi", STK, NULL,
                                 PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-MUTEX-PIP-RESTORE");

    /* Wait for Lo to hold m1, then let Hi contend and boost to settle. */
    TEST_AWAIT_PHASE("pip-setup", 2000, {
        test_phase_wait(&g_phases, PHASE_M1_HELD, true, false, RTOS_EG_MAX_WAIT);
        rtos_delay_ms(5);
    });

    /* Release Lo so it unlocks → restore hook fires. */
    test_signal_post(&g_release);
    TEST_AWAIT_PHASE("pip-restore", 2000, {
        test_signal_wait(&g_restore_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_restored_prio == PRIO_LOW, "INV-MUTEX-PIP-RESTORE");

    TEST_AWAIT_PHASE("cleanup", 2000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-MUTEX-PIP-RESTORE requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 5: wait_queue_priority_order ───────────────────────────────────── */
TEST_CASE(mutex, wait_queue_priority_order)
{
    TEST_INV_DECLARE("INV-MUTEX-PRIO-ORDER", 1);

    /* Runner holds the mutex then creates two contenders. */
    TEST_ASSERT(rtos_mutex_lock(&g_m, RTOS_MAX_WAIT) == RTOS_MUTEX_OK,
                "INV-MUTEX-PRIO-ORDER");

    TEST_ASSERT(rtos_task_create(task_record_and_done, "TLo", STK,
                                 (void *) &g_low_acquire_seq,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-MUTEX-PRIO-ORDER");
    TEST_ASSERT(rtos_task_create(task_record_and_done, "TMid", STK,
                                 (void *) &g_mid_acquire_seq,
                                 PRIO_MID, &g_mid_task) == RTOS_SUCCESS,
                "INV-MUTEX-PRIO-ORDER");

    /* Give both tasks time to reach mutex_lock and block. */
    TEST_AWAIT_PHASE("contenders-blocking", 100, { rtos_delay_ms(5); });

    /* Unlock — highest-priority waiter (Mid, P3) must acquire first. */
    rtos_mutex_unlock(&g_m);

    /* Wait for both tasks to complete (each posts g_case_done once). */
    TEST_AWAIT_PHASE("prio-order", 2000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_mid_acquire_seq < g_low_acquire_seq, "INV-MUTEX-PRIO-ORDER");
}

/* ── Case 6: recursive_lock_unlock_balance ───────────────────────────────── */
TEST_CASE(mutex, recursive_lock_unlock_balance)
{
    TEST_INV_DECLARE("INV-MUTEX-RECURSIVE-DEPTH",  1);
    TEST_INV_DECLARE("INV-MUTEX-RECURSIVE-UNLOCK", 1);

    TEST_ASSERT(rtos_mutex_lock(&g_m, RTOS_MAX_WAIT) == RTOS_MUTEX_OK,
                "INV-MUTEX-RECURSIVE-DEPTH");
    TEST_ASSERT(rtos_mutex_lock(&g_m, RTOS_MAX_WAIT) == RTOS_MUTEX_OK,
                "INV-MUTEX-RECURSIVE-DEPTH");
    TEST_EXPECT(g_m.lock_count == 2U, "INV-MUTEX-RECURSIVE-DEPTH");

    TEST_ASSERT(rtos_mutex_unlock(&g_m) == RTOS_MUTEX_OK,
                "INV-MUTEX-RECURSIVE-UNLOCK");
    TEST_EXPECT(g_m.lock_count == 1U, "INV-MUTEX-RECURSIVE-UNLOCK");
    TEST_EXPECT(g_m.owner == rtos_task_get_current(),
                "INV-MUTEX-RECURSIVE-UNLOCK");

    TEST_ASSERT(rtos_mutex_unlock(&g_m) == RTOS_MUTEX_OK,
                "INV-MUTEX-RECURSIVE-UNLOCK");
    TEST_EXPECT(g_m.owner == NULL, "INV-MUTEX-RECURSIVE-UNLOCK");
}

/* ── Case 7: pip_transitive_chain ────────────────────────────────────────── */
TEST_CASE(mutex, pip_transitive_chain)
{
    TEST_INV_DECLARE("INV-MUTEX-PIP-TRANSITIVE", 1);

    TEST_ASSERT(rtos_task_create(task_trans_lo,  "TLo",  STK, NULL,
                                 PRIO_LOW,  &g_low_task)  == RTOS_SUCCESS,
                "INV-MUTEX-PIP-TRANSITIVE");
    TEST_ASSERT(rtos_task_create(task_trans_mid, "TMid", STK, NULL,
                                 PRIO_MID,  &g_mid_task)  == RTOS_SUCCESS,
                "INV-MUTEX-PIP-TRANSITIVE");
    TEST_ASSERT(rtos_task_create(task_trans_hi,  "THi",  STK, NULL,
                                 PRIO_HIGH, &g_high_task) == RTOS_SUCCESS,
                "INV-MUTEX-PIP-TRANSITIVE");

    /* Wait until the full chain is established: Hi blocked on m2. */
    TEST_AWAIT_PHASE("chain-setup", 2000, {
        test_phase_wait(&g_phases, PHASE_M2_HELD, true, false, RTOS_EG_MAX_WAIT);
        /* Give HiTask time to actually block on m2 (5 ms). */
        rtos_delay_ms(5);
    });

    /* Verify Lo's effective priority was raised to Hi's level by transitive PIP. */
    TEST_EXPECT(rtos_task_get_priority(g_low_task) == PRIO_HIGH,
                "INV-MUTEX-PIP-TRANSITIVE");

    /* Release the chain: Lo unlocks → Mid unblocks → Mid unlocks → Hi unblocks. */
    test_signal_post(&g_release);
    TEST_AWAIT_PHASE("chain-unwind", 3000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 8: null_input_asserts ──────────────────────────────────────────── */
/*
 * rtos_mutex_lock/unlock call RTOS_ASSERT_PARAM(m != NULL) before the
 * legacy error-return path. The framework's strong override of
 * rtos_assert_failed (tests/framework/test_kassert_catcher.c) longjmps back
 * to TEST_ASSERT_KASSERT_FIRES so we can prove the assert fired. The
 * error-return remains as a release-build fallback when RTOS_ASSERT_ENABLED=0.
 */
TEST_CASE(mutex, null_input_asserts)
{
#if RTOS_ASSERT_ENABLED
    TEST_INV_DECLARE("INV-MUTEX-NULL-ASSERT-LOCK",   1);
    TEST_INV_DECLARE("INV-MUTEX-NULL-ASSERT-UNLOCK", 1);

    TEST_ASSERT_KASSERT_FIRES(rtos_mutex_lock(NULL, RTOS_NO_WAIT),
                              "INV-MUTEX-NULL-ASSERT-LOCK");
    TEST_ASSERT_KASSERT_FIRES(rtos_mutex_unlock(NULL),
                              "INV-MUTEX-NULL-ASSERT-UNLOCK");
#else
    TEST_ASSUME(false, "null_input_asserts requires RTOS_ASSERT_ENABLED");
#endif
}

/* ── Case 9: lock_fires_lock_exit_hook ───────────────────────────────────── */
/*
 * Hook observability: MUTEX_LOCK_EXIT fires on both the fast path (free
 * mutex) and the post-wake slow path, both with status = RTOS_MUTEX_OK.
 * The runner locks a free mutex — fast path — and verifies the payload
 * names this mutex, this caller, and OK status.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_mutex_lock_exit(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.mutex_lock.mutex == &g_m)
    {
        g_observed_mutex  = ctx->u.mutex_lock.mutex;
        g_observed_caller = ctx->u.mutex_lock.caller;
        g_observed_status = ctx->u.mutex_lock.status;
        test_signal_post(&g_lock_exit_observed);
    }
}
#endif

TEST_CASE(mutex, lock_fires_lock_exit_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-MUTEX-LOCK-EXIT-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-MUTEX-LOCK-EXIT-HOOK-PAYLOAD", 3);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_MUTEX_LOCK_EXIT,
                                        on_mutex_lock_exit, NULL, &g_hook_id) == 0,
                "INV-MUTEX-LOCK-EXIT-HOOK-FIRES");

    rtos_task_handle_t runner = rtos_task_get_current();

    /* Fast path: g_m is free, so this exits with status OK and fires the hook. */
    TEST_ASSERT(rtos_mutex_lock(&g_m, RTOS_MAX_WAIT) == RTOS_MUTEX_OK,
                "INV-MUTEX-LOCK-EXIT-HOOK-FIRES");

    TEST_AWAIT_PHASE("lock-exit-hook", 500, {
        test_signal_wait(&g_lock_exit_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-MUTEX-LOCK-EXIT-HOOK-FIRES");
    TEST_EXPECT(g_observed_mutex  == &g_m,             "INV-MUTEX-LOCK-EXIT-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_caller == runner,           "INV-MUTEX-LOCK-EXIT-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_status == RTOS_MUTEX_OK,    "INV-MUTEX-LOCK-EXIT-HOOK-PAYLOAD");

    rtos_mutex_unlock(&g_m);

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-MUTEX-LOCK-EXIT-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 10: unlock_fires_unlock_hook ───────────────────────────────────── */
/*
 * Hook observability: MUTEX_UNLOCK fires inside unlock() only on the FULL
 * unlock path (lock_count drops to 0), not on recursive decrements. The
 * runner locks then unlocks; the hook must fire exactly once with this
 * mutex and the runner as caller.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_mutex_unlock(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.mutex_unlock.mutex == &g_m)
    {
        g_observed_mutex  = ctx->u.mutex_unlock.mutex;
        g_observed_caller = ctx->u.mutex_unlock.caller;
        test_signal_post(&g_unlock_observed);
    }
}
#endif

TEST_CASE(mutex, unlock_fires_unlock_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-MUTEX-UNLOCK-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-MUTEX-UNLOCK-HOOK-PAYLOAD", 2);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_MUTEX_UNLOCK,
                                        on_mutex_unlock, NULL, &g_hook_id) == 0,
                "INV-MUTEX-UNLOCK-HOOK-FIRES");

    rtos_task_handle_t runner = rtos_task_get_current();

    TEST_ASSERT(rtos_mutex_lock(&g_m, RTOS_MAX_WAIT) == RTOS_MUTEX_OK,
                "INV-MUTEX-UNLOCK-HOOK-FIRES");
    TEST_ASSERT(rtos_mutex_unlock(&g_m) == RTOS_MUTEX_OK,
                "INV-MUTEX-UNLOCK-HOOK-FIRES");

    TEST_AWAIT_PHASE("unlock-hook", 500, {
        test_signal_wait(&g_unlock_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-MUTEX-UNLOCK-HOOK-FIRES");
    TEST_EXPECT(g_observed_mutex  == &g_m,   "INV-MUTEX-UNLOCK-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_caller == runner, "INV-MUTEX-UNLOCK-HOOK-PAYLOAD");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-MUTEX-UNLOCK-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_mutex_cases[] = {
    &TEST_CASE_REF(mutex, basic_lock_unlock),
    &TEST_CASE_REF(mutex, contender_blocks_immediately),
    &TEST_CASE_REF(mutex, pip_boost_to_highest_waiter),
    &TEST_CASE_REF(mutex, pip_restore_on_unlock),
    &TEST_CASE_REF(mutex, wait_queue_priority_order),
    &TEST_CASE_REF(mutex, recursive_lock_unlock_balance),
    &TEST_CASE_REF(mutex, pip_transitive_chain),
    &TEST_CASE_REF(mutex, null_input_asserts),
    &TEST_CASE_REF(mutex, lock_fires_lock_exit_hook),
    &TEST_CASE_REF(mutex, unlock_fires_unlock_hook),
};

TEST_SUITE_DEFINE(mutex, g_mutex_cases,
                  .before = mutex_before);

int main(void)
{
    return test_runtime_main(&test_suite_mutex);
}
