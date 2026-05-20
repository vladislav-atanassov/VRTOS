/**
 * @file test_queue_suite.c
 * @brief Queue test suite — 5 focused cases replacing test_queue_state.c.
 *
 * Invariant prefix: INV-Q-
 * Queue is created once in setup() and reset in before() to reuse heap memory.
 * Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "queue.h"
#include "task.h"

#if RTOS_TEST_HOOKS_ENABLED
#include "rtos_test_hooks.h"
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_LOW   2U
#define PRIO_HIGH  4U
#define STK        RTOS_DEFAULT_TASK_STACK_SIZE
#define Q_CAPACITY 4U

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static rtos_queue_handle_t g_q;

static test_signal_t g_case_done;

static rtos_task_handle_t g_low_task;
static rtos_task_handle_t g_high_task;

#if RTOS_TEST_HOOKS_ENABLED
static test_signal_t               g_block_observed;
static test_signal_t               g_send_observed;
static test_signal_t               g_recv_observed;
static test_signal_t               g_block_empty_observed;
static volatile rtos_task_handle_t g_observed_blocker;
static volatile void *             g_observed_prim;
static volatile rtos_task_handle_t g_observed_caller;
static int                         g_hook_id;
#endif

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static void queue_setup(void)
{
    /* Created once; heap memory is reused across cases via rtos_queue_reset. */
    rtos_queue_create(&g_q, Q_CAPACITY, sizeof(uint32_t));
}

