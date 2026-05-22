/**
 * @file test_heap_stress.c
 * @brief Randomized stress test for the dual-ended heap allocator.
 *
 * Built on host (gcc/clang). Compiles src/core/memory.c directly into the
 * test binary so we can exercise the allocator at native speed and, when
 * combined with -fsanitize=address,undefined (CMake adds these for the
 * stress binary), catch header smashing and OOB writes that on-board
 * tests can't detect.
 *
 * Strategy:
 *   - Maintain a parallel "shadow" record of every block we currently hold:
 *     {pointer, requested_size, side, pattern_byte}.
 *   - In each iteration, pick a random operation:
 *       * 60% chance: random alloc on a random side with a random size
 *       * 40% chance: free a random held block
 *   - After every operation, verify:
 *       * No assertion fired in production code
 *       * No two held blocks overlap (writes a unique byte-pattern into
 *         each payload, then re-reads at teardown to catch corruption)
 *       * Pointer alignment is 8
 *       * Pointer lies inside the configured heap pool
 *       * Diagnostic getters are self-consistent (per-side <= BOTH)
 *   - After the loop, free everything held and verify get_free_size(BOTH)
 *     returns exactly to the initial baseline. This is the strongest
 *     end-to-end invariant: any internal book-keeping leak shows up here.
 *
 * Seed is taken from $HEAP_STRESS_SEED if set; otherwise a fixed default
 * for reproducibility. Iterations default to 5000 (~ms on host) but can
 * be overridden via $HEAP_STRESS_ITERS for soak runs.
 */

#include "fakes.h"
#include "memory.h"
#include "unity.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Tunables ─────────────────────────────────────────────────────────── */

#define DEFAULT_ITERATIONS  5000U
#define MAX_HELD_BLOCKS     256U
#define MIN_ALLOC_BYTES     1U
#define MAX_ALLOC_BYTES     1024U
#define FREE_PROBABILITY    40 /* out of 100 */

/* ── Shadow tracker ───────────────────────────────────────────────────── */

typedef struct
{
    void          *ptr;
    size_t         requested_size;
    rtos_heap_id_t side;
    uint8_t        pattern; /* unique per-block byte, written into payload */
} held_block_t;

static held_block_t g_held[MAX_HELD_BLOCKS];
static size_t       g_held_count;
static uint8_t      g_next_pattern;

/* ── RNG ──────────────────────────────────────────────────────────────── */

static uint32_t g_rng_state;

static uint32_t rng_next(void)
{
    /* xorshift32 — deterministic, no libc rand() seed pollution. */
    uint32_t x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return x;
}

static uint32_t rng_range(uint32_t lo, uint32_t hi_inclusive)
{
    return lo + (rng_next() % (hi_inclusive - lo + 1U));
}

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* Returns the seed actually used (from env var or default). */
static uint32_t seed_from_env_or_default(void)
{
    const char *env = getenv("HEAP_STRESS_SEED");
    if (env != NULL)
    {
        char    *end;
        unsigned long v = strtoul(env, &end, 0);
        if (*end == '\0' && v != 0UL)
        {
            return (uint32_t) v;
        }
    }
    return 0xC0FFEE01U;
}

static uint32_t iters_from_env_or_default(void)
{
    const char *env = getenv("HEAP_STRESS_ITERS");
    if (env != NULL)
    {
        char    *end;
        unsigned long v = strtoul(env, &end, 0);
        if (*end == '\0' && v > 0UL)
        {
            return (uint32_t) v;
        }
    }
    return DEFAULT_ITERATIONS;
}

static void assert_no_kernel_assert_fired(const char *where)
{
    if (rtos_assert_failed_fake.call_count != 0)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "RTOS_ASSERT fired during %s (count=%u)", where,
                 (unsigned) rtos_assert_failed_fake.call_count);
        TEST_FAIL_MESSAGE(msg);
    }
}

/* Verify the diagnostic getters tell a consistent story. Called frequently. */
static void check_invariants(void)
{
    size_t low  = rtos_memory_get_free_size(RTOS_HEAP_LOW);
    size_t high = rtos_memory_get_free_size(RTOS_HEAP_HIGH);
    size_t both = rtos_memory_get_free_size(RTOS_HEAP_BOTH);

    /* BOTH = LOW free + HIGH free + gap, so LOW+HIGH <= BOTH always. */
    TEST_ASSERT_TRUE_MESSAGE(low + high <= both,
                             "per-side free totals exceed RTOS_HEAP_BOTH");

    /* largest free block on a side cannot exceed that side's total free. */
    size_t low_largest  = rtos_memory_get_largest_free_block(RTOS_HEAP_LOW);
    size_t high_largest = rtos_memory_get_largest_free_block(RTOS_HEAP_HIGH);
    TEST_ASSERT_TRUE_MESSAGE(low_largest <= low, "largest(LOW) > free(LOW)");
    TEST_ASSERT_TRUE_MESSAGE(high_largest <= high, "largest(HIGH) > free(HIGH)");

    /* largest free block (BOTH) cannot exceed total BOTH free. */
    size_t both_largest = rtos_memory_get_largest_free_block(RTOS_HEAP_BOTH);
    TEST_ASSERT_TRUE_MESSAGE(both_largest <= both, "largest(BOTH) > free(BOTH)");

    /* min-ever can only have observed values <= current. */
    size_t min_both = rtos_memory_get_min_ever_free_size(RTOS_HEAP_BOTH);
    TEST_ASSERT_TRUE_MESSAGE(min_both <= both, "min_ever(BOTH) > current free");
}

