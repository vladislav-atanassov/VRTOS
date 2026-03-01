/*******************************************************************************
 * File: src/utils/ulog.h
 * Description: User Logger — Mutex-Protected Formatted Logging
 ******************************************************************************/

#ifndef ULOG_H
#define ULOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================= CONFIGURATION ============================== */

#ifndef ULOG_BUFFER_SIZE
#define ULOG_BUFFER_SIZE 1024 /* Must be power of 2 */
#endif

#ifndef ULOG_LINE_MAX
#define ULOG_LINE_MAX 128 /* Max formatted line length */
#endif

/* ================================= TYPES ================================== */

typedef enum
{
    ULOG_LEVEL_NONE = 0,
    ULOG_LEVEL_ERROR,
    ULOG_LEVEL_WARN,
    ULOG_LEVEL_INFO,
    ULOG_LEVEL_DEBUG,
} ulog_level_t;

/* ================================== API =================================== */

/**
 * @brief Initialize the user logger
 * @param level Minimum log level to emit
 * @note Must be called after rtos_init() since it uses RTOS mutexes
 */
void ulog_init(ulog_level_t level);

/**
 * @brief Write a formatted log message (task-context only)
 *
 * Formats the message into a stack-local scratch buffer via vsnprintf,
 * then copies it into the ULog ring buffer under mutex protection.
 * The actual UART output is deferred to the flush task.
 *
 * @warning NOT ISR-safe — must only be called from task context.
 *
 * @param level Log level
 * @param fmt   printf-style format string
 * @param ...   Format arguments
 */
void ulog(ulog_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/**
 * @brief Drain formatted text from the ULog ring buffer
 * @param buf     Destination buffer
 * @param max_len Maximum bytes to read
 * @return Number of bytes actually read
 */
uint32_t ulog_drain(uint8_t *buf, uint32_t max_len);

/**
 * @brief Check if the ULog ring buffer has pending data
 * @return true if there is data to drain
 */
uint32_t ulog_pending(void);

/* ============================ SHORTHAND MACROS ============================ */

#define ulog_error(msg, ...) ulog(ULOG_LEVEL_ERROR, "[E] " msg, ##__VA_ARGS__)
#define ulog_warn(msg, ...)  ulog(ULOG_LEVEL_WARN, "[W] " msg, ##__VA_ARGS__)
#define ulog_info(msg, ...)  ulog(ULOG_LEVEL_INFO, "[I] " msg, ##__VA_ARGS__)
#define ulog_debug(msg, ...) ulog(ULOG_LEVEL_DEBUG, "[D] " msg, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ULOG_H */
