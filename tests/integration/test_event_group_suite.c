/**
 * @file test_event_group_suite.c
 * @brief Event-group test suite — 5 focused cases replacing test_event_group_state.c.
 *
 * Invariant prefix: INV-EG-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "event_group.h"
#include "task.h"

#if RTOS_TEST_HOOKS_ENABLED
#include "rtos_test_hooks.h"
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_LOW  2U
#define PRIO_HIGH 4U
#define STK       RTOS_DEFAULT_TASK_STACK_SIZE

#define BIT_A 0x01U
#define BIT_B 0x02U
#define BIT_C 0x04U

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static rtos_event_group_t g_eg;
static test_signal_t      g_case_done;
static rtos_task_handle_t g_low_task;

/* Bits observed by task on wake (set by task body). */
static volatile uint32_t g_received_bits;

#if RTOS_TEST_HOOKS_ENABLED
static test_signal_t      g_set_observed;
static test_signal_t      g_satisfied_observed;
static volatile uint32_t  g_observed_set_bits;
static volatile uint32_t  g_observed_satisfied_bits;
static int                g_hook_id;
#endif

static void eg_before(void)
{
    rtos_event_group_init(&g_eg);
    test_signal_init(&g_case_done);
    g_received_bits = 0U;
    g_low_task      = NULL;
#if RTOS_TEST_HOOKS_ENABLED
    test_signal_init(&g_set_observed);
    test_signal_init(&g_satisfied_observed);
    g_observed_set_bits       = 0U;
    g_observed_satisfied_bits = 0U;
    g_hook_id                 = -1;
#endif
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* Waits for ANY of BIT_A|BIT_B, records bits, signals done. */
static void task_wait_any(void *_)
{
    (void) _;
    uint32_t bits = 0U;
    rtos_event_group_wait_bits(&g_eg, BIT_A | BIT_B, false, false, &bits,
                               RTOS_EG_MAX_WAIT);
    g_received_bits = bits;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waits for ALL of BIT_A|BIT_B|BIT_C, records bits, signals done. */
static void task_wait_all(void *_)
{
    (void) _;
    uint32_t bits = 0U;
    rtos_event_group_wait_bits(&g_eg, BIT_A | BIT_B | BIT_C, true, false,
                               &bits, RTOS_EG_MAX_WAIT);
    g_received_bits = bits;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waits for BIT_A with clear_on_exit=true, signals done. */
static void task_wait_clear_on_exit(void *_)
{
    (void) _;
    rtos_event_group_wait_bits(&g_eg, BIT_A, true, true, NULL,
                               RTOS_EG_MAX_WAIT);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* ── Case 1: waiter_blocks_on_unset ──────────────────────────────────────── */
TEST_CASE(eg, waiter_blocks_on_unset)
{
    TEST_INV_DECLARE("INV-EG-BLOCK", 1);

    TEST_ASSERT(rtos_task_create(task_wait_any, "TEG", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-EG-BLOCK");

    TEST_AWAIT_PHASE("waiter-blocking", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_low_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-EG-BLOCK");

    /* Unblock and clean up. */
    rtos_event_group_set_bits(&g_eg, BIT_A);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: any_mode_wakes_on_one_bit ────────────────────────────────────── */
TEST_CASE(eg, any_mode_wakes_on_one_bit)
{
    TEST_INV_DECLARE("INV-EG-ANY", 1);

    TEST_ASSERT(rtos_task_create(task_wait_any, "TEG", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-EG-ANY");

    TEST_AWAIT_PHASE("waiter-blocking", 100, { rtos_delay_ms(5); });

    /* Set only BIT_B (one of the two waited bits) — waiter must wake. */
    rtos_event_group_set_bits(&g_eg, BIT_B);

    TEST_AWAIT_PHASE("any-wake", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT((g_received_bits & BIT_B) != 0U, "INV-EG-ANY");
}

/* ── Case 3: all_mode_waits_for_all_bits ──────────────────────────────────── */
TEST_CASE(eg, all_mode_waits_for_all_bits)
{
    TEST_INV_DECLARE("INV-EG-ALL", 1);

    TEST_ASSERT(rtos_task_create(task_wait_all, "TEG", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-EG-ALL");

    TEST_AWAIT_PHASE("waiter-blocking", 100, { rtos_delay_ms(5); });

    /* Set BIT_A and BIT_B — waiter must stay blocked (BIT_C missing). */
    rtos_event_group_set_bits(&g_eg, BIT_A | BIT_B);

    TEST_AWAIT_PHASE("still-blocking", 100, { rtos_delay_ms(5); });
    TEST_EXPECT(rtos_task_get_state(g_low_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-EG-ALL");

    /* Now set BIT_C — all bits present, waiter wakes. */
    rtos_event_group_set_bits(&g_eg, BIT_C);
    TEST_AWAIT_PHASE("all-wake", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT((g_received_bits & (BIT_A | BIT_B | BIT_C)) == (BIT_A | BIT_B | BIT_C),
                "INV-EG-ALL");
}

/* ── Case 4: clear_on_exit_clears_bits ────────────────────────────────────── */
TEST_CASE(eg, clear_on_exit_clears_bits)
{
    TEST_INV_DECLARE("INV-EG-CLEAR", 1);

    rtos_event_group_set_bits(&g_eg, BIT_A | BIT_B); /* pre-set both */

    TEST_ASSERT(rtos_task_create(task_wait_clear_on_exit, "TEG", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-EG-CLEAR");

    TEST_AWAIT_PHASE("clear-wake", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    /* BIT_A should be cleared; BIT_B (not waited) should remain. */
    uint32_t bits = rtos_event_group_get_bits(&g_eg);
    TEST_EXPECT((bits & BIT_A) == 0U,        "INV-EG-CLEAR");
    TEST_EXPECT((bits & BIT_B) == BIT_B,     "INV-EG-CLEAR");
}

/* ── Case 5: timeout_returns_error ───────────────────────────────────────── */
TEST_CASE(eg, timeout_returns_error)
{
    TEST_INV_DECLARE("INV-EG-TIMEOUT", 1);

    rtos_eg_status_t ret = rtos_event_group_wait_bits(
        &g_eg, BIT_A, true, false, NULL, 10U);

    TEST_EXPECT(ret == RTOS_EG_ERR_TIMEOUT, "INV-EG-TIMEOUT");
}

/* ── Case 6: set_bits_fires_set_hook ─────────────────────────────────────── */
/*
 * Hook observability: EVENT_SET fires synchronously inside set_bits with
 * the post-OR bit pattern. Sets two bits in sequence and verifies the
 * payload reflects the running union — proves the hook captures the
 * applied state, not a stale snapshot.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_event_set(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    g_observed_set_bits = ctx->u.event_bits.bits;
    test_signal_post(&g_set_observed);
}
#endif

TEST_CASE(eg, set_bits_fires_set_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-EG-SET-HOOK-FIRES",       2);
    TEST_INV_DECLARE("INV-EG-SET-HOOK-FIRST-BITS",  1);
    TEST_INV_DECLARE("INV-EG-SET-HOOK-SECOND-BITS", 1);

    /* Drain stale EVENT_SET events from prior cases before registering. */
    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_EVENT_SET, on_event_set,
                                        NULL, &g_hook_id) == 0,
                "INV-EG-SET-HOOK-FIRES");

    /* First set: BIT_A. Hook fires with bits = BIT_A. */
    rtos_event_group_set_bits(&g_eg, BIT_A);
    TEST_AWAIT_PHASE("set-hook-1", 200, {
        test_signal_wait(&g_set_observed, RTOS_SEM_MAX_WAIT);
    });
    TEST_INV_PASS("INV-EG-SET-HOOK-FIRES");
    TEST_EXPECT(g_observed_set_bits == BIT_A, "INV-EG-SET-HOOK-FIRST-BITS");

    /* Second set: BIT_B. Hook fires with bits = BIT_A | BIT_B (union). */
    rtos_event_group_set_bits(&g_eg, BIT_B);
    TEST_AWAIT_PHASE("set-hook-2", 200, {
        test_signal_wait(&g_set_observed, RTOS_SEM_MAX_WAIT);
    });
    TEST_INV_PASS("INV-EG-SET-HOOK-FIRES");
    TEST_EXPECT(g_observed_set_bits == (BIT_A | BIT_B), "INV-EG-SET-HOOK-SECOND-BITS");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-EG-SET-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 7: wait_fast_path_fires_satisfied_hook ─────────────────────────── */
/*
 * Hook observability: EVENT_WAIT_SATISFIED fires only when wait_bits hits
 * the fast path — condition already met at entry. The block-then-wake
 * path does NOT fire this event (that path returns via the unblock side).
 * Pre-set BIT_A | BIT_B, then call wait_bits(any of BIT_A) with
 * clear_on_exit=false; hook fires with bits = (BIT_A | BIT_B) post-clear,
 * which is unchanged because clear is disabled.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_event_wait_satisfied(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    g_observed_satisfied_bits = ctx->u.event_bits.bits;
    test_signal_post(&g_satisfied_observed);
}
#endif

TEST_CASE(eg, wait_fast_path_fires_satisfied_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-EG-WAIT-SATISFIED-HOOK-FIRES", 1);
    TEST_INV_DECLARE("INV-EG-WAIT-SATISFIED-HOOK-BITS",  1);

    /* Pre-set bits BEFORE registering, so the EVENT_SET event doesn't
     * leak into the EVENT_WAIT_SATISFIED registration cycle. */
    rtos_event_group_set_bits(&g_eg, BIT_A | BIT_B);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_EVENT_WAIT_SATISFIED,
                                        on_event_wait_satisfied, NULL,
                                        &g_hook_id) == 0,
                "INV-EG-WAIT-SATISFIED-HOOK-FIRES");

    /* Fast path: BIT_A is already set, wait_any with clear_on_exit=false. */
    uint32_t bits_out = 0U;
    TEST_ASSERT(rtos_event_group_wait_bits(&g_eg, BIT_A, false, false,
                                           &bits_out, RTOS_EG_NO_WAIT) == RTOS_EG_OK,
                "INV-EG-WAIT-SATISFIED-HOOK-FIRES");

    TEST_AWAIT_PHASE("satisfied-hook", 500, {
        test_signal_wait(&g_satisfied_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-EG-WAIT-SATISFIED-HOOK-FIRES");
    TEST_EXPECT(g_observed_satisfied_bits == (BIT_A | BIT_B),
                "INV-EG-WAIT-SATISFIED-HOOK-BITS");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-EG-WAIT-SATISFIED-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_eg_cases[] = {
    &TEST_CASE_REF(eg, waiter_blocks_on_unset),
    &TEST_CASE_REF(eg, any_mode_wakes_on_one_bit),
    &TEST_CASE_REF(eg, all_mode_waits_for_all_bits),
    &TEST_CASE_REF(eg, clear_on_exit_clears_bits),
    &TEST_CASE_REF(eg, timeout_returns_error),
    &TEST_CASE_REF(eg, set_bits_fires_set_hook),
    &TEST_CASE_REF(eg, wait_fast_path_fires_satisfied_hook),
};

TEST_SUITE_DEFINE(eg, g_eg_cases,
                  .before = eg_before);

int main(void)
{
    return test_runtime_main(&test_suite_eg);
}