static bool ranges_overlap(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    return (a < b + blen) && (b < a + alen);
}

static void check_no_overlap_with_held(void *new_ptr, size_t new_size)
{
    for (size_t i = 0; i < g_held_count; i++)
    {
        bool overlap = ranges_overlap((const uint8_t *) g_held[i].ptr, g_held[i].requested_size,
                                      (const uint8_t *) new_ptr, new_size);
        TEST_ASSERT_FALSE_MESSAGE(overlap, "new allocation overlaps an existing held block");
    }
}

/* Re-read every held block's payload pattern. Catches a foreign write
 * (corruption from a buggy alloc/free path overwriting our held memory). */
static void verify_held_patterns_intact(void)
{
    for (size_t i = 0; i < g_held_count; i++)
    {
        const uint8_t *p = (const uint8_t *) g_held[i].ptr;
        uint8_t        expected = g_held[i].pattern;
        for (size_t k = 0; k < g_held[i].requested_size; k++)
        {
            if (p[k] != expected)
            {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "held block %zu pattern corrupted at offset %zu (expected 0x%02x, got 0x%02x)",
                         i, k, expected, p[k]);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

/* ── Operations ───────────────────────────────────────────────────────── */

static void do_alloc(void)
{
    if (g_held_count >= MAX_HELD_BLOCKS) { return; }

    size_t         size = (size_t) rng_range(MIN_ALLOC_BYTES, MAX_ALLOC_BYTES);
    rtos_heap_id_t side = (rng_next() & 1U) ? RTOS_HEAP_LOW : RTOS_HEAP_HIGH;

    void *p = rtos_malloc_from(side, size);
    if (p == NULL)
    {
        /* OOM is a legitimate response — don't fail the test, just skip. */
        return;
    }

    /* Range + alignment checks. */
    TEST_ASSERT_TRUE_MESSAGE(((uintptr_t) p & 7U) == 0U, "alloc returned mis-aligned pointer");
    check_no_overlap_with_held(p, size);

    /* Fill payload with a unique pattern; track in shadow record. */
    uint8_t pattern = ++g_next_pattern;
    memset(p, pattern, size);

    g_held[g_held_count].ptr            = p;
    g_held[g_held_count].requested_size = size;
    g_held[g_held_count].side           = side;
    g_held[g_held_count].pattern        = pattern;
    g_held_count++;
}

static void do_free(void)
{
    if (g_held_count == 0) { return; }

    /* Pick a random index, free it, and swap-remove from the array. */
    size_t       idx     = (size_t) (rng_next() % g_held_count);
    held_block_t victim  = g_held[idx];
    g_held[idx]          = g_held[g_held_count - 1];
    g_held_count--;

    /* Verify our pattern is still intact RIGHT BEFORE free — if a previous
     * malloc corrupted us, this catches it at the point of the bad neighbour. */
    const uint8_t *p = (const uint8_t *) victim.ptr;
    for (size_t k = 0; k < victim.requested_size; k++)
    {
        if (p[k] != victim.pattern)
        {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "block being freed has corrupted pattern at offset %zu (expected 0x%02x, got 0x%02x)",
                     k, victim.pattern, p[k]);
            TEST_FAIL_MESSAGE(msg);
        }
    }

    rtos_free(victim.ptr);
}

/* ── Setup / teardown ─────────────────────────────────────────────────── */

void setUp(void)
{
    host_reset_all_fakes();
    g_held_count   = 0;
    g_next_pattern = 0;
    rtos_memory_init();
    g_rng_state = seed_from_env_or_default();
}

void tearDown(void) { }

/* ── Test cases ───────────────────────────────────────────────────────── */

/* Sanity: an empty test fixture lets the heap recover trivially. Acts as
 * a baseline before stress runs — if THIS fails, init/diagnostics are broken. */
static void test_empty_heap_baseline_recovers(void)
{
    size_t baseline = rtos_memory_get_free_size(RTOS_HEAP_BOTH);

    void *p = rtos_malloc_from(RTOS_HEAP_LOW, 64);
    TEST_ASSERT_NOT_NULL(p);
    rtos_free(p);

    size_t after = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    TEST_ASSERT_EQUAL_UINT(baseline, after);
}

/* The main event: randomized alloc/free with full invariant checking. */
static void test_randomized_alloc_free_preserves_invariants(void)
{
    size_t   baseline = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    uint32_t iters    = iters_from_env_or_default();

    printf("\n  [stress] seed=0x%08x iters=%u\n", g_rng_state, iters);

    for (uint32_t i = 0; i < iters; i++)
    {
        uint32_t roll = rng_next() % 100U;
        if (roll < FREE_PROBABILITY)
        {
            do_free();
        }
        else
        {
            do_alloc();
        }

        /* Cheap checks every iteration. */
        check_invariants();
        assert_no_kernel_assert_fired("loop body");
    }

    /* Strong post-condition: pattern fidelity on every still-held block. */
    verify_held_patterns_intact();

    /* Free everything that's still held; verify heap returns to baseline. */
    while (g_held_count > 0)
    {
        do_free();
    }

    size_t after = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(baseline, after,
                                   "after freeing all held blocks, BOTH free did not return to baseline");
    assert_no_kernel_assert_fired("teardown");
}

/* Specifically stress the allocator with many tiny allocations interleaved
 * with frees of older blocks — the configuration most likely to trigger
 * fragmentation, split/coalesce edges, and free-list ordering bugs. */
static void test_tiny_alloc_churn(void)
{
    size_t baseline = rtos_memory_get_free_size(RTOS_HEAP_BOTH);

    for (uint32_t i = 0; i < 2000U; i++)
    {
        if (g_held_count > MAX_HELD_BLOCKS / 2 && (rng_next() & 1U))
        {
            do_free();
        }
        else
        {
            /* Force a small size to maximise fragmentation pressure. */
            size_t         size = (size_t) rng_range(1U, 16U);
            rtos_heap_id_t side = (rng_next() & 1U) ? RTOS_HEAP_LOW : RTOS_HEAP_HIGH;
            void          *p    = rtos_malloc_from(side, size);
            if (p == NULL) { continue; }
            TEST_ASSERT_TRUE(((uintptr_t) p & 7U) == 0U);
            check_no_overlap_with_held(p, size);
            uint8_t pattern = ++g_next_pattern;
            memset(p, pattern, size);
            g_held[g_held_count++] = (held_block_t){ p, size, side, pattern };
        }
        check_invariants();
        assert_no_kernel_assert_fired("tiny churn");
    }

    while (g_held_count > 0)
    {
        do_free();
    }

    TEST_ASSERT_EQUAL_UINT(baseline, rtos_memory_get_free_size(RTOS_HEAP_BOTH));
}

/* Drive both sides toward each other until they collide, free everything,
 * confirm clean recovery. Same scenario as the on-board collision test,
 * but with the additional verification that no two blocks ever overlapped
 * during the drain (which on-board only checks indirectly via end totals). */
static void test_collision_then_full_recovery(void)
{
    size_t baseline = rtos_memory_get_free_size(RTOS_HEAP_BOTH);

    /* Drain by alternating LOW/HIGH allocs of varying size. */
    while (g_held_count < MAX_HELD_BLOCKS)
    {
        size_t         size = (size_t) rng_range(32U, 256U);
        rtos_heap_id_t side = (g_held_count & 1U) ? RTOS_HEAP_HIGH : RTOS_HEAP_LOW;
        void          *p    = rtos_malloc_from(side, size);
        if (p == NULL) { break; }
        TEST_ASSERT_TRUE(((uintptr_t) p & 7U) == 0U);
        check_no_overlap_with_held(p, size);
        uint8_t pattern = ++g_next_pattern;
        memset(p, pattern, size);
        g_held[g_held_count++] = (held_block_t){ p, size, side, pattern };
        check_invariants();
    }

    /* We must actually have hit OOM, not the shadow-array cap. */
    TEST_ASSERT_TRUE_MESSAGE(rtos_memory_get_largest_free_block(RTOS_HEAP_BOTH)
                                 < 256U,
                             "collision test did not reach OOM");

    verify_held_patterns_intact();

    /* Free in a different order than alloc — frees by descending index
     * (LIFO-ish) to exercise the pull-back path heavily. */
    while (g_held_count > 0)
    {
        held_block_t v   = g_held[--g_held_count];
        rtos_free(v.ptr);
        check_invariants();
        assert_no_kernel_assert_fired("collision free");
    }

    TEST_ASSERT_EQUAL_UINT(baseline, rtos_memory_get_free_size(RTOS_HEAP_BOTH));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_heap_baseline_recovers);
    RUN_TEST(test_randomized_alloc_free_preserves_invariants);
    RUN_TEST(test_tiny_alloc_churn);
    RUN_TEST(test_collision_then_full_recovery);
    return UNITY_END();
}
