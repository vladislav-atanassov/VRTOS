/**
 * @file test_scheduler_rr_suite.c
 * @brief Round-robin scheduler test suite — 3 cases.
 *
 * Invariant prefix: INV-RR-
 * All cases start with TEST_ASSUME(scheduler == ROUND_ROBIN).
 * Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "scheduler.h"
#include "semaphore.h"
#include "task.h"

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_LOW  2U
#define PRIO_HIGH 4U
#define STK       RTOS_DEFAULT_TASK_STACK_SIZE

/* CPU-bound work without yielding; spans multiple timeslices on Cortex-M4. */
#define WORK_ITERS 100000U

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static test_signal_t      g_done_a;
static test_signal_t      g_done_b;
static rtos_task_handle_t g_task_a;
static rtos_task_handle_t g_task_b;
static volatile uint32_t  g_acquire_seq;
static volatile uint32_t  g_seq_a;
static volatile uint32_t  g_seq_b;

static void rr_before(void)
{
    test_signal_init(&g_done_a);
    test_signal_init(&g_done_b);
    g_task_a      = NULL;
    g_task_b      = NULL;
    g_acquire_seq = 0U;
    g_seq_a       = 0U;
    g_seq_b       = 0U;
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

static void task_work_a(void *_)
{
    (void) _;
    for (volatile uint32_t i = 0U; i < WORK_ITERS; i++) { }
    g_seq_a = ++g_acquire_seq;
    test_signal_post(&g_done_a);
    rtos_task_delete(NULL);
}

static void task_work_b(void *_)
{
    (void) _;
    for (volatile uint32_t i = 0U; i < WORK_ITERS; i++) { }
    g_seq_b = ++g_acquire_seq;
    test_signal_post(&g_done_b);
    rtos_task_delete(NULL);
}

/* Blocks on the provided semaphore, then signals done. */
static rtos_semaphore_t g_block_sem;

static void task_wait_sem_done(void *which)
{
    test_signal_t *sig = (test_signal_t *) which;
    rtos_semaphore_wait(&g_block_sem, RTOS_SEM_MAX_WAIT);
    test_signal_post(sig);
    rtos_task_delete(NULL);
}

/* ── Case 1: equal_priority_tasks_both_complete ──────────────────────────── */
/*
 * Two tasks at equal priority do CPU-bound work without yielding.
 * RR timeslicing lets both make progress; both must complete.
 */
TEST_CASE(rr, equal_priority_tasks_both_complete)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_ROUND_ROBIN,
                "INV-RR-BOTH-COMPLETE requires RTOS_SCHEDULER_ROUND_ROBIN");
    TEST_INV_DECLARE("INV-RR-BOTH-COMPLETE", 1);

    TEST_ASSERT(rtos_task_create(task_work_a, "TA", STK, NULL,
                                 PRIO_LOW, &g_task_a) == RTOS_SUCCESS,
                "INV-RR-BOTH-COMPLETE");
    TEST_ASSERT(rtos_task_create(task_work_b, "TB", STK, NULL,
                                 PRIO_LOW, &g_task_b) == RTOS_SUCCESS,
                "INV-RR-BOTH-COMPLETE");

    TEST_AWAIT_PHASE("task-a-done", 5000, {
        test_signal_wait(&g_done_a, RTOS_SEM_MAX_WAIT);
    });
    TEST_AWAIT_PHASE("task-b-done", 5000, {
        test_signal_wait(&g_done_b, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-RR-BOTH-COMPLETE");
}

/* ── Case 2: higher_priority_runs_before_lower ───────────────────────────── */
/*
 * High (P4) and low (P2) tasks both do bounded work. In RR mode, higher
 * priority still takes precedence — high task completes first.
 */
TEST_CASE(rr, higher_priority_runs_before_lower)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_ROUND_ROBIN,
                "INV-RR-PRIO-ORDER requires RTOS_SCHEDULER_ROUND_ROBIN");
    TEST_INV_DECLARE("INV-RR-PRIO-ORDER", 1);

    /* Low task first so high task can preempt if it starts second. */
    TEST_ASSERT(rtos_task_create(task_work_b, "TL", STK, NULL,
                                 PRIO_LOW, &g_task_b) == RTOS_SUCCESS,
                "INV-RR-PRIO-ORDER");
    TEST_ASSERT(rtos_task_create(task_work_a, "TH", STK, NULL,
                                 PRIO_HIGH, &g_task_a) == RTOS_SUCCESS,
                "INV-RR-PRIO-ORDER");

    TEST_AWAIT_PHASE("high-done", 2000, {
        test_signal_wait(&g_done_a, RTOS_SEM_MAX_WAIT);
    });
    TEST_AWAIT_PHASE("low-done", 2000, {
        test_signal_wait(&g_done_b, RTOS_SEM_MAX_WAIT);
    });

    TEST_EXPECT(g_seq_a < g_seq_b, "INV-RR-PRIO-ORDER");
}

/* ── Case 3: blocked_task_does_not_consume_timeslice ─────────────────────── */
/*
 * A blocked task must not spin-consume its timeslice. Two tasks at equal
 * priority: one blocks immediately on a semaphore, the other does work.
 * The working task must complete even though the blocked task has the same
 * priority (blocked tasks do not steal timeslices in RR).
 */
TEST_CASE(rr, blocked_task_does_not_consume_timeslice)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_ROUND_ROBIN,
                "INV-RR-BLOCK-NO-STARVE requires RTOS_SCHEDULER_ROUND_ROBIN");
    TEST_INV_DECLARE("INV-RR-BLOCK-NO-STARVE", 1);

    rtos_semaphore_init(&g_block_sem, 0U, 1U);

    TEST_ASSERT(rtos_task_create(task_wait_sem_done, "TB", STK,
                                 (void *) &g_done_b, PRIO_LOW, &g_task_b) == RTOS_SUCCESS,
                "INV-RR-BLOCK-NO-STARVE");
    TEST_ASSERT(rtos_task_create(task_work_a, "TA", STK, NULL,
                                 PRIO_LOW, &g_task_a) == RTOS_SUCCESS,
                "INV-RR-BLOCK-NO-STARVE");

    /* The working task must complete even with a same-priority blocked peer. */
    TEST_AWAIT_PHASE("work-done", 5000, {
        test_signal_wait(&g_done_a, RTOS_SEM_MAX_WAIT);
    });
    TEST_INV_PASS("INV-RR-BLOCK-NO-STARVE");

    /* Unblock the blocked task to clean up. */
    rtos_semaphore_signal(&g_block_sem);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_done_b, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_rr_cases[] = {
    &TEST_CASE_REF(rr, equal_priority_tasks_both_complete),
    &TEST_CASE_REF(rr, higher_priority_runs_before_lower),
    &TEST_CASE_REF(rr, blocked_task_does_not_consume_timeslice),
};

TEST_SUITE_DEFINE(rr, g_rr_cases,
                  .before = rr_before);

int main(void)
{
    return test_runtime_main(&test_suite_rr);
}
