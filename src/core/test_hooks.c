#if RTOS_TEST_HOOKS_ENABLED

#include "rtos_test_hooks.h"
#include "KARTOS.h"
#include "kernel_priv.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Ring buffer ───────────────────────────────────────────────────────────── */
#define RING_SIZE 64U

static rtos_hook_ctx_t   s_ring[RING_SIZE];
static volatile uint32_t s_write = 0U;
static volatile uint32_t s_read  = 0U;

volatile uint32_t g_hook_overflow_count = 0U;

/* ── Registration table ────────────────────────────────────────────────────── */
typedef struct
{
    rtos_hook_event_t event;
    rtos_hook_fn_t    fn;
    void             *user_data;
    bool              active;
} hook_reg_t;

static hook_reg_t s_regs[RTOS_TEST_HOOKS_MAX_REGISTRATIONS];

/* ── Enqueue (single-writer: only one critical section active at a time) ───── */
void rtos_test_hook_enqueue(rtos_hook_ctx_t *ctx)
{
    ctx->tick = g_kernel.tick_count;

    uint32_t next_w = (s_write + 1U) % RING_SIZE;

    if (next_w == s_read)
    {
        g_hook_overflow_count++;
        return;
    }

    s_ring[s_write] = *ctx;
    /* Data must be visible to the drain task before the index advances. */
    __asm volatile("dmb" ::: "memory");
    s_write = next_w;
}

/* ── Drain task — runs in task context, dispatches to registered callbacks ─── */
static void dispatch(const rtos_hook_ctx_t *ctx)
{
    for (uint32_t i = 0U; i < RTOS_TEST_HOOKS_MAX_REGISTRATIONS; i++)
    {
        if (s_regs[i].active && s_regs[i].event == ctx->event)
        {
            s_regs[i].fn(ctx, s_regs[i].user_data);
        }
    }
}

void rtos_test_hook_drain_task(void *arg)
{
    (void) arg;
    for (;;)
    {
        while (s_read != s_write)
        {
            /* Read data after observing the producer's index update. */
            __asm volatile("dmb" ::: "memory");
            rtos_hook_ctx_t ctx = s_ring[s_read];
            s_read              = (s_read + 1U) % RING_SIZE;
            dispatch(&ctx);
        }
        rtos_yield();
    }
}

/* ── Registration API ──────────────────────────────────────────────────────── */
int rtos_test_hook_register(rtos_hook_event_t event, rtos_hook_fn_t fn,
                             void *user_data, int *id_out)
{
    for (uint32_t i = 0U; i < RTOS_TEST_HOOKS_MAX_REGISTRATIONS; i++)
    {
        if (!s_regs[i].active)
        {
            s_regs[i].event     = event;
            s_regs[i].fn        = fn;
            s_regs[i].user_data = user_data;
            s_regs[i].active    = true;
            if (id_out != NULL)
            {
                *id_out = (int) i;
            }
            return 0;
        }
    }
    return -1;
}

int rtos_test_hook_unregister(int id)
{
    if (id < 0 || (uint32_t) id >= RTOS_TEST_HOOKS_MAX_REGISTRATIONS)
    {
        return -1;
    }
    s_regs[(uint32_t) id].active = false;
    return 0;
}

#endif /* RTOS_TEST_HOOKS_ENABLED */
