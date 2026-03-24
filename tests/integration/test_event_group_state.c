/*******************************************************************************
 * File: tests/integration/test_event_group_state.c
 * Description: Event Group - State & Multi-Wake Invariant Test
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "event_group.h"
#include "hardware_env.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "test_common.h"
#include "test_log.h" /* thread-safe ulog overrides for test_log_task/framework */
#include "timer.h"
#include "uart_tx.h"

/**
 * @file test_event_group_state.c
 * @brief Event Group State & Multi-Wake Invariant Test
 *
 * SCENARIO
 * --------
 * Event group with 32-bit flags. Four tasks:
 *
 *   Setter     (priority 2) — sets bits in stages to test wake conditions
 *   WaiterAny  (priority 3) — waits for ANY of bits 0x01 | 0x02
 *   WaiterAll  (priority 4) — waits for ALL of bits 0x01 | 0x02
 *   Monitor    (priority 1) — samples states and emits verdict
 *
 * INVARIANTS
 * ----------
 * INV-EG1  A task waiting for bits not yet set is BLOCKED.
 * INV-EG2  ANY-mode waiter wakes when any requested bit is set.
 * INV-EG3  ALL-mode waiter stays blocked until ALL requested bits are set.
 * INV-EG4  Multiple waiters with satisfied conditions all wake.
 * INV-EG5  clear_on_exit clears the correct bits after waking.
 * INV-EG6  Timed-out wait returns RTOS_EG_ERR_TIMEOUT; task is not
 *          BLOCKED afterward.
 * INV-EG7  get_bits returns current bits without blocking.
 */

/* =================== Test Parameters =================== */

#define TASK_SET_PRIORITY  (2U)
#define TASK_ANY_PRIORITY  (3U)
#define TASK_ALL_PRIORITY  (4U)
#define TASK_MON_PRIORITY  (1U)

#define SCENARIO_CYCLES  (10U)
#define SETTLE_MS        (50U)
#define TIMEOUT_TEST_MS  (30U)
#define TEST_DURATION_MS (5000U)

#define EG_BIT_0 (0x01U)
#define EG_BIT_1 (0x02U)
#define EG_BIT_2 (0x04U)

/* =================== Shared State =================== */

static volatile bool g_test_started  = false;
static volatile bool g_test_complete = false;

static rtos_event_group_t g_eg;

static rtos_task_handle_t g_handle_set = NULL;
static rtos_task_handle_t g_handle_any = NULL;
static rtos_task_handle_t g_handle_all = NULL;

static volatile uint32_t g_cycle_signal  = 0;
static volatile uint32_t g_any_done      = 0;
static volatile uint32_t g_all_done      = 0;
static volatile uint32_t g_any_woke_count = 0;
static volatile uint32_t g_all_woke_count = 0;

static rtos_timer_handle_t g_test_timer;

/* =================== Task Implementations =================== */

/**
 * Setter (priority 2).
 *
 * Each cycle:
 *   1. Clear all bits to start fresh.
 *   2. Signal waiters to enter wait state.
 *   3. Wait for both to be BLOCKED (INV-EG1).
 *   4. Set BIT_0 only — WaiterAny should wake (INV-EG2),
 *      WaiterAll should stay blocked (INV-EG3).
 *   5. Set BIT_1 — WaiterAll should now wake (INV-EG4).
 *   6. Verify bits after clear_on_exit (INV-EG5).
 *   7. After cycles, test timeout (INV-EG6) and get_bits (INV-EG7).
 */
static void setter_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Setter");

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        /* Reset per-cycle flags */
        g_any_done = 0;
        g_all_done = 0;

        /* Clear all bits for a clean cycle */
        rtos_event_group_clear_bits(&g_eg, 0xFFFFFFFF);

        /* Signal waiters to start waiting */
        g_cycle_signal = cycle + 1;

        /* Let waiters reach BLOCKED state */
        rtos_delay_ms(SETTLE_MS);

        /* INV-EG1: both waiters must be BLOCKED */
        ASSERT_STATE(g_handle_any, RTOS_TASK_STATE_BLOCKED, "INV-EG1:AnyBlocked");
        ASSERT_STATE(g_handle_all, RTOS_TASK_STATE_BLOCKED, "INV-EG1:AllBlocked");

        /* Set only BIT_0 — WaiterAny (ANY of 0x01|0x02) should wake */
        test_log_task("SET_BIT0", "Setter");
        rtos_event_group_set_bits(&g_eg, EG_BIT_0);

        /* Let WaiterAny wake and run */
        rtos_delay_ms(SETTLE_MS);

        /* INV-EG2: WaiterAny should have woken */
        TEST_ASSERT(g_any_done == 1, "INV-EG2:AnyWoke");

        /* INV-EG3: WaiterAll (ALL of 0x01|0x02) should still be BLOCKED */
        ASSERT_STATE(g_handle_all, RTOS_TASK_STATE_BLOCKED, "INV-EG3:AllStillBlocked");

        /* Set BIT_1 — WaiterAll should now wake (all bits present) */
        test_log_task("SET_BIT1", "Setter");
        rtos_event_group_set_bits(&g_eg, EG_BIT_1);

        /* Let WaiterAll wake and run */
        rtos_delay_ms(SETTLE_MS);

        /* INV-EG4: WaiterAll should have woken */
        TEST_ASSERT(g_all_done == 1, "INV-EG4:AllWoke");

        /* INV-EG5: WaiterAll uses clear_on_exit, so BIT_0|BIT_1 should be cleared */
        uint32_t bits_now = rtos_event_group_get_bits(&g_eg);
        TEST_ASSERT((bits_now & (EG_BIT_0 | EG_BIT_1)) == 0, "INV-EG5:ClearOnExit");

        /* Wait for both to finish before next cycle */
        while (!g_any_done || !g_all_done)
        {
            rtos_delay_ms(10);
        }
    }

    /* INV-EG6: test timed-out wait */
    rtos_event_group_clear_bits(&g_eg, 0xFFFFFFFF);
    rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, EG_BIT_2, false, false, NULL, TIMEOUT_TEST_MS);
    TEST_ASSERT(s == RTOS_EG_ERR_TIMEOUT, "INV-EG6:TimeoutReturnCode");
    TEST_ASSERT(rtos_task_get_state(g_handle_set) != RTOS_TASK_STATE_BLOCKED, "INV-EG6:NotBlockedAfterTimeout");

    /* INV-EG7: get_bits returns current bits */
    rtos_event_group_set_bits(&g_eg, EG_BIT_2);
    uint32_t read_bits = rtos_event_group_get_bits(&g_eg);
    TEST_ASSERT((read_bits & EG_BIT_2) != 0, "INV-EG7:GetBitsReturnsSet");

    test_log_task("END", "Setter");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/**
 * WaiterAny (priority 3).
 *
 * Each cycle: wait for ANY of BIT_0|BIT_1 (no clear_on_exit).
 */
