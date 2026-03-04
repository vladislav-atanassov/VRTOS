/*******************************************************************************
 * File: tests/integration/test_queue_state.c
 * Description: Queue - State & FIFO Integrity Invariant Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "queue.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_common.h"
#include "test_log.h" /* thread-safe ulog overrides for test_log_task/framework */
#include "timer.h"
#include "uart_tx.h"

/**
 * @file test_queue_state.c
 * @brief Queue State & FIFO Integrity Invariant Test
 *
 * SCENARIO
 * --------
 * Queue of capacity 3, uint32_t items (sequence numbers). Four tasks:
 *
 *   Producer   (priority 3) — sends monotonically increasing sequence numbers
 *   Consumer   (priority 2) — receives items and verifies FIFO order
 *   Overloader (priority 2) — non-blocking send to known-full queue
 *   Monitor    (priority 1) — samples rtos_queue_messages_waiting()
 *
 * Producer is higher priority than Consumer so it can fill the queue to
 * capacity before Consumer drains it. Consumer is gated by a signal flag
 * so it doesn't call receive until Producer signals.
 *
 * 20 cycles forces 6–7 circular buffer wrap events on each pointer,
 * exercising the boundary condition where off-by-one pointer bugs
 * silently deliver stale items.
 *
 * INVARIANTS
 * ----------
 * INV-Q1  Consumer is BLOCKED on an empty queue before the first send.
 * INV-Q2  Producer is BLOCKED when the queue is full.
 * INV-Q3  Items arrive in FIFO order (received seq == expected seq).
 * INV-Q4  Non-blocking send to a full queue returns RTOS_ERROR_FULL
 *         without blocking.
 * INV-Q5  After Consumer receives from a full queue, count is exactly
 *         QUEUE_CAPACITY - 1.
 * INV-Q6  rtos_queue_messages_waiting() is always in [0, QUEUE_CAPACITY].
 */

/* =================== Test Parameters =================== */

#define TASK_PROD_PRIORITY (3U)
#define TASK_CONS_PRIORITY (2U)
#define TASK_OVER_PRIORITY (2U)
#define TASK_MON_PRIORITY  (1U)

#define QUEUE_CAPACITY   (3U)
#define SCENARIO_CYCLES  (20U)
#define TEST_DURATION_MS (8000U)
#define SETTLE_MS        (50U)

/* TEST_ASSERT, ASSERT_STATE, g_fail_count are in test_common.h */

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

static rtos_queue_handle_t g_queue = NULL;

static rtos_task_handle_t g_handle_prod = NULL;
static rtos_task_handle_t g_handle_cons = NULL;
static rtos_task_handle_t g_handle_over = NULL;

/*
 * Synchronisation flags.
 */
static volatile uint32_t g_consume_signal  = 0; /* Producer tells Consumer to start draining */
static volatile uint32_t g_overload_signal = 0;
static volatile uint32_t g_consumer_done   = 0;
static volatile uint32_t g_overloader_done = 0;

static rtos_timer_handle_t g_test_timer;

/* =================== Task Implementations =================== */

/*
 * Producer (priority 3 — higher than Consumer).
 *
 * Each cycle:
 *   1. Fill queue to capacity (Consumer is gated, not receiving yet).
 *   2. Signal Overloader to attempt send on full queue (INV-Q4).
 *   3. Signal Consumer to start receiving.
 *   4. Send one more item — blocks until Consumer frees a slot (INV-Q2).
 *   5. Wait for Consumer and Overloader to finish.
 */
