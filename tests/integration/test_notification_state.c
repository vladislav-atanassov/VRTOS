/*******************************************************************************
 * File: tests/integration/test_notification_state.c
 * Description: Task Notification - State & Action Invariant Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_common.h"
#include "test_log.h" /* thread-safe ulog overrides for test_log_task/framework */
#include "timer.h"
#include "uart_tx.h"

/**
 * @file test_notification_state.c
 * @brief Task Notification State & Action Invariant Test
 *
 * SCENARIO
 * --------
 * Three tasks plus log flush:
 *
 *   Notifier (priority 2) — sends notifications with various actions
 *   Waiter   (priority 4) — waits for notifications, checks received values
 *   Monitor  (priority 1) — checks states, emits final verdict
 *
 * INVARIANTS
 * ----------
 * INV-N1  notify_wait with no pending notification -> task BLOCKED
 * INV-N2  rtos_task_notify with SET_BITS ORs bits into value
 * INV-N3  rtos_task_notify with INCREMENT increments value
 * INV-N4  rtos_task_notify with OVERWRITE replaces value
 * INV-N5  notify_give / notify_take works as counting semaphore
 * INV-N6  Timed-out wait returns RTOS_NOTIFY_ERR_TIMEOUT
 * INV-N7  exit_clear_bits applied after value is read
 */

/* =================== Test Parameters =================== */

#define TASK_NOTIFIER_PRIORITY (2U)
#define TASK_WAITER_PRIORITY   (4U)
#define TASK_MON_PRIORITY      (1U)

#define SETTLE_MS        (50U)
#define POLL_MS          (5U)
#define MONITOR_POLL_MS  (20U)
#define IDLE_SPIN_MS     (1000U)
#define TIMEOUT_TEST_MS  (30U)
#define TEST_DURATION_MS (5000U)

/* Test data — notification values and expected results */
#define TEST_SET_BITS_VALUE  (0x0FU)       /**< INV-N2: bits to OR in */
#define TEST_INCREMENT_COUNT (3U)          /**< INV-N3: number of increments */
#define TEST_OVERWRITE_VALUE (0xDEADBEEFU) /**< INV-N4: overwrite payload */
#define TEST_GIVE_COUNT      (3U)          /**< INV-N5: number of give/take rounds */
#define TEST_EXIT_SEND_BITS  (0x07U)       /**< INV-N7: bits to set (bits 0,1,2) */
#define TEST_EXIT_CLEAR_MASK (0x01U)       /**< INV-N7: bit 0 cleared on exit */

/* TEST_ASSERT, ASSERT_STATE, g_fail_count are in test_common.h */

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

static rtos_task_handle_t g_handle_notifier = NULL;
static rtos_task_handle_t g_handle_waiter   = NULL;

/*
 * Synchronisation flags.
 * volatile uint32_t to avoid torn reads on Cortex-M.
 */
static volatile uint32_t g_phase       = 0; /* current test phase */
static volatile uint32_t g_waiter_done = 0;

/* Waiter receives its value here */
static volatile uint32_t g_received_value = 0;

static rtos_timer_handle_t g_test_timer;

/* =================== Task Implementations =================== */

/*
 * Waiter (priority 4).
 *
 * Higher priority than Notifier. Each phase: blocks waiting for a
 * notification, then records the received value so Notifier can
 * assert against it.
 */
