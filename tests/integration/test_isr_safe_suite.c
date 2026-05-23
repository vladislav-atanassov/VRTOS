/**
 * @file test_isr_safe_suite.c
 * @brief MR-5 — _from_isr variants of sem / queue / notify wake paths.
 *
 * Invariant prefix: INV-ISR-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 *
 * Strategy: a 1-tick one-shot software timer is used as a deterministic ISR
 * source. The timer callback fires from rtos_timer_tick() which is invoked
 * directly by rtos_kernel_tick_handler() in the SysTick ISR — i.e. genuine
 * interrupt context. Each case:
 *   1. Spawns a worker that blocks on the primitive.
 *   2. Waits for the worker to enter the BLOCKED state.
 *   3. Starts the timer; the callback calls the API under test.
 *   4. Waits for the worker's done-signal.
 *   5. Verifies the worker received the expected payload / count.
 *
 * The runner sits at MAX-1 (highest); workers run at a lower priority. When
 * the runner blocks on test_signal_wait the scheduler picks the worker (if
 * READY) or idle. The ISR pends PendSV via rtos_kernel_task_unblock_from_isr
 * so the worker preempts idle on ISR tail-chain.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "queue.h"
#include "semaphore.h"
#include "task.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_MID 3U
#define STK      RTOS_DEFAULT_TASK_STACK_SIZE

#define ISR_TIMER_PERIOD_TICKS 1U   /* fire 1 ms after start */

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static rtos_semaphore_t  g_sem;
static rtos_queue_t      g_queue_cb;
static uint32_t          g_queue_storage[4];
static rtos_queue_handle_t g_queue;

static rtos_timer_t      g_timer_cb;
static rtos_timer_handle_t g_timer;

static test_signal_t      g_worker_done;
static rtos_task_handle_t g_worker;

/* Cross-case communication: the ISR callback records what it did so the
 * runner can verify (in addition to whatever the worker observed). */
static volatile uint32_t s_isr_fire_count;
static volatile int      s_isr_last_status;

/* Worker side: what the worker received from the primitive. */
static volatile uint32_t s_worker_value;
static volatile int      s_worker_status;

/* Sized large enough for the longest test queue item. */
static uint32_t g_recv_item;

static void isr_safe_before(void)
{
    rtos_semaphore_init(&g_sem, 0U, 1U);
    rtos_queue_create_static(&g_queue_cb, g_queue_storage, 4U, sizeof(uint32_t), &g_queue);
    test_signal_init(&g_worker_done);
    g_timer            = NULL;
    g_worker           = NULL;
    s_isr_fire_count   = 0U;
    s_isr_last_status  = -1;
    s_worker_value     = 0U;
    s_worker_status    = -1;
    g_recv_item        = 0U;
    memset(g_queue_storage, 0, sizeof(g_queue_storage));
}

/* Tear down a timer that was started in a case. Safe to call when g_timer is
 * already NULL or already stopped. */
static void teardown_timer(void)
{
    if (g_timer != NULL)
    {
        rtos_timer_stop(g_timer);
        rtos_timer_delete(g_timer);
        g_timer = NULL;
    }
}

/* ── ISR callbacks ──────────────────────────────────────────────────────── */
/* All callbacks run from SysTick handler context (rtos_timer_tick is called
 * from rtos_kernel_tick_handler). They must not call blocking APIs. */

static void cb_sem_signal(void *th, void *param)
{
    (void) th;
    (void) param;
    s_isr_last_status = (int) rtos_semaphore_signal_from_isr(&g_sem);
    s_isr_fire_count++;
}

static void cb_queue_send(void *th, void *param)
{
    (void) th;
    uint32_t item     = (uint32_t) (uintptr_t) param;
    s_isr_last_status = (int) rtos_queue_send_from_isr(g_queue, &item);
    s_isr_fire_count++;
}

static void cb_queue_receive(void *th, void *param)
{
    (void) th;
    (void) param;
    s_isr_last_status = (int) rtos_queue_receive_from_isr(g_queue, &g_recv_item);
    s_isr_fire_count++;
}

static void cb_notify(void *th, void *param)
{
    (void) th;
    uint32_t value    = (uint32_t) (uintptr_t) param;
    s_isr_last_status = (int) rtos_task_notify_from_isr(g_worker, value, RTOS_NOTIFY_ACTION_OVERWRITE);
    s_isr_fire_count++;
}

static void cb_notify_give(void *th, void *param)
{
    (void) th;
    (void) param;
    s_isr_last_status = (int) rtos_task_notify_give_from_isr(g_worker);
    s_isr_fire_count++;
}

/* Helper: create + start a one-shot timer with the given callback/param. */
static void fire_isr_after_1_tick(rtos_timer_callback_t cb, void *param)
{
    rtos_timer_create(NULL, ISR_TIMER_PERIOD_TICKS, RTOS_TIMER_ONE_SHOT, cb, param, &g_timer);
    rtos_timer_start(g_timer);
}

