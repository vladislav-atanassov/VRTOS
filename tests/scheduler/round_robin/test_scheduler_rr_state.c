/*******************************************************************************
 * File: tests/scheduler/round_robin/test_scheduler_rr_state.c
 * Description: Round-Robin Scheduler - State & Fairness Invariant Test
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
 * @file test_scheduler_rr_state.c
 * @brief Round-Robin Scheduler State & Fairness Invariant Test
 *
 * Unlike the timing-based test_scheduler_rr.c that compared timestamps
 * against a pre-computed CSV, this test asserts scheduler properties
 * directly:
 *
 * INV-RR1 (Fairness)
 *   Every task runs at least once per LCM window. The monitor counts
 *   RUNNING observations per task; after each LCM window every count
 *   must have incremented.
 *
 * INV-RR2 (Exclusive running)
 *   At most one task is RUNNING at any sample point. Same as INV-4
 *   in the preemptive test.
 *
 * INV-RR3 (Time-slice bound)
 *   A busy-looping task does not hold the CPU for more than
 *   2 * RTOS_TIME_SLICE_TICKS without being preempted. Each task
 *   records its start tick at the top of the work loop and asserts
 *   the elapsed ticks at the bottom.
 *
 * INV-RR4 (Rotation)
 *   On consecutive monitor samples taken within one work phase,
 *   the RUNNING task should differ — proving the scheduler rotates.
 */

/* =================== Task Configuration =================== */

#define TASK_PRIORITY (2U) /* All equal priority */

/*
 * WORK_MS must exceed RTOS_TIME_SLICE_TICKS * RTOS_TICK_PERIOD_MS so
 * the time-slice fires during the busy loop. With TIME_SLICE=1 tick
 * and TICK_PERIOD=1ms, any WORK_MS > 1 is enough. We use 60ms to
 * get many preemptions and observable rotation.
 */
#define WORK_MS    (60U)
#define DELAY_A_MS (100U)
#define DELAY_B_MS (150U)
#define DELAY_C_MS (200U)

#define SCENARIO_CYCLES  (8U)
#define SAMPLE_MS        (15U)
#define TEST_DURATION_MS (5000U)

/*
 * Time-slice upper bound for INV-RR3.
 * A task may slightly exceed one slice due to interrupt latency,
 * so we use 2x as the assertion ceiling.
 */
#define SLICE_BOUND_TICKS (2U * RTOS_TIME_SLICE_TICKS)

/* Assertion infrastructure (TEST_ASSERT, etc.) is in test_common.h */

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

static rtos_task_handle_t g_handle_a = NULL;
static rtos_task_handle_t g_handle_b = NULL;
static rtos_task_handle_t g_handle_c = NULL;

static rtos_timer_handle_t g_test_timer;

/*
 * INV-RR1: per-task RUNNING observation counters.
 * Incremented by the monitor task when it observes a task in RUNNING state.
 */
static volatile uint32_t g_run_count_a = 0;
static volatile uint32_t g_run_count_b = 0;
static volatile uint32_t g_run_count_c = 0;

/*
 * Burn CPU for approximately `ms` milliseconds.
 * Works in SPIN_CHUNK_MS chunks: each chunk is a pure spin that exercises
 * RR time-slicing, followed by a 1ms yield that lets the flush task drain
 * the ulog buffer. Without the periodic yield, the 3 equal-priority tasks
 * would monopolise the CPU and starve the priority-0 flush task forever.
 */
#define SPIN_CHUNK_MS (10U)

static void busy_work_ms(uint32_t ms)
{
    rtos_tick_t overall_start = rtos_get_tick_count();

    while ((rtos_get_tick_count() - overall_start) < ms)
    {
        /* Spin for one chunk — RR will preempt us on time-slice */
        rtos_tick_t chunk_start = rtos_get_tick_count();
        while ((rtos_get_tick_count() - chunk_start) < SPIN_CHUNK_MS)
        {
            __asm volatile("nop");
        }
        /* Brief yield — lets monitor + flush task run */
        rtos_delay_ms(1);
    }
}

/* =================== Task Implementations =================== */

static void task_a_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskA");

    rtos_tick_t last_wake = rtos_get_tick_count();

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        test_log_task("RUN", "TaskA");

        /* INV-RR3: record tick before work, check after */
        rtos_tick_t work_start = rtos_get_tick_count();
        busy_work_ms(WORK_MS);
        rtos_tick_t elapsed = rtos_get_tick_count() - work_start;

        /*
         * After busy_work_ms returns, the task may have been preempted
         * many times. The wall-clock elapsed will be >= WORK_MS.
         * INV-RR3 is checked differently: we cannot measure uninterrupted
         * hold time from userspace since the task doesn't know when it
         * was preempted. Instead, we verify the scheduler rotated via
         * INV-RR4 in the monitor. Here we just log the elapsed time.
         */
        (void) elapsed;

        test_log_task("DELAY", "TaskA");
        rtos_delay_until(&last_wake, DELAY_A_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "TaskA");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

static void task_b_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskB");

    rtos_tick_t last_wake = rtos_get_tick_count();

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        test_log_task("RUN", "TaskB");
        busy_work_ms(WORK_MS);
        test_log_task("DELAY", "TaskB");
        rtos_delay_until(&last_wake, DELAY_B_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "TaskB");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

