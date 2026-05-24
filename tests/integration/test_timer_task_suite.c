/**
 * @file test_timer_task_suite.c
 * @brief MR-9 — software timer service task.
 *
 * Invariant prefix: INV-TIMER-TASK-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 *
 * The test_runtime_main path does not call rtos_timer_task_init() — that's
 * a deliberate opt-in. This suite calls it from suspend_before() before the
 * first case runs so subsequent timer creates dispatch via the service task
 * rather than the legacy ISR-direct path.
 *
 * Cases:
 *   1. Callback runs in task context (rtos_port_in_isr() == false) and the
 *      auto-reload period is preserved across multiple fires.
 *   2. Callback may call rtos_mutex_lock() with a non-zero timeout (would
 *      have asserted / hung in the legacy ISR-direct path).
 *   3. A long-running callback (rtos_delay_ms) does NOT block SysTick:
 *      the kernel tick counter keeps advancing while the callback is
 *      parked, demonstrating the dispatcher decoupled the callback from
 *      the tick ISR.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "mutex.h"
#include "rtos_port.h"
#include "task.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

#define AUTO_RELOAD_PERIOD_TICKS 10U
#define LONG_DELAY_MS            50U

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static rtos_timer_t       g_timer_storage;
static rtos_timer_handle_t g_timer;

static rtos_mutex_t g_mutex;

static volatile uint32_t s_cb_count;
static volatile bool     s_cb_seen_isr_context;       /* true if ANY fire was in ISR */
static volatile bool     s_cb_mutex_locked;
static volatile uint32_t s_cb_long_start_tick;
static volatile uint32_t s_cb_long_end_tick;

static test_signal_t g_cb_done;

static void timer_task_before(void)
{
    rtos_mutex_init(&g_mutex);
    test_signal_init(&g_cb_done);
    g_timer               = NULL;
    s_cb_count            = 0U;
    s_cb_seen_isr_context = false;
    s_cb_mutex_locked     = false;
    s_cb_long_start_tick  = 0U;
    s_cb_long_end_tick    = 0U;
}

/* The service task is global (one per system). Initialise it once before
 * any case runs. test_runtime_main creates the runner but does not call
 * rtos_timer_task_init(), so we do it here from runner context. */
static bool g_timer_task_initialized = false;
static void ensure_timer_task_running(void)
{
    if (!g_timer_task_initialized)
    {
        (void) rtos_timer_task_init();
        g_timer_task_initialized = true;
        /* Give the service task one tick to enter its wait loop so the
         * first signal from an ISR finds it parked. */
        rtos_delay_ms(2);
    }
}

/* ── Callbacks ──────────────────────────────────────────────────────────── */

static void cb_record_context(void *th, void *param)
{
    (void) th;
    (void) param;
    if (rtos_port_in_isr())
    {
        s_cb_seen_isr_context = true;
    }
    s_cb_count++;
}

static void cb_lock_mutex(void *th, void *param)
{
    (void) th;
    (void) param;
    /* This call is the headline reason for the timer task: in ISR context
     * rtos_mutex_lock() with a timeout would block the SysTick handler. */
    if (rtos_mutex_lock(&g_mutex, 200U) == RTOS_MUTEX_OK)
    {
        s_cb_mutex_locked = true;
        rtos_mutex_unlock(&g_mutex);
    }
    test_signal_post(&g_cb_done);
}

static void cb_long_delay(void *th, void *param)
{
    (void) th;
    (void) param;
    s_cb_long_start_tick = rtos_get_tick_count();
    rtos_delay_ms(LONG_DELAY_MS);
    s_cb_long_end_tick = rtos_get_tick_count();
    test_signal_post(&g_cb_done);
}

/* ── Case 1: auto_reload_runs_in_task_context ───────────────────────────── */
TEST_CASE(timer_task, auto_reload_runs_in_task_context)
{
    TEST_INV_DECLARE("INV-TIMER-TASK-FIRES",        1);
    TEST_INV_DECLARE("INV-TIMER-TASK-TASK-CONTEXT", 1);

    ensure_timer_task_running();

    TEST_ASSERT(rtos_timer_create_static(&g_timer_storage, "AR",
                                         AUTO_RELOAD_PERIOD_TICKS,
                                         RTOS_TIMER_AUTO_RELOAD,
                                         cb_record_context, NULL,
                                         &g_timer) == RTOS_SUCCESS,
                "INV-TIMER-TASK-FIRES");
    TEST_ASSERT(rtos_timer_start(g_timer) == RTOS_SUCCESS, "INV-TIMER-TASK-FIRES");

    /* 50 ms / 10 ms = ~5 fires expected. Accept 3..8 to absorb scheduling
     * jitter and the first-fire alignment to the next tick. */
    rtos_delay_ms(50U);

    TEST_EXPECT(s_cb_count >= 3U && s_cb_count <= 8U,    "INV-TIMER-TASK-FIRES");
    TEST_EXPECT(s_cb_seen_isr_context == false,          "INV-TIMER-TASK-TASK-CONTEXT");

    rtos_timer_stop(g_timer);
    rtos_timer_delete(g_timer);
    g_timer = NULL;
}

