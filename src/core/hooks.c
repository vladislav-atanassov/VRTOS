/**
 * Default implementations of the application hooks declared in rtos_hooks.h.
 * All four default to no-op — overrides are additive on top of the kernel's
 * existing KLOG calls at the same sites, so behaviour is unchanged unless the
 * application provides a strong definition.
 *
 * On ELF targets (arm-none-eabi firmware build) the defaults are weak, so an
 * application TU may provide a strong same-named definition and the linker
 * picks it. The host build uses MinGW/PE, where `__attribute__((weak))` puts
 * the body in a `.weak.<name>.` section and exports the public symbol as a
 * "weak external" that ld won't resolve from the same object — leaving
 * undefined references at link time. The host has no need to override these
 * hooks, so we compile the defaults as strong symbols there.
 */

#include "rtos_hooks.h"

#if defined(__ELF__)
#define KARTOS_HOOK_WEAK __attribute__((weak))
#else
#define KARTOS_HOOK_WEAK /* strong on PE/COFF and other formats where weak is unreliable */
#endif

KARTOS_HOOK_WEAK void rtos_application_idle_hook(void)
{
}

KARTOS_HOOK_WEAK void rtos_application_tick_hook(rtos_tick_t tick)
{
    (void) tick;
}

KARTOS_HOOK_WEAK void rtos_application_stack_overflow_hook(rtos_task_handle_t task)
{
    (void) task;
}

KARTOS_HOOK_WEAK void rtos_application_malloc_failed_hook(size_t requested_size, rtos_heap_id_t heap)
{
    (void) requested_size;
    (void) heap;
}

KARTOS_HOOK_WEAK void rtos_application_deadlock_hook(rtos_task_handle_t waiter, rtos_mutex_t *mutex)
{
    (void) waiter;
    (void) mutex;
}
