/**
 * @file test_hooks_suite.c
 * @brief Application-hook test suite — exercises the four weak hooks
 *        declared in rtos_hooks.h (idle, tick, stack-overflow, malloc-failed).
 *
 * Strategy: this TU provides STRONG (non-weak) overrides for each hook that
 * bump a counter and capture the last argument. The kernel's weak defaults in
 * src/core/hooks.c are bypassed by the linker, so every fire site lands in
 * our recorder. Each case then triggers the corresponding kernel condition
 * and asserts the recorder observed it.
 *
 * Invariant prefix: INV-HOOK-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"

#include "KARTOS.h"
#include "config.h"        /* RTOS_TOTAL_HEAP_SIZE, RTOS_MINIMUM_TASK_STACK_SIZE */
#include "memory.h"
#include "port_common.h"   /* PORT_STACK_CANARY_VALUE */
#include "rtos_hooks.h"
#include "task.h"

#include <stddef.h>
#include <stdint.h>

/* ── Hook recorders ──────────────────────────────────────────────────────── */

static volatile uint32_t           s_idle_calls;
static volatile uint32_t           s_tick_calls;
static volatile rtos_tick_t        s_last_tick;
static volatile uint32_t           s_stack_overflow_calls;
static volatile rtos_task_handle_t s_last_overflow_task;
static volatile uint32_t           s_malloc_failed_calls;
static volatile size_t             s_last_failed_size;
static volatile uint32_t           s_last_failed_heap;

/* Strong overrides — these shadow the kernel's weak defaults at link time. */

void rtos_application_idle_hook(void)
{
    s_idle_calls++;
}

void rtos_application_tick_hook(rtos_tick_t tick)
{
    s_tick_calls++;
    s_last_tick = tick;
}

void rtos_application_stack_overflow_hook(rtos_task_handle_t task)
{
    s_stack_overflow_calls++;
    s_last_overflow_task = task;
}

void rtos_application_malloc_failed_hook(size_t requested_size, rtos_heap_id_t heap)
{
    s_malloc_failed_calls++;
    s_last_failed_size = requested_size;
    s_last_failed_heap = (uint32_t) heap;
}

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static void hooks_before(void)
{
    s_idle_calls           = 0U;
    s_tick_calls           = 0U;
    s_last_tick            = 0U;
    s_stack_overflow_calls = 0U;
    s_last_overflow_task   = NULL;
    s_malloc_failed_calls  = 0U;
    s_last_failed_size     = 0U;
    s_last_failed_heap     = 0xFFU;
}

/* ── Case 1: tick_hook_fires_every_tick ──────────────────────────────────── */
/*
 * rtos_kernel_tick_handler increments g_kernel.tick_count then calls the
 * tick hook unconditionally from SysTick ISR. Sleeping 50 ms should yield
 * roughly 50 hook fires; tolerate a small range for jitter / tickless idle.
 */
TEST_CASE(hooks, tick_hook_fires_every_tick)
{
    TEST_INV_DECLARE("INV-HOOK-TICK-FIRES", 1);
    TEST_INV_DECLARE("INV-HOOK-TICK-VALUE", 1);

    uint32_t    before     = s_tick_calls;
    rtos_tick_t tick_start = rtos_get_tick_count();

    rtos_delay_ms(50);

    uint32_t    after = s_tick_calls;
    rtos_tick_t now   = rtos_get_tick_count();

    /* Tick count advanced by ~50; hook should have fired at least that many
     * times (could be slightly more if we got scheduled out and the ISR ran
     * a few extra times before/after our sample). */
    rtos_tick_t elapsed = now - tick_start;
    TEST_EXPECT((after - before) >= elapsed && (after - before) <= (elapsed + 5U),
                "INV-HOOK-TICK-FIRES");

    /* The last observed tick must be at or past the latest tick_count we read. */
    TEST_EXPECT(s_last_tick >= now - 1U, "INV-HOOK-TICK-VALUE");
}

/* ── Case 2: idle_hook_fires_when_blocked ────────────────────────────────── */
/*
 * Runner sits at MAX-1; when it blocks on rtos_delay_ms, no test workers are
 * ready and the only background tasks (LogFlush P0, HookDrain P1) sleep on
 * their own delays. The scheduler falls through to IDLE (P0), which fires
 * the idle hook on each loop iteration.
 */
TEST_CASE(hooks, idle_hook_fires_when_blocked)
{
    TEST_INV_DECLARE("INV-HOOK-IDLE-FIRES", 1);

    uint32_t before = s_idle_calls;
    rtos_delay_ms(100);
    uint32_t after = s_idle_calls;

    TEST_EXPECT(after > before, "INV-HOOK-IDLE-FIRES");
}

