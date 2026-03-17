#ifndef RTOS_PROFILING_H
#define RTOS_PROFILING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Profiles context switch, scheduler, and tick handler times. Disable in production. */
#ifndef RTOS_PROFILING_SYSTEM_ENABLED
#define RTOS_PROFILING_SYSTEM_ENABLED 1
#endif

/* When enabled, user code can use RTOS_USER_PROFILE_START/END macros. */
#ifndef RTOS_PROFILING_USER_ENABLED
#define RTOS_PROFILING_USER_ENABLED 1
#endif

typedef struct
{
    uint32_t    min_cycles;
    uint32_t    max_cycles;
    uint32_t    total_cycles; /* Be careful of overflow */
    uint32_t    count;
    const char *name;
} rtos_profile_stat_t;

/* Atomic snapshot in both cycles and microseconds — use in tests. */
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

void rtos_profiling_init(void);
uint32_t rtos_profiling_get_cycles(void);
void     rtos_profiling_reset_stat(rtos_profile_stat_t *stat, const char *name);
void     rtos_profiling_record(rtos_profile_stat_t *stat, uint32_t cycles);
void     rtos_profiling_print_stat(rtos_profile_stat_t *stat);
void     rtos_profiling_snapshot(const rtos_profile_stat_t *stat, rtos_profile_snapshot_t *out);
void     rtos_profiling_report_system_stats(void);
void     rtos_profiling_reset_system_stats(void);

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

#if RTOS_PROFILING_SYSTEM_ENABLED
#define RTOS_SYS_PROFILE_START(var_name)         RTOS_PROFILE_START(var_name)
#define RTOS_SYS_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_END(var_name, stat_ptr)
#else
#define RTOS_SYS_PROFILE_START(var_name)         RTOS_PROFILE_NOOP_START(var_name)
#define RTOS_SYS_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_NOOP_END(var_name, stat_ptr)
#endif

#if RTOS_PROFILING_USER_ENABLED
#define RTOS_USER_PROFILE_START(var_name)         RTOS_PROFILE_START(var_name)
#define RTOS_USER_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_END(var_name, stat_ptr)
#else
#define RTOS_USER_PROFILE_START(var_name)         RTOS_PROFILE_NOOP_START(var_name)
#define RTOS_USER_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_NOOP_END(var_name, stat_ptr)
#endif

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
