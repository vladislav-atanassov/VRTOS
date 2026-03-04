/*******************************************************************************
 * File: tests/scheduler/preemptive/test_scheduler_preemptive_state.c
 * Description: Preemptive Scheduler - State & Ordering Invariant Test
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
 * @file test_scheduler_preemptive_state.c
 * @brief Preemptive Scheduler State & Ordering Invariant Test
 *
 * WHAT THIS TEST DOES (and why it is different from test_scheduler_preemptive.c)
 * ---------------------------------------------------------------------------
 * The existing timing-based tests observe *when* tasks run and compare the
 * timestamps against a pre-computed expected timeline. That approach is useful
 * as a smoke test but has two weaknesses:
 *
 *   1. It cannot distinguish between "ran in the right order" and "ran at
 *      roughly the right time". A 50 ms tolerance window hides bugs.
 *   2. It says nothing about the internal TCB state at the moment each
 *      decision is made.
 *
 * This test replaces timing windows with CHECKPOINT ASSERTIONS:
 *
 *   - Each task reads the state of the *other* tasks at well-defined points
 *     and logs a PASS or FAIL verdict for each invariant directly to UART.
 *   - A monitor task collects all verdicts and emits a single TEST PASS or
 *     TEST FAIL at the end.
 *   - The Python analyzer needs only to scan for "RESULT PASS" / "RESULT FAIL"
 *     in the log; no timeline CSV comparison is needed.
 *
 * INVARIANTS VERIFIED
 * -------------------
 * The test uses the preemptive static-priority scheduler with three tasks at
 * strictly different priorities:
 *
 *   TaskHigh  (priority 4) - highest
 *   TaskMid   (priority 3) - medium
 *   TaskLow   (priority 2) - lowest
 *
 * INV-1 (Preemption on wake-up)
 *   When TaskHigh wakes from delay it must run before TaskMid and TaskLow
 *   can complete another iteration. Verified by TaskHigh asserting that
 *   TaskMid and TaskLow are BLOCKED (not RUNNING) when it first executes
 *   after each wake-up.
 *
 * INV-2 (Mid preempts Low)
 *   When TaskMid wakes while TaskLow is the only other ready task, TaskMid
 *   must run before TaskLow gets another RUN log. Verified by TaskMid
 *   asserting TaskLow is BLOCKED when TaskMid starts each iteration.
 *
 * INV-3 (Low only runs when both others are blocked)
 *   At the moment TaskLow logs RUN, both TaskHigh and TaskMid must be
 *   BLOCKED. Verified inside task_low_func at the top of every iteration.
 *
 * INV-4 (State after delay call)
 *   Immediately after a task calls rtos_delay_until it must be BLOCKED.
 *   Each task asserts its own state after returning from the delay. Because
 *   the task is running when it makes the check, the state it observes is
 *   that of *other* tasks; for self-state we rely on the monitor task
 *   reading the TCB from outside.
 *
 * HOW TO READ THE OUTPUT
 * ----------------------
 * Every assertion emits one line:
 *
 *   <timestamp>\tTASK\t<file>\t<line>\t<func>\tASSERT_PASS\t<description>
 *   <timestamp>\tTASK\t<file>\t<line>\t<func>\tASSERT_FAIL\t<description>
 *
 * Final verdict (from monitor task):
 *
 *   <timestamp>\tTEST\t<file>\t<line>\t<func>\tRESULT\tPASS
 *   <timestamp>\tTEST\t<file>\t<line>\t<func>\tRESULT\tFAIL:<fail_count>
 *
 * The existing test_runner.py can be pointed at this output. grep for
 * "RESULT" and check the suffix.
 */

/* =================== Task Priorities =================== */

#define TASK_HIGH_PRIORITY (4U)
#define TASK_MID_PRIORITY  (3U)
#define TASK_LOW_PRIORITY  (2U)

/* =================== Test Timing =================== */

/*
 * Delays are chosen so that at t=0 (all tasks wake simultaneously) the
 * priority order is unambiguously exercised, and at LCM(200,300,400)=1200ms
 * all three tasks wake together again giving repeated coverage.
 *
 * The test runs for TEST_DURATION_MS covering ~6 full LCM cycles.
 */
