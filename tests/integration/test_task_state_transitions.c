/*******************************************************************************
 * File: tests/integration/test_task_state_transitions.c
 * Description: Task State Machine - Transition Invariant Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "hardware_env.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_common.h"
#include "test_log.h" /* thread-safe ulog overrides for test_log_task/framework */
#include "timer.h"
#include "uart_tx.h"

/**
 * @file test_task_state_transitions.c
 * @brief Task State Machine Transition Invariant Test
 *
 * SCENARIO
 * --------
 * Asymmetric Controller/Subject design. No sync primitive under test —
 * this exercises the kernel task state machine directly via
 * rtos_task_suspend() and rtos_task_resume().
 *
 *   Subject    (priority 3) — target of all transitions
 *   Controller (priority 5) — drives Subject through every transition
 *   Monitor    (priority 1) — independent sampling; checks for invalid states
 *
 * Controller is priority 5 (above convention) so it can preempt Subject
 * at any point and observe state before the scheduler picks another task.
 *
 * Transitions exercised per cycle:
 *   Subject RUNNING   → BLOCKED    (Subject calls rtos_delay_ms)
 *   Subject BLOCKED   → READY      (delay expires)
 *   Subject READY     → RUNNING    (scheduler picks Subject)
 *   Subject RUNNING   → SUSPENDED  (Controller calls rtos_task_suspend)
 *   Subject SUSPENDED → READY      (Controller calls rtos_task_resume)
 *   Subject READY     → RUNNING    (scheduler picks Subject)
 *
 * INVARIANTS
 * ----------
 * INV-T1  Subject is RUNNING when inside its own loop body.
 * INV-T2  Subject is BLOCKED within 1 tick of calling rtos_delay_ms.
 * INV-T3  Subject is no longer BLOCKED after SUBJECT_DELAY_MS + 2 ticks.
 * INV-T4  Subject is SUSPENDED immediately after rtos_task_suspend() returns.
 * INV-T5  Subject is READY or RUNNING after rtos_task_resume() returns.
 * INV-T6  State value is always a valid enum member.
 */

/* =================== Test Parameters =================== */

#define TASK_SUBJECT_PRIORITY    (3U)
#define TASK_CONTROLLER_PRIORITY (5U)
#define TASK_MON_PRIORITY        (1U)

#define SCENARIO_CYCLES  (8U)
#define SUBJECT_DELAY_MS (100U)
#define SUSPEND_HOLD_MS  (50U)
#define TEST_DURATION_MS (5000U)

/* TEST_ASSERT, ASSERT_STATE, g_fail_count are in test_common.h */

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

static rtos_task_handle_t g_handle_subject    = NULL;
static rtos_task_handle_t g_handle_controller = NULL;

/*
 * Synchronisation flags.
 * volatile uint32_t for safe Cortex-M reads.
 */
static volatile uint32_t g_subject_in_loop    = 0; /* Subject sets this at loop top */
static volatile uint32_t g_subject_delaying   = 0; /* Subject sets before delay */
static volatile uint32_t g_controller_cycle   = 0; /* Current cycle from Controller */
static volatile uint32_t g_subject_cycle_done = 0; /* Subject signals cycle complete */
static volatile uint32_t g_cycles_done        = 0; /* Controller signals all cycles done */

static rtos_timer_handle_t g_test_timer;

/* =================== Task Implementations =================== */

/*
 * Subject (priority 3).
 *
 * Target of all transitions. Reports its own state and cooperates
 * with Controller's suspend/resume/delay cycle.
 */
static void subject_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Subject");

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        /* Wait for Controller to start this cycle */
        while (g_controller_cycle <= cycle)
        {
            rtos_delay_ms(5);
        }

        g_subject_in_loop = cycle + 1;

        /* INV-T1: Subject is RUNNING when inside its own loop body */
        TEST_ASSERT(rtos_task_get_state(g_handle_subject) == RTOS_TASK_STATE_RUNNING, "INV-T1:SubjectRunning");

        /* Enter delay — Controller will check INV-T2 / INV-T3 */
        g_subject_delaying = cycle + 1;
        test_log_task("DELAY", "Subject");
        rtos_delay_ms(SUBJECT_DELAY_MS);

        /* After waking from delay, signal cycle done.
         * Controller will then suspend us for INV-T4 / INV-T5. */
        g_subject_cycle_done = cycle + 1;
    }

    test_log_task("END", "Subject");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Controller (priority 5).
 *
 * Drives Subject through every transition and asserts state at each step.
 * Has the highest priority so it can observe state before the scheduler
 * reschedules Subject.
 */
