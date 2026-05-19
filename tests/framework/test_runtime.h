#ifndef TEST_RUNTIME_H
#define TEST_RUNTIME_H

#include "test_suite.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_runtime.h
 * @brief Boilerplate for on-board test binaries.
 *
 * Replaces the hand-written init sequence in each old test_*.c (HAL,
 * log_uart_init, rtos_init, log_flush_task, startup-hold timer, scheduler
 * start). Drop into main() like:
 *
 * @code
 *   int main(void) {
 *       return test_runtime_main(&test_suite_mutex);
 *   }
 * @endcode
 *
 * The function:
 *   1. Initialises hardware (HAL + UART).
 *   2. Initialises the RTOS, ulog, and the log flush task.
 *   3. Creates a high-priority runner task whose body runs the suite and
 *      then halts the system once the SUITE_RESULT line has been emitted.
 *   4. Starts the scheduler (does not return).
 *
 * @return Never returns. Annotated noreturn at the call site.
 */
int test_runtime_main(const test_suite_t *suite);

/**
 * @brief Halt after the suite finishes — spins the calling task forever.
 *
 * The verdict line has already been emitted by @ref test_suite_run, so
 * the test_runner CLI can disconnect at any time. Spinning is preferred
 * over deleting the task so that any post-mortem inspection over SWD
 * still sees the runner task alive.
 */
__attribute__((noreturn)) void test_runtime_halt(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_RUNTIME_H */
