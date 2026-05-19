#ifndef TEST_WATCHDOG_H
#define TEST_WATCHDOG_H

#include "test_invariants.h"
#include "rtos_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_watchdog.h
 * @brief Per-phase watchdog for on-board test cases.
 *
 * Each TEST_AWAIT_PHASE block gets an independent one-shot timer budget.
 * When the timer fires, @ref g_test_aborted is set and the blocked
 * test_signal_wait / test_phase_wait calls return false (their effective
 * timeout was clamped to the watchdog deadline). The failure is recorded as
 * "INV-WATCHDOG:<phase_name>" into the running case's invariant table.
 *
 * The suite runner calls @ref test_watchdog_disarm after every case body
 * (even after a TEST_ASSERT longjmp) to ensure the timer never leaks into
 * the next case.
 *
 * Usage:
 * @code
 *   TEST_AWAIT_PHASE("pip-case", 2000, {
 *       test_signal_wait(&g_case_done, RTOS_MAX_DELAY);
 *   });
 *
 *   TEST_AWAIT_PHASE("spin-hold", 500, {
 *       while (!g_flag && !g_test_aborted) rtos_yield();
 *   });
 * @endcode
 */

/**
 * Abort flag set by the watchdog ISR when a phase expires.
 * Reset to false at the start of each @ref test_watchdog_arm call.
 * Check in spinloops: `while (!done && !g_test_aborted) rtos_yield();`
 */
extern volatile bool g_test_aborted;

/**
 * Absolute tick deadline for the currently armed phase.
 * 0 = no active watchdog. Used internally by test_sync.c to clamp
 * blocking wait durations so they return when the budget expires.
 */
extern volatile rtos_tick_t g_test_watchdog_deadline;

/**
 * @brief Arm the per-phase watchdog.
 *
 * Resets g_test_aborted, records the phase name (used to build the
 * "INV-WATCHDOG:<name>" invariant ID), and starts or restarts the
 * single one-shot software timer. Creates the timer lazily on first call.
 *
 * @param phase_name  Short label used in the failure ID (string literal or
 *                    long-lived pointer — stored by reference, not copied).
 * @param budget_ms   Maximum allowed duration in milliseconds.
 */
void test_watchdog_arm(const char *phase_name, uint32_t budget_ms);

/**
 * @brief Disarm the watchdog and record a failure if it fired.
 *
 * Stops the timer, clears the deadline, and — if g_test_aborted is set
 * and not yet recorded — calls test_inv_fail with the phase's watchdog ID.
 * Then resets g_test_aborted for the next phase or case.
 *
 * Idempotent: safe to call multiple times (e.g., once by the macro and
 * once by the suite-runner cleanup after a TEST_ASSERT longjmp).
 */
void test_watchdog_disarm(void);

/**
 * @brief Execute @p expr under a watchdog budget of @p budget_ms.
 *
 * Arms the watchdog before @p expr, disarms it after. If the watchdog
 * fires during @p expr, g_test_aborted is set (spinloops and
 * test_signal_wait return), and "INV-WATCHDOG:<phase_name>" is recorded
 * as a failed invariant by test_watchdog_disarm().
 *
 * @note Do not put a return statement inside @p expr — it will skip the
 *       disarm call and leave the timer running.
 */
#define TEST_AWAIT_PHASE(phase_name, budget_ms, expr)                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        test_watchdog_arm((phase_name), (uint32_t)(budget_ms));                                                        \
        do                                                                                                             \
        {                                                                                                              \
            expr;                                                                                                      \
        } while (0);                                                                                                   \
        test_watchdog_disarm();                                                                                        \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* TEST_WATCHDOG_H */
