#ifndef KERNEL_PRIV_H
#define KERNEL_PRIV_H

#include "rtos_assert.h"
#include "rtos_types.h"

/* Not for application code. */

/* Kernel States */
typedef enum
{
    RTOS_KERNEL_STATE_INACTIVE = 0,
    RTOS_KERNEL_STATE_READY,
    RTOS_KERNEL_STATE_RUNNING,
} rtos_kernel_state_t;

/* Kernel Control Block */
typedef struct
{
    rtos_task_handle_t  current_task;        /**< Currently running task */
    rtos_task_handle_t  next_task;           /**< Next task to run */
    rtos_kernel_state_t state;               /**< Current kernel state */
    rtos_tick_t         tick_count;          /**< System tick counter */
    uint8_t             scheduler_suspended; /**< Nested suspend count; 0 = active */
    bool                yield_pending;       /**< Switch was deferred during suspension */
} rtos_kernel_cb_t;
RTOS_STATIC_ASSERT(offsetof(rtos_kernel_cb_t, current_task) == 0, "current_task at wrong offset in kernel_cb_t");

/* Global kernel control block */
extern rtos_kernel_cb_t g_kernel;

/* Internal kernel functions */
void rtos_kernel_tick_handler(void);
void rtos_kernel_switch_context(void);
bool rtos_kernel_validate_transition(rtos_task_handle_t task, rtos_task_state_t new_state);
void rtos_kernel_step_tick(uint32_t ticks_slept);

#endif /* KERNEL_PRIV_H */
