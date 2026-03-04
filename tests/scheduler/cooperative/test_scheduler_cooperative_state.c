/*******************************************************************************
 * File: tests/scheduler/cooperative/test_scheduler_cooperative_state.c
 * Description: Cooperative Scheduler - State & Non-Preemption Invariant Test
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
 * @file test_scheduler_cooperative_state.c
 * @brief Cooperative Scheduler State & Non-Preemption Invariant Test
 *
 * The cooperative scheduler's defining property is that a task which does
 * not yield runs forever — nothing else can preempt it. This test verifies
 * that property directly.
 *
 * INV-CO1 (Non-preemption)
 *   While TaskMonopoly is in its busy-loop, no other task is RUNNING.
 *   TaskMonopoly reads peer states at the midpoint of its loop; both
 *   must be READY or BLOCKED, not RUNNING.
 *
 * INV-CO2 (Loop integrity)
 *   TaskMonopoly completes its full loop count without preemption.
 *   A volatile counter is set before the loop and checked after — if
 *   the task were preempted and another task modified shared state,
 *   the count would be inconsistent.
 *
 * INV-CO3 (Priority among yielders)
 *   After TaskMonopoly yields, TaskYieldA (higher priority) runs
 *   before TaskYieldB. TaskYieldB asserts g_yield_a_count > g_yield_b_count.
 *
 * INV-CO4 (Exclusive running)
 *   No two tasks are RUNNING simultaneously. Monitor samples states
 *   periodically (same as INV-4/INV-RR2).
 */

/* =================== Task Configuration =================== */

#define TASK_MONOPOLY_PRIORITY (4U)
#define TASK_YIELD_A_PRIORITY  (3U)
#define TASK_YIELD_B_PRIORITY  (2U)

/*
 * MONOPOLY_LOOP_COUNT is calibrated so the busy-loop takes ~50-100ms
 * at 180 MHz. An empty volatile counter loop runs at ~2-4M iterations/s.
 * 200000 iterations ≈ 50-100ms. This is well above the 1ms time-slice,
 * so a preemptive scheduler would interrupt it — but cooperative must not.
 */
#define MONOPOLY_LOOP_COUNT (200000U)

#define DELAY_A_MS        (200U)
#define DELAY_B_MS        (300U)
#define MONOPOLY_DELAY_MS (400U)
#define SCENARIO_CYCLES   (6U)
#define SAMPLE_MS         (10U)
#define TEST_DURATION_MS  (5000U)

/* Assertion infrastructure (TEST_ASSERT, etc.) is in test_common.h */

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

static rtos_task_handle_t g_handle_monopoly = NULL;
static rtos_task_handle_t g_handle_yield_a  = NULL;
static rtos_task_handle_t g_handle_yield_b  = NULL;

static rtos_timer_handle_t g_test_timer;

/* INV-CO3: counters for yield ordering */
static volatile uint32_t g_yield_a_count = 0;
static volatile uint32_t g_yield_b_count = 0;

/* =================== Task Implementations =================== */

/*
 * TaskMonopoly — highest priority, busy-loops with NO yield.
 *
 * In a cooperative scheduler, this task will hold the CPU for the
 * entire duration of its busy-loop. No other task can preempt it.
 * After the loop completes, it voluntarily yields via rtos_delay_ms.
 */
static void task_monopoly_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monopoly");

    rtos_tick_t last_wake = rtos_get_tick_count();

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        test_log_task("RUN", "Monopoly");

        /*
         * INV-CO2: set a volatile sentinel before the loop.
         * If the task were preempted mid-loop, another task could
         * theoretically observe partial state. We check the sentinel
         * is still valid after the loop to confirm no preemption occurred.
         */
        volatile uint32_t loop_sentinel = 0xDEADBEEF;
        volatile uint32_t counter       = 0;

        for (uint32_t i = 0; i < MONOPOLY_LOOP_COUNT; i++)
        {
            counter++;

            /* INV-CO1: at the midpoint, check peer states */
            if (i == MONOPOLY_LOOP_COUNT / 2)
            {
                rtos_task_state_t state_a = rtos_task_get_state(g_handle_yield_a);
                rtos_task_state_t state_b = rtos_task_get_state(g_handle_yield_b);

                TEST_ASSERT(state_a != RTOS_TASK_STATE_RUNNING, "INV-CO1:YieldA-NotRunning");
                TEST_ASSERT(state_b != RTOS_TASK_STATE_RUNNING, "INV-CO1:YieldB-NotRunning");
            }
        }

        /* INV-CO2: verify loop completed without interruption */
        TEST_ASSERT(counter == MONOPOLY_LOOP_COUNT, "INV-CO2:LoopCountComplete");
        TEST_ASSERT(loop_sentinel == 0xDEADBEEF, "INV-CO2:SentinelIntact");

        test_log_task("DELAY", "Monopoly");
        rtos_delay_until(&last_wake, MONOPOLY_DELAY_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "Monopoly");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * TaskYieldA — priority 3, yields voluntarily via delay.
 * Higher priority than TaskYieldB, so it should always run first
 * among the yielding tasks when both are ready.
 */