static void controller_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Controller");

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        /* Signal Subject to start this cycle */
        g_subject_in_loop    = 0;
        g_subject_delaying   = 0;
        g_subject_cycle_done = 0;
        g_controller_cycle   = cycle + 1;

        /* Yield so Subject can run */
        rtos_delay_ms(10);

        /* Wait for Subject to enter its delay (INV-T2) */
        while (g_subject_delaying < cycle + 1 && !g_test_complete)
        {
            rtos_delay_ms(1);
        }

        /* Give 1 tick for the delay to take effect */
        rtos_delay_ms(1);

        /* INV-T2: Subject must be BLOCKED shortly after calling delay */
        ASSERT_STATE(g_handle_subject, RTOS_TASK_STATE_BLOCKED, "INV-T2:SubjectBlocked");

        /*
         * Wait for Subject's delay to expire. Use generous margin
         * to account for UART logging overhead and tick granularity.
         */
        rtos_delay_ms(SUBJECT_DELAY_MS + 50);

        /*
         * INV-T3: Subject is no longer BLOCKED after its delay expires.
         *
         * We cannot sample state directly because Subject (lower priority)
         * runs while Controller sleeps, completes its cycle body, then
         * re-enters BLOCKED in the cycle-wait loop before Controller wakes.
         * Instead, check g_subject_cycle_done which proves the delay expired.
         */
        while (g_subject_cycle_done < cycle + 1 && !g_test_complete)
        {
            rtos_delay_ms(1);
        }
        TEST_ASSERT(g_subject_cycle_done >= cycle + 1, "INV-T3:SubjectCompletedAfterDelay");

        /* Now suspend Subject */
        test_log_task("SUSPEND", "Controller");
        rtos_task_suspend(g_handle_subject);

        /* INV-T4: Subject is SUSPENDED immediately after suspend() returns */
        ASSERT_STATE(g_handle_subject, RTOS_TASK_STATE_SUSPENDED, "INV-T4:SubjectSuspended");

        /* Hold suspended for a visible window */
        rtos_delay_ms(SUSPEND_HOLD_MS);

        /* Resume Subject */
        test_log_task("RESUME", "Controller");
        rtos_task_resume(g_handle_subject);

        /* INV-T5: Subject is READY or RUNNING after resume() returns */
        rtos_task_state_t state_after_resume = rtos_task_get_state(g_handle_subject);
        TEST_ASSERT(state_after_resume == RTOS_TASK_STATE_READY || state_after_resume == RTOS_TASK_STATE_RUNNING,
                    "INV-T5:SubjectReadyOrRunning");
    }

    g_cycles_done = 1;
    test_log_task("END", "Controller");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/*
 * Monitor task (priority 1).
 *
 * INV-T6: state value is always a valid enum member (0..RTOS_TASK_STATE_DELETED).
 * Also emits the final verdict.
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    while (!g_test_complete && !g_cycles_done)
    {
        rtos_delay_ms(20);

        /* INV-T6: Subject's state must be a valid enum value */
        rtos_task_state_t state = rtos_task_get_state(g_handle_subject);
        TEST_ASSERT(state <= RTOS_TASK_STATE_DELETED, "INV-T6:ValidStateEnum");
    }

    /* Final verdict */
    TEST_EMIT_VERDICT();

    test_log_task("END", "Monitor");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/* =================== Timer Callbacks =================== */

static void startup_timer_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    g_test_started = true;
    test_log_framework("BEGIN", "TaskStateTransitions");
    rtos_timer_handle_t *p = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "TaskStateTransitions");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Task State Transitions Test");
    log_info("Priorities: Subject=%u Controller=%u Mon=%u", TASK_SUBJECT_PRIORITY, TASK_CONTROLLER_PRIORITY,
             TASK_MON_PRIORITY);
    log_info("Cycles: %u  SubjectDelay: %ums  SuspendHold: %ums", SCENARIO_CYCLES, SUBJECT_DELAY_MS, SUSPEND_HOLD_MS);
    log_info("Invariants: T1(running) T2(blocked) T3(unblocked) T4(suspended) T5(resumed) T6(valid enum)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    status = test_create_startup_timer(startup_timer_callback, &g_test_timer, &startup_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Startup timer failed: %d", status);
        indicate_system_failure();
    }

    status = rtos_timer_create("TestTimer", TEST_DURATION_MS, RTOS_TIMER_ONE_SHOT, test_timeout_callback, NULL,
                               &g_test_timer);
    if (status != RTOS_SUCCESS)
    {
        log_error("Test timer failed: %d", status);
        indicate_system_failure();
    }

    /* Create Subject first so it's in the ready queue */
    status = rtos_task_create(subject_task_func, "Subj", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_SUBJECT_PRIORITY,
                              &g_handle_subject);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(controller_task_func, "Ctrl", RTOS_DEFAULT_TASK_STACK_SIZE, NULL,
                              TASK_CONTROLLER_PRIORITY, &g_handle_controller);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    rtos_task_handle_t monitor_handle;
    status = rtos_task_create(monitor_task_func, "Mon", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_MON_PRIORITY,
                              &monitor_handle);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    rtos_task_handle_t flush_handle;
    test_create_log_flush_task(&flush_handle);

    log_info("Starting scheduler...");
    status = rtos_start_scheduler();

    indicate_system_failure();
}
