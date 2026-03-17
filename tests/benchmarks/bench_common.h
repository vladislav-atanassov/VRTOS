/*
 * Include test_log.h AFTER uart_tx.h in each benchmark .c file — not here.
 * Results are printed via rtos_profiling_print_stat() after all iterations.
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include "config.h"       /* RTOS_DEFAULT_TASK_STACK_SIZE, RTOS_SCHEDULER_* */
#include "profiling.h"    /* rtos_profile_stat_t, rtos_profiling_*, macros  */
#include "test_common.h"  /* test_create_startup_timer, test_create_log_flush_task */

#include <stdint.h>

/* Overridable: -D BENCH_ITERATIONS=500 */
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
