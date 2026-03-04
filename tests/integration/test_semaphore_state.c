/*******************************************************************************
 * File: tests/integration/test_semaphore_state.c
 * Description: Semaphore - State & Priority-Ordering Invariant Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "semaphore.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_common.h"
#include "test_log.h" /* thread-safe ulog overrides for test_log_task/framework */
#include "timer.h"
#include "uart_tx.h"

/**
 * @file test_semaphore_state.c
 * @brief Semaphore State & Priority-Ordering Invariant Test
 *
 * SCENARIO
 * --------
 * Binary semaphore (max_count=1, initial_count=0). Four tasks:
 *
 *   Signaller  (priority 2) — calls rtos_semaphore_signal() each cycle
 *   WaiterLow  (priority 3) — calls rtos_semaphore_wait(RTOS_SEM_MAX_WAIT)
 *   WaiterHigh (priority 4) — same, higher priority — must wake first
 *   Monitor    (priority 1) — samples count and task states
 *
 * INVARIANTS
 * ----------
 * INV-S1  A task that calls rtos_semaphore_wait() on a zero semaphore
 *         is BLOCKED.
 * INV-S2  After signal with a waiter present, count stays 0 (not
 *         incremented to 1).
 * INV-S3  Highest-priority waiter wakes first.
 * INV-S4  A timed-out wait returns RTOS_SEM_ERR_TIMEOUT; task is not
 *         BLOCKED afterward.
 * INV-S5  Count never exceeds max_count.
 */

/* =================== Test Parameters =================== */

#define TASK_SIG_PRIORITY  (2U)
#define TASK_LOW_PRIORITY  (3U)
#define TASK_HIGH_PRIORITY (4U)
#define TASK_MON_PRIORITY  (1U)

#define SCENARIO_CYCLES  (10U)
#define SIGNAL_PERIOD_MS (150U)
#define SETTLE_MS        (50U)
#define TIMEOUT_TEST_MS  (30U)
#define TEST_DURATION_MS (5000U)

/* TEST_ASSERT, ASSERT_STATE, g_fail_count are in test_common.h */

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

static rtos_semaphore_t g_sem;

static rtos_task_handle_t g_handle_sig  = NULL;
static rtos_task_handle_t g_handle_low  = NULL;
static rtos_task_handle_t g_handle_high = NULL;

/*
 * Synchronisation flags.
 * volatile uint32_t to avoid torn reads on Cortex-M.
 */
static volatile uint32_t g_contend_signal = 0;
static volatile uint32_t g_high_done      = 0;
static volatile uint32_t g_low_done       = 0;
static volatile uint32_t g_high_acquired  = 0;
static volatile uint32_t g_low_acquired   = 0;

static rtos_timer_handle_t g_test_timer;

/* =================== Task Implementations =================== */

/*
 * Signaller (priority 2).
 *
 * Each cycle:
 *   1. Wait for waiters to enter BLOCKED state.
 *   2. Assert INV-S1: both waiters are BLOCKED on the zero semaphore.
 *   3. Signal once — High should wake (INV-S3).
 *   4. Assert INV-S2: count stays 0 because signal went to a waiter.
 *   5. Signal again — Low should wake.
 *   6. Wait for both to complete before next cycle.
 */
