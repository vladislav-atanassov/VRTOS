#ifndef LOG_H
#define LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Shared log level enum used by both KLog and ULog backends.
 *
 * Values are ordered: lower number = higher severity.
 * FAULT (0) is always logged; TRACE (5) requires maximum verbosity.
 */
typedef enum
{
    LOG_LEVEL_FAULT = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,
} log_level_t;

/**
 * @brief Initialise whichever logging backends are enabled.
 *
 * Calls klog_backend_init() and/or ulog_backend_init() depending on
 * RTOS_KLOG_ENABLED / RTOS_ULOG_ENABLED.  Safe to call before the
 * scheduler starts.  Replaces the separate klog_init() + ulog_init()
 * call sites at the kernel boundary.
 */
void log_init(void);

/**
 * @brief Drain all enabled backends and emit buffered output to UART.
 *
 * Called once per flush period by log_flush_task.
 */
void log_flush_drain(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
