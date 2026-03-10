/*******************************************************************************
 * File: tests/benchmarks/bench_queue/bench_queue.c
 * Description: Queue Message Delivery Latency Benchmark
 * Author: Student
 * Date: 2025
 *
 * WHAT IS MEASURED
 * ----------------
 * End-to-end message delivery latency: the time from the moment a producer
 * task captures a DWT timestamp and calls rtos_queue_send(), to the moment
 * the consumer task reads DWT after waking from rtos_queue_receive().
 *
 * This latency encompasses:
 *   1. Time inside rtos_queue_send() to copy the item and unblock the consumer.
 *   2. PendSV context switch (consumer has higher priority → immediate preempt).
 *   3. Time from context switch completion to the consumer's first DWT read.
 *
 * Message format: a single uint32_t containing the producer's DWT cycle count
 * at the moment of capture.  The consumer subtracts this from its own DWT
 * reading immediately after waking to get the one-way delivery latency.
 *
 * SCENARIO
 * --------
 *   Producer (priority 2):
 *     1. Captures ts = rtos_profiling_get_cycles()
 *     2. Calls rtos_queue_send(q, &ts, 0)           — queue was empty,
 *        consumer is blocked → consumer unblocked → PendSV fires
 *     3. Consumer preempts Producer inside queue_send
 *     4. After Consumer re-blocks, Producer's queue_send returns
 *     5. Producer loops for next iteration
 *
 *   Consumer (priority 3, higher than producer):
 *     1. Blocks on rtos_queue_receive(q, &buf, RTOS_MAX_DELAY)
 *     2. Wakes when Producer sends
 *     3. Reads DWT immediately: wake_ts = rtos_profiling_get_cycles()
 *     4. Computes latency = wake_ts - buf
 *     5. Records via rtos_profiling_record(&g_stat_queue_latency, latency)
 *     6. Loops back to receive (re-blocks on empty queue)
 *
 * After BENCH_ITERATIONS messages, Consumer signals g_done_sem.
 * ResultTask wakes, prints g_stat_queue_latency, and suspends.
 *
 * WHY CONSUMER IS HIGHER PRIORITY
 * ---------------------------------
 * With Consumer at priority 3 > Producer at priority 2, every queue_send
 * that finds a blocked consumer triggers an immediate context switch.  This
 * gives a deterministic, worst-case latency measurement that matches the
 * real-time scenario where data consumers must react as fast as possible.
 *
 * BUILD
 * -----
 *   pio run -e bench_queue -t upload
 *
 * EXPECTED OUTPUT (at 180 MHz, STM32F446RE)
 *   [BENCH] ===== queue_delivery_latency =====
 *   queue_delivery_latency | count=1000 | min=83cy(0us) max=127cy(1us) avg=91cy(1us)
 ******************************************************************************/

#include "VRTOS.h"
#include "bench_common.h"
#include "hardware_env.h"
#include "profiling.h"
#include "queue.h"
#include "semaphore.h"
#include "stm32f4xx_hal.h" /* IWYU pragma: keep */
#include "uart_tx.h"
#include "ulog.h"

/* ========================= SHARED STATE =================================== */

/** Message queue: holds up to 4 uint32_t timestamps (deep enough to never block producer). */
static rtos_queue_handle_t g_queue;

/** Startup gate: set to 1 by the startup timer. */
static volatile uint32_t g_test_started = 0;

/** Signalled by Consumer when all BENCH_ITERATIONS messages have been processed. */
static rtos_semaphore_t g_done_sem;

/* ========================= PROFILING STAT ================================= */

/** One-way message delivery latency (Producer DWT capture → Consumer DWT read on wake). */
static rtos_profile_stat_t g_stat_queue_latency = BENCH_STAT_INIT("queue_delivery_latency");

/* ========================= TASK FUNCTIONS ================================= */

/**
 * @brief Producer — sends DWT timestamps to the queue
 *
 * Runs BENCH_WARMUP iterations (discarded, no stat recording by Consumer
 * during this phase) followed by BENCH_ITERATIONS measured iterations.
 *
 * A volatile timestamp is captured immediately before queue_send to minimise
 * the gap between "clock read" and "send call".
 */
void ProducerTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    /* --- Warmup phase --- */
    for (uint32_t i = 0; i < BENCH_WARMUP; i++)
    {
        uint32_t ts = rtos_profiling_get_cycles();
        /*
         * Non-blocking send (timeout = 0): queue has capacity 4.  Consumer
         * drains it every iteration so the queue is always empty here.
         * During warmup we use non-blocking to avoid recording in the stat.
         */
        rtos_queue_send(g_queue, &ts, 0);
        /* Consumer runs and re-blocks before we get back here. */
    }

    /* --- Measured phase --- */
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        uint32_t ts = rtos_profiling_get_cycles();
        rtos_queue_send(g_queue, &ts, 0);
        /* Consumer preempts, processes, re-blocks; we resume here. */
    }

    rtos_task_suspend(NULL);
}

/**
 * @brief Consumer — measures delivery latency from each received message
 *
 * Blocks on the queue until a message arrives, reads DWT immediately on wake,
 * computes the delivery latency, and records it.  After BENCH_ITERATIONS
 * measured messages it signals g_done_sem.
 *
 * The Consumer ignores the first BENCH_WARMUP messages (warmup phase) by
 * not calling rtos_profiling_record() during that window.
 */
void ConsumerTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    uint32_t buf;

    /* --- Warmup: receive and discard --- */
    for (uint32_t i = 0; i < BENCH_WARMUP; i++)
    {
        rtos_queue_receive(g_queue, &buf, RTOS_MAX_DELAY);
        /* DWT delta discarded — warmup only. */
    }

    /* --- Measured phase --- */
    for (uint32_t i = 0; i < BENCH_ITERATIONS; i++)
    {
        rtos_queue_receive(g_queue, &buf, RTOS_MAX_DELAY);

        /*
         * Read DWT as the very first thing after waking.
         * buf holds the producer's DWT at time of capture.
         * The delta = delivery latency (queue_send path + PendSV + resume).
         */
        uint32_t wake_cycles = rtos_profiling_get_cycles();
        uint32_t latency     = wake_cycles - buf;
        rtos_profiling_record(&g_stat_queue_latency, latency);
    }

    rtos_semaphore_signal(&g_done_sem);
    rtos_task_suspend(NULL);
}

/**
 * @brief ResultTask — prints delivery latency statistics
 */
void ResultTask(void *param)
{
    (void)param;

    TEST_WAIT_FOR_START(g_test_started);

    rtos_semaphore_wait(&g_done_sem, RTOS_MAX_DELAY);

    bench_header("queue_delivery_latency");
    bench_report(&g_stat_queue_latency);

    ulog_info("[BENCH] Done.");

    rtos_task_suspend(NULL);
}

/* ========================= STARTUP TIMER ================================== */

static void startup_cb(void *timer_handle, void *param)
{
    (void)timer_handle;
    (void)param;
    g_test_started = 1;
    ulog_info("[BENCH] Startup hold complete — starting queue benchmark");
}

/* ========================= MAIN =========================================== */

int main(void)
{
    hardware_env_config();
    log_uart_init(LOG_LEVEL_INFO);

    rtos_init();
    rtos_profiling_init();

    ulog_init(ULOG_LEVEL_INFO);
    ulog_info("[BENCH] Queue Delivery Latency Benchmark — " __DATE__ " " __TIME__);
    ulog_info("[BENCH] Iterations: %u  Warmup: %u", BENCH_ITERATIONS, BENCH_WARMUP);

    rtos_queue_create(&g_queue, 4, sizeof(uint32_t));
    rtos_semaphore_init(&g_done_sem, 0, 1);

    rtos_task_handle_t handle;
    rtos_timer_handle_t startup_timer;

    /*
     * Task priority layout:
     *   ConsumerTask (3) — higher priority: preempts Producer on every send
     *   ProducerTask (2) — sends timestamps, runs when Consumer is blocked
     *   ResultTask   (1) — prints results after Consumer finishes
     *   LogFlush     (0) — drains ulog ring buffer to UART
     */
    rtos_task_create(ConsumerTask, "Consumer", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &handle);
    rtos_task_create(ProducerTask, "Producer", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 2, &handle);
    rtos_task_create(ResultTask,   "Result",   RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &handle);

    test_create_log_flush_task(&handle);
    test_create_startup_timer(startup_cb, NULL, &startup_timer);

    rtos_start_scheduler();

    while (1) {}
    return 0;
}
