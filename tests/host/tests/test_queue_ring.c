/*
 * Exercises queue.c's circular-buffer index math and full/empty bookkeeping.
 * Only the non-blocking paths (timeout=0) are tested here — blocking paths
 * require simulating a wake from a fake task, which Phase 3's test_sync
 * primitives will make ergonomic. For Phase 1 the ring-wrap correctness
 * is the high-value question.
 */

#include "fakes.h"
#include "queue.h"
#include "queue_priv.h"
#include "unity.h"

void setUp(void)
{
    host_reset_all_fakes();
}

void tearDown(void) { }

static rtos_queue_handle_t make_queue(uint32_t count, uint32_t size)
{
    rtos_queue_handle_t h;
    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_create(&h, count, size));
    return h;
}

static void test_empty_queue_reports_empty_and_zero_items(void)
{
    rtos_queue_handle_t q = make_queue(4, sizeof(int));

    TEST_ASSERT_TRUE(rtos_queue_is_empty(q));
    TEST_ASSERT_FALSE(rtos_queue_is_full(q));
    TEST_ASSERT_EQUAL_UINT(0, rtos_queue_messages_waiting(q));
    TEST_ASSERT_EQUAL_UINT(4, rtos_queue_spaces_available(q));
}

static void test_send_then_receive_round_trips_value(void)
{
    rtos_queue_handle_t q = make_queue(4, sizeof(int));
    int                 in = 42, out = 0;

    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_send(q, &in, 0));
    TEST_ASSERT_EQUAL_UINT(1, rtos_queue_messages_waiting(q));
    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_receive(q, &out, 0));
    TEST_ASSERT_EQUAL_INT(42, out);
    TEST_ASSERT_TRUE(rtos_queue_is_empty(q));
}

static void test_full_queue_returns_full_on_non_blocking_send(void)
{
    rtos_queue_handle_t q = make_queue(2, sizeof(int));
    int                 v = 0;

    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_send(q, &v, 0));
    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_send(q, &v, 0));
    TEST_ASSERT_TRUE(rtos_queue_is_full(q));
    TEST_ASSERT_EQUAL_INT(RTOS_ERROR_FULL, rtos_queue_send(q, &v, 0));
}

static void test_empty_queue_returns_empty_on_non_blocking_receive(void)
{
    rtos_queue_handle_t q   = make_queue(2, sizeof(int));
    int                 out = 0;

    TEST_ASSERT_EQUAL_INT(RTOS_ERROR_EMPTY, rtos_queue_receive(q, &out, 0));
}

static void test_pointers_wrap_through_buffer_with_correct_data_order(void)
{
    /*
     * Drive enough send/receive operations to force read_ptr and write_ptr
     * to wrap past the buffer end at least twice. If wrap arithmetic is
     * off by an item, FIFO order breaks immediately.
     */
    rtos_queue_handle_t q       = make_queue(3, sizeof(int));
    int                 next_in = 0;
    int                 next_expected_out = 0;
    int                 out;

    /* Fill the queue. */
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_send(q, &next_in, 0));
        next_in++;
    }
    TEST_ASSERT_TRUE(rtos_queue_is_full(q));

    /* Drain + refill repeatedly so both pointers wrap. */
    for (int round = 0; round < 8; round++)
    {
        TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_receive(q, &out, 0));
        TEST_ASSERT_EQUAL_INT(next_expected_out, out);
        next_expected_out++;

        TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_send(q, &next_in, 0));
        next_in++;
    }

    /* Drain the tail and confirm the remaining items are still in order. */
    while (!rtos_queue_is_empty(q))
    {
        TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_receive(q, &out, 0));
        TEST_ASSERT_EQUAL_INT(next_expected_out, out);
        next_expected_out++;
    }
    TEST_ASSERT_EQUAL_INT(next_in, next_expected_out);
}

static void test_reset_clears_count_and_pointers(void)
{
    rtos_queue_handle_t q = make_queue(3, sizeof(int));
    int                 v = 1;

    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_send(q, &v, 0));
    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_send(q, &v, 0));
    TEST_ASSERT_EQUAL_INT(RTOS_SUCCESS, rtos_queue_reset(q));

    TEST_ASSERT_TRUE(rtos_queue_is_empty(q));
    TEST_ASSERT_EQUAL_UINT(0, rtos_queue_messages_waiting(q));
}

static void test_null_handle_rejects_with_invalid_param(void)
{
    int v = 0;
    TEST_ASSERT_EQUAL_INT(RTOS_ERROR_INVALID_PARAM, rtos_queue_send(NULL, &v, 0));
    TEST_ASSERT_EQUAL_INT(RTOS_ERROR_INVALID_PARAM, rtos_queue_receive(NULL, &v, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_queue_reports_empty_and_zero_items);
    RUN_TEST(test_send_then_receive_round_trips_value);
    RUN_TEST(test_full_queue_returns_full_on_non_blocking_send);
    RUN_TEST(test_empty_queue_returns_empty_on_non_blocking_receive);
    RUN_TEST(test_pointers_wrap_through_buffer_with_correct_data_order);
    RUN_TEST(test_reset_clears_count_and_pointers);
    RUN_TEST(test_null_handle_rejects_with_invalid_param);
    return UNITY_END();
}
