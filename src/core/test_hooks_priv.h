#ifndef TEST_HOOKS_PRIV_H
#define TEST_HOOKS_PRIV_H

#if RTOS_TEST_HOOKS_ENABLED

#include "rtos_test_hooks.h"

/* Called from within a critical section or task context.
 * Reads the current kernel tick and writes the record into the ring buffer. */
void rtos_test_hook_enqueue(rtos_hook_ctx_t *ctx);

/* Build a hook record on the stack and enqueue it.
 * setup_block must only assign fields on _ctx_ (the local variable). */
#define RTOS_TEST_HOOK_FIRE(event_id, setup_block) \
    do {                                            \
        rtos_hook_ctx_t _ctx_ = {0};               \
        _ctx_.event = (event_id);                  \
        { setup_block }                             \
        rtos_test_hook_enqueue(&_ctx_);             \
    } while (0)

#else /* !RTOS_TEST_HOOKS_ENABLED */

#define RTOS_TEST_HOOK_FIRE(event_id, setup_block) do {} while (0)

#endif /* RTOS_TEST_HOOKS_ENABLED */

#endif /* TEST_HOOKS_PRIV_H */
