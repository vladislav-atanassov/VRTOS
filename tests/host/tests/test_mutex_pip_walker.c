/*
 * Exercises mutex.c's transitive priority-inheritance walker.
 *
 * Same `-Dstatic=` trick as test_mutex_list.c exposes the helper. Targets
 * are BLOCKED in every test so the walker takes the "priority field only,
 * no ready-list re-bucket" branch — that branch is the one without external
 * side effects, so it's the cleanest one to assert on with mocked-out
 * scheduler list helpers.
 */

#include "fakes.h"
#include "mutex.h"
#include "task_priv.h"
#include "unity.h"

void mutex_apply_priority_inheritance(rtos_mutex_t *m, rtos_tcb_t *waiter);

static rtos_mutex_t g_m1, g_m2;
static rtos_tcb_t   g_lo, g_mid, g_hi;

void setUp(void)
{
    host_reset_all_fakes();
    rtos_mutex_init(&g_m1);
    rtos_mutex_init(&g_m2);
    host_init_tcb(&g_lo,  1, 1);
    host_init_tcb(&g_mid, 2, 3);
    host_init_tcb(&g_hi,  3, 5);
}

void tearDown(void) { }

static void test_no_boost_when_owner_already_higher(void)
{
    /* Owner priority 5, waiter priority 3 — no boost. */
    g_m1.owner    = &g_hi;
    g_hi.state    = RTOS_TASK_STATE_BLOCKED;

    mutex_apply_priority_inheritance(&g_m1, &g_mid);

    TEST_ASSERT_EQUAL_UINT8(5, g_hi.priority);
}

static void test_direct_boost_owner_to_waiter_priority(void)
{
    g_m1.owner = &g_lo;
    g_lo.state = RTOS_TASK_STATE_BLOCKED; /* avoid the ready-list re-bucket path */

    mutex_apply_priority_inheritance(&g_m1, &g_hi);

    TEST_ASSERT_EQUAL_UINT8(5, g_lo.priority);
    TEST_ASSERT_EQUAL_UINT8(1, g_lo.base_priority); /* base preserved */
}

static void test_transitive_chain_two_deep_boosts_both_owners(void)
{
    /*
     *   hi  (prio 5) is about to block on m2 owned by mid
     *   mid (prio 3) is already blocked on m1 owned by lo
     *   lo  (prio 1)
     *
     * Expected after apply_priority_inheritance(m2, hi):
     *   mid -> 5  (direct boost)
     *   lo  -> 5  (transitive — walker follows mid.blocked_on == m1)
     */
    g_m2.owner = &g_mid;
    g_m1.owner = &g_lo;

    g_mid.state          = RTOS_TASK_STATE_BLOCKED;
    g_mid.blocked_on     = &g_m1;
    g_mid.blocked_on_type = RTOS_SYNC_TYPE_MUTEX;

    g_lo.state = RTOS_TASK_STATE_BLOCKED;

    mutex_apply_priority_inheritance(&g_m2, &g_hi);

    TEST_ASSERT_EQUAL_UINT8(5, g_mid.priority);
    TEST_ASSERT_EQUAL_UINT8(5, g_lo.priority);
    /* Base priorities never change. */
    TEST_ASSERT_EQUAL_UINT8(3, g_mid.base_priority);
    TEST_ASSERT_EQUAL_UINT8(1, g_lo.base_priority);
}

static void test_ready_owner_triggers_ready_list_rebucket(void)
{
    /*
     * The walker has a special case: if the boosted task is in READY state,
     * it must be removed and re-inserted into the scheduler's bucketed list
     * so it sits at the new priority. Verify that path calls the scheduler
     * helpers exactly once each, with the boosted task as the argument.
     */
    g_m1.owner = &g_lo;
    g_lo.state = RTOS_TASK_STATE_READY;

    mutex_apply_priority_inheritance(&g_m1, &g_hi);

    TEST_ASSERT_EQUAL_UINT(1, rtos_scheduler_remove_from_ready_list_fake.call_count);
    TEST_ASSERT_EQUAL_UINT(1, rtos_scheduler_add_to_ready_list_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(&g_lo, rtos_scheduler_remove_from_ready_list_fake.arg0_val);
    TEST_ASSERT_EQUAL_PTR(&g_lo, rtos_scheduler_add_to_ready_list_fake.arg0_val);
    TEST_ASSERT_EQUAL_UINT8(5, g_lo.priority);
}

static void test_chain_terminates_when_owner_not_blocked_on_mutex(void)
{
    /* mid is BLOCKED but on a SEMAPHORE — chain stops, lo is untouched. */
    g_m2.owner = &g_mid;
    g_m1.owner = &g_lo;

    g_mid.state           = RTOS_TASK_STATE_BLOCKED;
    g_mid.blocked_on      = &g_m1;
    g_mid.blocked_on_type = RTOS_SYNC_TYPE_SEMAPHORE;

    g_lo.state = RTOS_TASK_STATE_BLOCKED;

    mutex_apply_priority_inheritance(&g_m2, &g_hi);

    TEST_ASSERT_EQUAL_UINT8(5, g_mid.priority);
    TEST_ASSERT_EQUAL_UINT8(1, g_lo.priority); /* unchanged */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_boost_when_owner_already_higher);
    RUN_TEST(test_direct_boost_owner_to_waiter_priority);
    RUN_TEST(test_transitive_chain_two_deep_boosts_both_owners);
    RUN_TEST(test_ready_owner_triggers_ready_list_rebucket);
    RUN_TEST(test_chain_terminates_when_owner_not_blocked_on_mutex);
    return UNITY_END();
}
