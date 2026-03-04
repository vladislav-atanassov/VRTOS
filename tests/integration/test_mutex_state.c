/*******************************************************************************
 * File: tests/integration/test_mutex_state.c
 * Description: Mutex - State & Priority Inheritance Invariant Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "mutex.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "task_priv.h" /* rtos_tcb_t - needed to read base_priority directly */
#include "test_common.h"
#include "test_log.h" /* thread-safe ulog overrides for test_log_task/framework */
#include "timer.h"
#include "uart_tx.h"

/**
 * @file test_mutex_state.c
 * @brief Mutex State & Priority Inheritance Invariant Test
 *
 * WHY THIS TEST EXISTS
 * --------------------
 * Mutexes introduce correctness requirements that are fundamentally different
 * from scheduler correctness and cannot be validated by observing timing windows:
 *
 *   1. A task that calls rtos_mutex_lock() on a held mutex must immediately
 *      transition to BLOCKED state and stay there until the owner unlocks.
 *
 *   2. While a higher-priority task is waiting, the owner's effective priority
 *      must be raised to match the waiter (Priority Inheritance Protocol, PIP).
 *      Without this, the owner can be preempted by medium-priority tasks,
 *      causing unbounded priority inversion.
 *
 *   3. After the owner unlocks, its priority must be restored to its original
 *      base priority immediately — not left elevated.
 *
 *   4. The mutex must be handed off to the highest-priority waiter first,
 *      not FIFO. If two tasks are waiting, the higher-priority one runs first.
 *
 * SCENARIO & TASK DESIGN
 * ----------------------
 * Four tasks participate:
 *
 *   TaskLow  (priority 2) — acquires the mutex first and holds it for a
 *                           controlled duration while the others contend.
 *
 *   TaskMid  (priority 3) — medium-priority contender. Attempts to lock
 *                           while TaskLow holds it. Should NOT run while
 *                           TaskHigh is also waiting (PIP + wait-queue ordering).
 *
 *   TaskHigh (priority 4) — highest contender. Blocks on the mutex after
 *                           TaskMid. Triggers PIP: TaskLow must be boosted
 *                           to priority 4.
 *
 *   Monitor  (priority 1) — reads TCB fields and emits ASSERT_PASS/FAIL.
 *                           Lowest test priority so it never interferes with
 *                           the scenario.
 *
 * The test runs SCENARIO_CYCLES fixed cycles. In each cycle:
 *
 *   Phase 1 - TaskLow acquires mutex, signals others to contend, holds for
 *             HOLD_TICKS. During this window the Monitor checks PIP is active.
 *
 *   Phase 2 - TaskLow releases. TaskHigh must run before TaskMid (priority
 *             ordering of the wait queue). Monitor checks priority restoration.
 *
 *   Phase 3 - TaskHigh and TaskMid each complete their critical section and
 *             signal done. Cycle repeats.
 *
 * INVARIANTS
 * ----------
 * INV-M1  (Contender blocks immediately)
 *   After TaskHigh calls rtos_mutex_lock() while TaskLow holds the mutex,
 *   TaskHigh's state must be BLOCKED. Checked by TaskLow while it still
 *   holds the mutex.
 *
 * INV-M2  (PIP boosts owner to highest waiter's priority)
 *   While TaskHigh is blocked waiting, TaskLow's effective priority must
 *   equal TaskHigh's priority (4). Checked by Monitor during the hold window.
 *
 * INV-M3  (Priority restoration after unlock)
 *   After TaskLow unlocks, its priority must return to its base priority (2).
 *   Checked by TaskLow immediately after unlock, and sampled by Monitor.
 *
 * INV-M4  (Highest-priority waiter acquires first)
 *   After unlock, TaskHigh must acquire and execute before TaskMid gets a
 *   turn. Enforced by sequencing flags and asserted by TaskMid: when TaskMid
 *   acquires the mutex, g_high_acquired_count must exceed g_mid_acquired_count.
 *
 * INV-M5  (Owner field is correct while mutex is held)
 *   While any task holds the mutex, the mutex owner pointer must equal that
 *   task's handle. Checked inside each task's critical section.
 */

/* =================== Test Parameters =================== */

#define TASK_LOW_PRIORITY  (2U)
#define TASK_MID_PRIORITY  (3U)
#define TASK_HIGH_PRIORITY (4U)
#define TASK_MON_PRIORITY  (1U)

/*
 * How long TaskLow holds the mutex each cycle (ms).
 * Long enough for Monitor to sample PIP state and for Mid/High to block.
 * Short enough to complete SCENARIO_CYCLES within TEST_DURATION_MS.
 */
#define MUTEX_HOLD_MS (100U)