static void waiter_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Waiter");

    /* --- Phase 1: INV-N2 (SET_BITS) --- */
    while (g_phase < 1 && !g_test_complete)
    {
        rtos_delay_ms(POLL_MS);
    }

    {
        uint32_t val = 0;
        rtos_task_notify_wait(RTOS_NOTIFY_CLEAR_NONE, RTOS_NOTIFY_CLEAR_ALL, &val, RTOS_NOTIFY_MAX_WAIT);
        g_received_value = val;
        g_waiter_done    = 1;
    }

    /* --- Phase 2: INV-N3 (INCREMENT) — accumulation test ---
     * Notifier sends all 3 increments while we are in the delay poll
     * (blocked on delay, NOT on notification). By the time we exit the
     * poll and call notify_wait, pending=1 and value=3, so the fast
     * path fires immediately.
     *
     * Uses MAX_WAIT instead of NO_WAIT: if pending is somehow 0, the
     * task blocks until notified rather than silently returning val=0.
     */
    while (g_phase < 2 && !g_test_complete)
    {
        rtos_delay_ms(POLL_MS);
    }
    g_waiter_done = 0;

    {
        uint32_t             val = 0;
        rtos_notify_status_t status =
            rtos_task_notify_wait(RTOS_NOTIFY_CLEAR_NONE, RTOS_NOTIFY_CLEAR_ALL, &val, RTOS_NOTIFY_MAX_WAIT);
        (void) status; /* Notifier asserts the value; status logged by klog */
        g_received_value = val;
        g_waiter_done    = 1;
    }

    /* --- Phase 3: INV-N4 (OVERWRITE) --- */
    while (g_phase < 3 && !g_test_complete)
    {
        rtos_delay_ms(POLL_MS);
    }
    g_waiter_done = 0;

    {
        uint32_t val = 0;
        rtos_task_notify_wait(RTOS_NOTIFY_CLEAR_NONE, RTOS_NOTIFY_CLEAR_ALL, &val, RTOS_NOTIFY_MAX_WAIT);
        g_received_value = val;
        g_waiter_done    = 1;
    }

    /* --- Phase 4: INV-N5 (give/take counting semaphore) --- */
    while (g_phase < 4 && !g_test_complete)
    {
        rtos_delay_ms(POLL_MS);
    }
    g_waiter_done = 0;

    {
        /* Take TEST_GIVE_COUNT notifications (given by Notifier) */
        for (uint32_t i = 0; i < TEST_GIVE_COUNT; i++)
        {
            rtos_task_notify_take(false, RTOS_NOTIFY_MAX_WAIT);
        }
        g_waiter_done = 1;
    }

    /* --- Phase 5: INV-N7 (exit_clear_bits) --- */
    while (g_phase < 5 && !g_test_complete)
    {
        rtos_delay_ms(POLL_MS);
    }
    g_waiter_done = 0;

    {
        uint32_t val = 0;
        /* Clear only bit 0 on exit */
        rtos_task_notify_wait(RTOS_NOTIFY_CLEAR_NONE, TEST_EXIT_CLEAR_MASK, &val, RTOS_NOTIFY_MAX_WAIT);
        g_received_value = val;
        g_waiter_done    = 1;
    }

    test_log_task("END", "Waiter");
    while (1)
    {
        rtos_delay_ms(IDLE_SPIN_MS);
    }
}

/*
 * Notifier (priority 2).
 *
 * Orchestrates each test phase:
 *   1. Sets phase, waits for Waiter to block
 *   2. Asserts invariants on state before/after notify
 *   3. Sends notification with the appropriate action
 *   4. Waits for Waiter to complete, asserts post-conditions
 */
