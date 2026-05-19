#ifndef TEST_SUITE_H
#define TEST_SUITE_H

#include "test_assert.h"
#include "test_invariants.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_suite.h
 * @brief Suite + multiple test cases per binary.
 *
 * Modeled after Zephyr's ztest. One binary = one suite. One suite = N cases,
 * each checking 1-3 invariants. Cases share setup/teardown lifecycle hooks
 * but every case has its own invariant table and its own verdict.
 *
 * Usage:
 * @code
 *   TEST_CASE(mutex, basic_lock_unlock) {
 *       TEST_INV_DECLARE("INV-MUTEX-OWNER", 1);
 *       TEST_ASSERT(rtos_mutex_lock(&m, 0) == 0, "INV-MUTEX-OWNER");
 *       TEST_EXPECT(m.owner == self, "INV-MUTEX-OWNER");
 *       rtos_mutex_unlock(&m);
 *   }
 *
 *   static const test_case_t *const mutex_cases[] = {
 *       &TEST_CASE_REF(mutex, basic_lock_unlock),
 *   };
 *
 *   TEST_SUITE_DEFINE(mutex, mutex_cases,
 *       .setup    = mutex_setup,
 *       .before   = mutex_before,
 *       .after    = mutex_after);
 *
 *   int main(void) {
 *       test_runtime_init();
 *       test_suite_run(&test_suite_mutex);
 *       test_runtime_halt();
 *   }
 * @endcode
 */

typedef void (*test_lifecycle_fn_t)(void);
typedef void (*test_case_body_fn_t)(void);

typedef struct
{
    const char         *name;
    test_case_body_fn_t body;
} test_case_t;

typedef struct
{
    const char                *name;
    const char                *source_file;
    test_lifecycle_fn_t        setup;     /**< Runs once before any case. May be NULL. */
    test_lifecycle_fn_t        teardown;  /**< Runs once after the last case. May be NULL. */
    test_lifecycle_fn_t        before;    /**< Runs before every case. May be NULL. */
    test_lifecycle_fn_t        after;     /**< Runs after every case. May be NULL. */
    const test_case_t *const  *cases;
    size_t                     case_count;
} test_suite_t;

/* ---- Suite / case declaration macros ---- */

/** Token-pasted symbol name for a case, used by @ref TEST_CASE_REF. */
#define TEST_CASE_SYMBOL(suite, casename) test_case_##suite##_##casename

/** Address-of-friendly reference to a case definition (use with `&`). */
#define TEST_CASE_REF(suite, casename) TEST_CASE_SYMBOL(suite, casename)

/**
 * Declares a test case and emits a const @ref test_case_t descriptor.
 * The braces after the macro form the case body.
 */
#define TEST_CASE(suite, casename)                                                                                     \
    static void test_case_body_##suite##_##casename(void);                                                             \
    const test_case_t TEST_CASE_SYMBOL(suite, casename) = {                                                            \
        .name = #casename,                                                                                             \
        .body = test_case_body_##suite##_##casename,                                                                   \
    };                                                                                                                 \
    static void test_case_body_##suite##_##casename(void)

/**
 * Define a suite descriptor. @p cases_array must be a static array of
 * `const test_case_t *` whose name is visible at the call site (sizeof is
 * applied to it directly). Pass lifecycle hooks via designated initializers
 * in @p __VA_ARGS__:
 *   TEST_SUITE_DEFINE(mutex, mutex_cases, .setup = mutex_setup);
 */
#define TEST_SUITE_DEFINE(suite, cases_array, ...)                                                                     \
    const test_suite_t test_suite_##suite = {                                                                          \
        .name        = #suite,                                                                                         \
        .source_file = __FILE__,                                                                                       \
        .cases       = (cases_array),                                                                                  \
        .case_count  = sizeof(cases_array) / sizeof((cases_array)[0]),                                                 \
        __VA_ARGS__                                                                                                    \
    }

/* ---- Per-case invariant declaration ---- */

/**
 * Declare an invariant the case intends to record passes/fails against.
 *
 * @param id          Stable invariant identifier (string literal).
 * @param min_checks  Minimum observations required for the case to PASS.
 *                    Use >= 1 to turn unexercised invariants into failures.
 */
#define TEST_INV_DECLARE(id, min_checks) test_inv_declare((id), (uint16_t) (min_checks))

#define TEST_INV_PASS(id)               test_inv_pass((id))
#define TEST_INV_FAIL(id, detail)       test_inv_fail((id), (detail))

/* ---- Runner ---- */

/**
 * @brief Run every case in @p suite sequentially.
 *
 * For each case the runner: invokes @c before, sets up a setjmp barrier,
 * begins the invariant table, runs the case body, computes the verdict,
 * emits the CASE_RESULT line, invokes @c after. After all cases, emits
 * SUITE_RESULT.
 *
 * Returns the suite verdict: 0 if every case passed (skips count as pass-equivalent
 * at the suite level), non-zero if any case failed.
 */
int test_suite_run(const test_suite_t *suite);

#ifdef __cplusplus
}
#endif

#endif /* TEST_SUITE_H */
