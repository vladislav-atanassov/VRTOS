#include "test_kassert_catcher.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx.h"

/* State owned by TEST_ASSERT_KASSERT_FIRES. Defined here rather than in the
 * macro's TU so a single setjmp buffer is visible to both the test case and
 * the strong override of rtos_assert_failed below. */
jmp_buf       g_test_kassert_jmp_buf;
volatile bool g_test_kassert_jmp_active = false;

/*
 * Strong override of the kernel's weak rtos_assert_failed (see
 * src/utils/rtos_assert.c). Linker picks this one because the test
 * framework is linked into every test variant.
 *
 * Behavior:
 *   - If a TEST_ASSERT_KASSERT_FIRES block has armed the catcher, re-enable
 *     interrupts (the kernel disabled them on entry) and longjmp back so the
 *     test case can record the invariant as passed.
 *   - Otherwise behave like the production handler: halt with IRQs off so a
 *     debugger can inspect.
 */
void rtos_assert_failed(const char *file, uint32_t line, const char *func, const char *expr)
{
    (void) file;
    (void) line;
    (void) func;
    (void) expr;

    if (g_test_kassert_jmp_active)
    {
        g_test_kassert_jmp_active = false;
        __enable_irq();
        longjmp(g_test_kassert_jmp_buf, 1);
        /* unreachable */
    }

    /* No catcher armed: mimic production halt. */
    __disable_irq();
    while (1)
    {
        __NOP();
    }
}
