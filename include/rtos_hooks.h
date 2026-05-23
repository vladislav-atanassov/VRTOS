#ifndef RTOS_HOOKS_H
#define RTOS_HOOKS_H

#include "memory.h"     /* rtos_heap_id_t */
#include "mutex.h"      /* rtos_mutex_t */
#include "rtos_types.h" /* rtos_tick_t, rtos_task_handle_t */

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Application hooks. Each is a weak symbol with a no-op default. To take
 * action, provide a strong definition (same prototype, no weak attribute) in
 * application code; the linker will prefer it over the kernel default.
 *
 * The kernel still emits a KLOG at every hook fire site, so overrides are
 * additive — turning a hook off (or leaving the default in place) does not
 * suppress kernel diagnostics.
 */

/**
 * @brief Called once per iteration of the idle task, after any pending
 *        stack reclaim and before the CPU enters sleep (WFI or tickless).
 *        Must not call blocking RTOS APIs.
 */
void rtos_application_idle_hook(void);

/**
 * @brief Called from the SysTick ISR after the tick counter is incremented.
 *        Runs in ISR context; keep it short. Only ISR-safe APIs allowed.
 * @param tick The just-incremented kernel tick count.
 */
void rtos_application_tick_hook(rtos_tick_t tick);

/**
 * @brief Called when the per-task stack canary is observed corrupted.
 *        Currently fires from rtos_task_check_stack(); MR-7 will additionally
 *        invoke it from the context-switch path.
 * @param task The task whose canary failed (NULL is possible during the
 *             "check all" sweep if invoked that way).
 */
void rtos_application_stack_overflow_hook(rtos_task_handle_t task);

/**
 * @brief Called from rtos_malloc_from() before returning NULL on OOM. The
 *        allocator still returns NULL after the hook so existing callers'
 *        graceful error paths continue to work.
 * @param requested_size The size argument passed to rtos_malloc_from() (not
 *                       the internally-padded block size).
 * @param heap           The heap side the request targeted.
 */
void rtos_application_malloc_failed_hook(size_t requested_size, rtos_heap_id_t heap);

/**
 * @brief Called from rtos_mutex_lock() when acquiring `mutex` would close a
 *        cycle in the wait-for graph. The lock attempt is aborted and the
 *        caller receives RTOS_MUTEX_ERR_DEADLOCK; the hook runs first so the
 *        application can log, halt, or record diagnostics.
 * @param waiter The task that tried to lock the mutex.
 * @param mutex  The mutex whose acquisition would close the cycle.
 */
void rtos_application_deadlock_hook(rtos_task_handle_t waiter, rtos_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_HOOKS_H */
