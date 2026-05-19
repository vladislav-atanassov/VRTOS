#include "test_rand.h"

#include "KARTOS.h" /* rtos_get_tick_count */
#include "ulog.h"

#ifndef TEST_SEED
#define TEST_SEED 0U
#endif

static uint32_t g_rand_state = 1U; /* xorshift32 state; must not be 0 */

void test_rand_init(void)
{
    uint32_t seed = (uint32_t) TEST_SEED;

    if (seed == 0U)
    {
        seed = rtos_get_tick_count();
        if (seed == 0U)
        {
            /* Fallback when called before the first tick fires. */
            seed = 0xA5A5A5A5U;
        }
    }

    g_rand_state = seed;
    ulog_info("TEST-RNG seed=%lu", (unsigned long) seed);
}

uint32_t test_rand_u32(void)
{
    uint32_t x = g_rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rand_state = x;
    return x;
}

uint32_t test_rand_range(uint32_t lo, uint32_t hi)
{
    uint32_t range = hi - lo + 1U;
    return lo + (test_rand_u32() % range);
}
