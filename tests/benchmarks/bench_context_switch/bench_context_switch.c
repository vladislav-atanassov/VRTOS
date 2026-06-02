/*******************************************************************************
 * File: tests/benchmarks/bench_context_switch/bench_context_switch.c
 * Description: Context-switch latency benchmark — external GPIO + cross-task DWT
 * Author: Student
 * Date: 2026
 *
 * WHY THIS VARIANT
 * ----------------
 * The DWT bench (bench_context_switch) reads the kernel's own profiling stats,
 * which (a) measure only sub-parts of the switch and (b) bake the profiling
 * instrumentation into the number. This variant measures the FULL task->task
 * switch with ZERO kernel instrumentation, two complementary ways:
 *
 *   1) GPIO toggle (external, the apples-to-apples number for cross-RTOS
 *      comparison). PingA drives the probe pin HIGH then yields; PingB drives
 *      it LOW then yields. On a logic analyzer the **HIGH pulse width** is one
 *      PingA->PingB switch: yield trigger + PendSV save/restore + the HW
 *      exception entry/exit + a single GPIO store. (The LOW width additionally
 *      includes PingB's DWT bookkeeping, so measure the HIGH pulses.)
 *
 *   2) Cross-task DWT (on-serial sanity number). PingA samples CYCCNT into a
 *      shared word immediately before yielding; PingB samples CYCCNT the instant
 *      it resumes and records the delta. That delta is one full A->B switch,
 *      timed entirely from task context — the kernel switch path is untouched.
 *
 * BUILD FOR A CLEAN NUMBER
 * ------------------------
 * The kernel's switch path only loses its profiling overhead when the *library*
 * is built without it. Configure with:
 *     cmake -B build/stm32f446re_nucleo -DKARTOS_PROFILING=OFF [-DKARTOS_LTO=ON]
 * then build/flash this variant. (rtos_profiling_get_cycles/record stay
 * available regardless — only the kernel's in-path instrumentation is gated.)
 *
 * PROBE POINT
 * -----------
 *   PA5 — Nucleo LD2 / Arduino header D13. Push-pull output (MX_GPIO_Init);
 *   this benchmark bumps it to very-high speed for crisp edges.
 *
 * SCHEDULER
 * ---------
 * RTOS_SCHEDULER_PREEMPTIVE_SP. Equal-priority tasks switch only on rtos_yield,
 * and the FIFO ready list alternates them — so each yield is exactly one switch.
 ******************************************************************************/

#include "KARTOS.h"
#include "bench_common.h"
#include "hardware_env.h"
#include "profiling.h"
#include "semaphore.h"
#include "stm32f4xx_hal.h" /* IWYU pragma: keep — GPIOA */
#include "ulog.h"

/* ========================= PROBE PIN (PA5) ================================ */

#define PROBE_PIN    5U
#define PROBE_HIGH() (GPIOA->BSRR = (1U << PROBE_PIN))
#define PROBE_LOW()  (GPIOA->BSRR = (1U << (PROBE_PIN + 16U)))

static void probe_init(void)
{
    /* MX_GPIO_Init already made PA5 a push-pull output; raise the slew rate to
     * very-high so the toggle edges stay sharp under a logic analyzer. */
    GPIOA->OSPEEDR |= (3U << (PROBE_PIN * 2U));
    PROBE_LOW();
}

/* ========================= SHARED STATE =================================== */

/** Opened by the startup timer once the serial monitor has had time to attach. */
static volatile uint32_t g_started = 0;

/** Cleared by PingB when the measured burst is complete; tells PingA to stop. */
static volatile uint32_t g_run = 1;

/** CYCCNT captured by PingA immediately before it yields (switch start). */
static volatile uint32_t g_switch_start = 0;

/** One full PingA->PingB switch, in cycles (DWT, no kernel instrumentation). */
static rtos_profile_stat_t g_switch = BENCH_STAT_INIT("SwitchGPIO");

/** Released by PingB after the burst so ResultTask can print. */
static rtos_semaphore_t g_done;

/* ========================= TASKS ========================================= */

/**
 * @brief PingA — drives the probe HIGH, timestamps, and yields to PingB.
 *
 * The CYCCNT read is the very last thing before the yield, so g_switch_start
 * marks the instant the switch begins. Stops (suspends) once PingB clears
 * g_run at the end of the burst.
 */
