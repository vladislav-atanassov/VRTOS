#ifndef TEST_RAND_H
#define TEST_RAND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_rand.h
 * @brief Reproducible xorshift32 PRNG for randomised stress tests.
 *
 * Build with -DTEST_SEED=<value> to pin the seed for a specific run.
 * Omit it (or set it to 0) for time-based entropy. Either way, the
 * seed actually used is logged at test_rand_init() time so any failing
 * run can be reproduced by passing -DTEST_SEED=<logged value>.
 *
 * Usage:
 * @code
 *   // In main:
 *   test_rand_init();           // logs "TEST-RNG seed=<N>"
 *
 *   // In a stress case:
 *   uint32_t delay = test_rand_range(1, 50);   // [1, 50] inclusive
 *   rtos_delay_ms(delay);
 * @endcode
 */

/**
 * @brief Seed the PRNG and log the seed value via ulog.
 *
 * If TEST_SEED is defined (and non-zero), that value is used. Otherwise
 * the current tick count is used as entropy; if the tick count is also
 * zero (very early call), falls back to a compile-time constant.
 *
 * Call once from test_runtime_main() after ulog_init().
 */
void test_rand_init(void);

/**
 * @brief Return the next 32-bit pseudorandom value (xorshift32).
 * Not ISR-safe (no critical section around the state update).
 */
uint32_t test_rand_u32(void);

/**
 * @brief Return a pseudorandom value in the inclusive range [lo, hi].
 * Undefined behaviour if lo > hi.
 */
uint32_t test_rand_range(uint32_t lo, uint32_t hi);

#ifdef __cplusplus
}
#endif

#endif /* TEST_RAND_H */
