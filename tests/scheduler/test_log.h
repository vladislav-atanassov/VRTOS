/*******************************************************************************
 * File: tests/scheduler/test_log.h
 * Description: Thread-Safe Test Logging — ulog-backed replacements for
 *              the printf-based test_log macros in uart_tx.h
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * uart_tx.h provides test_log_task / test_log_framework via raw printf().
 * printf is NOT reentrant: when a SysTick fires mid-printf and preemption
 * switches to another task that also calls printf, the internal state gets
 * corrupted and the system hangs.
 *
 * This header re-defines those macros to use the ulog() ring buffer, which
 * is protected by a critical section. A log_flush_task (priority 0) drains
 * the ring buffer to UART in the background.
 *
 * USAGE
 * -----
 * In every test source file, include this AFTER uart_tx.h:
 *
 *   #include "uart_tx.h"
 *   #include "test_log.h"   // overrides test_log_task / test_log_framework
 *
 * In main(), call test_create_log_flush_task() (from test_common.h) before
 * rtos_start_scheduler().
 ******************************************************************************/

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
