/*
 * Exercises mutex.c's priority-ordered wait-list insertion / removal /
 * pop-highest. These are file-scope statics in production; the host build
 * passes `-Dstatic=` so they are visible here as ordinary extern symbols.
 */

#include "fakes.h"
#include "mutex.h"
#include "task_priv.h"
#include "unity.h"

/*
 * Forward declarations matching mutex.c's static helpers, callable thanks
 * to the host build's `-Dstatic=` compile flag. If a refactor renames or
 * inlines one of these, the test failure points exactly at the change.
 */
void        mutex_add_to_waiting_list(rtos_mutex_t *m, rtos_tcb_t *task);
void        mutex_remove_from_waiting_list(rtos_mutex_t *m, rtos_tcb_t *task);
rtos_tcb_t *mutex_pop_highest_priority_waiter(rtos_mutex_t *m);

static rtos_mutex_t g_m;
static rtos_tcb_t   g_t[5];

void setUp(void)
{
    host_reset_all_fakes();
    rtos_mutex_init(&g_m);
    for (int i = 0; i < 5; i++)
    {
        host_init_tcb(&g_t[i], (rtos_task_id_t) i, 0);
    }
}

void tearDown(void) { }

/* ---------- Cases ---------- */

static void test_empty_list_returns_null_on_pop(void)
{
    TEST_ASSERT_NULL(mutex_pop_highest_priority_waiter(&g_m));
}

static void test_single_insert_links_blocked_on(void)
{
    g_t[0].priority = 3;
    mutex_add_to_waiting_list(&g_m, &g_t[0]);

    TEST_ASSERT_EQUAL_PTR(&g_t[0], g_m.waiting_list);
    TEST_ASSERT_EQUAL_PTR(&g_m, g_t[0].blocked_on);
    TEST_ASSERT_EQUAL_INT(RTOS_SYNC_TYPE_MUTEX, g_t[0].blocked_on_type);
    TEST_ASSERT_NULL(g_t[0].next_waiting);
}

static void test_higher_priority_inserts_at_head(void)
{
    g_t[0].priority = 1;
    g_t[1].priority = 5;

    mutex_add_to_waiting_list(&g_m, &g_t[0]);
    mutex_add_to_waiting_list(&g_m, &g_t[1]);

    /* Higher priority must be at head regardless of insertion order. */
    TEST_ASSERT_EQUAL_PTR(&g_t[1], g_m.waiting_list);
    TEST_ASSERT_EQUAL_PTR(&g_t[0], g_t[1].next_waiting);
}

static void test_unsorted_insertions_end_up_sorted_high_to_low(void)
{
    rtos_priority_t prios[] = {2, 5, 1, 4, 3};
    for (int i = 0; i < 5; i++)
    {
        g_t[i].priority = prios[i];
        mutex_add_to_waiting_list(&g_m, &g_t[i]);
    }

    rtos_priority_t expected[] = {5, 4, 3, 2, 1};
    rtos_tcb_t     *cur        = g_m.waiting_list;
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_NOT_NULL(cur);
        TEST_ASSERT_EQUAL_UINT8(expected[i], cur->priority);
        cur = cur->next_waiting;
    }
    TEST_ASSERT_NULL(cur);
}

static void test_pop_returns_head_and_clears_block_state(void)
{
    g_t[0].priority = 1;
    g_t[1].priority = 9;
    mutex_add_to_waiting_list(&g_m, &g_t[0]);
    mutex_add_to_waiting_list(&g_m, &g_t[1]);

    rtos_tcb_t *winner = mutex_pop_highest_priority_waiter(&g_m);

    TEST_ASSERT_EQUAL_PTR(&g_t[1], winner);
    TEST_ASSERT_NULL(winner->next_waiting);
    TEST_ASSERT_NULL(winner->blocked_on);
    TEST_ASSERT_EQUAL_INT(RTOS_SYNC_TYPE_NONE, winner->blocked_on_type);
    /* Remaining list still has the loser. */
    TEST_ASSERT_EQUAL_PTR(&g_t[0], g_m.waiting_list);
}

static void test_remove_from_middle_links_neighbors(void)
{
    g_t[0].priority = 1;
    g_t[1].priority = 5;
    g_t[2].priority = 3;

    mutex_add_to_waiting_list(&g_m, &g_t[0]); /* list: t0 */
    mutex_add_to_waiting_list(&g_m, &g_t[1]); /* list: t1 -> t0 */
    mutex_add_to_waiting_list(&g_m, &g_t[2]); /* list: t1 -> t2 -> t0 */

    /* Sanity */
    TEST_ASSERT_EQUAL_PTR(&g_t[1], g_m.waiting_list);
    TEST_ASSERT_EQUAL_PTR(&g_t[2], g_t[1].next_waiting);
    TEST_ASSERT_EQUAL_PTR(&g_t[0], g_t[2].next_waiting);

    mutex_remove_from_waiting_list(&g_m, &g_t[2]);

    TEST_ASSERT_EQUAL_PTR(&g_t[1], g_m.waiting_list);
    TEST_ASSERT_EQUAL_PTR(&g_t[0], g_t[1].next_waiting);
    /* Removed task is fully detached. */
    TEST_ASSERT_NULL(g_t[2].next_waiting);
    TEST_ASSERT_NULL(g_t[2].blocked_on);
}

static void test_remove_unlisted_task_is_safe(void)
{
    g_t[0].priority = 4;
    mutex_add_to_waiting_list(&g_m, &g_t[0]);

    /* g_t[1] was never added — must not corrupt anything. */
    mutex_remove_from_waiting_list(&g_m, &g_t[1]);

    TEST_ASSERT_EQUAL_PTR(&g_t[0], g_m.waiting_list);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_list_returns_null_on_pop);
    RUN_TEST(test_single_insert_links_blocked_on);
    RUN_TEST(test_higher_priority_inserts_at_head);
    RUN_TEST(test_unsorted_insertions_end_up_sorted_high_to_low);
    RUN_TEST(test_pop_returns_head_and_clears_block_state);
    RUN_TEST(test_remove_from_middle_links_neighbors);
    RUN_TEST(test_remove_unlisted_task_is_safe);
    return UNITY_END();
}
