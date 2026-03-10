/*******************************************************************************
 * File: tests/benchmarks/bench_mutex/bench_mutex.c
 * Description: Mutex Acquire/Release Latency Benchmark
 * Author: Student
 * Date: 2025
 *
 * WHAT IS MEASURED
 * ----------------
 * Two distinct sub-benchmarks run sequentially:
 *
 * Phase 1 — Uncontended acquire+release:
 *   A single task acquires and immediately releases a mutex in a tight loop.
 *   Measures the raw software overhead of the lock/unlock path with no
 *   blocking or context switches.
 *
 *   Measurement window:
 *     RTOS_USER_PROFILE_START(mu)
 *     rtos_mutex_lock(&g_mutex, RTOS_MAX_DELAY)
 *     rtos_mutex_unlock(&g_mutex)
 *     RTOS_USER_PROFILE_END(mu, &g_stat_uncontended)
 *
 * Phase 2 — Contended acquire wake latency:
 *   Two tasks at different priorities:
 *     MutexHigh (priority 4) — blocks waiting for mutex held by MutexLow.
 *     MutexLow  (priority 2) — holds mutex briefly, then releases it.
 *
 *   MutexLow captures DWT immediately before rtos_mutex_unlock().
 *   MutexHigh captures DWT immediately after rtos_mutex_lock() returns.
 *   Delta = unlock-to-wake latency (PendSV + context switch).
 *
 * TASK PRIORITY DESIGN
 * --------------------
 * MutexHigh is priority 4 > BenchTask priority 3.  To avoid MutexHigh
 * starving BenchTask during Phase 1 polling, MutexHigh blocks on
 * g_low_acquired immediately from startup — it never polls a flag.
 * Only MutexLow (priority 2 < BenchTask priority 3) polls g_phase2_start,
 * so BenchTask always gets CPU during Phase 1.
 *
 * Phase 2 ping-pong via g_low_acquired (binary semaphore):
 *   MutexLow: acquire mutex → signal g_low_acquired → capture DWT → unlock
 *   MutexHigh: wait g_low_acquired → rtos_mutex_lock (blocks, PIP) → wake →
 *              read DWT → record → unlock → wait g_low_acquired (next iter)
 *
 * BUILD
 *   pio run -e bench_mutex -t upload
 ******************************************************************************/

#include "VRTOS.h"
#include "bench_common.h"
#include "hardware_env.h"
#include "mutex.h"
#include "profiling.h"
#include "semaphore.h"
#include "stm32f4xx_hal.h" /* IWYU pragma: keep */
#include "uart_tx.h"
#include "ulog.h"

/* ========================= SHARED STATE =================================== */

static rtos_mutex_t          g_mutex;
static volatile uint32_t     g_test_started = 0;

/**
 * Set to 1 by BenchTask when Phase 1 is complete.
 * Only polled by MutexLow (priority 2 < BenchTask priority 3).
 * MutexHigh never reads this — it blocks on g_low_acquired instead.
 */
static volatile uint32_t     g_phase2_start = 0;

/**
 * Binary semaphore: MutexLow signals after it holds the mutex each iteration.
 * MutexHigh blocks on this from startup onward, ensuring it is always blocked
 * (not polling) during Phase 1 so it cannot starve BenchTask.
 */
static rtos_semaphore_t      g_low_acquired;

/** Counting semaphore (max 2): BenchTask (Phase 1) and MutexHigh (Phase 2) each signal once. */
static rtos_semaphore_t      g_all_done_sem;

/* ========================= PROFILING STATS ================================ */

static rtos_profile_stat_t g_stat_uncontended = BENCH_STAT_INIT("mutex_uncontended");
static rtos_profile_stat_t g_stat_contended   = BENCH_STAT_INIT("mutex_contended_wake");

/** Written by MutexLow immediately before unlock; read by MutexHigh after wake. */
static volatile uint32_t g_release_cycles = 0;

/* ========================= TASK FUNCTIONS ================================= */

/**
 * @brief BenchTask (priority 3) — Phase 1 uncontended benchmark
 *
 * MutexHigh is blocked on g_low_acquired the entire time, so BenchTask
 * runs Phase 1 without any interference from the higher-priority task.
 * After Phase 1, sets g_phase2_start so MutexLow can begin Phase 2.
 */
void BenchTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);
    /* Warmup: discarded */
    for (uint32_t i = 0; i < BENCH_WARMUP; i++)
    {
        rtos_mutex_lock(&g_mutex, RTOS_MAX_DELAY);
        rtos_mutex_unlock(&g_mutex);
    }

    /* Measured: uncontended lock+unlock */
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        RTOS_USER_PROFILE_START(mu);
        rtos_mutex_lock(&g_mutex, RTOS_MAX_DELAY);
        rtos_mutex_unlock(&g_mutex);
        RTOS_USER_PROFILE_END(mu, &g_stat_uncontended);
    }

    /*
     * Start Phase 2.  Simple flag write — BenchTask is NOT preempted here
     * because MutexHigh is still blocked on g_low_acquired (count=0).
     */
    g_phase2_start = 1;
    rtos_semaphore_signal(&g_all_done_sem);
    rtos_task_suspend(NULL);
}

