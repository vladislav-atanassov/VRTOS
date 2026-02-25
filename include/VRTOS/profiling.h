/*******************************************************************************
 * File: include/VRTOS/profiling.h
 * Description: Profiling and WCET Measurement API
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_PROFILING_H
#define RTOS_PROFILING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* =============================================================================
 * PROFILING CONFIGURATION
 * ============================================================================= */

/**
 * @brief Enable/disable RTOS system profiling
 *
 * When enabled, the RTOS will profile internal operations:
 * - Context switch time
 * - Scheduler decision time
 * - Tick handler time
 *
 * Useful for understanding RTOS overhead on your specific controller.
 * Disable in production for minimal overhead.
 */
#ifndef RTOS_PROFILING_SYSTEM_ENABLED
#define RTOS_PROFILING_SYSTEM_ENABLED 1
#endif

/**
 * @brief Enable/disable user/application profiling
 *
 * When enabled, user code can use RTOS_USER_PROFILE_START/END macros
 * to measure execution time of application code blocks.
 *
 * Can be independently enabled/disabled from system profiling.
 */
#ifndef RTOS_PROFILING_USER_ENABLED
#define RTOS_PROFILING_USER_ENABLED 1
#endif

/* =============================================================================
 * PROFILING DATA STRUCTURES
 * ============================================================================= */

/**
 * @brief Profiling statistics structure
 */
typedef struct
{
    uint32_t    min_cycles;
    uint32_t    max_cycles;
    uint32_t    total_cycles; /* Be careful of overflow */
    uint32_t    count;
    const char *name;
} rtos_profile_stat_t;

/**
 * @brief Snapshot of profiling statistics for programmatic access
 *
 * Provides an atomic copy of stats converted to both cycles and microseconds,
 * suitable for assertions in tests without parsing log output.
 */
typedef struct
{
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t avg_cycles;
    uint32_t count;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t avg_us;
} rtos_profile_snapshot_t;

/* =============================================================================
 * PROFILING API FUNCTIONS
 * ============================================================================= */

/**
 * @brief Initialize profiling (Enable DWT Cycle Counter)
 * @note Must be called before using any profiling macros
 */
void rtos_profiling_init(void);

/**
 * @brief Get current CPU cycle count
 * @return Cycle count (DWT_CYCCNT)
 */
uint32_t rtos_profiling_get_cycles(void);

/**
 * @brief Reset a statistics structure
 * @param stat Pointer to stat structure
 * @param name Name of the block being profiled
 */
void rtos_profiling_reset_stat(rtos_profile_stat_t *stat, const char *name);

/**
 * @brief Record a measurement
 * @param stat Pointer to stat structure
 * @param cycles Cycles elapsed for the block
 */
void rtos_profiling_record(rtos_profile_stat_t *stat, uint32_t cycles);

/**
 * @brief Print profiling statistics to log
 * @param stat Pointer to stat structure
 */
void rtos_profiling_print_stat(rtos_profile_stat_t *stat);

/**
 * @brief Take an atomic snapshot of profiling statistics
 * @param stat Pointer to stat structure
 * @param out Pointer to snapshot output structure
 */
void rtos_profiling_snapshot(const rtos_profile_stat_t *stat, rtos_profile_snapshot_t *out);

/**
 * @brief Print all RTOS system statistics (Context Switch, Scheduler, Tick)
 * @note Only prints stats if RTOS_PROFILING_SYSTEM_ENABLED is 1
 */
void rtos_profiling_report_system_stats(void);

/* =============================================================================
 * CORE PROFILING MACROS
 * ============================================================================= */

#define RTOS_PROFILE_START(var_name) uint32_t var_name##_start = rtos_profiling_get_cycles()

#define RTOS_PROFILE_END(var_name, stat_ptr)                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        uint32_t var_name##_end  = rtos_profiling_get_cycles();                                                        \
        uint32_t var_name##_diff = var_name##_end - var_name##_start;                                                  \
        rtos_profiling_record(stat_ptr, var_name##_diff);                                                              \
    } while (0)

#define RTOS_PROFILE_NOOP_START(var_name)         ((void) 0)
#define RTOS_PROFILE_NOOP_END(var_name, stat_ptr) ((void) 0)

/* =============================================================================
 * SYSTEM PROFILING MACROS (RTOS Internal Use)
 *
 * Controlled by RTOS_PROFILING_SYSTEM_ENABLED
 * ============================================================================= */

#if RTOS_PROFILING_SYSTEM_ENABLED
#define RTOS_SYS_PROFILE_START(var_name)         RTOS_PROFILE_START(var_name)
#define RTOS_SYS_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_END(var_name, stat_ptr)
#else
#define RTOS_SYS_PROFILE_START(var_name)         RTOS_PROFILE_NOOP_START(var_name)
#define RTOS_SYS_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_NOOP_END(var_name, stat_ptr)
#endif

/* =============================================================================
 * USER/APPLICATION PROFILING MACROS
 *
 * Controlled by RTOS_PROFILING_USER_ENABLED
 * ============================================================================= */

#if RTOS_PROFILING_USER_ENABLED
#define RTOS_USER_PROFILE_START(var_name)         RTOS_PROFILE_START(var_name)
#define RTOS_USER_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_END(var_name, stat_ptr)
#else
#define RTOS_USER_PROFILE_START(var_name)         RTOS_PROFILE_NOOP_START(var_name)
#define RTOS_USER_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_NOOP_END(var_name, stat_ptr)
#endif

/* =============================================================================
 * SYSTEM PROFILING STATISTICS (RTOS Internal)
 * ============================================================================= */

#if RTOS_PROFILING_SYSTEM_ENABLED
extern rtos_profile_stat_t g_prof_context_switch;
extern rtos_profile_stat_t g_prof_scheduler;
extern rtos_profile_stat_t g_prof_tick;
extern rtos_profile_stat_t g_prof_pendsv_full;
extern rtos_profile_stat_t g_prof_tick_jitter;
extern rtos_profile_stat_t g_prof_scheduling_latency;

/** Global written by PendSV ASM to pass full context switch cycle count to C */
extern volatile uint32_t g_pendsv_cycles;

/** Global used by PendSV ASM to hold the start cycle count across the switch */
extern volatile uint32_t g_pendsv_start_cycles;
#endif

#ifdef __cplusplus
}
#endif

#endif /* RTOS_PROFILING_H */
