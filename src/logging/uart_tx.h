#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <stdio.h> // IWYU pragma: keep

typedef enum
{
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_PROFILE,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_ALL
} log_level_t;

extern log_level_t g_log_level;

void log_uart_init(log_level_t level);
void uart_tx_flush(void);
void uart_rx_process_commands(void);

/* vsnprintf + direct _write — bypasses newlib's broken stdio buffering. */
void uart_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Internal macro */
#define log_printf(level, tag, msg, ...)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (g_log_level >= level)                                                                                      \
        {                                                                                                              \
            uart_printf("[" tag "] %s:%d:%s(): " msg "\r\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);             \
        }                                                                                                              \
    } while (0)

/* Public logging macros */
#define log_print(msg, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        uart_printf("[PRINT] " msg "\r\n", ##__VA_ARGS__);                                                             \
    } while (0)

#define log_profile(msg, ...) log_printf(LOG_LEVEL_PROFILE, "PROFILE", msg, ##__VA_ARGS__)
#define log_error(msg, ...)   log_printf(LOG_LEVEL_ERROR, "ERROR", msg, ##__VA_ARGS__)
#define log_info(msg, ...)    log_printf(LOG_LEVEL_INFO, "INFO", msg, ##__VA_ARGS__)
#define log_debug(msg, ...)   log_printf(LOG_LEVEL_DEBUG, "DEBUG", msg, ##__VA_ARGS__)

#endif /* LOG_H */