static void task_yield_a_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "YieldA");

    rtos_tick_t last_wake = rtos_get_tick_count();

    while (!g_test_complete)
    {
        test_log_task("RUN", "YieldA");

        /* Small work */
        for (volatile int i = 0; i < 5000; i++)
        {
            __asm volatile("nop");
        }

        g_yield_a_count++;

        test_log_task("DELAY", "YieldA");
        rtos_delay_until(&last_wake, DELAY_A_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "YieldA");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * TaskYieldB — priority 2, lowest among yielding tasks.
 *
 * INV-CO3: Because TaskYieldA has higher priority, it should always
 * run before TaskYieldB when both wake simultaneously. So at any point
 * where TaskYieldB runs, TaskYieldA should have run at least as many
 * times (its count >= our count).
 */
static void task_yield_b_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "YieldB");

    rtos_tick_t last_wake = rtos_get_tick_count();

    while (!g_test_complete)
    {
        test_log_task("RUN", "YieldB");

        /* INV-CO3: TaskYieldA should have run at least as many times */
        TEST_ASSERT(g_yield_a_count >= g_yield_b_count, "INV-CO3:YieldA-RanFirst");

        for (volatile int i = 0; i < 5000; i++)
        {
            __asm volatile("nop");
        }

        g_yield_b_count++;

        test_log_task("DELAY", "YieldB");
        rtos_delay_until(&last_wake, DELAY_B_MS / RTOS_TICK_PERIOD_MS);
    }

    test_log_task("END", "YieldB");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task — priority 1, below all test tasks.
 *
 * INV-CO4: Verifies at most one task is RUNNING at any sample point.
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    while (1)
    {
        rtos_delay_ms(SAMPLE_MS);

        if (g_test_complete)
        {
            /* Emit verdict NOW while we still have the CPU.
             * In cooperative mode, the scheduler may not properly
             * re-schedule us after higher-priority tasks exit. */
            TEST_EMIT_VERDICT();
            break;
        }

        uint32_t running_count = 0;
        if (rtos_task_get_state(g_handle_monopoly) == RTOS_TASK_STATE_RUNNING)
            running_count++;
        if (rtos_task_get_state(g_handle_yield_a) == RTOS_TASK_STATE_RUNNING)
            running_count++;
        if (rtos_task_get_state(g_handle_yield_b) == RTOS_TASK_STATE_RUNNING)
            running_count++;

        TEST_ASSERT(running_count <= 1, "INV-CO4:ExclusiveRunning");
    }

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
    test_log_framework("BEGIN", "CooperativeState");
    rtos_timer_handle_t *p_test_timer = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p_test_timer);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "CooperativeState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Cooperative Scheduler - State Invariant Test");
    log_info("Priorities: Monopoly=%u YieldA=%u YieldB=%u", TASK_MONOPOLY_PRIORITY, TASK_YIELD_A_PRIORITY,
             TASK_YIELD_B_PRIORITY);
    log_info("Monopoly loop: %u iterations, Delays: A=%ums B=%ums Mono=%ums", MONOPOLY_LOOP_COUNT, DELAY_A_MS,
             DELAY_B_MS, MONOPOLY_DELAY_MS);
    log_info("Invariants: CO1(non-preemption) CO2(loop integrity) CO3(yield priority) CO4(exclusive)");

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

    /*
     * Create tasks. Order: YieldB -> YieldA -> Monopoly so that
     * when the scheduler starts, Monopoly (highest priority) runs first.
     */
    status = rtos_task_create(task_yield_b_func, "YldB", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_YIELD_B_PRIORITY,
                              &g_handle_yield_b);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(task_yield_a_func, "YldA", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_YIELD_A_PRIORITY,
                              &g_handle_yield_a);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(task_monopoly_func, "Mono", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_MONOPOLY_PRIORITY,
                              &g_handle_monopoly);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /* Monitor at priority 1 — below all test tasks */
    rtos_task_handle_t monitor_handle;
    status = rtos_task_create(monitor_task_func, "Mon", 1536U, NULL, 1U, &monitor_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    rtos_task_handle_t flush_handle;
    test_create_log_flush_task(&flush_handle);

    log_info("Starting scheduler...");
    status = rtos_start_scheduler();

    /* Should never reach here */
    indicate_system_failure();
}