/* ── Worker bodies ──────────────────────────────────────────────────────── */

static void worker_sem_wait(void *_)
{
    (void) _;
    s_worker_status = (int) rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);
    test_signal_post(&g_worker_done);
    rtos_task_delete(NULL);
}

static void worker_queue_receive(void *_)
{
    (void) _;
    uint32_t v      = 0U;
    s_worker_status = (int) rtos_queue_receive(g_queue, &v, RTOS_MAX_DELAY);
    s_worker_value  = v;
    test_signal_post(&g_worker_done);
    rtos_task_delete(NULL);
}

static void worker_queue_send(void *arg)
{
    uint32_t item   = (uint32_t) (uintptr_t) arg;
    s_worker_status = (int) rtos_queue_send(g_queue, &item, RTOS_MAX_DELAY);
    test_signal_post(&g_worker_done);
    rtos_task_delete(NULL);
}

static void worker_notify_wait(void *_)
{
    (void) _;
    uint32_t v      = 0U;
    s_worker_status = (int) rtos_task_notify_wait(0U, 0U, &v, RTOS_NOTIFY_MAX_WAIT);
    s_worker_value  = v;
    test_signal_post(&g_worker_done);
    rtos_task_delete(NULL);
}

static void worker_notify_take(void *_)
{
    (void) _;
    s_worker_status = (int) rtos_task_notify_take(true, RTOS_NOTIFY_MAX_WAIT);
    test_signal_post(&g_worker_done);
    rtos_task_delete(NULL);
}

