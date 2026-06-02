#ifndef RTOS_PROFILING_H
#define RTOS_PROFILING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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

void rtos_profiling_init(void);
uint32_t rtos_profiling_get_cycles(void);
void     rtos_profiling_reset_stat(rtos_profile_stat_t *stat, const char *name);
void     rtos_profiling_record(rtos_profile_stat_t *stat, uint32_t cycles);
void     rtos_profiling_print_stat(rtos_profile_stat_t *stat);

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

#if RTOS_PROFILING_USER_ENABLED
#define RTOS_USER_PROFILE_START(var_name)         RTOS_PROFILE_START(var_name)
#define RTOS_USER_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_END(var_name, stat_ptr)
#else
#define RTOS_USER_PROFILE_START(var_name)         RTOS_PROFILE_NOOP_START(var_name)
#define RTOS_USER_PROFILE_END(var_name, stat_ptr) RTOS_PROFILE_NOOP_END(var_name, stat_ptr)
#endif

#ifdef __cplusplus
}
#endif

#endif /* RTOS_PROFILING_H */