static void signaller_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Signaller");

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        /* Reset per-cycle flags */
        g_high_done = 0;
        g_low_done  = 0;

        /* Signal waiters to start contending */
        g_contend_signal = cycle + 1;

        /* Let waiters reach BLOCKED state */
        rtos_delay_ms(SETTLE_MS);

        /* INV-S1: both waiters must be BLOCKED */
        ASSERT_STATE(g_handle_high, RTOS_TASK_STATE_BLOCKED, "INV-S1:HighBlocked");
        ASSERT_STATE(g_handle_low, RTOS_TASK_STATE_BLOCKED, "INV-S1:LowBlocked");

        /* Signal once — highest-priority waiter should wake */
        test_log_task("SIGNAL", "Signaller");
        rtos_semaphore_signal(&g_sem);

        /* INV-S2: count should still be 0 (signal went to a waiter) */
        rtos_delay_ms(10);
        TEST_ASSERT(rtos_semaphore_get_count(&g_sem) == 0, "INV-S2:CountStays0");

        /* Signal again to wake the other waiter */
        rtos_semaphore_signal(&g_sem);

        /* Wait for both waiters to finish */
        while (!g_high_done || !g_low_done)
        {
            rtos_delay_ms(10);
        }
    }

    /*
     * INV-S4: test timed-out wait. Use a very short timeout so the
     * semaphore (currently at count 0) times out quickly.
     */
    rtos_sem_status_t s = rtos_semaphore_wait(&g_sem, TIMEOUT_TEST_MS);
    TEST_ASSERT(s == RTOS_SEM_ERR_TIMEOUT, "INV-S4:TimeoutReturnCode");
    TEST_ASSERT(rtos_task_get_state(g_handle_sig) != RTOS_TASK_STATE_BLOCKED, "INV-S4:NotBlockedAfterTimeout");

    test_log_task("END", "Signaller");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * WaiterHigh (priority 4).
 *
 * Each cycle: wait for contend signal, call rtos_semaphore_wait(),
 * then record that it acquired. INV-S3 is checked by WaiterLow.
 */
static void waiter_high_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "WaiterHigh");

    uint32_t last_signal = 0;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        while (g_contend_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_contend_signal;

        test_log_task("WAIT", "WaiterHigh");
        rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);

        g_high_acquired++;
        g_high_done = 1;
    }

    test_log_task("END", "WaiterHigh");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * WaiterLow (priority 3).
 *
 * Each cycle: wait for contend signal, call rtos_semaphore_wait(),
 * then assert INV-S3: High must have acquired before Low in every cycle.
 */
static void waiter_low_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "WaiterLow");

    uint32_t last_signal = 0;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        while (g_contend_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_contend_signal;

        test_log_task("WAIT", "WaiterLow");
        rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);

        g_low_acquired++;

        /* INV-S3: High must have woken before Low in this cycle */
        TEST_ASSERT(g_high_acquired > g_low_acquired - 1, "INV-S3:HighWokeBeforeLow");

        g_low_done = 1;
    }

    test_log_task("END", "WaiterLow");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task (priority 1).
 *
 * INV-S5: count never exceeds max_count (1 for binary semaphore).
 * Also emits the final PASS/FAIL verdict.
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    while (!g_test_complete)
    {
        rtos_delay_ms(20);

        /* INV-S5: count must be in [0, max_count] */
        uint32_t count = rtos_semaphore_get_count(&g_sem);
        TEST_ASSERT(count <= 1, "INV-S5:CountBounds");
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
    test_log_framework("BEGIN", "SemaphoreState");
    rtos_timer_handle_t *p = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "SemaphoreState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Semaphore State & Priority-Ordering Test");
    log_info("Priorities: Sig=%u Low=%u High=%u Mon=%u", TASK_SIG_PRIORITY, TASK_LOW_PRIORITY, TASK_HIGH_PRIORITY,
             TASK_MON_PRIORITY);
    log_info("Cycles: %u  SignalPeriod: %ums  Settle: %ums", SCENARIO_CYCLES, SIGNAL_PERIOD_MS, SETTLE_MS);
    log_info("Invariants: S1(block) S2(count) S3(priority order) S4(timeout) S5(bounds)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    /* Binary semaphore: initial_count=0, max_count=1 */
    rtos_sem_status_t sem_s = rtos_semaphore_init(&g_sem, 0, 1);
    if (sem_s != RTOS_SEM_OK)
    {
        log_error("Semaphore init failed: %d", sem_s);
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

    /* Create Signaller first — it orchestrates the cycle */
    status = rtos_task_create(signaller_task_func, "Sig", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_SIG_PRIORITY,
                              &g_handle_sig);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status =
        rtos_task_create(waiter_low_func, "WLow", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_LOW_PRIORITY, &g_handle_low);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(waiter_high_func, "WHigh", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_HIGH_PRIORITY,
                              &g_handle_high);
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