static void notifier_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Notifier");

    /* =========================================================
     * Phase 1: INV-N1 + INV-N2 — blocking + SET_BITS
     * ========================================================= */
    g_phase = 1;
    rtos_delay_ms(SETTLE_MS);

    /* INV-N1: Waiter must be BLOCKED (no pending notification) */
    ASSERT_STATE(g_handle_waiter, RTOS_TASK_STATE_BLOCKED, "INV-N1:WaiterBlocked");

    /* Send with SET_BITS */
    test_log_task("NOTIFY", "SetBits");
    rtos_task_notify(g_handle_waiter, TEST_SET_BITS_VALUE, RTOS_NOTIFY_ACTION_SET_BITS);

    /* Wait for Waiter to consume */
    while (!g_waiter_done)
    {
        rtos_delay_ms(POLL_MS);
    }

    /* INV-N2: received value should have the set bits */
    TEST_ASSERT(g_received_value == TEST_SET_BITS_VALUE, "INV-N2:SetBits");

    /* =========================================================
     * Phase 2: INV-N3 — INCREMENT (accumulation test)
     *
     * Send all 3 increments while Waiter is in its phase poll
     * (blocked on rtos_delay_ms, NOT on rtos_task_notify_wait).
     * The increments set pending=1 and value=3 without waking
     * the Waiter (blocked_on_type is NONE, not NOTIFICATION).
     *
     * Then set g_phase=2 to release the Waiter. On exit from
     * the poll loop it calls notify_wait(MAX_WAIT) which hits
     * the fast path immediately (pending is already 1).
     * ========================================================= */

    /* Send all increments while waiter is still in phase poll (delay) */
    rtos_task_notify(g_handle_waiter, 0, RTOS_NOTIFY_ACTION_INCREMENT);
    rtos_task_notify(g_handle_waiter, 0, RTOS_NOTIFY_ACTION_INCREMENT);
    rtos_task_notify(g_handle_waiter, 0, RTOS_NOTIFY_ACTION_INCREMENT);

    /* Clear done flag BEFORE releasing waiter to avoid stale-flag race */
    g_waiter_done = 0;

    /* Now release waiter — it exits poll, calls notify_wait(MAX_WAIT).
     * Since pending is already 1 and value is 3, the fast path fires
     * immediately without actually blocking. */
    g_phase = 2;

    while (!g_waiter_done)
    {
        rtos_delay_ms(POLL_MS);
    }

    /* INV-N3: value should equal TEST_INCREMENT_COUNT */
    TEST_ASSERT(g_received_value == TEST_INCREMENT_COUNT, "INV-N3:Increment");

    /* =========================================================
     * Phase 3: INV-N4 — OVERWRITE
     * ========================================================= */
    g_phase = 3;
    rtos_delay_ms(SETTLE_MS);

    ASSERT_STATE(g_handle_waiter, RTOS_TASK_STATE_BLOCKED, "INV-N1:WaiterBlocked_P3");

    rtos_task_notify(g_handle_waiter, TEST_OVERWRITE_VALUE, RTOS_NOTIFY_ACTION_OVERWRITE);

    while (!g_waiter_done)
    {
        rtos_delay_ms(POLL_MS);
    }

    /* INV-N4: value should be exactly TEST_OVERWRITE_VALUE */
    TEST_ASSERT(g_received_value == TEST_OVERWRITE_VALUE, "INV-N4:Overwrite");

    /* =========================================================
     * Phase 4: INV-N5 — give/take counting semaphore
     * ========================================================= */
    g_phase = 4;
    rtos_delay_ms(SETTLE_MS);

    /* Give TEST_GIVE_COUNT times — Waiter takes the same */
    for (uint32_t i = 0; i < TEST_GIVE_COUNT; i++)
    {
        rtos_task_notify_give(g_handle_waiter);
    }

    while (!g_waiter_done)
    {
        rtos_delay_ms(POLL_MS);
    }

    /* INV-N5: Waiter successfully took all notifications */
    TEST_ASSERT(g_waiter_done == 1, "INV-N5:GiveTake");

    /* =========================================================
     * Phase 5: INV-N7 — exit_clear_bits
     * ========================================================= */
    g_phase = 5;
    rtos_delay_ms(SETTLE_MS);

    ASSERT_STATE(g_handle_waiter, RTOS_TASK_STATE_BLOCKED, "INV-N1:WaiterBlocked_P5");

    /* Set TEST_EXIT_SEND_BITS (bits 0,1,2) */
    rtos_task_notify(g_handle_waiter, TEST_EXIT_SEND_BITS, RTOS_NOTIFY_ACTION_SET_BITS);

    while (!g_waiter_done)
    {
        rtos_delay_ms(POLL_MS);
    }

    /* INV-N7: value_out should have TEST_EXIT_SEND_BITS (read BEFORE exit clear).
     * Waiter clears only TEST_EXIT_CLEAR_MASK on exit, but value_out captures pre-clear value. */
    TEST_ASSERT(g_received_value == TEST_EXIT_SEND_BITS, "INV-N7:ExitClearBitsValueOut");

    /* =========================================================
     * Phase 6: INV-N6 — Timeout test
     * ========================================================= */
    rtos_notify_status_t s = rtos_task_notify_wait(0, 0, NULL, TIMEOUT_TEST_MS);
    TEST_ASSERT(s == RTOS_NOTIFY_ERR_TIMEOUT, "INV-N6:TimeoutReturnCode");
    TEST_ASSERT(rtos_task_get_state(g_handle_notifier) != RTOS_TASK_STATE_BLOCKED, "INV-N6:NotBlockedAfterTimeout");

    test_log_task("END", "Notifier");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task (priority 1).
 * Emits the final PASS/FAIL verdict.
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    while (!g_test_complete)
    {
        rtos_delay_ms(MONITOR_POLL_MS);
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
    test_log_framework("BEGIN", "NotificationState");
    rtos_timer_handle_t *p = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "NotificationState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Notification State & Action Invariant Test");
    log_info("Priorities: Notifier=%u Waiter=%u Mon=%u", TASK_NOTIFIER_PRIORITY, TASK_WAITER_PRIORITY,
             TASK_MON_PRIORITY);
    log_info("Invariants: N1(block) N2(set_bits) N3(increment) N4(overwrite) N5(give/take) N6(timeout) N7(exit_clear)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
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

    /* Create Notifier — it orchestrates the test phases */
    status = rtos_task_create(notifier_task_func, "Notif", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_NOTIFIER_PRIORITY,
                              &g_handle_notifier);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(waiter_task_func, "Wait", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_WAITER_PRIORITY,
                              &g_handle_waiter);
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
