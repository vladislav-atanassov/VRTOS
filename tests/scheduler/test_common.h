/*******************************************************************************
 * File: tests/scheduler/test_common.h
 * Description: Common test infrastructure for scheduler tests
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "VRTOS.h" // IWYU pragma: keep (rtos_delay_ms)
#include "timer.h" // rtos_timer_handle_t, rtos_timer_create, rtos_timer_start

#include <stdbool.h>

/**
 * @brief Startup hold time before test begins
 *
 * Gives the serial monitor time to connect after flashing.
 * An RTOS one-shot timer fires after this duration to gate
 * test tasks via g_test_started flag.
 */
#define TEST_STARTUP_HOLD_MS (5000U)

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

#endif /* TEST_COMMON_H */