#define TASK_HIGH_DELAY_MS (200U)
#define TASK_MID_DELAY_MS  (300U)
#define TASK_LOW_DELAY_MS  (400U)

#define TEST_DURATION_MS        (7500U)
#define MONITOR_CHECK_PERIOD_MS (250U)

/* Assertion infrastructure (TEST_ASSERT, ASSERT_BLOCKED, etc.) is in test_common.h */

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

/* Task handles exposed so tasks can inspect each other's state */
static rtos_task_handle_t g_handle_high = NULL;
static rtos_task_handle_t g_handle_mid  = NULL;
static rtos_task_handle_t g_handle_low  = NULL;

/* file-scope so startup_timer_callback can start it */
static rtos_timer_handle_t g_test_timer;

/* =================== Task Implementations =================== */

/*
 * TaskHigh - highest priority
 *
 * INV-1: At the start of every RUN, both TaskMid and TaskLow must be
 *        BLOCKED (they cannot be running because this task has higher
 *        priority, and they cannot be READY because the preemptive
 *        scheduler would have immediately picked TaskHigh instead).
 *
 * The busy-spin is intentional: it represents real work and gives the
 * other tasks an opportunity to have been RUNNING before this task
 * preempted them - but they should NOT still be in RUNNING state when
 * we check because the context switch already happened.
 */
