/*
 * Include this AFTER uart_tx.h to override its printf-based macros with
 * ulog() ring-buffer versions. printf is not reentrant — without this,
 * a SysTick preemption mid-printf corrupts state and hangs the system.
 *
 *   #include "uart_tx.h"
 *   #include "test_log.h"
 */

#ifndef TEST_LOG_H
#define TEST_LOG_H

#include "ulog.h" /* ulog(), ULOG_LEVEL_INFO */

#include <stdint.h>

/* Forward declaration (defined in rtos_types.h, available via VRTOS.h) */
uint32_t rtos_get_tick_count(void);

/* ---- Override the printf-based macros from uart_tx.h ---- */

#ifdef test_log
#undef test_log
#endif

#ifdef test_log_task
#undef test_log_task
#endif

#ifdef test_log_framework
#undef test_log_framework
#endif

/**
 * @brief Thread-safe test log — writes to ulog ring buffer
 *
 * Same tab-delimited format as the original printf version so the
 * Python test_runner.py parser works unchanged.
 */
#define test_log(level, tag, event, ctx)                                                                               \
    ulog(ULOG_LEVEL_INFO, "%08lu\t%s\t%s\t%d\t%s\t%s\t%s", (unsigned long) rtos_get_tick_count(), tag, __FILE__,       \
         __LINE__, __func__, event, ctx)

#define test_log_task(event, task_name) test_log(LOG_LEVEL_INFO, "TASK", event, task_name)

#define test_log_framework(event, test_name) test_log(LOG_LEVEL_INFO, "TEST", event, test_name)

#endif /* TEST_LOG_H */
