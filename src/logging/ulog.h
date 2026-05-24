#ifndef ULOG_H
#define ULOG_H

#include "log.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef ULOG_BUFFER_SIZE
#define ULOG_BUFFER_SIZE 1024 /* Must be power of 2 */
#endif

#ifndef ULOG_LINE_MAX
#define ULOG_LINE_MAX 128 /* Max formatted line length */
#endif

/* ulog_level_t is an alias for the shared log_level_t enum. */
typedef log_level_t ulog_level_t;

/* Source-compatibility aliases for ULOG_LEVEL_* spellings.
 * Note: ULOG_LEVEL_NONE maps to LOG_LEVEL_FAULT (0), not a "silence all"
 * sentinel — there are no callers that relied on the old silence semantics. */
#define ULOG_LEVEL_NONE  LOG_LEVEL_FAULT
#define ULOG_LEVEL_ERROR LOG_LEVEL_ERROR
#define ULOG_LEVEL_WARN  LOG_LEVEL_WARN
#define ULOG_LEVEL_INFO  LOG_LEVEL_INFO
#define ULOG_LEVEL_DEBUG LOG_LEVEL_DEBUG

/* Runtime verbosity — lives in .noinit, survives NVIC_SystemReset().
 * Symmetric with klog_verbosity; independent control of each channel. */
extern volatile uint8_t ulog_verbosity;

/* Change/query runtime verbosity. */
void    ulog_set_verbosity(uint8_t level);
uint8_t ulog_get_verbosity(void);

/* NOT ISR-safe. Output is deferred to the flush task via ring buffer.
 * Prefer the ulog_*() macros below — they early-out before the function
 * call when the level is filtered, avoiding varargs push overhead. */
void ulog(ulog_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Per-level macros — early-out at the call site against ulog_verbosity. */
#define ulog_fault(msg, ...) \
    do { if (LOG_LEVEL_FAULT <= ulog_verbosity) ulog(LOG_LEVEL_FAULT, "[F] " msg, ##__VA_ARGS__); } while (0)
#define ulog_error(msg, ...) \
    do { if (LOG_LEVEL_ERROR <= ulog_verbosity) ulog(LOG_LEVEL_ERROR, "[E] " msg, ##__VA_ARGS__); } while (0)
#define ulog_warn(msg, ...)  \
    do { if (LOG_LEVEL_WARN  <= ulog_verbosity) ulog(LOG_LEVEL_WARN,  "[W] " msg, ##__VA_ARGS__); } while (0)
#define ulog_info(msg, ...)  \
    do { if (LOG_LEVEL_INFO  <= ulog_verbosity) ulog(LOG_LEVEL_INFO,  "[I] " msg, ##__VA_ARGS__); } while (0)
#define ulog_debug(msg, ...) \
    do { if (LOG_LEVEL_DEBUG <= ulog_verbosity) ulog(LOG_LEVEL_DEBUG, "[D] " msg, ##__VA_ARGS__); } while (0)
#define ulog_trace(msg, ...) \
    do { if (LOG_LEVEL_TRACE <= ulog_verbosity) ulog(LOG_LEVEL_TRACE, "[T] " msg, ##__VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ULOG_H */
