/*******************************************************************************
 * File: tests/benchmarks/bench_context_switch/bench_context_switch.c
 * Description: Context Switch Latency Benchmark
 * Author: Student
 * Date: 2025
 *
 * WHAT IS MEASURED
 * ----------------
 * The full cost of one preemptive context switch: from the moment PendSV fires
 * to the moment the incoming task resumes execution.  This is the PendSV
 * handler time — saving the outgoing task's register set (R4-R11, EXC_RETURN,
 * optional FPU regs S16-S31) and restoring the incoming task's set.
 *
 * The kernel already measures this in assembly inside PendSV_Handler and
 * accumulates it in g_prof_context_switch (see src/profiling/profiling.c and
 * src/port/cortex_m4/port.c).  This benchmark simply triggers a controlled
 * number of context switches and reads that stat.  Using the kernel's own
 * measurement avoids adding any observer effect to the switch path.
 *
 * SCENARIO
 * --------
 * Two tasks (BenchA and BenchB) at identical priority call rtos_yield() in a
 * tight loop.  Each yield triggers exactly one context switch (A→B or B→A).
 * After BENCH_ITERATIONS yields per task, both tasks suspend themselves.
 * A lower-priority ResultTask wakes up, snapshots g_prof_context_switch, and
 * prints the statistics.
 *
 * WARMUP
 * ------
 * Before the measured phase, each task calls rtos_yield() BENCH_WARMUP times.
 * After warmup, BenchA resets all system stats (atomic, inside critical section)
 * and both tasks enter the measured phase.  The reset in BenchA is safe because
 * BenchA runs to completion of the reset before yielding to BenchB (equal
 * priority, preemptive_sp scheduler will not preempt without a yield).
 *
 * SCHEDULER
 * ---------
 * Built with RTOS_SCHEDULER_TYPE=RTOS_SCHEDULER_PREEMPTIVE_SP.
 * Equal-priority tasks only switch when one calls rtos_yield().
 *
 * BUILD
 * -----
 *   pio run -e bench_context_switch -t upload
 *
 * EXPECTED OUTPUT (at 180 MHz, STM32F446RE)
 *   [BENCH] ===== context_switch =====
 *   context_switch | count=2000 | min=47cy(0us) max=63cy(0us) avg=52cy(0us)
 ******************************************************************************/

#include "VRTOS.h"
#include "bench_common.h"
#include "hardware_env.h"
#include "profiling.h"
#include "semaphore.h"
#include "stm32f4xx_hal.h" /* IWYU pragma: keep */
#include "uart_tx.h"
#include "ulog.h"

/* ========================= SYNCHRONIZATION ================================ */

/** Set to 1 by the startup timer callback to ungate benchmark tasks. */
static volatile uint32_t g_test_started = 0;

/** Counting semaphore: each bench task signals once when its loop finishes. */
static rtos_semaphore_t g_done_sem;

/**
 * Flag written by BenchA to signal that warmup is complete and stats have
 * been reset.  BenchB polls this before entering its measured loop so that
 * it does not record iterations before BenchA's reset.
 */
static volatile uint32_t g_warmup_done = 0;

/* ========================= TASK FUNCTIONS ================================= */

/**
 * @brief BenchA — primary yield task (runs first at equal priority)
 *
 * Responsibilities:
 *  1. Wait for startup gate.
 *  2. Run BENCH_WARMUP yields (discarded).
 *  3. Reset system profiling stats atomically.
 *  4. Signal BenchB to enter measured phase.
 *  5. Run BENCH_ITERATIONS yields (measured by kernel).
 *  6. Signal g_done_sem and suspend.
 */
void BenchA(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    /* --- Warmup phase (results discarded) --- */
    for (uint32_t i = 0; i < BENCH_WARMUP; i++)
    {
        rtos_yield();
    }

    /*
     * Reset all system profiling stats to clear warmup noise.
     * BenchA runs this without yielding first, so it completes atomically
     * before BenchB can enter its measured loop.
     */
    rtos_profiling_reset_system_stats();

    /* Signal BenchB: warmup is done, measured phase begins now. */
    g_warmup_done = 1;

    /* --- Measured phase --- */
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        rtos_yield(); /* triggers A→B or B→A context switch */
    }

    rtos_semaphore_signal(&g_done_sem);
    rtos_task_suspend(NULL);
}

/**
 * @brief BenchB — secondary yield task (mirrors BenchA)
 *
 * Waits for g_warmup_done before entering its measured loop so that it
 * does not record spurious context switches from warmup.
 */
void BenchB(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    /* Wait until BenchA has completed warmup and reset stats. */
    while (!g_warmup_done)
    {
        rtos_yield();
    }

    /* --- Measured phase --- */
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        rtos_yield();
    }

    rtos_semaphore_signal(&g_done_sem);
    rtos_task_suspend(NULL);
}

/**
 * @brief ResultTask — prints statistics after both bench tasks finish
 *
 * Runs at priority 1 (below bench tasks).  Waits on g_done_sem twice
 * (once per bench task), then snapshots and prints g_prof_context_switch.
 */
void ResultTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    /* Block until both BenchA and BenchB have finished. */
    rtos_semaphore_wait(&g_done_sem, RTOS_MAX_DELAY);
    rtos_semaphore_wait(&g_done_sem, RTOS_MAX_DELAY);

    bench_header("context_switch");

    /*
     * g_prof_context_switch is updated by the kernel in PendSV_Handler.
     * It reflects the full save+restore cost of the context switch, measured
     * in assembly with minimal observer effect.
     */
    bench_report(&g_prof_context_switch);

    ulog_info("[BENCH] Done. Total measured switches: %lu",
              (unsigned long)g_prof_context_switch.count);

    rtos_task_suspend(NULL);
}

/* ========================= STARTUP TIMER ================================== */

/** One-shot timer callback: opens the gate for all benchmark tasks. */
static void startup_cb(void *timer_handle, void *param)
{
    (void)timer_handle;
    (void)param;
    g_test_started = 1;
    ulog_info("[BENCH] Startup hold complete — starting context switch benchmark");
}

/* ========================= MAIN =========================================== */

int main(void)
{
    hardware_env_config();
    log_uart_init(LOG_LEVEL_INFO);

    rtos_init();
    rtos_profiling_init();

    ulog_init(ULOG_LEVEL_INFO);
    ulog_info("[BENCH] Context Switch Benchmark — " __DATE__ " " __TIME__);
    ulog_info("[BENCH] Iterations: %u  Warmup: %u", BENCH_ITERATIONS, BENCH_WARMUP);

    /* Binary semaphore used as a 2-count release gate. */
    rtos_semaphore_init(&g_done_sem, 0, 2);

    rtos_task_handle_t handle;
    rtos_timer_handle_t startup_timer;

    /*
     * Task priority layout:
     *   BenchA / BenchB  (3) — equal priority, yield to each other
     *   ResultTask       (1) — prints results after bench tasks suspend
     *   LogFlush         (0) — drains ulog ring buffer to UART
     */
    rtos_task_create(BenchA,      "BenchA",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &handle);
    rtos_task_create(BenchB,      "BenchB",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &handle);
    rtos_task_create(ResultTask,  "Result",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &handle);

    test_create_log_flush_task(&handle);
    test_create_startup_timer(startup_cb, NULL, &startup_timer);

    rtos_start_scheduler();

    /* Should not reach here */
    while (1) {}
    return 0;
}