static void queue_before(void)
{
    rtos_queue_reset(g_q);
    test_signal_init(&g_case_done);
    g_low_task  = NULL;
    g_high_task = NULL;
#if RTOS_TEST_HOOKS_ENABLED
    test_signal_init(&g_block_observed);
    test_signal_init(&g_send_observed);
    test_signal_init(&g_recv_observed);
    test_signal_init(&g_block_empty_observed);
    g_observed_blocker = NULL;
    g_observed_prim    = NULL;
    g_observed_caller  = NULL;
    g_hook_id          = -1;
#endif
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* Tries to receive from empty queue — blocks, wakes when item arrives. */
static void task_receive_and_done(void *_)
{
    (void) _;
    uint32_t item;
    rtos_queue_receive(g_q, &item, RTOS_MAX_DELAY);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Fills the queue to capacity, then tries one more send (blocks). */
static void task_fill_then_block(void *_)
{
    (void) _;
    for (uint32_t i = 0U; i < Q_CAPACITY; i++)
    {
        rtos_queue_send(g_q, &i, RTOS_MAX_DELAY);
    }
    /* One more send blocks (queue full). */
    uint32_t extra = 0xFFU;
    rtos_queue_send(g_q, &extra, RTOS_MAX_DELAY);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* ── Case 1: consumer_blocks_on_empty ────────────────────────────────────── */
TEST_CASE(queue, consumer_blocks_on_empty)
{
    TEST_INV_DECLARE("INV-Q-BLOCK-EMPTY", 1);

    TEST_ASSERT(rtos_task_create(task_receive_and_done, "TQR", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-Q-BLOCK-EMPTY");

    TEST_AWAIT_PHASE("consumer-blocking", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_low_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-Q-BLOCK-EMPTY");

    /* Send one item to unblock consumer. */
    uint32_t val = 42U;
    rtos_queue_send(g_q, &val, RTOS_MAX_DELAY);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: producer_blocks_on_full ─────────────────────────────────────── */
TEST_CASE(queue, producer_blocks_on_full)
{
    TEST_INV_DECLARE("INV-Q-BLOCK-FULL", 1);

    TEST_ASSERT(rtos_task_create(task_fill_then_block, "TQP", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-Q-BLOCK-FULL");

    /* Give producer time to fill the queue and attempt the blocking send. */
    TEST_AWAIT_PHASE("producer-blocking", 500, { rtos_delay_ms(20); });

    TEST_EXPECT(rtos_task_get_state(g_low_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-Q-BLOCK-FULL");
    TEST_EXPECT(rtos_queue_is_full(g_q), "INV-Q-BLOCK-FULL");

    /* Make room by receiving one item — producer unblocks and self-deletes. */
    uint32_t item;
    rtos_queue_receive(g_q, &item, RTOS_MAX_DELAY);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 3: fifo_ordering ───────────────────────────────────────────────── */
TEST_CASE(queue, fifo_ordering)
{
    TEST_INV_DECLARE("INV-Q-FIFO", Q_CAPACITY);

    /* Send items 0..Q_CAPACITY-1 in order. */
    for (uint32_t i = 0U; i < Q_CAPACITY; i++)
    {
        TEST_EXPECT(rtos_queue_send(g_q, &i, 0U) == RTOS_SUCCESS,
                    "INV-Q-FIFO");
    }

    /* Receive and verify strict FIFO order. */
    for (uint32_t i = 0U; i < Q_CAPACITY; i++)
    {
        uint32_t received = 0xDEADBEEFU;
        TEST_EXPECT(rtos_queue_receive(g_q, &received, 0U) == RTOS_SUCCESS,
                    "INV-Q-FIFO");
        TEST_EXPECT(received == i, "INV-Q-FIFO");
    }
}

/* ── Case 4: nonblocking_full_returns_error ──────────────────────────────── */
TEST_CASE(queue, nonblocking_full_returns_error)
{
    TEST_INV_DECLARE("INV-Q-NONBLOCK-FULL", 1);

    /* Fill queue to capacity. */
    for (uint32_t i = 0U; i < Q_CAPACITY; i++)
    {
        rtos_queue_send(g_q, &i, 0U);
    }

    /* Non-blocking send to full queue must return FULL error without blocking. */
    uint32_t extra = 0xFFU;
    rtos_status_t ret = rtos_queue_send(g_q, &extra, 0U);
    TEST_EXPECT(ret == RTOS_ERROR_FULL, "INV-Q-NONBLOCK-FULL");

    /* Count should still be exactly Q_CAPACITY. */
    TEST_EXPECT(rtos_queue_messages_waiting(g_q) == Q_CAPACITY,
                "INV-Q-NONBLOCK-FULL");
}

/* ── Case 5: count_stays_in_bounds ───────────────────────────────────────── */
TEST_CASE(queue, count_stays_in_bounds)
{
    TEST_INV_DECLARE("INV-Q-COUNT", Q_CAPACITY * 2U);

    for (uint32_t i = 0U; i < Q_CAPACITY; i++)
    {
        uint32_t count_before = rtos_queue_messages_waiting(g_q);
        TEST_EXPECT(count_before <= Q_CAPACITY, "INV-Q-COUNT");

        rtos_queue_send(g_q, &i, 0U);

        uint32_t count_after = rtos_queue_messages_waiting(g_q);
        TEST_EXPECT(count_after <= Q_CAPACITY, "INV-Q-COUNT");
    }

    for (uint32_t i = 0U; i < Q_CAPACITY; i++)
    {
        uint32_t item;
        rtos_queue_receive(g_q, &item, 0U);
        TEST_EXPECT(rtos_queue_messages_waiting(g_q) <= Q_CAPACITY,
                    "INV-Q-COUNT");
    }
}

/* ── Case 6: full_send_fires_block_hook ──────────────────────────────────── */
/*
 * Hook observability: QUEUE_BLOCK_FULL only fires from inside the kernel's
 * sender-block path — proves a producer who hit a full queue actually
 * blocked rather than returning early with an error. The pre-fix state
 * check (poll task state after a delay) has a race window; the hook fires
 * synchronously inside the kernel at the moment the block decision is made.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_queue_block_full(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.prim.primitive == g_q)
    {
        g_observed_blocker = ctx->u.prim.caller;
        test_signal_post(&g_block_observed);
    }
}
#endif

TEST_CASE(queue, full_send_fires_block_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-Q-BLOCK-FULL-HOOK",       1);
    TEST_INV_DECLARE("INV-Q-BLOCK-FULL-HOOK-TASK",  1);

    /* Drain stale events from prior cases before registering. */
    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_QUEUE_BLOCK_FULL,
                                        on_queue_block_full, NULL, &g_hook_id) == 0,
                "INV-Q-BLOCK-FULL-HOOK");

    TEST_ASSERT(rtos_task_create(task_fill_then_block, "TQP", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-Q-BLOCK-FULL-HOOK");

    /* Hook fires synchronously inside the kernel when the producer's
     * (Q_CAPACITY+1)-th send hits the block path. No polling needed. */
    TEST_AWAIT_PHASE("block-hook", 1000, {
        test_signal_wait(&g_block_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-Q-BLOCK-FULL-HOOK");
    TEST_EXPECT(g_observed_blocker == g_low_task, "INV-Q-BLOCK-FULL-HOOK-TASK");

    /* Drain one item to let the producer complete; clean up. */
    uint32_t item;
    rtos_queue_receive(g_q, &item, RTOS_MAX_DELAY);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-Q-BLOCK-FULL-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 7: send_fires_send_hook ────────────────────────────────────────── */
/*
 * Hook observability: QUEUE_SEND fires after the item is copied into the
 * queue and count is incremented. Runner sends one item to an empty queue
 * — verifies the hook fires with this queue and this caller.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_queue_send(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.prim.primitive == g_q)
    {
        g_observed_prim   = ctx->u.prim.primitive;
        g_observed_caller = ctx->u.prim.caller;
        test_signal_post(&g_send_observed);
    }
}
#endif

TEST_CASE(queue, send_fires_send_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-Q-SEND-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-Q-SEND-HOOK-PAYLOAD", 2);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_QUEUE_SEND,
                                        on_queue_send, NULL, &g_hook_id) == 0,
                "INV-Q-SEND-HOOK-FIRES");

    rtos_task_handle_t runner = rtos_task_get_current();

    uint32_t val = 0x1234U;
    TEST_ASSERT(rtos_queue_send(g_q, &val, 0U) == RTOS_SUCCESS,
                "INV-Q-SEND-HOOK-FIRES");

    TEST_AWAIT_PHASE("send-hook", 500, {
        test_signal_wait(&g_send_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-Q-SEND-HOOK-FIRES");
    TEST_EXPECT(g_observed_prim   == g_q,    "INV-Q-SEND-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_caller == runner, "INV-Q-SEND-HOOK-PAYLOAD");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-Q-SEND-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 8: receive_fires_receive_hook ──────────────────────────────────── */
/*
 * Hook observability: QUEUE_RECEIVE fires after the item is copied out and
 * count is decremented. Runner prefills one item then receives — fast path
 * (queue non-empty) — and verifies the hook fires with this queue/caller.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_queue_recv(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.prim.primitive == g_q)
    {
        g_observed_prim   = ctx->u.prim.primitive;
        g_observed_caller = ctx->u.prim.caller;
        test_signal_post(&g_recv_observed);
    }
}
#endif

TEST_CASE(queue, receive_fires_receive_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-Q-RECV-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-Q-RECV-HOOK-PAYLOAD", 2);

    /* Pre-fill BEFORE registering the receive hook so the producing send
     * doesn't leave a stale event for the wrong hook. */
    uint32_t fill = 0xABCDU;
    rtos_queue_send(g_q, &fill, 0U);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_QUEUE_RECEIVE,
                                        on_queue_recv, NULL, &g_hook_id) == 0,
                "INV-Q-RECV-HOOK-FIRES");

    rtos_task_handle_t runner = rtos_task_get_current();

    uint32_t item = 0U;
    TEST_ASSERT(rtos_queue_receive(g_q, &item, 0U) == RTOS_SUCCESS,
                "INV-Q-RECV-HOOK-FIRES");

    TEST_AWAIT_PHASE("recv-hook", 500, {
        test_signal_wait(&g_recv_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-Q-RECV-HOOK-FIRES");
    TEST_EXPECT(g_observed_prim   == g_q,    "INV-Q-RECV-HOOK-PAYLOAD");
    TEST_EXPECT(g_observed_caller == runner, "INV-Q-RECV-HOOK-PAYLOAD");

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-Q-RECV-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case 9: empty_receive_fires_block_empty_hook ────────────────────────── */
/*
 * Hook observability: QUEUE_BLOCK_EMPTY only fires from inside the
 * receiver-block path when an item isn't available — proves a consumer
 * who hit an empty queue actually blocked rather than busy-spinning.
 */
#if RTOS_TEST_HOOKS_ENABLED
static void on_queue_block_empty(const rtos_hook_ctx_t *ctx, void *ud)
{
    (void) ud;
    if (ctx->u.prim.primitive == g_q)
    {
        g_observed_prim   = ctx->u.prim.primitive;
        g_observed_caller = ctx->u.prim.caller;
        test_signal_post(&g_block_empty_observed);
    }
}
#endif

TEST_CASE(queue, empty_receive_fires_block_empty_hook)
{
#if RTOS_TEST_HOOKS_ENABLED
    TEST_INV_DECLARE("INV-Q-BLOCK-EMPTY-HOOK-FIRES",   1);
    TEST_INV_DECLARE("INV-Q-BLOCK-EMPTY-HOOK-CALLER",  1);

    TEST_AWAIT_PHASE("hook-stale-drain", 100, { rtos_delay_ms(20); });

    TEST_ASSERT(rtos_test_hook_register(RTOS_HOOK_QUEUE_BLOCK_EMPTY,
                                        on_queue_block_empty, NULL, &g_hook_id) == 0,
                "INV-Q-BLOCK-EMPTY-HOOK-FIRES");

    TEST_ASSERT(rtos_task_create(task_receive_and_done, "TQR", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-Q-BLOCK-EMPTY-HOOK-FIRES");

    /* Hook fires synchronously when the worker hits the block path. */
    TEST_AWAIT_PHASE("block-empty-hook", 1000, {
        test_signal_wait(&g_block_empty_observed, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-Q-BLOCK-EMPTY-HOOK-FIRES");
    TEST_EXPECT(g_observed_caller == g_low_task, "INV-Q-BLOCK-EMPTY-HOOK-CALLER");

    /* Unblock and clean up. */
    uint32_t v = 1U;
    rtos_queue_send(g_q, &v, RTOS_MAX_DELAY);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    rtos_test_hook_unregister(g_hook_id);
    g_hook_id = -1;
#else
    TEST_ASSUME(false, "INV-Q-BLOCK-EMPTY-HOOK requires RTOS_TEST_HOOKS_ENABLED");
#endif
}

/* ── Case: null_input_asserts ────────────────────────────────────────────── */
/*
 * Queue create/send/receive call RTOS_ASSERT_PARAM on each pointer argument
 * before the legacy ERR_INVALID_PARAM return. The framework's KASSERT catcher
 * verifies each assert fires; the error-return remains as a release-build
 * fallback when RTOS_ASSERT_ENABLED=0.
 */
TEST_CASE(queue, null_input_asserts)
{
#if RTOS_ASSERT_ENABLED
    TEST_INV_DECLARE("INV-Q-NULL-ASSERT-CREATE",        1);
    TEST_INV_DECLARE("INV-Q-NULL-ASSERT-SEND-HANDLE",   1);
    TEST_INV_DECLARE("INV-Q-NULL-ASSERT-SEND-ITEM",     1);
    TEST_INV_DECLARE("INV-Q-NULL-ASSERT-RECV-HANDLE",   1);
    TEST_INV_DECLARE("INV-Q-NULL-ASSERT-RECV-BUFFER",   1);

    uint32_t dummy = 0U;

    TEST_ASSERT_KASSERT_FIRES(rtos_queue_create(NULL, 4U, sizeof(uint32_t)),
                              "INV-Q-NULL-ASSERT-CREATE");
    TEST_ASSERT_KASSERT_FIRES(rtos_queue_send(NULL, &dummy, 0U),
                              "INV-Q-NULL-ASSERT-SEND-HANDLE");
    TEST_ASSERT_KASSERT_FIRES(rtos_queue_send(g_q, NULL, 0U),
                              "INV-Q-NULL-ASSERT-SEND-ITEM");
    TEST_ASSERT_KASSERT_FIRES(rtos_queue_receive(NULL, &dummy, 0U),
                              "INV-Q-NULL-ASSERT-RECV-HANDLE");
    TEST_ASSERT_KASSERT_FIRES(rtos_queue_receive(g_q, NULL, 0U),
                              "INV-Q-NULL-ASSERT-RECV-BUFFER");
#else
    TEST_ASSUME(false, "null_input_asserts requires RTOS_ASSERT_ENABLED");
#endif
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_q_cases[] = {
    &TEST_CASE_REF(queue, consumer_blocks_on_empty),
    &TEST_CASE_REF(queue, producer_blocks_on_full),
    &TEST_CASE_REF(queue, fifo_ordering),
    &TEST_CASE_REF(queue, nonblocking_full_returns_error),
    &TEST_CASE_REF(queue, count_stays_in_bounds),
    &TEST_CASE_REF(queue, full_send_fires_block_hook),
    &TEST_CASE_REF(queue, send_fires_send_hook),
    &TEST_CASE_REF(queue, receive_fires_receive_hook),
    &TEST_CASE_REF(queue, empty_receive_fires_block_empty_hook),
    &TEST_CASE_REF(queue, null_input_asserts),
};

TEST_SUITE_DEFINE(queue, g_q_cases,
                  .setup  = queue_setup,
                  .before = queue_before);

int main(void)
{
    return test_runtime_main(&test_suite_queue);
}
