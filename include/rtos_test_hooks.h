#ifndef RTOS_TEST_HOOKS_H
#define RTOS_TEST_HOOKS_H

#include "rtos_types.h"

#if RTOS_TEST_HOOKS_ENABLED

#include <stdint.h>

/* ── Hook event identifiers ────────────────────────────────────────────────── */
typedef enum
{
    RTOS_HOOK_CTX_SWITCH = 0,
    RTOS_HOOK_TASK_STATE,
    RTOS_HOOK_TICK,
    RTOS_HOOK_MUTEX_BOOST,
    RTOS_HOOK_MUTEX_RESTORE,
    RTOS_HOOK_MUTEX_LOCK_EXIT,
    RTOS_HOOK_MUTEX_UNLOCK,
    RTOS_HOOK_QUEUE_SEND,
    RTOS_HOOK_QUEUE_RECEIVE,
    RTOS_HOOK_QUEUE_BLOCK_FULL,
    RTOS_HOOK_QUEUE_BLOCK_EMPTY,
    RTOS_HOOK_SEM_GIVE,
    RTOS_HOOK_SEM_TAKE,
    RTOS_HOOK_NOTIFY_SEND,
    RTOS_HOOK_NOTIFY_RECEIVE,
    RTOS_HOOK_EVENT_SET,
    RTOS_HOOK_EVENT_WAIT_SATISFIED,
    RTOS_HOOK_COUNT
} rtos_hook_event_t;

/* ── Per-event payload union ───────────────────────────────────────────────── */
typedef struct
{
    rtos_hook_event_t event;
    uint32_t          tick;

    union {
        struct {
            rtos_task_handle_t out_task;
            rtos_task_handle_t in_task;
        } ctx_switch;

        struct {
            rtos_task_handle_t task;
            uint8_t            old_state;
            uint8_t            new_state;
        } task_state;

        struct {
            rtos_task_handle_t owner;
            rtos_priority_t    new_prio;
        } mutex_boost;

        struct {
            rtos_task_handle_t owner;
            rtos_priority_t    restored_prio;
        } mutex_restore;

        struct {
            void *             mutex;
            rtos_task_handle_t caller;
            int                status;
        } mutex_lock;

        struct {
            void *             mutex;
            rtos_task_handle_t caller;
        } mutex_unlock;

        /* Queue and semaphore events: primitive pointer + calling task */
        struct {
            void *             primitive;
            rtos_task_handle_t caller;
        } prim;

        struct {
            uint32_t bits;
        } event_bits;

        struct {
            rtos_task_handle_t task;
            uint32_t           value;
        } notify;

        uint32_t tick_val;
    } u;
} rtos_hook_ctx_t;

/* ── Registration API ──────────────────────────────────────────────────────── */
#define RTOS_TEST_HOOKS_MAX_REGISTRATIONS 16U

typedef void (*rtos_hook_fn_t)(const rtos_hook_ctx_t *ctx, void *user_data);

int  rtos_test_hook_register(rtos_hook_event_t event, rtos_hook_fn_t fn,
                              void *user_data, int *id_out);
int  rtos_test_hook_unregister(int id);

/* Entry point for the HookDrain task — created by test_runtime_main(). */
void rtos_test_hook_drain_task(void *arg);

/* Counts dropped hook records when the ring buffer is full. */
extern volatile uint32_t g_hook_overflow_count;

#endif /* RTOS_TEST_HOOKS_ENABLED */

#endif /* RTOS_TEST_HOOKS_H */