static void waiter_any_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "WaiterAny");

    uint32_t last_signal = 0;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        while (g_cycle_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_cycle_signal;

        test_log_task("WAIT", "WaiterAny");
        uint32_t         bits_out = 0;
        rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, EG_BIT_0 | EG_BIT_1, false, false, &bits_out,
                                                        RTOS_EG_MAX_WAIT);

        TEST_ASSERT(s == RTOS_EG_OK, "WaiterAny:WokeOK");
        TEST_ASSERT((bits_out & (EG_BIT_0 | EG_BIT_1)) != 0, "WaiterAny:BitsOutValid");

        g_any_woke_count++;
        g_any_done = 1;
    }

    test_log_task("END", "WaiterAny");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/**
 * WaiterAll (priority 4).
 *
 * Each cycle: wait for ALL of BIT_0|BIT_1 (with clear_on_exit).
 */
static void waiter_all_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "WaiterAll");

    uint32_t last_signal = 0;

    for (uint32_t cycle = 0; cycle < SCENARIO_CYCLES && !g_test_complete; cycle++)
    {
        while (g_cycle_signal <= last_signal)
        {
            rtos_delay_ms(5);
        }
        last_signal = g_cycle_signal;

        test_log_task("WAIT", "WaiterAll");
        uint32_t         bits_out = 0;
        rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, EG_BIT_0 | EG_BIT_1, true, true, &bits_out,
                                                        RTOS_EG_MAX_WAIT);

        TEST_ASSERT(s == RTOS_EG_OK, "WaiterAll:WokeOK");
        TEST_ASSERT((bits_out & (EG_BIT_0 | EG_BIT_1)) == (EG_BIT_0 | EG_BIT_1), "WaiterAll:AllBitsPresent");

        g_all_woke_count++;
        g_all_done = 1;
    }

    test_log_task("END", "WaiterAll");
    while (1)
    {
        rtos_delay_ms(1000);
    }
}

/**
 * Monitor task (priority 1).
 *
 * Emits the final PASS/FAIL verdict when the test completes.
 */
static void monitor_task_func(void *param)
{
    (void) param;

    TEST_WAIT_FOR_START(g_test_started);
    test_log_task("START", "Monitor");

    while (!g_test_complete)
    {
        rtos_delay_ms(20);
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
    test_log_framework("BEGIN", "EventGroupState");
    rtos_timer_handle_t *p = (rtos_timer_handle_t *) param;
    rtos_timer_start(*p);
}

static void test_timeout_callback(void *timer_handle, void *param)
{
    (void) timer_handle;
    (void) param;
    g_test_complete = true;
    test_log_framework("TIMEOUT", "EventGroupState");
}

/* =================== Main =================== */

__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t       status;
    rtos_timer_handle_t startup_timer;

    hardware_env_config();
    log_uart_init(LOG_LEVEL_ALL);

    log_info("Event Group State & Multi-Wake Test");
    log_info("Priorities: Set=%u Any=%u All=%u Mon=%u", TASK_SET_PRIORITY, TASK_ANY_PRIORITY, TASK_ALL_PRIORITY,
             TASK_MON_PRIORITY);
    log_info("Cycles: %u  Settle: %ums", SCENARIO_CYCLES, SETTLE_MS);
    log_info("Invariants: EG1(block) EG2(any) EG3(all) EG4(multi-wake) EG5(clear) EG6(timeout) EG7(get_bits)");

    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    /* Initialize event group */
    rtos_eg_status_t eg_s = rtos_event_group_init(&g_eg);
    if (eg_s != RTOS_EG_OK)
    {
        log_error("Event group init failed: %d", eg_s);
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

    /* Create tasks */
    status = rtos_task_create(setter_task_func, "Set", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_SET_PRIORITY,
                              &g_handle_set);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(waiter_any_func, "WAny", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_ANY_PRIORITY,
                              &g_handle_any);
    if (status != RTOS_SUCCESS)
    {
        indicate_system_failure();
    }

    status = rtos_task_create(waiter_all_func, "WAll", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TASK_ALL_PRIORITY,
                              &g_handle_all);
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