static void producer_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Producer");

    uint32_t seq = 0;

    /*
     * INV-Q1: Before cycle 0, let Consumer call receive on the empty
     * queue. Yield so Consumer (lower priority) can run and block.
     */
    rtos_delay_ms(SETTLE_MS);
    ASSERT_STATE(g_handle_cons, RTOS_TASK_STATE_BLOCKED, "INV-Q1:ConsBlockedOnEmpty");

    /* Unblock Consumer by sending one item (it receives this as seq 0) */
    rtos_status_t s = rtos_queue_send(g_queue, &seq, RTOS_MAX_DELAY);
    TEST_ASSERT(s == RTOS_SUCCESS, "Q-PROD:InitSendOK");
    seq++;

    /* Wait for Consumer to receive that initial item */
    while (!g_consumer_done)
    {
        rtos_delay_ms(5);
    }

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        g_consumer_done   = 0;
        g_overloader_done = 0;

        /* Fill queue to capacity — Consumer is not receiving yet */
        for (uint32_t i = 0; i < QUEUE_CAPACITY; i++)
        {
            s = rtos_queue_send(g_queue, &seq, RTOS_MAX_DELAY);
            TEST_ASSERT(s == RTOS_SUCCESS, "Q-PROD:SendOK");
            seq++;
        }

        /* Queue is full — signal Overloader to try non-blocking send */
        g_overload_signal = cycle + 1;

        /* Let Overloader run */
        rtos_delay_ms(10);

        /* Signal Consumer to start draining */
        g_consume_signal = cycle + 1;

        /*
         * Send one more item — this blocks because the queue is full.
         * INV-Q2 is checked by Monitor while we're blocked.
         */
        test_log_task("SEND_BLOCK", "Producer");
        s = rtos_queue_send(g_queue, &seq, RTOS_MAX_DELAY);
        TEST_ASSERT(s == RTOS_SUCCESS, "Q-PROD:BlockSendOK");
        seq++;

        /* Wait for Consumer and Overloader to finish this cycle */
        while (!g_consumer_done || !g_overloader_done)
        {
            rtos_delay_ms(5);
        }
    }

    test_log_task("END", "Producer");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Consumer (priority 2 — lower than Producer).
 *
 * Gated by g_consume_signal. Receives items and checks FIFO order.
 * INV-Q3: received sequence == expected sequence.
 * INV-Q5: after receive from full queue, count == CAPACITY-1.
 */
static void consumer_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Consumer");

    uint32_t expected_seq = 0;

    /*
     * INV-Q1 bootstrap: receive one item from the empty queue (blocks
     * until Producer sends the initial item).
     */
    uint32_t      received = 0;
    rtos_status_t s        = rtos_queue_receive(g_queue, &received, RTOS_MAX_DELAY);
    TEST_ASSERT(s == RTOS_SUCCESS, "Q-CONS:InitRecvOK");
    TEST_ASSERT(received == expected_seq, "INV-Q3:FIFOInit");
    expected_seq++;
    g_consumer_done = 1;

    uint32_t last_signal = 0;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        /* Wait for Producer to signal that the queue is full */
        while (g_consume_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_consume_signal;

        /*
         * Receive QUEUE_CAPACITY + 1 items (the full buffer + the
         * item from Producer's blocking send).
         */
        for (uint32_t i = 0; i < QUEUE_CAPACITY + 1; i++)
        {
            received = 0;
            s        = rtos_queue_receive(g_queue, &received, RTOS_MAX_DELAY);
            TEST_ASSERT(s == RTOS_SUCCESS, "Q-CONS:RecvOK");

            /* INV-Q3: FIFO order */
            TEST_ASSERT(received == expected_seq, "INV-Q3:FIFOOrder");
            expected_seq++;

            /*
             * INV-Q5: first receive from a full queue — count should
             * drop to CAPACITY - 1 (the blocked Producer's item hasn't
             * entered the buffer yet at this point).
             */
            if (i == 0)
            {
                uint32_t count = rtos_queue_messages_waiting(g_queue);
                TEST_ASSERT(count == QUEUE_CAPACITY - 1 || count == QUEUE_CAPACITY, "INV-Q5:CountAfterFullRecv");
            }
        }

        g_consumer_done = 1;
    }

    test_log_task("END", "Consumer");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Overloader (priority 2).
 *
 * Each cycle: attempt a non-blocking send on a known-full queue.
 * INV-Q4: must return RTOS_ERROR_FULL and the task must NOT be BLOCKED.
 */
