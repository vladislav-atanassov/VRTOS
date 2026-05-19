/**
 * @file test_task_state_suite.c
 * @brief Task-state test suite — 3 cases covering delay, suspend/resume, and deletion.
 *
 * Invariant prefix: INV-TASK-
 * Scheduler: PREEMPTIVE_SP. Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h"
#include "config.h"
#include "task.h"

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PRIO_WORKER 2U
#define STK         RTOS_DEFAULT_TASK_STACK_SIZE

/* ── Fixture ─────────────────────────────────────────────────────────────── */

static test_signal_t      g_case_done;
static rtos_task_handle_t g_worker_task;

static void task_state_before(void)
{
    test_signal_init(&g_case_done);
    g_worker_task = NULL;
}

/* ── Task bodies ─────────────────────────────────────────────────────────── */

/* Delays 50 ms then signals done. */
static void task_delay_and_done(void *_)
{
    (void) _;
    rtos_delay_ms(50U);
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Signals done then deletes self. */
static void task_signal_then_delete(void *_)
{
    (void) _;
    test_signal_post(&g_case_done);
    rtos_task_delete(NULL);
}

/* Waits for a semaphore (never posted) — used as a blocking body.
 * Deleted externally via the handle. */
static rtos_semaphore_t g_block_sem;

static void task_block_forever(void *_)
{
    (void) _;
    rtos_semaphore_wait(&g_block_sem, RTOS_SEM_MAX_WAIT);
    rtos_task_delete(NULL);
}

/* ── Case 1: delay_blocks_task ───────────────────────────────────────────── */
TEST_CASE(task_state, delay_blocks_task)
{
    TEST_INV_DECLARE("INV-TASK-DELAY-BLOCKED", 1);

    TEST_ASSERT(rtos_task_create(task_delay_and_done, "TW", STK, NULL,
                                 PRIO_WORKER, &g_worker_task) == RTOS_SUCCESS,
                "INV-TASK-DELAY-BLOCKED");

    /* Give worker time to enter rtos_delay_ms and become BLOCKED. */
    TEST_AWAIT_PHASE("worker-blocking", 100, { rtos_delay_ms(5); });

    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-TASK-DELAY-BLOCKED");

    /* Wait for the 50 ms delay to expire and worker to self-complete. */
    TEST_AWAIT_PHASE("worker-done", 1000, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });
}

/* ── Case 2: suspend_blocks_execution ────────────────────────────────────── */
TEST_CASE(task_state, suspend_blocks_execution)
{
    TEST_INV_DECLARE("INV-TASK-SUSPENDED",  1);
    TEST_INV_DECLARE("INV-TASK-RESUMED",    1);

    /* Worker blocks on a semaphore so it stays alive long enough to suspend. */
    rtos_semaphore_init(&g_block_sem, 0U, 1U);
    TEST_ASSERT(rtos_task_create(task_block_forever, "TW", STK, NULL,
                                 PRIO_WORKER, &g_worker_task) == RTOS_SUCCESS,
                "INV-TASK-SUSPENDED");

    /* Let worker reach semaphore_wait and block. */
    TEST_AWAIT_PHASE("worker-blocking", 100, { rtos_delay_ms(5); });

    /* Suspend the task. */
    rtos_task_suspend(g_worker_task);
    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_SUSPENDED,
                "INV-TASK-SUSPENDED");

    /* Give it time — while suspended it must remain suspended. */
    TEST_AWAIT_PHASE("still-suspended", 100, { rtos_delay_ms(10); });
    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_SUSPENDED,
                "INV-TASK-SUSPENDED");

    /* Resume — worker returns to BLOCKED (still waiting on sem). */
    rtos_task_resume(g_worker_task);
    TEST_AWAIT_PHASE("resumed", 100, { rtos_delay_ms(2); });
    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_BLOCKED,
                "INV-TASK-RESUMED");

    /* Clean up: signal the semaphore so worker can exit. */
    rtos_semaphore_signal(&g_block_sem);
}

/* ── Case 3: delete_transitions_to_deleted ───────────────────────────────── */
TEST_CASE(task_state, delete_transitions_to_deleted)
{
    TEST_INV_DECLARE("INV-TASK-DELETED", 1);

    TEST_ASSERT(rtos_task_create(task_signal_then_delete, "TW", STK, NULL,
                                 PRIO_WORKER, &g_worker_task) == RTOS_SUCCESS,
                "INV-TASK-DELETED");

    /* Worker signals done then deletes itself. */
    TEST_AWAIT_PHASE("worker-done-signal", 500, {
        test_signal_wait(&g_case_done, RTOS_SEM_MAX_WAIT);
    });

    /* Worker may have been preempted by the runner before deleting itself.
     * A brief delay lets the delete complete. */
    TEST_AWAIT_PHASE("delete-settling", 50, { rtos_delay_ms(2); });

    TEST_EXPECT(rtos_task_get_state(g_worker_task) == RTOS_TASK_STATE_DELETED,
                "INV-TASK-DELETED");
}

/* ── Suite registration ──────────────────────────────────────────────────── */

static const test_case_t *const g_task_state_cases[] = {
    &TEST_CASE_REF(task_state, delay_blocks_task),
    &TEST_CASE_REF(task_state, suspend_blocks_execution),
    &TEST_CASE_REF(task_state, delete_transitions_to_deleted),
};

TEST_SUITE_DEFINE(task_state, g_task_state_cases,
                  .before = task_state_before);

int main(void)
{
    return test_runtime_main(&test_suite_task_state);
}