/* Total number of scenario cycles to run. */
#define SCENARIO_CYCLES (8U)

/* Timeout for individual mutex_lock calls. */
#define LOCK_TIMEOUT_TICKS (2000U)

#define TEST_DURATION_MS (6000U)

/* TEST_ASSERT, ASSERT_STATE, g_fail_count are in test_common.h */

#define ASSERT_PRIORITY(handle, expected_prio, desc)                                                                   \
    TEST_ASSERT(rtos_task_get_priority((handle)) == (expected_prio), (desc))

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

/* The mutex under test */
static rtos_mutex_t g_mutex;

/* Task handles — needed for state/priority inspection */
static rtos_task_handle_t g_handle_low  = NULL;
static rtos_task_handle_t g_handle_mid  = NULL;
static rtos_task_handle_t g_handle_high = NULL;

/*
 * Cycle synchronisation flags.
 * volatile uint32_t rather than bool to avoid torn reads on Cortex-M.
 */
static volatile uint32_t g_cycle               = 0;
static volatile uint32_t g_contend_signal      = 0; /* set by TaskLow: tells Mid/High to contend */
static volatile uint32_t g_high_done           = 0; /* set by TaskHigh after releasing mutex */
static volatile uint32_t g_mid_done            = 0; /* set by TaskMid  after releasing mutex */
static volatile uint32_t g_high_acquired_count = 0;
static volatile uint32_t g_mid_acquired_count  = 0;
static volatile bool     g_high_contending     = false; /* true only while High is blocked on mutex */

/* file-scope so startup callback can start it */
static rtos_timer_handle_t g_test_timer;

/* =================== Task Implementations =================== */

/*
 * TaskLow - the initial mutex holder (priority 2).
 *
 * Each cycle:
 *   1. Acquire mutex.
 *   2. Signal Mid and High to start contending.
 *   3. Hold for MUTEX_HOLD_MS (contenders block during this window).
 *   4. Assert both contenders are BLOCKED before releasing (INV-M1).
 *   5. Release mutex — priority must restore immediately (INV-M3).
 *   6. Wait for both contenders to finish before next cycle.
 */
static void task_low_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskLow");

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        test_log_task("LOCK", "TaskLow");
        rtos_mutex_status_t s = rtos_mutex_lock(&g_mutex, LOCK_TIMEOUT_TICKS);
        TEST_ASSERT(s == RTOS_MUTEX_OK, "M-LOW:LockOK");

        /* INV-M5: we are the owner */
        TEST_ASSERT(g_mutex.owner == g_handle_low, "INV-M5:LowIsOwner");

        /* Signal contenders — they will call rtos_mutex_lock() and block */
        test_log_task("HOLD", "TaskLow");
        g_high_done      = 0;
        g_mid_done       = 0;
        g_contend_signal = cycle + 1;

        /*
         * Hold for MUTEX_HOLD_MS. We yield here via rtos_delay_ms so the
         * scheduler runs and the contenders actually execute their lock calls.
         * Despite the delay, PIP ensures TaskLow is re-scheduled ahead of any
         * medium-priority tasks once it wakes — but the delay is explicit, so
         * it yields voluntarily.
         */
        rtos_delay_ms(MUTEX_HOLD_MS);

        /*
         * After waking: High and Mid should both be BLOCKED on the mutex.
         * INV-M1: contenders are BLOCKED.
         */
        ASSERT_STATE(g_handle_high, RTOS_TASK_STATE_BLOCKED, "INV-M1:HighBlocked");
        ASSERT_STATE(g_handle_mid, RTOS_TASK_STATE_BLOCKED, "INV-M1:MidBlocked");

        test_log_task("UNLOCK", "TaskLow");
        rtos_mutex_unlock(&g_mutex);

        /*
         * INV-M3: priority restored synchronously inside rtos_mutex_unlock().
         * By the time this line executes we are still running (not yet
         * preempted) and must already be back at base priority.
         */
        ASSERT_PRIORITY(g_handle_low, TASK_LOW_PRIORITY, "INV-M3:LowPriorityRestored");

        /* Wait for both contenders to finish their critical sections. */
        while (!g_high_done || !g_mid_done)
        {
            rtos_delay_ms(10);
        }

        g_cycle = cycle + 1;
    }

    test_log_task("END", "TaskLow");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * TaskMid - medium-priority contender (priority 3).
 *
 * Each cycle:
 *   1. Wait for contend signal from TaskLow.
 *   2. Call rtos_mutex_lock() — blocks immediately (TaskLow holds it).
 *   3. After acquiring (must be after TaskHigh due to priority ordering):
 *      check INV-M4 and INV-M5, then release.
 */
