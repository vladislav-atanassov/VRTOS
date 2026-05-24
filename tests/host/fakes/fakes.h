#ifndef HOST_TEST_FAKES_H
#define HOST_TEST_FAKES_H

/*
 * Single-header inventory of every kernel/port/HAL/heap symbol the
 * production .c files under test need at link time. Implemented in fakes.c
 * via the Fake Function Framework (FFF, vendor/fff.h).
 *
 * Each test's setUp() should `RESET_FAKE` the fakes it cares about and
 * `FFF_RESET_HISTORY()` at the top. Tests that need a specific return
 * value from a fake set it via `<name>_fake.return_val` before exercising
 * the production code.
 */

#include "fff.h"

#include "kernel_priv.h"
#include "memory.h"
#include "rtos_types.h"
#include "task_priv.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

DECLARE_FAKE_VOID_FUNC(rtos_port_enter_critical);
DECLARE_FAKE_VOID_FUNC(rtos_port_exit_critical);
DECLARE_FAKE_VALUE_FUNC(uint32_t, rtos_port_enter_critical_from_isr);
DECLARE_FAKE_VOID_FUNC(rtos_port_exit_critical_from_isr, uint32_t);
/* Returns false by default — host is never in interrupt context. Tests that
 * exercise ISR-only paths can set return_val to true before calling. */
DECLARE_FAKE_VALUE_FUNC(bool, rtos_port_in_isr);

DECLARE_FAKE_VALUE_FUNC(rtos_task_handle_t, rtos_task_get_current);

DECLARE_FAKE_VOID_FUNC(rtos_scheduler_add_to_ready_list, rtos_task_handle_t);
DECLARE_FAKE_VOID_FUNC(rtos_scheduler_remove_from_ready_list, rtos_task_handle_t);

DECLARE_FAKE_VOID_FUNC(rtos_kernel_task_block, rtos_task_handle_t, rtos_tick_t);
DECLARE_FAKE_VOID_FUNC(rtos_kernel_task_unblock, rtos_task_handle_t);
DECLARE_FAKE_VOID_FUNC(rtos_kernel_task_unblock_from_isr, rtos_task_handle_t);

DECLARE_FAKE_VOID_FUNC(rtos_yield);

/*
 * RTOS_ASSERT_PARAM expands to rtos_assert_failed() on the kernel's NULL
 * pre-checks. Host tests pass valid inputs so this fake is essentially a
 * link-satisfier — call count is still observable if a test wants to assert
 * "this call should NOT have asserted".
 */
DECLARE_FAKE_VOID_FUNC(rtos_assert_failed, const char *, uint32_t, const char *, const char *);

/*
 * KLOG_* expand to klog_write(...) gated on the klog_verbosity global. The
 * host build sets klog_verbosity = KLOG_LEVEL_FAULT - 1 (effectively "off")
 * so the fake is rarely called; we still need it linkable.
 *
 * ulog_verbosity is the symmetric runtime control for ULog. Production .c
 * files don't call ulog macros today, but the symbol must be linkable for
 * any TU that transitively includes ulog.h.
 */
DECLARE_FAKE_VOID_FUNC(klog_write, int, const char *, const char *, uint16_t, const char *,
                       uint32_t, uint32_t, uint32_t, uint32_t);
extern volatile uint8_t klog_verbosity;
extern volatile uint8_t ulog_verbosity;

/* Heap: pass-through to libc malloc/free so tests can actually allocate buffers.
 * Side hint is ignored on host — there is no dual-ended split. */
void *rtos_malloc_from(rtos_heap_id_t heap, size_t size);
void *rtos_malloc(size_t size);
void  rtos_free(void *ptr);

/*
 * g_kernel — queue.c reads g_kernel.current_task directly. We provide a
 * concrete definition here. Tests should set g_kernel.current_task before
 * exercising a blocking call.
 */
extern rtos_kernel_cb_t g_kernel;

/* ---- Helpers used by tests to script TCBs ---- */

/**
 * @brief Initialise a TCB to a known state — zeros all fields, sets task_id,
 *        priority, base_priority, and READY state. The pointer must outlive
 *        every call that uses it.
 */
void host_init_tcb(rtos_tcb_t *tcb, rtos_task_id_t id, rtos_priority_t priority);

/**
 * @brief Reset every framework fake, FFF history, and rebuild g_kernel.
 *        Called from each Unity setUp().
 */
void host_reset_all_fakes(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_TEST_FAKES_H */