void PingA(void *param)
{
    (void) param;
    TEST_WAIT_FOR_START(g_started);

    for (;;)
    {
        PROBE_HIGH();
        g_switch_start = rtos_profiling_get_cycles();
        rtos_yield(); /* PingA -> PingB */

        if (!g_run)
        {
            rtos_task_suspend(NULL);
        }
    }
}

/**
 * @brief PingB — timestamps on resume, drives the probe LOW, records the switch.
 *
 * The CYCCNT read is the first thing after the switch completes, so
 * (now - g_switch_start) is one full PingA->PingB switch. Owns the warmup /
 * measure / stop sequence.
 */
void PingB(void *param)
{
    (void) param;
    TEST_WAIT_FOR_START(g_started);

    uint32_t warm = 0;
    uint32_t meas = 0;

    for (;;)
    {
        uint32_t now = rtos_profiling_get_cycles(); /* switch end */
        PROBE_LOW();

        if (warm < BENCH_WARMUP)
        {
            if (++warm == BENCH_WARMUP)
            {
                rtos_profiling_reset_stat(&g_switch, "SwitchGPIO");
            }
        }
        else if (meas < BENCH_ITERATIONS)
        {
            rtos_profiling_record(&g_switch, now - g_switch_start);
            if (++meas == BENCH_ITERATIONS)
            {
                g_run = 0; /* tell PingA to stop after its next resume */
                rtos_semaphore_signal(&g_done);
            }
        }

        rtos_yield(); /* PingB -> PingA (PingA suspends here once g_run == 0) */

        if (!g_run)
        {
            rtos_task_suspend(NULL);
        }
    }
}

/**
 * @brief ResultTask — prints the cross-task DWT stat once the burst finishes.
 *
 * Runs at priority 1 (below the ping tasks). Once both ping tasks have
 * suspended, this is the highest-priority ready task; after it prints and
 * suspends, the priority-0 LogFlush task drains ulog to UART.
 */
void ResultTask(void *param)
{
    (void) param;
    TEST_WAIT_FOR_START(g_started);

    rtos_semaphore_wait(&g_done, RTOS_MAX_DELAY);

    bench_header("context_switch");
    ulog_info("[BENCH] Probe PA5 (LD2 / Arduino D13): HIGH pulse width = one task->task switch");
    ulog_info("[BENCH] Build with -DKARTOS_PROFILING=OFF for the zero-observer number.");

    /* Reprint every second so a serial monitor attached at any time sees the
     * result (the measured burst is one-shot; this loop is just for display).
     * The 1 s delay lets the priority-0 LogFlush task drain ulog between prints. */
    for (;;)
    {
        bench_report(&g_switch); /* one full PingA->PingB switch (cycles) */
        rtos_delay_ms(1000);
    }
}

/* ========================= STARTUP TIMER ================================== */

static void startup_cb(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_started = 1;
    ulog_info("[BENCH] Startup hold complete — starting GPIO context switch benchmark");
}

/* ========================= MAIN ========================================== */

int main(void)
{
    hardware_env_config();

    rtos_init();
    rtos_profiling_init(); /* enable DWT->CYCCNT (used directly; system profiling may be OFF) */
    probe_init();

    ulog_info("[BENCH] Context Switch Benchmark (GPIO) — " __DATE__ " " __TIME__);
    ulog_info("[BENCH] Iterations: %u  Warmup: %u  Probe: PA5 (LD2/D13)", BENCH_ITERATIONS, BENCH_WARMUP);

    rtos_semaphore_init(&g_done, 0, 1);

    rtos_task_handle_t  handle;
    rtos_timer_handle_t startup_timer;

    /*
     * Priority layout:
     *   PingA / PingB (3) — equal priority, yield to each other (one switch each)
     *   ResultTask    (1) — prints after the burst, once the ping tasks suspend
     *   LogFlush      (0) — drains the ulog ring to UART
     */
    rtos_task_create(PingA, "PingA", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &handle);
    rtos_task_create(PingB, "PingB", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 3, &handle);
    rtos_task_create(ResultTask, "Result", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &handle);

    test_create_startup_timer(startup_cb, NULL, &startup_timer);

    rtos_start_scheduler();

    while (1)
    {
    }
    return 0;
}