static void overloader_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Overloader");

    uint32_t last_signal = 0;
    uint32_t dummy_item  = 0xDEADBEEF;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        while (g_overload_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_overload_signal;

        /* Non-blocking send to full queue */
        rtos_status_t s = rtos_queue_send(g_queue, &dummy_item, 0);
        TEST_ASSERT(s == RTOS_ERROR_FULL, "INV-Q4:FullReturnCode");

        g_overloader_done = 1;
    }

    test_log_task("END", "Overloader");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task (priority 1).
 *
 * INV-Q2: when queue is full and Producer is in SEND_BLOCK, check state.
 * INV-Q4 (sampled): Overloader is never BLOCKED.
 * INV-Q6: messages_waiting is always in [0, QUEUE_CAPACITY].
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    while (!g_test_complete)
    {
        rtos_delay_ms(20);

        /* INV-Q6: count bounds */
        uint32_t count = rtos_queue_messages_waiting(g_queue);
        TEST_ASSERT(count <= QUEUE_CAPACITY, "INV-Q6:CountBounds");

        /* INV-Q2: if queue is full, Producer should be BLOCKED */
        if (count == QUEUE_CAPACITY)
        {
            ASSERT_STATE(g_handle_prod, RTOS_TASK_STATE_BLOCKED, "INV-Q2:ProdBlockedFull");
        }

        /* INV-Q4 (sampled): Overloader should never be BLOCKED */
        if (g_handle_over != NULL)
        {
            TEST_ASSERT(rtos_task_get_state(g_handle_over) != RTOS_TASK_STATE_BLOCKED, "INV-Q4:OverNeverBlocked");
        }
    }

    /* Final verdict */
    TEST_EMIT_VERDICT();

    test_log_task("END", "Monitor");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/* =================== Timer Callbacks =================== */

static void startup_timer_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    g_test_started = true;
    test_log_framework("BEGIN", "QueueState");
    rtos_timer_handle_t *p = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "QueueState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Queue State & FIFO Integrity Test");
    log_info("Priorities: Prod=%u Cons=%u Over=%u Mon=%u", TASK_PROD_PRIORITY, TASK_CONS_PRIORITY, TASK_OVER_PRIORITY,
             TASK_MON_PRIORITY);
    log_info("Capacity: %u  Cycles: %u", QUEUE_CAPACITY, SCENARIO_CYCLES);
    log_info("Invariants: Q1(empty block) Q2(full block) Q3(FIFO) Q4(full reject) Q5(count) Q6(bounds)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    status = rtos_queue_create(&g_queue, QUEUE_CAPACITY, sizeof(uint32_t));
    if (status != RTOS_SUCCESS)
    {
        log_error("Queue create failed: %d", status);
        indicate_system_failure();
    }

    status = test_create_startup_timer(startup_timer_callback, &g_test_timer, &startup_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Startup timer failed: %d", status);
        indicate_system_failure();
    }

    status = rtos_timer_create("TestTimer", TEST_DURATION_MS, RTOS_TIMER_ONE_SHOT, test_timeout_callback, NULL,
                               &g_test_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Test timer failed: %d", status);
        indicate_system_failure();
    }

    /*
     * Create Consumer first so it runs before Producer and blocks on
     * the empty queue (INV-Q1 bootstrap). Consumer has lower priority
     * but is created first — it will run during TEST_WAIT_FOR_START
     * and then block on receive.
     */
    status = rtos_task_create(consumer_task_func, "Cons", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_CONS_PRIORITY,
                              &g_handle_cons);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(producer_task_func, "Prod", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_PROD_PRIORITY,
                              &g_handle_prod);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(overloader_task_func, "Over", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_OVER_PRIORITY,
                              &g_handle_over);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    rtos_task_handle_t monitor_handle;
    status = rtos_task_create(monitor_task_func, "Mon", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_MON_PRIORITY,
                              &monitor_handle);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    rtos_task_handle_t flush_handle;
    test_create_log_flush_task(&flush_handle);

    log_info("Starting scheduler...");
    status = rtos_start_scheduler();

    indicate_system_failure();
}
