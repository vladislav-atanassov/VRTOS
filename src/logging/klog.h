#ifndef KLOG_H
#define KLOG_H

#include "config.h"
#include "log.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef KLOG_BUFFER_SIZE
#define KLOG_BUFFER_SIZE 4096 /* Must be power of 2 */
#endif

#define KLOG_MAX_ARGS 4

/* klog_level_t is an alias for the shared log_level_t enum. */
typedef log_level_t klog_level_t;

/* Source-compatibility aliases — existing KLOG*() call sites unchanged. */
#define KLOG_LEVEL_FAULT LOG_LEVEL_FAULT
#define KLOG_LEVEL_ERROR LOG_LEVEL_ERROR
#define KLOG_LEVEL_WARN  LOG_LEVEL_WARN
#define KLOG_LEVEL_INFO  LOG_LEVEL_INFO
#define KLOG_LEVEL_DEBUG LOG_LEVEL_DEBUG
#define KLOG_LEVEL_TRACE LOG_LEVEL_TRACE

/**
 * @brief Fixed-size log packet.
 *
 * All string pointers reference Flash (.rodata) — safe to store without copying.
 * Args pre-extracted from the call site to keep klog_write ISR-safe with no
 * formatting in the hot path.
 */
typedef struct __attribute__((packed))
{
    uint32_t    timestamp_us; /* Microseconds since boot (wraps ~71 min) */
    uint16_t    line;         /* __LINE__ */
    uint8_t     level;        /* klog_level_t */
    uint8_t     cpu_context;  /* task ID or ISR number */
    const char *module;       /* Subsystem tag, e.g. "Kernel", "Queue" */
    const char *file;         /* __FILE__ */
    const char *fmt;          /* Format string in Flash */
    uint32_t    args[KLOG_MAX_ARGS];
} log_packet_t;

/* Runtime verbosity — lives in .noinit, survives NVIC_SystemReset() */
extern volatile uint8_t klog_verbosity;

/* Change/query runtime verbosity. */
void    klog_set_verbosity(uint8_t level);
uint8_t klog_get_verbosity(void);

/* ISR-safe. Never blocks, never allocates. Drops silently on full buffer. */
void klog_write(klog_level_t level, const char *module, const char *file, uint16_t line,
                const char *fmt, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

uint32_t klog_drain(log_packet_t *out, uint32_t max_records);

/* Internal: absorbs a dummy sentinel then picks the first 4 real args, padding with 0. */
#define KLOG_PAD(_d, a0, a1, a2, a3, ...) \
    (uint32_t)(a0), (uint32_t)(a1), (uint32_t)(a2), (uint32_t)(a3)

/* Runtime level check — module is the subsystem tag string.
 *
 * When RTOS_KLOG_ENABLED is 0 the macro compiles to a no-op and its arguments
 * are NOT evaluated, so every kernel KLOG* call site disappears from the build
 * (mirrors the rtos_assert.h convention). This is what removes the hot-path
 * verbosity branch from the scheduler/context-switch path. ULog is unaffected. */
#if RTOS_KLOG_ENABLED
#define KLOG(level, module, fmt, ...)                                   \
    do                                                                  \
    {                                                                   \
        if ((level) <= klog_verbosity)                                  \
            klog_write((level), (module), __FILE__, __LINE__, (fmt),    \
                       KLOG_PAD(_, ##__VA_ARGS__, 0, 0, 0, 0));        \
    } while (0)
#else
#define KLOG(level, module, fmt, ...) ((void) 0)
#endif

/* Shorthand wrappers — first arg is the module/subsystem tag */
#define KLOGF(module, fmt, ...) KLOG(KLOG_LEVEL_FAULT, (module), (fmt), ##__VA_ARGS__)
#define KLOGE(module, fmt, ...) KLOG(KLOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
#define KLOGW(module, fmt, ...) KLOG(KLOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
#define KLOGI(module, fmt, ...) KLOG(KLOG_LEVEL_INFO,  (module), (fmt), ##__VA_ARGS__)
#define KLOGD(module, fmt, ...) KLOG(KLOG_LEVEL_DEBUG, (module), (fmt), ##__VA_ARGS__)
#define KLOGT(module, fmt, ...) KLOG(KLOG_LEVEL_TRACE, (module), (fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* KLOG_H */