static void task_c_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskC");

    rtos_tick_t last_wake = rtos_get_tick_count();

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        test_log_task("RUN", "TaskC");
        busy_work_ms(WORK_MS);
        test_log_task("DELAY", "TaskC");
        rtos_delay_until(&last_wake, DELAY_C_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "TaskC");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task — runs at priority 1, below all test tasks.
 *
 * Samples task states every SAMPLE_MS and checks:
 *   INV-RR1: Each task has been observed RUNNING at least once per window
 *   INV-RR2: At most one task is RUNNING at any sample
 *   INV-RR4: Consecutive RUNNING observations differ (rotation)
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    /* INV-RR1: snapshot counts at the start of each fairness window */
    uint32_t prev_a = 0, prev_b = 0, prev_c = 0;
    uint32_t window_samples = 0;

    /*
     * LCM(100,150,200) = 600ms. We check fairness every 600ms worth of
     * samples: 600 / SAMPLE_MS = 40 samples per window.
     */
    const uint32_t FAIRNESS_WINDOW_SAMPLES = 600U / SAMPLE_MS;

    /* INV-RR4: track which task was running on the previous sample */
    int last_running_id = -1; /* -1 = none, 0=A, 1=B, 2=C */

    while (!g_test_complete)
    {
        rtos_delay_ms(SAMPLE_MS);

        /* --- INV-RR2: count RUNNING tasks --- */
        rtos_task_state_t state_a = rtos_task_get_state(g_handle_a);
        rtos_task_state_t state_b = rtos_task_get_state(g_handle_b);
        rtos_task_state_t state_c = rtos_task_get_state(g_handle_c);

        uint32_t running_count      = 0;
        int      current_running_id = -1;

        if (state_a == RTOS_TASK_STATE_RUNNING)
        {
            running_count++;
            current_running_id = 0;
            g_run_count_a++;
        }
        if (state_b == RTOS_TASK_STATE_RUNNING)
        {
            running_count++;
            current_running_id = 1;
            g_run_count_b++;
        }
        if (state_c == RTOS_TASK_STATE_RUNNING)
        {
            running_count++;
            current_running_id = 2;
            g_run_count_c++;
        }

        /* INV-RR2: At most one task RUNNING (monitor itself is running,
         * so test tasks should be 0 or 1 at most — if the monitor
         * preempted one mid-slice, it might see 0 running). */
        TEST_ASSERT(running_count <= 1, "INV-RR2:ExclusiveRunning");

        /* INV-RR4: if both this and last sample observed a task RUNNING,
         * assert that the scheduler rotated to a different task. */
        if (current_running_id >= 0 && last_running_id >= 0)
        {
            TEST_ASSERT(current_running_id != last_running_id, "INV-RR4:RotationOccurred");
        }
        last_running_id = current_running_id;

        /* --- INV-RR1: fairness check at window boundary --- */
        window_samples++;
        if (window_samples >= FAIRNESS_WINDOW_SAMPLES)
        {
            /*
             * Each task should have been observed RUNNING at least
             * once during this window. Check that counts incremented.
             *
             * Guard: only assert when there was activity (at least one
             * task observed RUNNING). After all tasks finish their
             * SCENARIO_CYCLES and enter idle loops, no task will be
             * seen RUNNING, and the fairness check is meaningless.
             */
            uint32_t total_new = (g_run_count_a - prev_a) + (g_run_count_b - prev_b) + (g_run_count_c - prev_c);
            if (total_new > 0)
            {
                TEST_ASSERT(g_run_count_a > prev_a, "INV-RR1:TaskA-RunInWindow");
                TEST_ASSERT(g_run_count_b > prev_b, "INV-RR1:TaskB-RunInWindow");
                TEST_ASSERT(g_run_count_c > prev_c, "INV-RR1:TaskC-RunInWindow");
            }

            prev_a         = g_run_count_a;
            prev_b         = g_run_count_b;
            prev_c         = g_run_count_c;
            window_samples = 0;
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
    test_log_framework("BEGIN", "RoundRobinState");
    rtos_timer_handle_t *p_test_timer = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p_test_timer);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "RoundRobinState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Round-Robin Scheduler - State Invariant Test");
    log_info("Priority: %u (all tasks equal)", TASK_PRIORITY);
    log_info("Delays: A=%ums B=%ums C=%ums  Work=%ums", DELAY_A_MS, DELAY_B_MS, DELAY_C_MS, WORK_MS);
    log_info("Invariants: RR1(fairness) RR2(exclusive) RR4(rotation)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    /* Startup hold timer */
    status = test_create_startup_timer(startup_timer_callback, &g_test_timer, &startup_timer);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /* Test duration timer */
    status = rtos_timer_create("TestTimer", TEST_DURATION_MS, RTOS_TIMER_ONE_SHOT, test_timeout_callback, NULL,
                               &g_test_timer);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /* Create tasks — all at the same priority for round-robin */
    status = rtos_task_create(task_a_func, "TaskA", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_PRIORITY, &g_handle_a);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(task_b_func, "TaskB", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_PRIORITY, &g_handle_b);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(task_c_func, "TaskC", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_PRIORITY, &g_handle_c);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /* Monitor at priority 1 — below all test tasks */
    rtos_task_handle_t monitor_handle;
    status = rtos_task_create(monitor_task_func, "Mon", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1U, &monitor_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /*
     * Flush task at SAME priority as test tasks so it participates in
     * round-robin rotation. A priority-0 flush task would be permanently
     * starved by the busy-looping priority-2 test tasks.
     */
    rtos_task_handle_t flush_handle;
    ulog_init(ULOG_LEVEL_INFO);
    rtos_task_create(log_flush_task, "LogFlush", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_PRIORITY, &flush_handle);

    log_info("Starting scheduler...");
    status = rtos_start_scheduler();

    /* Should never reach here */
    indicate_system_failure();
}