/**
 * @brief MutexLow (priority 2) — low-priority mutex holder for Phase 2
 *
 * Polls g_phase2_start (safe: priority 2 < BenchTask priority 3, no
 * starvation).  Then per iteration:
 *   1. Acquire mutex (free — MutexHigh is blocked on g_low_acquired)
 *   2. Signal g_low_acquired → MutexHigh wakes and immediately blocks on mutex
 *   3. Capture DWT reference → unlock → MutexHigh preempts and records latency
 */
void MutexLow(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);
    /* Poll flag — safe because priority 2 < BenchTask priority 3. */
    while (!g_phase2_start)
    {
        rtos_yield();
    }

    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        /* Acquire: mutex is free because MutexHigh is blocked on g_low_acquired. */
        rtos_mutex_lock(&g_mutex, RTOS_MAX_DELAY);

        /*
         * Signal MutexHigh.  It wakes (priority 4 > 2) and immediately calls
         * rtos_mutex_lock — which blocks because we still hold it.  PIP raises
         * our priority to 4.  We resume here after MutexHigh blocks.
         */
        rtos_semaphore_signal(&g_low_acquired);

        /* Capture DWT as the reference point for MutexHigh's wake latency. */
        g_release_cycles = rtos_profiling_get_cycles();

        rtos_mutex_unlock(&g_mutex);
        /*
         * MutexHigh preempts here: it wakes, reads DWT, records latency,
         * releases the mutex, then waits on g_low_acquired for the next iter.
         * MutexLow resumes once MutexHigh has released the mutex.
         */
    }

    rtos_task_suspend(NULL);
}

/**
 * @brief MutexHigh (priority 4) — high-priority contender for Phase 2
 *
 * Blocks on g_low_acquired from the very start — never polls — so it cannot
 * starve BenchTask during Phase 1.  Per iteration:
 *   1. Wait g_low_acquired (MutexLow holds mutex when this fires)
 *   2. rtos_mutex_lock → BLOCKS (PIP activates)
 *   3. Wake: read DWT, record contended latency, release mutex
 */
void MutexHigh(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);
    /*
     * Block immediately on g_low_acquired.  Count starts at 0, so this
     * suspends MutexHigh for the entire duration of Phase 1 and until
     * MutexLow begins Phase 2 and acquires the mutex for the first time.
     */
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        /* Wait until MutexLow holds the mutex. */
        rtos_semaphore_wait(&g_low_acquired, RTOS_SEM_MAX_WAIT);

        /*
         * Mutex is held by MutexLow — we block here.
         * PIP raises MutexLow's priority to 4 while we wait.
         */
        rtos_mutex_lock(&g_mutex, RTOS_MAX_DELAY);

        /* Read DWT immediately on wake. */
        uint32_t wake_cycles = rtos_profiling_get_cycles();
        uint32_t latency     = wake_cycles - g_release_cycles;
        rtos_profiling_record(&g_stat_contended, latency);

        /* Release so MutexLow can acquire for the next iteration. */
        rtos_mutex_unlock(&g_mutex);
    }

    rtos_semaphore_signal(&g_all_done_sem);
    rtos_task_suspend(NULL);
}

/**
 * @brief ResultTask (priority 1) — prints results after both phases complete
 */
void ResultTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    rtos_semaphore_wait(&g_all_done_sem, RTOS_MAX_DELAY);
    rtos_semaphore_wait(&g_all_done_sem, RTOS_MAX_DELAY);

    bench_header("mutex_uncontended");
    bench_report(&g_stat_uncontended);

    bench_header("mutex_contended_wake");
    bench_report(&g_stat_contended);

    ulog_info("[BENCH] Done.");

    rtos_task_suspend(NULL);
}

/* ========================= STARTUP TIMER ================================== */

static void startup_cb(void *timer_handle, void *param)
{
    (void)timer_handle;
    (void)param;
    g_test_started = 1;
    ulog_info("[BENCH] Startup hold complete — starting mutex benchmark");
}

/* ========================= MAIN =========================================== */

int main(void)
{
    hardware_env_config();
    log_uart_init(LOG_LEVEL_INFO);

    rtos_init();
    rtos_profiling_init();

    ulog_init(ULOG_LEVEL_INFO);
    ulog_info("[BENCH] Mutex Benchmark — " __DATE__ " " __TIME__);
    ulog_info("[BENCH] Iterations: %u  Warmup: %u", BENCH_ITERATIONS, BENCH_WARMUP);

    rtos_mutex_init(&g_mutex);
    rtos_semaphore_init(&g_low_acquired, 0, 1);
    rtos_semaphore_init(&g_all_done_sem, 0, 2);

    rtos_task_handle_t  handle;
    rtos_timer_handle_t startup_timer;

    /*
     * Priority layout:
     *   MutexHigh  (4) — blocks on g_low_acquired; never polls
     *   BenchTask  (3) — Phase 1 uncontended; runs freely while MutexHigh blocks
     *   MutexLow   (2) — Phase 2 holder; polls g_phase2_start safely
     *   ResultTask (1) — prints results
     *   LogFlush   (0) — drains ulog to UART
     */
    rtos_task_create(MutexHigh,  "MtxHigh",  RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 4, &handle);
    rtos_task_create(BenchTask,  "Bench",    RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &handle);
    rtos_task_create(MutexLow,   "MtxLow",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 2, &handle);
    rtos_task_create(ResultTask, "Result",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &handle);

    test_create_log_flush_task(&handle);
    test_create_startup_timer(startup_cb, NULL, &startup_timer);

    rtos_start_scheduler();

    while (1) {}
    return 0;
}
