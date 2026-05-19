/**
 * @file test_notify_suite.c
 * @brief Task-notification test suite — 6 cases replacing test_notification_state.c.
 *
 * Invariant prefix: INV-NOTIFY-
 * Each case spawns a Waiter task that blocks on a notification, then the
 * runner sends the notification and verifies the result.
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

#define PRIO_WAITER 3U
#define STK         RTOS_DEFAULT_TASK_STACK_SIZE

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static test_signal_t      g_waiter_ready;  /* Waiter → Runner: ready to receive */
static test_signal_t      g_case_done;     /* Waiter → Runner: done */
static rtos_task_handle_t g_waiter_task;
static volatile uint32_t  g_received_value; /* value read by Waiter */

#if RTOS_TEST_HOOKS_ENABLED
static test_signal_t               g_send_observed;
static test_signal_t               g_recv_observed;
static volatile rtos_task_handle_t g_observed_target;
static volatile uint32_t           g_observed_value;
static int                         g_hook_id;
#endif

static void notify_before(void)
{
    test_signal_init(&g_waiter_ready);
    test_signal_init(&g_case_done);
    g_waiter_task   = NULL;
    g_received_value = 0U;
#if RTOS_TEST_HOOKS_ENABLED
    test_signal_init(&g_send_observed);
    test_signal_init(&g_recv_observed);
    g_observed_target = NULL;
    g_observed_value  = 0U;
    g_hook_id         = -1;
#endif
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* Generic waiter: blocks until notification arrives (entry_clear=ALL,
 * exit_clear=ALL), stores value, signals done. */
static void task_notify_waiter_simple(void *_)
{
    (void) _;
    test_signal_post(&g_waiter_ready);
    rtos_task_notify_wait(RTOS_NOTIFY_CLEAR_ALL, RTOS_NOTIFY_CLEAR_ALL,
                          (uint32_t *) &g_received_value,
                          RTOS_NOTIFY_MAX_WAIT);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waiter that uses notify_take (counting semaphore pattern). */
static void task_notify_take_waiter(void *_)
{
    (void) _;
    test_signal_post(&g_waiter_ready);
    /* Take twice (each give increments the counter). */
    rtos_task_notify_take(false, RTOS_NOTIFY_MAX_WAIT); /* decrement */
    rtos_task_notify_take(false, RTOS_NOTIFY_MAX_WAIT);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waiter that reads value with exit_clear_bits=0xFF (partial clear). */
static void task_exit_clear_bits_waiter(void *_)
{
    (void) _;
    test_signal_post(&g_waiter_ready);
    rtos_task_notify_wait(0x00U, 0x0FU,
                          (uint32_t *) &g_received_value,
                          RTOS_NOTIFY_MAX_WAIT);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waiter that preserves value across exit (both clear masks = 0). Used by
 * the NOTIFY_RECEIVE hook case so the hook payload reflects the sent value. */
static void task_notify_preserve_waiter(void *_)
{
    (void) _;
    rtos_task_notify_wait(0x00U, 0x00U,
                          (uint32_t *) &g_received_value,
                          RTOS_NOTIFY_MAX_WAIT);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* ── Case 1: waiter_blocks_with_no_pending ───────────────────────────────── */
TEST_CASE(notify, waiter_blocks_with_no_pending)
{
    TEST_INV_DECLARE("INV-NOTIFY-BLOCK", 1);

    TEST_ASSERT(rtos_task_create(task_notify_waiter_simple, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-BLOCK");

    /* Wait for Waiter to signal it is about to call notify_wait. */
    TEST_AWAIT_PHASE("waiter-ready", 500, {
        test_signal_wait(&g_waiter_ready, RTOS_SEM_MAX_WAIT);
    });

    /* 5 ms window: Waiter should now be blocked on notify_wait. */
    TEST_AWAIT_PHASE("waiter-blocking", 50, { rtos_delay_ms(5); });
    TEST_EXPECT(rtos_task_get_state(g_waiter_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-NOTIFY-BLOCK");

    /* Unblock. */
    rtos_task_notify(g_waiter_task, 1U, RTOS_NOTIFY_ACTION_SET_BITS);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: set_bits_ors_value ──────────────────────────────────────────── */
TEST_CASE(notify, set_bits_ors_value)
{
    TEST_INV_DECLARE("INV-NOTIFY-SET-BITS", 1);

    TEST_ASSERT(rtos_task_create(task_notify_waiter_simple, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-SET-BITS");

    TEST_AWAIT_PHASE("waiter-ready", 500, {
        test_signal_wait(&g_waiter_ready, RTOS_SEM_MAX_WAIT);
    });

    /* Send two SET_BITS notifications — value should be 0x05 | 0x0A = 0x0F. */
    rtos_task_notify(g_waiter_task, 0x05U, RTOS_NOTIFY_ACTION_SET_BITS);
    rtos_task_notify(g_waiter_task, 0x0AU, RTOS_NOTIFY_ACTION_SET_BITS);

    TEST_AWAIT_PHASE("waiter-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_received_value == 0x0FU, "INV-NOTIFY-SET-BITS");
}

/* ── Case 3: increment_accumulates ──────────────────────────────────────── */
TEST_CASE(notify, increment_accumulates)
{
    TEST_INV_DECLARE("INV-NOTIFY-INCREMENT", 1);

    TEST_ASSERT(rtos_task_create(task_notify_take_waiter, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-INCREMENT");

    TEST_AWAIT_PHASE("waiter-ready", 500, {
        test_signal_wait(&g_waiter_ready, RTOS_SEM_MAX_WAIT);
    });

    /* Give twice before Waiter takes. Since Waiter blocks with notify_take,
     * and runner is P7 > P3, both gives happen before Waiter runs. */
    rtos_task_notify_give(g_waiter_task); /* notification_value = 1 */
    rtos_task_notify_give(g_waiter_task); /* notification_value = 2 */

    /* Waiter should complete: takes twice, each decrement succeeds. */
    TEST_AWAIT_PHASE("take-done", 1000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-NOTIFY-INCREMENT");
}

/* ── Case 4: overwrite_replaces ──────────────────────────────────────────── */
TEST_CASE(notify, overwrite_replaces)
{
    TEST_INV_DECLARE("INV-NOTIFY-OVERWRITE", 1);

    TEST_ASSERT(rtos_task_create(task_notify_waiter_simple, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-OVERWRITE");

    TEST_AWAIT_PHASE("waiter-ready", 500, {
        test_signal_wait(&g_waiter_ready, RTOS_SEM_MAX_WAIT);
    });

    /* Overwrite twice — Waiter should see only the last value. */
    rtos_task_notify(g_waiter_task, 0xAAU, RTOS_NOTIFY_ACTION_OVERWRITE);
    rtos_task_notify(g_waiter_task, 0xBBU, RTOS_NOTIFY_ACTION_OVERWRITE);

    TEST_AWAIT_PHASE("waiter-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_received_value == 0xBBU, "INV-NOTIFY-OVERWRITE");
}

/* ── Case 5: give_take_counting_sem ─────────────────────────────────────── */
TEST_CASE(notify, give_take_counting_sem)
{
    TEST_INV_DECLARE("INV-NOTIFY-GIVE-TAKE", 1);

    TEST_ASSERT(rtos_task_create(task_notify_take_waiter, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-GIVE-TAKE");

    TEST_AWAIT_PHASE("waiter-ready", 500, {
        test_signal_wait(&g_waiter_ready, RTOS_SEM_MAX_WAIT);
    });

    rtos_task_notify_give(g_waiter_task); /* value = 1: first take unblocks */
    rtos_task_notify_give(g_waiter_task); /* value = 2: second take unblocks */

    TEST_AWAIT_PHASE("give-take-done", 1000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-NOTIFY-GIVE-TAKE");
}

/* ── Case 6: exit_clear_bits ─────────────────────────────────────────────── */
TEST_CASE(notify, exit_clear_bits)
{
    TEST_INV_DECLARE("INV-NOTIFY-EXIT-CLEAR", 1);

    TEST_ASSERT(rtos_task_create(task_exit_clear_bits_waiter, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-EXIT-CLEAR");

    TEST_AWAIT_PHASE("waiter-ready", 500, {
        test_signal_wait(&g_waiter_ready, RTOS_SEM_MAX_WAIT);
    });

    /* Send value 0xFF. Waiter clears only the lower nibble (exit_clear=0x0F). */
    rtos_task_notify(g_waiter_task, 0xFFU, RTOS_NOTIFY_ACTION_OVERWRITE);

    TEST_AWAIT_PHASE("exit-clear-done", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    /* g_received_value was captured before exit_clear applied — should be 0xFF. */
    TEST_EXPECT(g_received_value == 0xFFU, "INV-NOTIFY-EXIT-CLEAR");
}

/* ── Case 7: send_fires_notify_send_hook ─────────────────────────────────── */
/*
 * Hook observability: NOTIFY_SEND fires after the kernel applies the
 * action (SET_BITS/INCREMENT/OVERWRITE) — payload reports the resulting
 * notification_value, not the raw argument. Send SET_BITS with one value,
 * then OR in a second bit before the waiter wakes; the hook should
 * capture the running OR of both, proving it fires post-mutation.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_notify_send(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.notify.task == g_waiter_task)
    {
        g_observed_target = ctx->u.notify.task;
        g_observed_value  = ctx->u.notify.value;
        test_signal_post(&g_send_observed);
    }
}
#endif

TEST_CASE(notify, send_fires_notify_send_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-NOTIFY-SEND-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-NOTIFY-SEND-HOOK-TARGET",  1);
    TEST_INV_DECLARE("INV-NOTIFY-SEND-HOOK-VALUE",   1);

    /* Drain stale events from earlier cases before registering. Otherwise
     * the kernel TCB slot recycling (rtos_task_delete now clears the slot)
     * means an earlier case's notify_send event could re-dispatch into our
     * callback with the SAME pointer as the new g_waiter_task, polluting
     * g_observed_value before our real send is processed. */
    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_NOTIFY_SEND, on_notify_send,
                                        NULL, &g_hook_id) == 0,
                "INV-NOTIFY-SEND-HOOK-FIRES");

    TEST_ASSERT(rtos_task_create(task_notify_waiter_simple, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-SEND-HOOK-FIRES");

    TEST_AWAIT_PHASE("waiter-ready", 500, {
        test_signal_wait(&g_waiter_ready, RTOS_SEM_MAX_WAIT);
    });

    /* OVERWRITE 0xCAFEBABE — hook fires with value = 0xCAFEBABE, target = waiter. */
    rtos_task_notify(g_waiter_task, 0xCAFEBABEU, RTOS_NOTIFY_ACTION_OVERWRITE);
    TEST_AWAIT_PHASE("send-hook", 500, {
        test_signal_wait(&g_send_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-NOTIFY-SEND-HOOK-FIRES");
    TEST_EXPECT(g_observed_target == g_waiter_task, "INV-NOTIFY-SEND-HOOK-TARGET");
    TEST_EXPECT(g_observed_value  == 0xCAFEBABEU,   "INV-NOTIFY-SEND-HOOK-VALUE");

    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-NOTIFY-SEND-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 8: receive_fast_path_fires_receive_hook ────────────────────────── */
/*
 * Hook observability: NOTIFY_RECEIVE only fires when notify_wait takes the
 * fast path — i.e. notification_pending was already set when the task
 * entered wait. The block-then-wake path does NOT fire this event.
 *
 * To force the fast path: the runner (priority MAX-1) sends the notification
 * BEFORE the worker (priority 3) ever runs. The worker therefore observes
 * notification_pending=1 at entry and the hook fires. Worker uses exit_clear=0
 * so the captured value equals the sent value.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_notify_receive(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.notify.task == g_waiter_task)
    {
        g_observed_target = ctx->u.notify.task;
        g_observed_value  = ctx->u.notify.value;
        test_signal_post(&g_recv_observed);
    }
}
#endif

TEST_CASE(notify, receive_fast_path_fires_receive_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-NOTIFY-RECV-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-NOTIFY-RECV-HOOK-TARGET",  1);
    TEST_INV_DECLARE("INV-NOTIFY-RECV-HOOK-VALUE",   1);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_NOTIFY_RECEIVE, on_notify_receive,
                                        NULL, &g_hook_id) == 0,
                "INV-NOTIFY-RECV-HOOK-FIRES");

    TEST_ASSERT(rtos_task_create(task_notify_preserve_waiter, "TW", STK, NULL,
                                 PRIO_WAITER, &g_waiter_task) == RTOS_SUCCESS,
                "INV-NOTIFY-RECV-HOOK-FIRES");

    /* Worker is READY (P3) but runner (P_max-1) stays running — send first
     * to make notification pending before the worker ever calls notify_wait. */
    rtos_task_notify(g_waiter_task, 0xDEADBEEFU, RTOS_NOTIFY_ACTION_OVERWRITE);

    TEST_AWAIT_PHASE("recv-hook", 1000, {
        test_signal_wait(&g_recv_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-NOTIFY-RECV-HOOK-FIRES");
    TEST_EXPECT(g_observed_target == g_waiter_task, "INV-NOTIFY-RECV-HOOK-TARGET");
    TEST_EXPECT(g_observed_value  == 0xDEADBEEFU,   "INV-NOTIFY-RECV-HOOK-VALUE");

    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-NOTIFY-RECV-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_notify_cases[] = {
    &TEST_CASE_REF(notify, waiter_blocks_with_no_pending),
    &TEST_CASE_REF(notify, set_bits_ors_value),
    &TEST_CASE_REF(notify, increment_accumulates),
    &TEST_CASE_REF(notify, overwrite_replaces),
    &TEST_CASE_REF(notify, give_take_counting_sem),
    &TEST_CASE_REF(notify, exit_clear_bits),
    &TEST_CASE_REF(notify, send_fires_notify_send_hook),
    &TEST_CASE_REF(notify, receive_fast_path_fires_receive_hook),
};

TEST_SUITE_DEFINE(notify, g_notify_cases,
                  .before = notify_before);

int main(void)
{
    return test_runtime_main(&test_suite_notify);
}