/* ── Case 2: callback_can_lock_mutex ─────────────────────────────────────── */
TEST_CASE(timer_task, callback_can_lock_mutex)
{
    TEST_INV_DECLARE("INV-TIMER-TASK-MUTEX-LOCK", 1);

    ensure_timer_task_running();

    /* Runner holds the mutex; the callback must wait. Holding it briefly
     * proves the wait actually blocked (and didn't trivially fast-path). */
    TEST_ASSERT(rtos_mutex_lock(&g_mutex, RTOS_MAX_WAIT) == RTOS_MUTEX_OK,
                "INV-TIMER-TASK-MUTEX-LOCK");

    TEST_ASSERT(rtos_timer_create_static(&g_timer_storage, "MUTEX_CB",
                                         5U, RTOS_TIMER_ONE_SHOT,
                                         cb_lock_mutex, NULL,
                                         &g_timer) == RTOS_SUCCESS,
                "INV-TIMER-TASK-MUTEX-LOCK");
    TEST_ASSERT(rtos_timer_start(g_timer) == RTOS_SUCCESS, "INV-TIMER-TASK-MUTEX-LOCK");

    /* Hold the mutex for ~15 ms so the callback definitely tries-and-blocks
     * before we release. */
    rtos_delay_ms(15U);
    rtos_mutex_unlock(&g_mutex);

    TEST_AWAIT_PHASE("cb-done", 500, {
        test_signal_wait(&g_cb_done, RTOS_SEM_MAX_WAIT);
    });
    TEST_EXPECT(s_cb_mutex_locked == true, "INV-TIMER-TASK-MUTEX-LOCK");

    rtos_timer_delete(g_timer);
    g_timer = NULL;
}

/* ── Case 3: long_callback_does_not_block_systick ───────────────────────── */
TEST_CASE(timer_task, long_callback_does_not_block_systick)
{
    TEST_INV_DECLARE("INV-TIMER-TASK-CB-ELAPSED",  1);
    TEST_INV_DECLARE("INV-TIMER-TASK-TICK-ADVANCE", 1);

    ensure_timer_task_running();

    TEST_ASSERT(rtos_timer_create_static(&g_timer_storage, "LONG",
                                         5U, RTOS_TIMER_ONE_SHOT,
                                         cb_long_delay, NULL,
                                         &g_timer) == RTOS_SUCCESS,
                "INV-TIMER-TASK-CB-ELAPSED");
    TEST_ASSERT(rtos_timer_start(g_timer) == RTOS_SUCCESS, "INV-TIMER-TASK-CB-ELAPSED");

    /* Wait for the callback to finish. The watchdog is well above the
     * 50 ms the callback parks for. */
    TEST_AWAIT_PHASE("long-cb-done", 1000, {
        test_signal_wait(&g_cb_done, RTOS_SEM_MAX_WAIT);
    });

    uint32_t elapsed_ticks = s_cb_long_end_tick - s_cb_long_start_tick;

    /* If SysTick had stalled while the callback was parked, the tick
     * counter delta would be ~0, not ~50. Allow ±5 ticks of jitter. */
    TEST_EXPECT(elapsed_ticks >= (LONG_DELAY_MS - 5U) &&
                elapsed_ticks <= (LONG_DELAY_MS + 10U),
                "INV-TIMER-TASK-CB-ELAPSED");
    TEST_EXPECT(elapsed_ticks > 0U, "INV-TIMER-TASK-TICK-ADVANCE");

    rtos_timer_delete(g_timer);
    g_timer = NULL;
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_timer_task_cases[] = {
    &TEST_CASE_REF(timer_task, auto_reload_runs_in_task_context),
    &TEST_CASE_REF(timer_task, callback_can_lock_mutex),
    &TEST_CASE_REF(timer_task, long_callback_does_not_block_systick),
};

TEST_SUITE_DEFINE(timer_task, g_timer_task_cases, .before = timer_task_before);

int main(void)
{
    return test_runtime_main(&test_suite_timer_task);
}