/* ── Case 3: malloc_failed_hook_fires_on_oom ─────────────────────────────── */
/*
 * Requesting more than RTOS_TOTAL_HEAP_SIZE hits the "AllocTooLarge" early
 * reject path; the hook fires with the original (un-padded) size and the
 * targeted heap id, and the caller still receives NULL.
 */
TEST_CASE(hooks, malloc_failed_hook_fires_on_oom)
{
    TEST_INV_DECLARE("INV-HOOK-MALLOC-FAILED-FIRES",    1);
    TEST_INV_DECLARE("INV-HOOK-MALLOC-FAILED-NULL",     1);
    TEST_INV_DECLARE("INV-HOOK-MALLOC-FAILED-SIZE",     1);
    TEST_INV_DECLARE("INV-HOOK-MALLOC-FAILED-HEAP",     1);

    uint32_t before     = s_malloc_failed_calls;
    size_t   request_sz = (size_t) RTOS_TOTAL_HEAP_SIZE + 1024U;

    void *p = rtos_malloc_from(RTOS_HEAP_HIGH, request_sz);

    TEST_EXPECT(p == NULL,                            "INV-HOOK-MALLOC-FAILED-NULL");
    TEST_EXPECT(s_malloc_failed_calls == before + 1U, "INV-HOOK-MALLOC-FAILED-FIRES");
    TEST_EXPECT(s_last_failed_size == request_sz,     "INV-HOOK-MALLOC-FAILED-SIZE");
    TEST_EXPECT(s_last_failed_heap == (uint32_t) RTOS_HEAP_HIGH,
                "INV-HOOK-MALLOC-FAILED-HEAP");
}

/* ── Case 4: stack_overflow_hook_fires_on_canary_corrupt ─────────────────── */
/*
 * Create a static-stack victim at lower priority than the runner (so it
 * never actually executes during the case), corrupt the canary word at
 * stack_base, then call rtos_task_check_stack() from the runner. The check
 * must observe the mismatch, fire the hook with the victim handle, and
 * return true.
 */
static uint32_t s_victim_stack[RTOS_MINIMUM_TASK_STACK_SIZE / sizeof(uint32_t)]
    __attribute__((aligned(8)));

static void victim_task_body(void *_)
{
    (void) _;
    for (;;)
    {
        rtos_delay_ms(1000);
    }
}

TEST_CASE(hooks, stack_overflow_hook_fires_on_canary_corrupt)
{
    TEST_INV_DECLARE("INV-HOOK-STACK-OVERFLOW-FIRES",  1);
    TEST_INV_DECLARE("INV-HOOK-STACK-OVERFLOW-RETURN", 1);
    TEST_INV_DECLARE("INV-HOOK-STACK-OVERFLOW-TASK",   1);

    rtos_task_handle_t victim = NULL;
    TEST_ASSERT(rtos_task_create_static(victim_task_body, "VIC", s_victim_stack,
                                        sizeof(s_victim_stack), NULL, 2U,
                                        &victim) == RTOS_SUCCESS,
                "INV-HOOK-STACK-OVERFLOW-FIRES");

    /* Corrupt the canary at the base of the victim's stack. task_create_common
     * wrote PORT_STACK_CANARY_VALUE there; clobber it before the check. */
    uint32_t before = s_stack_overflow_calls;
    s_victim_stack[0] = 0U;

    bool detected = rtos_task_check_stack(victim);

    TEST_EXPECT(detected == true,                       "INV-HOOK-STACK-OVERFLOW-RETURN");
    TEST_EXPECT(s_stack_overflow_calls == before + 1U,  "INV-HOOK-STACK-OVERFLOW-FIRES");
    TEST_EXPECT(s_last_overflow_task == victim,         "INV-HOOK-STACK-OVERFLOW-TASK");

    /* Restore the canary so a subsequent all-tasks sweep wouldn't trigger
     * again, then delete the victim. */
    s_victim_stack[0] = PORT_STACK_CANARY_VALUE;
    rtos_task_delete(victim);
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_hooks_cases[] = {
    &TEST_CASE_REF(hooks, tick_hook_fires_every_tick),
    &TEST_CASE_REF(hooks, idle_hook_fires_when_blocked),
    &TEST_CASE_REF(hooks, malloc_failed_hook_fires_on_oom),
    &TEST_CASE_REF(hooks, stack_overflow_hook_fires_on_canary_corrupt),
};

TEST_SUITE_DEFINE(hooks, g_hooks_cases, .before = hooks_before);

int main(void)
{
    return test_runtime_main(&test_suite_hooks);
}
