/*******************************************************************************
 * File: tests/scheduler/test_common.h
 * Description: Common test infrastructure for scheduler tests
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "VRTOS.h"          // IWYU pragma: keep (rtos_delay_ms, RTOS_DEFAULT_TASK_STACK_SIZE)
#include "log_flush_task.h" // log_flush_task
#include "task.h"           // rtos_task_create
#include "timer.h"          // rtos_timer_handle_t, rtos_timer_create, rtos_timer_start
#include "ulog.h"           // ulog_init

/*
 * NOTE: We deliberately do NOT include test_log.h here.
 * test_log.h must be included AFTER uart_tx.h in every .c file
 * so that its #undef / #define sequence correctly overrides the
 * printf-based macros with ulog-based ones.  Including it here
 * would process the #undefs before uart_tx.h is parsed, allowing
 * uart_tx.h to re-define the printf versions and causing
 * reentrancy hangs under preemption.
 *
 * The macros below (TEST_ASSERT, test_emit_verdict, etc.) use
 * test_log_task / test_log_framework, which are only resolved
 * at the call site in each .c file — by which point test_log.h
 * has already run.
 */

#include <stdbool.h>
#include <stdint.h>

/* =================== Assertion Infrastructure =================== */

/**
 * @brief Per-translation-unit failure counter.
 *
 * Static so each test .c file gets its own copy (intentional).
 * Incremented inside a critical section by TEST_ASSERT on failure.
 */
static volatile uint32_t g_fail_count = 0;

/**
 * @brief Assert a boolean condition and log the verdict.
 *
 * Logs ASSERT_PASS or ASSERT_FAIL with a description string.
 * Does NOT halt on failure so remaining invariants are still checked.
 */
#define TEST_ASSERT(condition, description)                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (condition)                                                                                                 \
        {                                                                                                              \
            test_log_task("ASSERT_PASS", (description));                                                               \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            test_log_task("ASSERT_FAIL", (description));                                                               \
            rtos_port_enter_critical();                                                                                \
            g_fail_count++;                                                                                            \
            rtos_port_exit_critical();                                                                                 \
        }                                                                                                              \
    } while (0)

/** @brief Assert that a task is in BLOCKED state. */
#define ASSERT_BLOCKED(handle, desc) TEST_ASSERT(rtos_task_get_state((handle)) == RTOS_TASK_STATE_BLOCKED, (desc))

/** @brief Assert that a task is NOT in RUNNING state. */
#define ASSERT_NOT_RUNNING(handle, desc) TEST_ASSERT(rtos_task_get_state((handle)) != RTOS_TASK_STATE_RUNNING, (desc))

/** @brief Assert that a task is in a specific state. */
#define ASSERT_STATE(handle, expected_state, desc)                                                                     \
    TEST_ASSERT(rtos_task_get_state((handle)) == (expected_state), (desc))

/**
 * @brief Emit the final RESULT PASS / RESULT FAIL:<N> verdict line.
 *
 * The verdict is written DIRECTLY to the UART data register by
 * polling, bypassing both the ulog ring buffer AND the interrupt-
 * driven _write() TX ring buffer.
 *
 * Why: _write() spin-waits for the TXE ISR to drain its buffer.
 * If called with interrupts disabled, or concurrently with the
 * flush task, it hangs or corrupts the TX buffer.  Polling the
 * UART DR register is fully self-contained and deterministic.
 *
 * The format matches test_log_framework exactly so the Python
 * test_runner.py parser works unchanged:
 *   <tick>\tTEST\t<file>\t<line>\t<func>\tRESULT\t<verdict>\r\n
 */
