#ifndef LOG_H
#define LOG_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdio.h>

typedef enum { 
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_PRINT,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_ALL
} log_level_t;

extern log_level_t g_log_level;

void log_uart_init(log_level_t level);

/* Internal macro */
#define log_printf(level, tag, msg, ...)                                                                               \
    do {                                                                                                               \
        if (g_log_level >= level) {                                                                                    \
            printf("[" tag "] %s:%d:%s(): " msg "\r\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);                  \
        }                                                                                                              \
    } while (0)

/* Public logging macros */
#define log_print(msg, ...) log_printf(LOG_LEVEL_PRINT, "PRINT", msg, ##__VA_ARGS__)
#define log_error(msg, ...) log_printf(LOG_LEVEL_ERROR, "ERROR", msg, ##__VA_ARGS__)
#define log_info(msg, ...) log_printf(LOG_LEVEL_INFO, "INFO", msg, ##__VA_ARGS__)
#define log_debug(msg, ...) log_printf(LOG_LEVEL_DEBUG, "DEBUG", msg, ##__VA_ARGS__)

#endif /* LOG_H */
