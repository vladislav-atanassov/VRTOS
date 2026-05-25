#include "test_runtime.h"

#include "KARTOS.h"
#include "config.h"        /* RTOS_DEFAULT_TASK_STACK_SIZE, RTOS_MAX_TASK_PRIORITIES */
#include "hardware_env.h"  /* hardware_env_config, indicate_system_failure */
#include "task.h"
#include "test_rand.h"

#if RTOS_TEST_HOOKS_ENABLED
#include "rtos_test_hooks.h"
#endif

/*
 * Priority for the suite runner task.
 *
 * It must outrank the log flush task (priority 0) so that case execution
 * isn't preempted by background log drainage between assertions; and it
 * must outrank test-spawned tasks unless they are explicitly created at
 * a higher priority for scenario-specific reasons. Real preemption tests
 * normally use priorities 1..MAX-1; the runner sits at the top.
 */
#ifndef TEST_RUNNER_PRIORITY
#define TEST_RUNNER_PRIORITY ((rtos_priority_t) (RTOS_MAX_TASK_PRIORITIES - 1U))
#endif

#ifndef TEST_RUNNER_STACK_SIZE
#define TEST_RUNNER_STACK_SIZE (RTOS_DEFAULT_TASK_STACK_SIZE * 2U)
#endif

/* HookDrain sits just above LogFlush (P0) but below every test-spawned worker.
 * At MAX-2 it starved the P2..P4 workers — drain ran constantly between every
 * hook event, leaving no CPU for the scenarios under test. P1 lets workers
 * make progress and only drains when everyone else is blocked or done. */
#ifndef TEST_HOOK_DRAIN_PRIORITY
#define TEST_HOOK_DRAIN_PRIORITY ((rtos_priority_t) 1U)
#endif

#ifndef TEST_HOOK_DRAIN_STACK_SIZE
#define TEST_HOOK_DRAIN_STACK_SIZE RTOS_DEFAULT_TASK_STACK_SIZE
#endif

static const test_suite_t *g_runner_suite = NULL;

__attribute__((noreturn)) void test_runtime_halt(void)
{
    for (;;)
    {
        rtos_delay_ms(1000);
    }
}

static void runner_task(void *param)
{
    (void) param;
    if (g_runner_suite != NULL)
    {
        (void) test_suite_run(g_runner_suite);
    }
    test_runtime_halt();
}

int test_runtime_main(const test_suite_t *suite)
{
    g_runner_suite = suite;

    hardware_env_config();

    if (rtos_init() != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    test_rand_init();

#if RTOS_TEST_HOOKS_ENABLED
    rtos_task_handle_t drain_handle;
    if (rtos_task_create(rtos_test_hook_drain_task, "HookDrain", TEST_HOOK_DRAIN_STACK_SIZE,
                         NULL, TEST_HOOK_DRAIN_PRIORITY, &drain_handle) != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }
#endif

    rtos_task_handle_t runner_handle;
    if (rtos_task_create(runner_task, "TestRunner", TEST_RUNNER_STACK_SIZE,
                         NULL, TEST_RUNNER_PRIORITY, &runner_handle) != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    (void) rtos_start_scheduler();
    indicate_system_failure();
    return 1; /* unreachable */
}
