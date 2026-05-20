#ifndef TEST_ASSERT_H
#define TEST_ASSERT_H

#include "test_invariants.h"
#include "test_kassert_catcher.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_assert.h
 * @brief Three-tier assertion macros plus a KASSERT-firing catcher.
 *
 * | Macro                           | On failure                          |
 * |---------------------------------|-------------------------------------|
 * | @ref TEST_ASSERT                | record fail, longjmp out of case    |
 * | @ref TEST_EXPECT                | record fail, continue                |
 * | @ref TEST_ASSUME                | record skip, longjmp out of case    |
 * | @ref TEST_ASSERT_KASSERT_FIRES  | succeeds iff KASSERT triggers       |
 *
 * The longjmp targets are owned by the suite runner. Assertion macros are
 * safe to call only from the suite runner's task context (i.e. directly
 * inside a TEST_CASE body, or in code synchronously called from there).
 * Inside spawned test tasks, use @ref TEST_EXPECT — it never longjmps.
 */

/* Suite-runner-owned longjmp state. Defined in test_suite.c. */
extern jmp_buf  g_test_case_jmp_buf;
extern bool     g_test_case_jmp_active;

/* Reason codes encoded in the longjmp value. */
#define TEST_LONGJMP_HARD_FAIL 1
#define TEST_LONGJMP_SKIP      2

#define TEST_INTERNAL_TRIGGER_LONGJMP(reason)                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (g_test_case_jmp_active)                                                                                    \
        {                                                                                                              \
            longjmp(g_test_case_jmp_buf, (reason));                                                                    \
        }                                                                                                              \
    } while (0)

/**
 * @brief Hard assertion. On failure: record against @p inv_id and abort the
 *        current case (longjmp). The suite continues with the next case.
 */
#define TEST_ASSERT(cond, inv_id)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (cond)                                                                                                      \
        {                                                                                                              \
            test_inv_pass((inv_id));                                                                                   \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            test_inv_fail((inv_id), #cond);                                                                            \
            test_inv_mark_hard_fail((inv_id), __FILE__, __LINE__, #cond);                                              \
            TEST_INTERNAL_TRIGGER_LONGJMP(TEST_LONGJMP_HARD_FAIL);                                                     \
        }                                                                                                              \
    } while (0)

/**
 * @brief Soft assertion. On failure: record against @p inv_id but continue.
 *        Use when you want every invariant violation reported, not just the
 *        first one. Safe to call from spawned test tasks.
 */
#define TEST_EXPECT(cond, inv_id)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (cond)                                                                                                      \
        {                                                                                                              \
            test_inv_pass((inv_id));                                                                                   \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            test_inv_fail((inv_id), #cond);                                                                            \
        }                                                                                                              \
    } while (0)

/**
 * @brief Environmental precondition. If @p cond is false, the case is
 *        marked SKIPPED (not FAILED) and aborted.
 *
 * Example: TEST_ASSUME(rtos_scheduler_get_type() == PREEMPTIVE, "preemptive-only");
 */
#define TEST_ASSUME(cond, reason)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            test_inv_mark_skip((reason));                                                                              \
            TEST_INTERNAL_TRIGGER_LONGJMP(TEST_LONGJMP_SKIP);                                                          \
        }                                                                                                              \
    } while (0)

/**
 * @brief Negative test: prove RTOS_ASSERT fires when @p stmt runs.
 *
 * Wraps @p stmt in a setjmp; the framework's strong override of
 * @c rtos_assert_failed (test_kassert_catcher.c) longjmps back here when the
 * assert triggers. Records @p inv_id as passed iff the assert fired, failed
 * otherwise.
 *
 * Requires RTOS_ASSERT_ENABLED at build time. Only safe for asserts that
 * fire from synchronous code in the runner task before any state is mutated
 * (e.g. entry-point NULL checks) — the longjmp bypasses normal unwinding.
 */
#define TEST_ASSERT_KASSERT_FIRES(stmt, inv_id)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (setjmp(g_test_kassert_jmp_buf) == 0)                                                                       \
        {                                                                                                              \
            g_test_kassert_jmp_active = true;                                                                          \
            stmt;                                                                                                      \
            g_test_kassert_jmp_active = false;                                                                         \
            test_inv_fail((inv_id), "kassert did not fire for: " #stmt);                                               \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            /* longjmp path: catcher already cleared the flag and re-enabled IRQs */                                   \
            test_inv_pass((inv_id));                                                                                   \
        }                                                                                                              \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* TEST_ASSERT_H */
