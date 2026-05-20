#ifndef TEST_KASSERT_CATCHER_H
#define TEST_KASSERT_CATCHER_H

#include <setjmp.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_kassert_catcher.h
 * @brief setjmp/longjmp plumbing that lets a test prove a KASSERT fires.
 *
 * The framework provides a strong override of `rtos_assert_failed` in
 * test_kassert_catcher.c. When @ref g_test_kassert_jmp_active is true, the
 * override longjmps back to the buffer instead of halting. Tests arm the
 * catcher via @ref TEST_ASSERT_KASSERT_FIRES; the macro:
 *   1. setjmp-saves the current context,
 *   2. flips the flag on,
 *   3. runs @p stmt (which is expected to KASSERT),
 *   4. on longjmp, re-enables interrupts and records the invariant as passed,
 *   5. on no-longjmp, records the invariant as failed.
 *
 * Only useful for asserts that fire from synchronous code in the runner's
 * task context (entry-point parameter checks, e.g. NULL handle). KASSERTs
 * from inside critical sections leave kernel state inconsistent across
 * the longjmp — the catcher is NOT a general-purpose recovery mechanism.
 */

extern jmp_buf       g_test_kassert_jmp_buf;
extern volatile bool g_test_kassert_jmp_active;

#ifdef __cplusplus
}
#endif

#endif /* TEST_KASSERT_CATCHER_H */
