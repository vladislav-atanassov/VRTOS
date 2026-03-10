/*******************************************************************************
 * File: tests/benchmarks/bench_common.h
 * Description: Shared infrastructure for VRTOS benchmark executables
 * Author: Student
 * Date: 2025
 *
 * PURPOSE
 * -------
 * Provides common macros, defaults, and startup infrastructure shared across
 * all VRTOS benchmark programs in tests/benchmarks/.
 *
 * Benchmarks are standalone executables (separate platformio environments)
 * that measure cycle-accurate timing of RTOS primitives using the DWT-based
 * profiling subsystem already built into VRTOS.
 *
 * DESIGN NOTES
 * ------------
 * - Deliberately does NOT include test_log.h here.
 *   test_log.h must be included AFTER uart_tx.h in every .c file so that
 *   its #undef/#define sequence correctly overrides printf-based macros.
 *   Each benchmark .c file is responsible for its own include order.
 *
 * - Reuses test_common.h infrastructure (startup timer, log flush task)
 *   for consistent startup behavior across tests and benchmarks.
 *
 * - Output goes to ulog (ring buffer), drained by the LogFlush task.
 *   Benchmark results are printed with rtos_profiling_print_stat() after
 *   all measurement iterations complete.
 ******************************************************************************/

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include "config.h"       /* RTOS_DEFAULT_TASK_STACK_SIZE, RTOS_SCHEDULER_* */
#include "profiling.h"    /* rtos_profile_stat_t, rtos_profiling_*, macros  */
#include "test_common.h"  /* test_create_startup_timer, test_create_log_flush_task */

#include <stdint.h>

/* ========================= BENCHMARK CONFIGURATION ======================== */

/**
 * @brief Number of measured iterations per benchmark.
 *
 * Overridable via build flag: -D BENCH_ITERATIONS=500
 * 1000 iterations gives a statistically stable min/max/avg at low overhead.
 */
#ifndef BENCH_ITERATIONS
#define BENCH_ITERATIONS (1000U)
#endif

/**
 * @brief Number of warmup iterations run before measurement begins.
 *
 * Warmup excludes cold-cache and branch-predictor effects from the results.
 * These iterations are discarded — stats are reset after warmup completes.
 */
#ifndef BENCH_WARMUP
#define BENCH_WARMUP (50U)
#endif

/* ========================= OUTPUT HELPERS ================================== */

/**
 * @brief Print a benchmark section header via ulog.
 *
 * @param name  Human-readable benchmark name string literal
 *
 * Example output:
 *   [BENCH] ===== context_switch =====
 */
#define bench_header(name) ulog_info("[BENCH] ===== " name " =====")

/**
 * @brief Print benchmark results for a single stat using the standard
 *        profiling formatter, prefixed with a [BENCH] tag.
 *
 * Wraps rtos_profiling_print_stat() which outputs:
 *   <name> | count=N | min=Xcy(Yus) max=Xcy(Yus) avg=Xcy(Yus)
 *
 * @param stat_ptr  Pointer to rtos_profile_stat_t to report
 */
#define bench_report(stat_ptr) rtos_profiling_print_stat(stat_ptr)

/* ========================= STAT INITIALIZER ================================ */

/**
 * @brief Static initializer for a profiling stat structure.
 *
 * Sets min to UINT32_MAX (sentinel for "no measurement yet"), all others 0.
 *
 * @param label  String name for the stat (stored in stat->name)
 */
#define BENCH_STAT_INIT(label) \
    { .min_cycles = UINT32_MAX, .max_cycles = 0U, .total_cycles = 0U, .count = 0U, .name = (label) }

#endif /* BENCH_COMMON_H */
