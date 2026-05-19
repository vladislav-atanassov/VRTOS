#include "fakes.h"

#include <stdlib.h>
#include <string.h>

/* FFF needs exactly one TU to instantiate its internal state. */
DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VOID_FUNC(rtos_port_enter_critical);
DEFINE_FAKE_VOID_FUNC(rtos_port_exit_critical);
DEFINE_FAKE_VALUE_FUNC(uint32_t, rtos_port_enter_critical_from_isr);
DEFINE_FAKE_VOID_FUNC(rtos_port_exit_critical_from_isr, uint32_t);

DEFINE_FAKE_VALUE_FUNC(rtos_task_handle_t, rtos_task_get_current);

DEFINE_FAKE_VOID_FUNC(rtos_scheduler_add_to_ready_list, rtos_task_handle_t);
DEFINE_FAKE_VOID_FUNC(rtos_scheduler_remove_from_ready_list, rtos_task_handle_t);

DEFINE_FAKE_VOID_FUNC(rtos_kernel_task_block, rtos_task_handle_t, rtos_tick_t);
DEFINE_FAKE_VOID_FUNC(rtos_kernel_task_unblock, rtos_task_handle_t);

DEFINE_FAKE_VOID_FUNC(rtos_yield);

DEFINE_FAKE_VOID_FUNC(klog_write, int, const char *, const char *, uint16_t, const char *,
                      uint32_t, uint32_t, uint32_t, uint32_t);
volatile uint8_t klog_verbosity = 0; /* below KLOG_LEVEL_FAULT (0) — every call filtered out */

/* Concrete kernel control block — queue.c reads g_kernel.current_task. */
rtos_kernel_cb_t g_kernel;

/* Pass-through heap. The bump allocator in production doesn't free; we do. */
void *rtos_malloc(size_t size)
{
    return malloc(size);
}

void rtos_free(void *ptr)
{
    free(ptr);
}

void host_init_tcb(rtos_tcb_t *tcb, rtos_task_id_t id, rtos_priority_t priority)
{
    memset(tcb, 0, sizeof(*tcb));
    tcb->task_id        = id;
    tcb->priority       = priority;
    tcb->base_priority  = priority;
    tcb->state          = RTOS_TASK_STATE_READY;
}

void host_reset_all_fakes(void)
{
    RESET_FAKE(rtos_port_enter_critical);
    RESET_FAKE(rtos_port_exit_critical);
    RESET_FAKE(rtos_port_enter_critical_from_isr);
    RESET_FAKE(rtos_port_exit_critical_from_isr);
    RESET_FAKE(rtos_task_get_current);
    RESET_FAKE(rtos_scheduler_add_to_ready_list);
    RESET_FAKE(rtos_scheduler_remove_from_ready_list);
    RESET_FAKE(rtos_kernel_task_block);
    RESET_FAKE(rtos_kernel_task_unblock);
    RESET_FAKE(rtos_yield);
    RESET_FAKE(klog_write);
    FFF_RESET_HISTORY();

    memset(&g_kernel, 0, sizeof(g_kernel));
}
