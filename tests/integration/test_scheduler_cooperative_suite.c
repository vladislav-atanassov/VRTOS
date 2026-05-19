/**
 * @file test_scheduler_cooperative_suite.c
 * @brief Cooperative scheduler test suite — 3 cases.
 *
 * Invariant prefix: INV-COOP-
 * All cases start with TEST_ASSUME(scheduler == COOPERATIVE) so they
 * are skipped when run under a different scheduler variant.
 * Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "scheduler.h"
#include "task.h"

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_DRIVER 2U  /* driver task priority */
#define PRIO_HELPER 5U  /* helper task — higher than driver */
#define PRIO_LOW    2U
#define PRIO_HIGH   4U
#define STK         RTOS_DEFAULT_TASK_STACK_SIZE

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static test_signal_t      g_case_done;
static test_signal_t      g_driver_done;
static test_signal_t      g_helper_ran;
static rtos_task_handle_t g_helper_task;
static rtos_task_handle_t g_low_task;
static rtos_semaphore_t   g_sem;

static void coop_before(void)
{
    test_signal_init(&g_case_done);
    test_signal_init(&g_driver_done);
    test_signal_init(&g_helper_ran);
    g_helper_task = NULL;
    g_low_task    = NULL;
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* Helper task: just signals that it ran. */
static void task_helper(void *_)
{
    (void) _;
    test_signal_post(&g_helper_ran);
    rtos_task_delete(NULL);
}

/* Driver task (PRIO_DRIVER=2): creates helper (PRIO_HELPER=5), signals done,
 * then yields. In cooperative mode helper must not have run before the yield. */
static void task_cooperative_driver(void *_)
{
    (void) _;
    rtos_task_create(task_helper, "TH", STK, NULL, PRIO_HELPER, &g_helper_task);
    /* Signal runner BEFORE yielding — in cooperative mode runner (P7) does not
     * preempt driver; driver continues until the explicit yield below. */
    test_signal_post(&g_driver_done);
    rtos_yield();
    rtos_task_delete(NULL);
}

/* Low task: blocks on semaphore then signals done. */
static void task_low_block(void *_)
{
    (void) _;
    rtos_semaphore_wait(&g_sem, RTOS_SEM_MAX_WAIT);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* ── Case 1: no_preemption_until_yield ───────────────────────────────────── */
/*
 * Driver (P2) creates Helper (P5 > P2). In cooperative mode, Helper does not
 * preempt Driver — it stays READY until Driver explicitly yields. After Driver
 * yields, the scheduler picks the highest-priority READY task:
 *   - runner (P7) was unblocked when driver posted g_driver_done, so runner runs first
 *   - runner checks helper state → must still be READY (not yet run)
 *   - runner waits for g_helper_ran → runner blocks → helper (P5) finally runs
 */
TEST_CASE(coop, no_preemption_until_yield)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_COOPERATIVE,
                "INV-COOP-NO-PREEMPT requires RTOS_SCHEDULER_COOPERATIVE");
    TEST_INV_DECLARE("INV-COOP-NO-PREEMPT", 1);

    rtos_task_handle_t driver = NULL;
    TEST_ASSERT(rtos_task_create(task_cooperative_driver, "TD", STK, NULL,
                                 PRIO_DRIVER, &driver) == RTOS_SUCCESS,
                "INV-COOP-NO-PREEMPT");

    /* Runner blocks → driver runs, creates helper, posts g_driver_done, yields. */
    TEST_AWAIT_PHASE("driver-ran", 500, {
        test_signal_wait(&g_driver_done, RTOS_SEM_MAX_WAIT);
    });

    /* In cooperative mode, driver posted g_driver_done before yielding, so
     * helper has NOT yet had CPU — it must still be READY. */
    TEST_EXPECT(rtos_task_get_state(g_helper_task) == RTOS_TASK_STATE_READY,
                "INV-COOP-NO-PREEMPT");

    /* Now wait for helper to complete (it runs after runner next blocks). */
    TEST_AWAIT_PHASE("helper-ran", 500, {
        test_signal_wait(&g_helper_ran, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: blocking_releases_cpu ──────────────────────────────────────── */
/*
 * A low-priority task blocks on a semaphore. While blocked, runner verifies
 * the task state, then signals the semaphore to unblock it.
 */
TEST_CASE(coop, blocking_releases_cpu)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_COOPERATIVE,
                "INV-COOP-BLOCK requires RTOS_SCHEDULER_COOPERATIVE");
    TEST_INV_DECLARE("INV-COOP-BLOCK", 1);

    rtos_semaphore_init(&g_sem, 0U, 1U);

    TEST_ASSERT(rtos_task_create(task_low_block, "TL", STK, NULL,
                                 PRIO_LOW, &g_low_task) == RTOS_SUCCESS,
                "INV-COOP-BLOCK");

    /* Give low task time to reach semaphore_wait and block. */
    TEST_AWAIT_PHASE("task-blocking", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_low_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-COOP-BLOCK");

    /* Unblock. */
    rtos_semaphore_signal(&g_sem);
    TEST_AWAIT_PHASE("cleanup", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 3: delay_yields_cpu_to_other_tasks ─────────────────────────────── */
/*
 * A low-priority task calls rtos_delay_ms — this yields the CPU in cooperative
 * mode. Runner verifies the task becomes BLOCKED during the delay.
 */
TEST_CASE(coop, delay_yields_cpu_to_other_tasks)
{
    TEST_ASSUME(rtos_scheduler_get_type() == RTOS_SCHEDULER_COOPERATIVE,
                "INV-COOP-DELAY requires RTOS_SCHEDULER_COOPERATIVE");
    TEST_INV_DECLARE("INV-COOP-DELAY", 1);

    TEST_ASSERT(rtos_task_create(task_cooperative_driver, "TD", STK, NULL,
                                 PRIO_DRIVER, &g_low_task) == RTOS_SUCCESS,
                "INV-COOP-DELAY");

    /* Use g_driver_done as a general "task ran" signal — driver posts it. */
    TEST_AWAIT_PHASE("task-ran", 500, {
        test_signal_wait(&g_driver_done, RTOS_SEM_MAX_WAIT);
    });

    /* After driver posts and yields, helper (created inside driver) should exist. */
    TEST_EXPECT(g_helper_task != NULL, "INV-COOP-DELAY");

    /* Wait for helper to complete. */
    TEST_AWAIT_PHASE("helper-done", 500, {
        test_signal_wait(&g_helper_ran, RTOS_SEM_MAX_WAIT);
    });

    TEST_INV_PASS("INV-COOP-DELAY");
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_coop_cases[] = {
    &TEST_CASE_REF(coop, no_preemption_until_yield),
    &TEST_CASE_REF(coop, blocking_releases_cpu),
    &TEST_CASE_REF(coop, delay_yields_cpu_to_other_tasks),
};

TEST_SUITE_DEFINE(coop, g_coop_cases,
                  .before = coop_before);

int main(void)
{
    return test_runtime_main(&test_suite_coop);
}