/* ── Case 1: semaphore_signal_from_isr_wakes_waiter ─────────────────────── */
TEST_CASE(isr, semaphore_signal_from_isr_wakes_waiter)
{
    TEST_INV_DECLARE("INV-ISR-SEM-STATUS", 1);
    TEST_INV_DECLARE("INV-ISR-SEM-WAKE",   1);
    TEST_INV_DECLARE("INV-ISR-SEM-FIRED",  1);

    TEST_ASSERT(rtos_task_create(worker_sem_wait, "TSW", STK, NULL, PRIO_MID, &g_worker) == RTOS_SUCCESS,
                "INV-ISR-SEM-WAKE");

    TEST_AWAIT_PHASE("worker-blocked-sem", 500, {
        while (rtos_task_get_state(g_worker) != RTOS_TASK_STATE_BLOCKED) { rtos_delay_ms(1); }
    });

    fire_isr_after_1_tick(cb_sem_signal, NULL);

    TEST_AWAIT_PHASE("isr-wakes-worker", 500, {
        test_signal_wait(&g_worker_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(s_isr_fire_count == 1U,             "INV-ISR-SEM-FIRED");
    TEST_EXPECT(s_isr_last_status == (int) RTOS_SEM_OK, "INV-ISR-SEM-STATUS");
    TEST_EXPECT(s_worker_status == (int) RTOS_SEM_OK,   "INV-ISR-SEM-WAKE");

    teardown_timer();
}

/* ── Case 2: queue_send_from_isr_wakes_receiver ─────────────────────────── */
TEST_CASE(isr, queue_send_from_isr_wakes_receiver)
{
    TEST_INV_DECLARE("INV-ISR-QSEND-STATUS",  1);
    TEST_INV_DECLARE("INV-ISR-QSEND-PAYLOAD", 1);
    TEST_INV_DECLARE("INV-ISR-QSEND-WAKE",    1);

    TEST_ASSERT(rtos_task_create(worker_queue_receive, "TQR", STK, NULL, PRIO_MID, &g_worker) == RTOS_SUCCESS,
                "INV-ISR-QSEND-WAKE");

    TEST_AWAIT_PHASE("worker-blocked-recv", 500, {
        while (rtos_task_get_state(g_worker) != RTOS_TASK_STATE_BLOCKED) { rtos_delay_ms(1); }
    });

    fire_isr_after_1_tick(cb_queue_send, (void *) (uintptr_t) 0xABCD1234U);

    TEST_AWAIT_PHASE("isr-wakes-recv", 500, {
        test_signal_wait(&g_worker_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(s_isr_last_status == (int) RTOS_SUCCESS,  "INV-ISR-QSEND-STATUS");
    TEST_EXPECT(s_worker_status == (int) RTOS_SUCCESS,    "INV-ISR-QSEND-WAKE");
    TEST_EXPECT(s_worker_value == 0xABCD1234U,            "INV-ISR-QSEND-PAYLOAD");

    teardown_timer();
}

/* ── Case 3: queue_receive_from_isr_wakes_sender ────────────────────────── */
TEST_CASE(isr, queue_receive_from_isr_wakes_sender)
{
    TEST_INV_DECLARE("INV-ISR-QRECV-STATUS",  1);
    TEST_INV_DECLARE("INV-ISR-QRECV-PAYLOAD", 1);
    TEST_INV_DECLARE("INV-ISR-QRECV-WAKE",    1);

    /* Fill the 4-slot queue so the next send blocks. */
    for (uint32_t i = 0U; i < 4U; i++)
    {
        uint32_t v = 0x100U + i;
        TEST_ASSERT(rtos_queue_send(g_queue, &v, 0U) == RTOS_SUCCESS, "INV-ISR-QRECV-WAKE");
    }

    TEST_ASSERT(rtos_task_create(worker_queue_send, "TQS", STK,
                                 (void *) (uintptr_t) 0xDEADBEEFU,
                                 PRIO_MID, &g_worker) == RTOS_SUCCESS,
                "INV-ISR-QRECV-WAKE");

    TEST_AWAIT_PHASE("worker-blocked-send", 500, {
        while (rtos_task_get_state(g_worker) != RTOS_TASK_STATE_BLOCKED) { rtos_delay_ms(1); }
    });

    fire_isr_after_1_tick(cb_queue_receive, NULL);

    TEST_AWAIT_PHASE("isr-wakes-send", 500, {
        test_signal_wait(&g_worker_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(s_isr_last_status == (int) RTOS_SUCCESS, "INV-ISR-QRECV-STATUS");
    /* Receive pulled the head (0x100). */
    TEST_EXPECT(g_recv_item == 0x100U,                   "INV-ISR-QRECV-PAYLOAD");
    /* Worker successfully sent its 0xDEADBEEF after waking. */
    TEST_EXPECT(s_worker_status == (int) RTOS_SUCCESS,   "INV-ISR-QRECV-WAKE");

    teardown_timer();
}

/* ── Case 4: task_notify_from_isr_wakes_waiter ──────────────────────────── */
TEST_CASE(isr, task_notify_from_isr_wakes_waiter)
{
    TEST_INV_DECLARE("INV-ISR-NOTIFY-STATUS",  1);
    TEST_INV_DECLARE("INV-ISR-NOTIFY-VALUE",   1);
    TEST_INV_DECLARE("INV-ISR-NOTIFY-WAKE",    1);

    TEST_ASSERT(rtos_task_create(worker_notify_wait, "TNW", STK, NULL, PRIO_MID, &g_worker) == RTOS_SUCCESS,
                "INV-ISR-NOTIFY-WAKE");

    TEST_AWAIT_PHASE("worker-blocked-notify", 500, {
        while (rtos_task_get_state(g_worker) != RTOS_TASK_STATE_BLOCKED) { rtos_delay_ms(1); }
    });

    fire_isr_after_1_tick(cb_notify, (void *) (uintptr_t) 0xCAFEBABEU);

    TEST_AWAIT_PHASE("isr-wakes-notify", 500, {
        test_signal_wait(&g_worker_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(s_isr_last_status == (int) RTOS_NOTIFY_OK, "INV-ISR-NOTIFY-STATUS");
    TEST_EXPECT(s_worker_status == (int) RTOS_NOTIFY_OK,   "INV-ISR-NOTIFY-WAKE");
    TEST_EXPECT(s_worker_value == 0xCAFEBABEU,             "INV-ISR-NOTIFY-VALUE");

    teardown_timer();
}

/* ── Case 5: task_notify_give_from_isr_wakes_take ───────────────────────── */
TEST_CASE(isr, task_notify_give_from_isr_wakes_take)
{
    TEST_INV_DECLARE("INV-ISR-NGIVE-STATUS", 1);
    TEST_INV_DECLARE("INV-ISR-NGIVE-WAKE",   1);

    TEST_ASSERT(rtos_task_create(worker_notify_take, "TNT", STK, NULL, PRIO_MID, &g_worker) == RTOS_SUCCESS,
                "INV-ISR-NGIVE-WAKE");

    TEST_AWAIT_PHASE("worker-blocked-take", 500, {
        while (rtos_task_get_state(g_worker) != RTOS_TASK_STATE_BLOCKED) { rtos_delay_ms(1); }
    });

    fire_isr_after_1_tick(cb_notify_give, NULL);

    TEST_AWAIT_PHASE("isr-wakes-take", 500, {
        test_signal_wait(&g_worker_done, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(s_isr_last_status == (int) RTOS_NOTIFY_OK, "INV-ISR-NGIVE-STATUS");
    TEST_EXPECT(s_worker_status == (int) RTOS_NOTIFY_OK,   "INV-ISR-NGIVE-WAKE");

    teardown_timer();
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_isr_cases[] = {
    &TEST_CASE_REF(isr, semaphore_signal_from_isr_wakes_waiter),
    &TEST_CASE_REF(isr, queue_send_from_isr_wakes_receiver),
    &TEST_CASE_REF(isr, queue_receive_from_isr_wakes_sender),
    &TEST_CASE_REF(isr, task_notify_from_isr_wakes_waiter),
    &TEST_CASE_REF(isr, task_notify_give_from_isr_wakes_take),
};

TEST_SUITE_DEFINE(isr, g_isr_cases, .before = isr_safe_before);

int main(void)
{
    return test_runtime_main(&test_suite_isr);
}
