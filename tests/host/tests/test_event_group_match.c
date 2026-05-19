/*
 * Exercises event_group.c's wait predicate (`eg_condition_met`) plus the
 * fast-path "condition already true at call time" branches of
 * rtos_event_group_wait_bits. Blocking waits are not covered here — they
 * require simulating a wake from a fake task, deferred to Phase 3.
 */

#include "event_group.h"
#include "fakes.h"
#include "unity.h"

static rtos_event_group_t g_eg;

void setUp(void)
{
    host_reset_all_fakes();
    rtos_event_group_init(&g_eg);
}

void tearDown(void) { }

static void test_init_zeroes_bits_and_empties_wait_list(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, rtos_event_group_get_bits(&g_eg));
    TEST_ASSERT_NULL(g_eg.waiting_list);
}

static void test_set_then_get_round_trips(void)
{
    rtos_event_group_set_bits(&g_eg, 0xA5);
    TEST_ASSERT_EQUAL_UINT32(0xA5, rtos_event_group_get_bits(&g_eg));
}

static void test_wait_all_succeeds_immediately_when_all_bits_set(void)
{
    rtos_event_group_set_bits(&g_eg, 0x0F);
    uint32_t out = 0;
    rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, 0x05, true, false, &out, RTOS_EG_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(RTOS_EG_OK, s);
    TEST_ASSERT_EQUAL_UINT32(0x0F, out);
}

static void test_wait_all_times_out_when_any_bit_missing(void)
{
    rtos_event_group_set_bits(&g_eg, 0x05); /* 0x02 is missing */
    rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, 0x07, true, false, NULL, RTOS_EG_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(RTOS_EG_ERR_TIMEOUT, s);
}

static void test_wait_any_succeeds_with_one_bit_set(void)
{
    rtos_event_group_set_bits(&g_eg, 0x01);
    rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, 0x07, false, false, NULL, RTOS_EG_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(RTOS_EG_OK, s);
}

static void test_wait_any_times_out_when_no_bits_set(void)
{
    rtos_event_group_set_bits(&g_eg, 0x10);
    rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, 0x07, false, false, NULL, RTOS_EG_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(RTOS_EG_ERR_TIMEOUT, s);
}

static void test_clear_on_exit_removes_only_waited_bits(void)
{
    rtos_event_group_set_bits(&g_eg, 0x0F);
    rtos_eg_status_t s = rtos_event_group_wait_bits(&g_eg, 0x03, true, true, NULL, RTOS_EG_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(RTOS_EG_OK, s);
    /* Bits 0x03 should be cleared; bits 0x0C should remain. */
    TEST_ASSERT_EQUAL_UINT32(0x0C, rtos_event_group_get_bits(&g_eg));
}

static void test_clear_bits_subtracts(void)
{
    rtos_event_group_set_bits(&g_eg, 0xFF);
    rtos_event_group_clear_bits(&g_eg, 0x0F);
    TEST_ASSERT_EQUAL_UINT32(0xF0, rtos_event_group_get_bits(&g_eg));
}

static void test_invalid_inputs_rejected(void)
{
    /* NULL group. */
    TEST_ASSERT_EQUAL_INT(RTOS_EG_ERR_INVALID,
                          rtos_event_group_wait_bits(NULL, 0x01, true, false, NULL, RTOS_EG_NO_WAIT));
    /* Zero bitmask is meaningless. */
    TEST_ASSERT_EQUAL_INT(RTOS_EG_ERR_INVALID,
                          rtos_event_group_wait_bits(&g_eg, 0, true, false, NULL, RTOS_EG_NO_WAIT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_zeroes_bits_and_empties_wait_list);
    RUN_TEST(test_set_then_get_round_trips);
    RUN_TEST(test_wait_all_succeeds_immediately_when_all_bits_set);
    RUN_TEST(test_wait_all_times_out_when_any_bit_missing);
    RUN_TEST(test_wait_any_succeeds_with_one_bit_set);
    RUN_TEST(test_wait_any_times_out_when_no_bits_set);
    RUN_TEST(test_clear_on_exit_removes_only_waited_bits);
    RUN_TEST(test_clear_bits_subtracts);
    RUN_TEST(test_invalid_inputs_rejected);
    return UNITY_END();
}