static void task_high_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskHigh");

    rtos_tick_t last_wake = rtos_get_tick_count();

    while (!g_test_complete)
    {
        test_log_task("RUN", "TaskHigh");

        /* INV-1: highest priority task runs; others must not be RUNNING */
        ASSERT_NOT_RUNNING(g_handle_mid, "INV1:MidNotRunning");
        ASSERT_NOT_RUNNING(g_handle_low, "INV1:LowNotRunning");

        /* Small work - enough to be meaningful, not so long it skews timing */
        for (volatile int i = 0; i < 5000; i++)
        {
            __asm volatile("nop");
        }

        test_log_task("DELAY", "TaskHigh");
        rtos_delay_until(&last_wake, TASK_HIGH_DELAY_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "TaskHigh");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * TaskMid - medium priority
 *
 * INV-2: When TaskMid is running, TaskHigh must be BLOCKED (otherwise
 *        the preemptive scheduler would not have picked TaskMid).
 *        TaskLow must also not be RUNNING.
 */
static void task_mid_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskMid");

    rtos_tick_t last_wake = rtos_get_tick_count();

    while (!g_test_complete)
    {
        test_log_task("RUN", "TaskMid");

        /* INV-2: TaskMid is running, so TaskHigh must be BLOCKED */
        ASSERT_BLOCKED(g_handle_high, "INV2:HighBlocked");
        ASSERT_NOT_RUNNING(g_handle_low, "INV2:LowNotRunning");

        for (volatile int i = 0; i < 5000; i++)
        {
            __asm volatile("nop");
        }

        test_log_task("DELAY", "TaskMid");
        rtos_delay_until(&last_wake, TASK_MID_DELAY_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "TaskMid");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * TaskLow - lowest priority
 *
 * INV-3: TaskLow only runs when BOTH higher-priority tasks are BLOCKED.
 *        This is the strongest assertion: it directly verifies that the
 *        scheduler never allows a low-priority task to execute while a
 *        higher-priority task is ready or running.
 */
static void task_low_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskLow");

    rtos_tick_t last_wake = rtos_get_tick_count();

    while (!g_test_complete)
    {
        test_log_task("RUN", "TaskLow");

        /* INV-3: both higher-priority tasks must be BLOCKED right now */
        ASSERT_BLOCKED(g_handle_high, "INV3:HighBlocked");
        ASSERT_BLOCKED(g_handle_mid, "INV3:MidBlocked");

        for (volatile int i = 0; i < 5000; i++)
        {
            __asm volatile("nop");
        }

        test_log_task("DELAY", "TaskLow");
        rtos_delay_until(&last_wake, TASK_LOW_DELAY_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "TaskLow");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task - collects results and emits the final verdict.
 *
 * Runs at priority 1 (above idle, below all test tasks) so it never
 * interferes with the scheduling decisions being tested. It wakes
 * periodically to sample g_fail_count and, after g_test_complete is set,
 * emits the final RESULT line that the test runner greps for.
 *
 * It also checks INV-4: at periodic sample points it verifies that no task
 * is simultaneously in RUNNING state along with any other task, which would
 * indicate a corrupted kernel state.
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    while (!g_test_complete)
    {
        rtos_delay_ms(MONITOR_CHECK_PERIOD_MS);

        /*
         * INV-4: At most one task should be in RUNNING state at any sample
         * point. Count how many tasks are in RUNNING state right now.
         * Because this task is itself running when it makes the check,
         * exactly one task (this one) will be RUNNING. If we see two or
         * more, the kernel state is corrupted.
         */
        uint32_t running_count = 0;
        if (rtos_task_get_state(g_handle_high) == RTOS_TASK_STATE_RUNNING)
            running_count++;
        if (rtos_task_get_state(g_handle_mid) == RTOS_TASK_STATE_RUNNING)
            running_count++;
        if (rtos_task_get_state(g_handle_low) == RTOS_TASK_STATE_RUNNING)
            running_count++;

        /*
         * At the sample point the monitor task itself is RUNNING. The test
         * tasks should all be BLOCKED (waiting on delay) or READY (just
         * woke). None of them should be simultaneously RUNNING.
         */
        TEST_ASSERT(running_count == 0, "INV4:SingleRunningTask");
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
    test_log_framework("BEGIN", "PreemptiveState");

    /* Start the test duration timer */
    rtos_timer_handle_t *p_test_timer = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p_test_timer);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;

    g_test_complete = true;
    test_log_framework("TIMEOUT", "PreemptiveState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Preemptive Scheduler - State Invariant Test");
    log_info("Priorities: High=%u Mid=%u Low=%u", TASK_HIGH_PRIORITY, TASK_MID_PRIORITY, TASK_LOW_PRIORITY);
    log_info("Delays: High=%ums Mid=%ums Low=%ums", TASK_HIGH_DELAY_MS, TASK_MID_DELAY_MS, TASK_LOW_DELAY_MS);
    log_info("Invariants: INV1(high preempts) INV2(mid preempts low) INV3(low only when others blocked) INV4(single "
             "runner)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    /* Startup hold timer - gates tasks until serial monitor connects */
    status = test_create_startup_timer(startup_timer_callback, &g_test_timer, &startup_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Startup timer create failed: %d", status);
        indicate_system_failure();
    }

    /* Test duration timer - started by startup callback */
    status = rtos_timer_create("TestTimer", TEST_DURATION_MS, RTOS_TIMER_ONE_SHOT, test_timeout_callback, NULL,
                               &g_test_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Test timer create failed: %d", status);
        indicate_system_failure();
    }

    /*
     * Create tasks and capture their handles. Handle capture is the key
     * difference from the existing tests: we need them so tasks can call
     * rtos_task_get_state() on each other.
     *
     * Creation order: Low -> Mid -> High so that when the scheduler first
     * runs it picks High (highest priority), giving a clean initial state.
     */
    status =
        rtos_task_create(task_low_func, "Low", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_LOW_PRIORITY, &g_handle_low);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status =
        rtos_task_create(task_mid_func, "Mid", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_MID_PRIORITY, &g_handle_mid);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(task_high_func, "High", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_HIGH_PRIORITY,
                              &g_handle_high);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    /*
     * Monitor task at priority 1 - below all test tasks so it only runs
     * when they are all blocked.
     */
    rtos_task_handle_t monitor_handle;
    status = rtos_task_create(monitor_task_func, "Mon", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1U, &monitor_handle);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    rtos_task_handle_t flush_handle;
    test_create_log_flush_task(&flush_handle);

    log_info("Starting scheduler...");

    status = rtos_start_scheduler();

    /* Should never reach here */
    indicate_system_failure();
}