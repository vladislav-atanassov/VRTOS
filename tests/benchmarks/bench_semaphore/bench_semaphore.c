/*******************************************************************************
 * File: tests/benchmarks/bench_semaphore/bench_semaphore.c
 * Description: Semaphore Signal/Wait Round-Trip Latency Benchmark
 * Author: Student
 * Date: 2025
 *
 * WHAT IS MEASURED
 * ----------------
 * Two sub-benchmarks run sequentially:
 *
 * Phase 1 — Uncontended round-trip (single task):
 *   A single task signals a binary semaphore (count 0 → 1) and then
 *   immediately waits on it (count 1 → 0) in a tight loop.  Because the
 *   count is already 1 when wait is called, wait returns immediately with no
 *   blocking or context switch.  This measures the raw software overhead of
 *   the semaphore signal+wait path.
 *
 *   Measurement window:
 *     RTOS_USER_PROFILE_START(sem)
 *     rtos_semaphore_signal(&g_sem)   -- count: 0 → 1
 *     rtos_semaphore_wait(&g_sem, 0) -- count: 1 → 0  (non-blocking, instant)
 *     RTOS_USER_PROFILE_END(sem, &g_stat_uncontended)
 *
 * Phase 2 — Contended wake latency (two tasks):
 *   SemHigh (priority 4) — blocks waiting for a signal.
 *   SemLow  (priority 2) — signals the semaphore periodically.
 *
 *   SemLow captures a DWT timestamp immediately before rtos_semaphore_signal().
 *   SemHigh captures a DWT timestamp immediately after rtos_semaphore_wait()
 *   returns.  The delta is the "signal-to-wake" latency: time from the signal
 *   call to the high-priority task running after preemption.
 *
 *   This includes:
 *     - signal() path: unblock the waiter, pend PendSV
 *     - PendSV context switch time
 *     - First instruction of SemHigh reading DWT
 *
 * SEQUENCING
 * ----------
 * BenchTask runs Phase 1 entirely, then signals g_phase2_sem to release
 * SemHigh and SemLow.  ResultTask waits on g_all_done_sem (signalled twice:
 * once by BenchTask for Phase 1, once by SemHigh for Phase 2).
 *
 * BUILD
 * -----
 *   pio run -e bench_semaphore -t upload
 *
 * EXPECTED OUTPUT (at 180 MHz, STM32F446RE)
 *   [BENCH] ===== semaphore_uncontended =====
 *   semaphore_uncontended | count=1000 | min=8cy(0us) max=14cy(0us) avg=10cy(0us)
 *   [BENCH] ===== semaphore_wake_latency =====
 *   semaphore_wake_latency | count=1000 | min=53cy(0us) max=71cy(0us) avg=58cy(0us)
 ******************************************************************************/

#include "VRTOS.h"
#include "bench_common.h"
#include "hardware_env.h"
#include "profiling.h"
#include "semaphore.h"
#include "stm32f4xx_hal.h" /* IWYU pragma: keep */
#include "uart_tx.h"
#include "ulog.h"

/* ========================= SHARED STATE =================================== */

/** Semaphore under test (binary, initial count 0, max count 1). */
static rtos_semaphore_t g_sem;

/** Startup gate: set to 1 by the startup timer. */
static volatile uint32_t g_test_started = 0;

/** Phase 2 gate: BenchTask signals this twice (once for SemHigh, once for SemLow). */
static rtos_semaphore_t g_phase2_sem;

/** Completion gate: signalled by BenchTask (Phase 1) and SemHigh (Phase 2). */
static rtos_semaphore_t g_all_done_sem;

/* ========================= PROFILING STATS ================================ */

/** Phase 1: signal+wait round-trip with no blocking. */
static rtos_profile_stat_t g_stat_uncontended = BENCH_STAT_INIT("semaphore_uncontended");

/** Phase 2: cycles from SemLow's signal call to SemHigh resuming. */
static rtos_profile_stat_t g_stat_contended = BENCH_STAT_INIT("semaphore_wake_latency");

/**
 * DWT timestamp captured by SemLow immediately before rtos_semaphore_signal().
 * volatile: prevents the compiler from reordering the write past the signal call.
 */
static volatile uint32_t g_signal_cycles = 0;

/* ========================= TASK FUNCTIONS ================================= */

/**
 * @brief BenchTask — runs Phase 1 (uncontended signal+wait round-trip)
 */
void BenchTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    /* --- Warmup: discarded iterations --- */
    for (uint32_t i = 0; i < BENCH_WARMUP; i++)
    {
        rtos_semaphore_signal(&g_sem);
        rtos_semaphore_wait(&g_sem, RTOS_SEM_NO_WAIT);
    }

    /* --- Measured phase: uncontended signal+wait --- */
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        RTOS_USER_PROFILE_START(sem);
        rtos_semaphore_signal(&g_sem);          /* count: 0 → 1 */
        rtos_semaphore_wait(&g_sem, RTOS_SEM_NO_WAIT); /* count: 1 → 0 (no block) */
        RTOS_USER_PROFILE_END(sem, &g_stat_uncontended);
    }

    /* Gate Phase 2 tasks. */
    rtos_semaphore_signal(&g_phase2_sem);
    rtos_semaphore_signal(&g_phase2_sem);

    /* Notify ResultTask that Phase 1 is done. */
    rtos_semaphore_signal(&g_all_done_sem);

    rtos_task_suspend(NULL);
}

/**
 * @brief SemLow — low-priority signaller for Phase 2
 *
 * Captures DWT immediately before signalling.  After signal, SemHigh preempts
 * (higher priority, was waiting), records wake time, then SemLow resumes.
 */
void SemLow(void *param)
{
    (void)param;

    rtos_semaphore_wait(&g_phase2_sem, RTOS_SEM_MAX_WAIT);

    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        /*
         * Capture DWT immediately before signal.
         * volatile ensures compiler does not reorder this write past signal().
         */
        g_signal_cycles = rtos_profiling_get_cycles();

        rtos_semaphore_signal(&g_sem);
        /*
         * SemHigh preempts here (higher priority, was blocked).
         * SemHigh reads DWT, records latency, loops back and blocks again.
         * SemLow resumes here after SemHigh re-blocks.
         */
    }

    rtos_task_suspend(NULL);
}

/**
 * @brief SemHigh — high-priority waiter for Phase 2
 *
 * Blocks on the semaphore until SemLow signals it, then reads DWT immediately
 * to compute the wake latency.
 */
void SemHigh(void *param)
{
    (void)param;

    rtos_semaphore_wait(&g_phase2_sem, RTOS_SEM_MAX_WAIT);

    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        /* Block until SemLow signals. */
        rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);

        /*
         * Read DWT as the first thing after waking.
         * g_signal_cycles was written by SemLow immediately before signal().
         */
        uint32_t wake_cycles = rtos_profiling_get_cycles();
        uint32_t latency     = wake_cycles - g_signal_cycles;
        rtos_profiling_record(&g_stat_contended, latency);
    }

    rtos_semaphore_signal(&g_all_done_sem);
    rtos_task_suspend(NULL);
}

/**
 * @brief ResultTask — prints both phase results after all tasks finish
 */
void ResultTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    /* Wait for Phase 1 (BenchTask) and Phase 2 (SemHigh) to finish. */
    rtos_semaphore_wait(&g_all_done_sem, RTOS_SEM_MAX_WAIT);
    rtos_semaphore_wait(&g_all_done_sem, RTOS_SEM_MAX_WAIT);

    bench_header("semaphore_uncontended");
    bench_report(&g_stat_uncontended);

    bench_header("semaphore_wake_latency");
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
    ulog_info("[BENCH] Startup hold complete — starting semaphore benchmark");
}

/* ========================= MAIN =========================================== */

int main(void)
{
    hardware_env_config();
    log_uart_init(LOG_LEVEL_INFO);

    rtos_init();
    rtos_profiling_init();

    ulog_init(ULOG_LEVEL_INFO);
    ulog_info("[BENCH] Semaphore Benchmark — " __DATE__ " " __TIME__);
    ulog_info("[BENCH] Iterations: %u  Warmup: %u", BENCH_ITERATIONS, BENCH_WARMUP);

    rtos_semaphore_init(&g_sem,          0, 1); /* binary semaphore, starts empty */
    rtos_semaphore_init(&g_phase2_sem,   0, 2);
    rtos_semaphore_init(&g_all_done_sem, 0, 2);

    rtos_task_handle_t handle;
    rtos_timer_handle_t startup_timer;

    /*
     * Task priority layout:
     *   SemHigh    (4) — high-priority waiter (Phase 2)
     *   BenchTask  (3) — uncontended benchmark (Phase 1)
     *   SemLow     (2) — low-priority signaller (Phase 2)
     *   ResultTask (1) — prints results
     *   LogFlush   (0) — drains ulog to UART
     */
    rtos_task_create(SemHigh,    "SemHigh",  RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 4, &handle);
    rtos_task_create(BenchTask,  "Bench",    RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &handle);
    rtos_task_create(SemLow,     "SemLow",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 2, &handle);
    rtos_task_create(ResultTask, "Result",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &handle);

    test_create_log_flush_task(&handle);
    test_create_startup_timer(startup_cb, NULL, &startup_timer);

    printf("[DBG] About to start scheduler\r\n");

    rtos_start_scheduler();

    while (1) {}
    return 0;
}