#define TEST_EMIT_VERDICT()                                                                                                  \
    do                                                                                                                       \
    {                                                                                                                        \
        extern int _write(int file, char *ptr, int len);                                                                     \
        char       _vline[192];                                                                                              \
        int        _vlen;                                                                                                    \
        if (g_fail_count == 0)                                                                                               \
        {                                                                                                                    \
            _vlen = snprintf(_vline, sizeof(_vline), "%08lu\tTEST\t%s\t%d\t%s\tRESULT\tPASS\r\n",                            \
                             (unsigned long) rtos_get_tick_count(), __FILE__, __LINE__, __func__);                           \
        }                                                                                                                    \
        else                                                                                                                 \
        {                                                                                                                    \
            _vlen = snprintf(_vline, sizeof(_vline), "%08lu\tTEST\t%s\t%d\t%s\tRESULT\tFAIL:%lu\r\n",                        \
                             (unsigned long) rtos_get_tick_count(), __FILE__, __LINE__, __func__,                            \
                             (unsigned long) g_fail_count);                                                                  \
        }                                                                                                                    \
        if (_vlen > 0 && _vlen < (int) sizeof(_vline))                                                                       \
        {                                                                                                                    \
            /*                                                                                                            \
             * Enter critical section to prevent preemption: the flush                                                    \
             * task may be mid-_write() and we must not touch the TX                                                      \
             * ring buffer (which _write uses).                                                                           \
             *                                                                                                            \
             * uart_tx_flush() drains any pending TX buffer data by                                                       \
             * polling DR directly (works with IRQs disabled).                                                            \
             *                                                                                                            \
             * Then we write the verdict byte-by-byte to USART2->DR,                                                     \
             * bypassing the TX ring buffer entirely.                                                                     \
             */ \
            rtos_port_enter_critical();                                                                                      \
            uart_tx_flush();                                                                                                 \
            for (int _vi = 0; _vi < _vlen; _vi++)                                                                            \
            {                                                                                                                \
                while (!(USART2->SR & USART_SR_TXE))                                                                         \
                {                                                                                                            \
                }                                                                                                            \
                USART2->DR = (uint8_t) _vline[_vi];                                                                          \
            }                                                                                                                \
            while (!(USART2->SR & USART_SR_TC))                                                                              \
            {                                                                                                                \
            }                                                                                                                \
            rtos_port_exit_critical();                                                                                       \
        }                                                                                                                    \
    } while (0)

/**
 * @brief Startup hold time before test begins
 *
 * Gives the serial monitor time to connect after flashing.
 * An RTOS one-shot timer fires after this duration to gate
 * test tasks via g_test_started flag.
 */
#define TEST_STARTUP_HOLD_MS (2000U)

/**
 * @brief Poll-wait macro for test tasks
 *
 * Each task calls this at the top of its function to wait
 * for the startup timer to fire before beginning real work.
 * Uses rtos_delay_ms to yield CPU while waiting.
 */
#define TEST_WAIT_FOR_START(started_flag)                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        while (!(started_flag))                                                                                        \
        {                                                                                                              \
            rtos_delay_ms(100);                                                                                        \
        }                                                                                                              \
    } while (0)

/**
 * @brief Create and start the startup hold timer
 *
 * @param callback  Timer callback (should set started flag, log BEGIN, and
 *                  start the test timeout timer via param)
 * @param param     User parameter forwarded to callback (typically &test_timer)
 * @param p_handle  Pointer to timer handle storage
 * @return rtos_status_t RTOS_SUCCESS on success
 */
static inline rtos_status_t test_create_startup_timer(void (*callback)(void *, void *), void *param,
                                                      rtos_timer_handle_t *p_handle)
{
    rtos_status_t status =
        rtos_timer_create("StartupHold", TEST_STARTUP_HOLD_MS, RTOS_TIMER_ONE_SHOT, callback, param, p_handle);
    if (status != RTOS_SUCCESS)
    {
        return status;
    }
    return rtos_timer_start(*p_handle);
}

/**
 * @brief Initialize thread-safe logging and create the flush task
 *
 * Must be called in each test's main() after rtos_init() and before
 * rtos_start_scheduler(). Initializes the ulog ring buffer and creates
 * a low-priority flush task that drains log output to UART.
 *
 * @param p_handle  Pointer to task handle storage (can be a throwaway variable)
 * @return rtos_status_t RTOS_SUCCESS on success
 */
static inline rtos_status_t test_create_log_flush_task(rtos_task_handle_t *p_handle)
{
    ulog_init(ULOG_LEVEL_INFO);
    return rtos_task_create(log_flush_task, "LogFlush", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 0, p_handle);
}

#endif /* TEST_COMMON_H */
