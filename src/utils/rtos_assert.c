#include "rtos_assert.h"

#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <stm32f4xx_hal.h>

#if defined(__GNUC__)
#include "core_cm4.h"
#endif

#if RTOS_ASSERT_ENABLED

__attribute__((weak))
void rtos_assert_failed(const char *file, uint32_t line, const char *func, const char *expr)
{
#if defined(__GNUC__)
    __disable_irq();
#else
    __asm volatile("cpsid i" ::: "memory");
#endif

    /* Store in volatile statics so a debugger can inspect them */
    static volatile const char *assert_file = NULL;
    static volatile uint32_t    assert_line = 0;
    static volatile const char *assert_func = NULL;
    static volatile const char *assert_expr = NULL;

    assert_file = file;
    assert_line = line;
    assert_func = func;
    assert_expr = expr;

    (void) assert_file;
    (void) assert_line;
    (void) assert_func;
    (void) assert_expr;

#ifdef DEBUG
#if defined(__GNUC__)
    __BKPT(0);
#else
    __asm volatile("bkpt #0");
#endif
#endif

    while (1)
    {
#if defined(__GNUC__)
        __NOP();
#else
        __asm volatile("nop");
#endif
    }
}

#endif /* RTOS_ASSERT_ENABLED */