static void task_mid_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskMid");

    uint32_t last_signal = 0;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        while (g_contend_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_contend_signal;

        test_log_task("LOCK", "TaskMid");
        rtos_mutex_status_t s = rtos_mutex_lock(&g_mutex, LOCK_TIMEOUT_TICKS);
        TEST_ASSERT(s == RTOS_MUTEX_OK, "M-MID:LockOK");

        /*
         * INV-M4: High must have already acquired and released before us.
         * If the wait queue is FIFO instead of priority-ordered, Mid would
         * sometimes acquire before High and this assertion would fail.
         */
        TEST_ASSERT(g_high_acquired_count > g_mid_acquired_count, "INV-M4:HighAcquiredBeforeMid");

        /* INV-M5: Mid is now the owner */
        TEST_ASSERT(g_mutex.owner == g_handle_mid, "INV-M5:MidIsOwner");

        g_mid_acquired_count++;

        test_log_task("UNLOCK", "TaskMid");
        rtos_mutex_unlock(&g_mutex);

        g_mid_done = 1;
    }

    test_log_task("END", "TaskMid");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * TaskHigh - highest-priority contender (priority 4).
 *
 * Each cycle:
 *   1. Wait for contend signal.
 *   2. Call rtos_mutex_lock() — blocks immediately (TaskLow holds it).
 *      This also triggers PIP: TaskLow's priority is raised to 4.
 *   3. After waking (first after TaskLow unlocks):
 *      check INV-M5, increment counter, release.
 */
static void task_high_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "TaskHigh");

    uint32_t last_signal = 0;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        while (g_contend_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_contend_signal;

        test_log_task("LOCK", "TaskHigh");
        g_high_contending     = true;
        rtos_mutex_status_t s = rtos_mutex_lock(&g_mutex, LOCK_TIMEOUT_TICKS);
        g_high_contending     = false;
        TEST_ASSERT(s == RTOS_MUTEX_OK, "M-HIGH:LockOK");

        /* INV-M5: High is now the owner */
        TEST_ASSERT(g_mutex.owner == g_handle_high, "INV-M5:HighIsOwner");

        g_high_acquired_count++;

        test_log_task("UNLOCK", "TaskHigh");
        rtos_mutex_unlock(&g_mutex);

        g_high_done = 1;
    }

    test_log_task("END", "TaskHigh");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task (priority 1).
 *
 * Samples system state during the hold window to check PIP and restoration.
 *
 * INV-M2: while TaskHigh is BLOCKED and a contend cycle is active,
 *         TaskLow's effective priority must equal TASK_HIGH_PRIORITY.
 *
 * INV-M3 (sampled): when the mutex is free between cycles,
 *         TaskLow's priority must be back at base.
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    uint32_t last_cycle_seen = 0;

    while (!g_test_complete)
    {
        rtos_delay_ms(20);

        /* INV-M2: only check when TaskHigh is actually blocked inside
         * rtos_mutex_lock (g_high_contending == true) AND TaskLow is
         * the owner.  Eliminates the race where the monitor samples
         * while High is in its rtos_delay_ms(5) poll loop — no PIP
         * boost exists at that point. */
        if (g_high_contending && g_mutex.owner == g_handle_low)
        {
            ASSERT_PRIORITY(g_handle_low, TASK_HIGH_PRIORITY, "INV-M2:LowBoostedToHigh");
        }

        /* INV-M3 sampled: mutex free between cycles */
        if (g_mutex.owner == NULL && g_cycle > last_cycle_seen)
        {
            last_cycle_seen = g_cycle;
            ASSERT_PRIORITY(g_handle_low, TASK_LOW_PRIORITY, "INV-M3:LowBaseRestoredSample");
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
    test_log_framework("BEGIN", "MutexState");
    rtos_timer_handle_t *p = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "MutexState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Mutex State & Priority Inheritance Test");
    log_info("Priorities: Low=%u Mid=%u High=%u Mon=%u", TASK_LOW_PRIORITY, TASK_MID_PRIORITY, TASK_HIGH_PRIORITY,
             TASK_MON_PRIORITY);
    log_info("Cycles: %u  Hold: %ums", SCENARIO_CYCLES, MUTEX_HOLD_MS);
    log_info("Invariants: M1(block) M2(PIP boost) M3(restore) M4(priority order) M5(owner)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    status = (rtos_status_t) rtos_mutex_init(&g_mutex);
    if (status != RTOS_SUCCESS)
    {
        log_error("Mutex init failed: %d", status);
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
     * Create order: Low first so it acquires the mutex first when the
     * test starts, before High and Mid get scheduled.
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
