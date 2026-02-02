#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <stdio.h> // IWYU pragma: keep

typedef enum
{
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_ALL
} log_level_t;

extern log_level_t g_log_level;

void log_uart_init(log_level_t level);

/* Internal macro */
#define log_printf(level, tag, msg, ...)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (g_log_level >= level)                                                                                      \
        {                                                                                                              \
            printf("[" tag "] %s:%d:%s(): " msg "\r\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);                  \
        }                                                                                                              \
    } while (0)

/* Public logging macros */
#define log_print(msg, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        printf("[PRINT] " msg "\r\n", ##__VA_ARGS__);                                                                  \
    } while (0)

#define log_error(msg, ...) log_printf(LOG_LEVEL_ERROR, "ERROR", msg, ##__VA_ARGS__)
#define log_info(msg, ...)  log_printf(LOG_LEVEL_INFO, "INFO", msg, ##__VA_ARGS__)
#define log_debug(msg, ...) log_printf(LOG_LEVEL_DEBUG, "DEBUG", msg, ##__VA_ARGS__)

/* =================== Test Logging Macros =================== */
/* Tab-delimited format for easy CSV parsing by Python tools */
/* Format: timestamp_ms \t level \t file \t line \t func \t event \t context */

/* Forward declaration to get tick count (defined in rtos_types.h) */
uint32_t rtos_get_tick_count(void);

/**
 * @brief Generic test log macro with tab-delimited output
 * @param level Log level (LOG_LEVEL_*)
 * @param tag Level tag string (e.g., "INFO")
 * @param event Event type (e.g., "START", "RUN", "END")
 * @param ctx Context string (e.g., task name)
 */
#define test_log(level, tag, event, ctx)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (g_log_level >= level)                                                                                      \
        {                                                                                                              \
            printf("%08lu\t%s\t%s\t%d\t%s\t%s\t%s\r\n", (unsigned long) rtos_get_tick_count(), tag, __FILE__,          \
                   __LINE__, __func__, event, ctx);                                                                    \
        }                                                                                                              \
    } while (0)

/**
 * @brief Log task execution events
 * @param event Event type: "START", "RUN", "DELAY", "END"
 * @param task_name Name of the task
 */
#define test_log_task(event, task_name) test_log(LOG_LEVEL_INFO, "TASK", event, task_name)

/**
 * @brief Log test framework events
 * @param event Event type: "BEGIN", "END", "TIMEOUT"
 * @param test_name Name of the test
 */
#define test_log_framework(event, test_name) test_log(LOG_LEVEL_INFO, "TEST", event, test_name)

#endif /* LOG_H */
