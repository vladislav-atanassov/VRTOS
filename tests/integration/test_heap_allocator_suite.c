/**
 * @file test_heap_allocator_suite.c
 * @brief Bi-directional dual-ended heap test suite.
 *
 * Invariant prefix: INV-HEAP-
 *
 * Covers:
 *   - Single-heap allocator behaviour (alloc/free/coalesce/fragmentation) on
 *     both LOW and HIGH sides.
 *   - Dual-ended-specific behaviour: gap pull-back, side inference on free,
 *     cross-heap independence, collision at the central gap.
 *   - Integration with subsystems: static-create variants for task/timer/queue
 *     bypass the heap; dynamic create+delete returns memory to the heap.
 *
 * The kernel is already up when cases run (idle task + log flush + runner
 * are already in their respective heaps), so cases work in DELTAS from a
 * baseline captured in heap_before() rather than asserting absolute heap
 * sizes. The runner is the only application-priority task active during a
 * case; no other context allocates concurrently.
 *
 * Build with RTOS_MAX_TASKS=24.
 */

#include "test_runtime.h"
#include "test_suite.h"
#include "test_sync.h"

#include "KARTOS.h"
#include "config.h"
#include "memory.h"
#include "queue.h"
#include "task.h"
#include "timer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ── Tunables ─────────────────────────────────────────────────────────── */

#define SMALL_ALLOC   64U  /* generous, well above header+min-block */
#define MEDIUM_ALLOC  128U
#define LARGE_ALLOC   512U
#define STATIC_STACK  512U
#define PRIO_WORKER   2U

/* ── Fixture ──────────────────────────────────────────────────────────── */

typedef struct
{
    size_t free_low;
    size_t free_high;
    size_t free_both;
} heap_snapshot_t;

static heap_snapshot_t g_baseline;

static void snapshot(heap_snapshot_t *s)
{
    s->free_low  = rtos_memory_get_free_size(RTOS_HEAP_LOW);
    s->free_high = rtos_memory_get_free_size(RTOS_HEAP_HIGH);
    s->free_both = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
}

static void heap_before(void)
{
    snapshot(&g_baseline);
}

/* ── Case: init_sanity ─────────────────────────────────────────────────── */
TEST_CASE(heap, init_sanity)
{
    TEST_INV_DECLARE("INV-HEAP-INIT-SANITY", 3);

    /* After kernel boot, idle + log_flush + runner have stacks on HIGH; LOW
     * has been touched only by anything the kernel itself routes there.
     * Total free must be positive and within the configured pool. */
    size_t both = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    TEST_EXPECT(both > 0U, "INV-HEAP-INIT-SANITY");
    TEST_EXPECT(both <= (size_t) RTOS_TOTAL_HEAP_SIZE, "INV-HEAP-INIT-SANITY");

    /* Per-side accounting must add up consistently with BOTH (modulo gap). */
    size_t low  = rtos_memory_get_free_size(RTOS_HEAP_LOW);
    size_t high = rtos_memory_get_free_size(RTOS_HEAP_HIGH);
    TEST_EXPECT(low + high <= both, "INV-HEAP-INIT-SANITY");
}

/* ── Case: low_alloc_routes_to_low ─────────────────────────────────────── */
TEST_CASE(heap, low_alloc_routes_to_low)
{
    TEST_INV_DECLARE("INV-HEAP-ROUTE-LOW", 2);

    heap_snapshot_t before;
    snapshot(&before);

    void *p = rtos_malloc_from(RTOS_HEAP_LOW, MEDIUM_ALLOC);
    TEST_ASSERT(p != NULL, "INV-HEAP-ROUTE-LOW");

    /* HIGH side must be untouched. */
    size_t high_after = rtos_memory_get_free_size(RTOS_HEAP_HIGH);
    TEST_EXPECT(high_after == before.free_high, "INV-HEAP-ROUTE-LOW");

    rtos_free(p);
}

/* ── Case: high_alloc_routes_to_high ───────────────────────────────────── */
TEST_CASE(heap, high_alloc_routes_to_high)
{
    TEST_INV_DECLARE("INV-HEAP-ROUTE-HIGH", 2);

    heap_snapshot_t before;
    snapshot(&before);

    void *p = rtos_malloc_from(RTOS_HEAP_HIGH, MEDIUM_ALLOC);
    TEST_ASSERT(p != NULL, "INV-HEAP-ROUTE-HIGH");

    size_t low_after = rtos_memory_get_free_size(RTOS_HEAP_LOW);
    TEST_EXPECT(low_after == before.free_low, "INV-HEAP-ROUTE-HIGH");

    rtos_free(p);
}

/* ── Case: alloc_free_round_trip ───────────────────────────────────────── */
TEST_CASE(heap, alloc_free_round_trip)
{
    TEST_INV_DECLARE("INV-HEAP-ROUNDTRIP", 2);

    heap_snapshot_t before;
    snapshot(&before);

    void *p = rtos_malloc(MEDIUM_ALLOC);
    TEST_ASSERT(p != NULL, "INV-HEAP-ROUNDTRIP");
    memset(p, 0xA5, MEDIUM_ALLOC);

    rtos_free(p);

    heap_snapshot_t after;
    snapshot(&after);

    /* Total free returns exactly to baseline (pull-back returns block to gap). */
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-ROUNDTRIP");
}

/* ── Case: free_null_is_noop ───────────────────────────────────────────── */
TEST_CASE(heap, free_null_is_noop)
{
    TEST_INV_DECLARE("INV-HEAP-FREE-NULL", 1);

    heap_snapshot_t before;
    snapshot(&before);

    rtos_free(NULL);

    heap_snapshot_t after;
    snapshot(&after);

    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-FREE-NULL");
}

/* ── Case: dual_heap_independence ──────────────────────────────────────── */
TEST_CASE(heap, dual_heap_independence)
{
    TEST_INV_DECLARE("INV-HEAP-DUAL-INDEP", 4);

    void *l1 = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    void *h1 = rtos_malloc_from(RTOS_HEAP_HIGH, SMALL_ALLOC);
    TEST_ASSERT(l1 != NULL && h1 != NULL, "INV-HEAP-DUAL-INDEP");

    size_t high_with_both = rtos_memory_get_free_size(RTOS_HEAP_HIGH);

    /* Free LOW only — HIGH must be unaffected. */
    rtos_free(l1);
    size_t high_after_low_free = rtos_memory_get_free_size(RTOS_HEAP_HIGH);
    TEST_EXPECT(high_after_low_free == high_with_both, "INV-HEAP-DUAL-INDEP");

    /* Free HIGH only — LOW (now back to its earlier state) must not change. */
    size_t low_before_high_free = rtos_memory_get_free_size(RTOS_HEAP_LOW);
    rtos_free(h1);
    size_t low_after_high_free = rtos_memory_get_free_size(RTOS_HEAP_LOW);
    TEST_EXPECT(low_after_high_free == low_before_high_free, "INV-HEAP-DUAL-INDEP");

    /* Sanity: addresses are in expected halves. */
    TEST_EXPECT((uintptr_t) l1 < (uintptr_t) h1, "INV-HEAP-DUAL-INDEP");
}

/* ── Case: free_side_inferred_from_pointer ─────────────────────────────── */
TEST_CASE(heap, free_side_inferred_from_pointer)
{
    TEST_INV_DECLARE("INV-HEAP-INFER-SIDE", 4);

    heap_snapshot_t before;
    snapshot(&before);

    void *l = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    void *h = rtos_malloc_from(RTOS_HEAP_HIGH, SMALL_ALLOC);
    TEST_ASSERT(l != NULL && h != NULL, "INV-HEAP-INFER-SIDE");

    rtos_free(l); /* must reduce LH usage / restore LOW or BOTH */
    size_t low_after_lf = rtos_memory_get_free_size(RTOS_HEAP_LOW);

    rtos_free(h);
    size_t high_after_hf = rtos_memory_get_free_size(RTOS_HEAP_HIGH);

    /* Each side ended up restored at least to its baseline (allocations pulled
     * back into the gap or remained in the side's free list). */
    TEST_EXPECT(low_after_lf >= before.free_low, "INV-HEAP-INFER-SIDE");
    TEST_EXPECT(high_after_hf >= before.free_high, "INV-HEAP-INFER-SIDE");

    /* Total free is restored to baseline. */
    size_t both_after = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    TEST_EXPECT(both_after == before.free_both, "INV-HEAP-INFER-SIDE");
}

/* ── Case: gap_pullback_lh ─────────────────────────────────────────────── */
TEST_CASE(heap, gap_pullback_lh)
{
    TEST_INV_DECLARE("INV-HEAP-PULLBACK-LH", 2);

    heap_snapshot_t before;
    snapshot(&before);

    /* A single alloc that's the only LH block of its lifetime. On free,
     * pull-back returns the block to the gap; LH free list ends empty. */
    void *p = rtos_malloc_from(RTOS_HEAP_LOW, LARGE_ALLOC);
    TEST_ASSERT(p != NULL, "INV-HEAP-PULLBACK-LH");

    rtos_free(p);

    heap_snapshot_t after;
    snapshot(&after);

    TEST_EXPECT(after.free_low == before.free_low, "INV-HEAP-PULLBACK-LH");
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-PULLBACK-LH");
}

/* ── Case: gap_pullback_hh ─────────────────────────────────────────────── */
TEST_CASE(heap, gap_pullback_hh)
{
    TEST_INV_DECLARE("INV-HEAP-PULLBACK-HH", 2);

    heap_snapshot_t before;
    snapshot(&before);

    void *p = rtos_malloc_from(RTOS_HEAP_HIGH, LARGE_ALLOC);
    TEST_ASSERT(p != NULL, "INV-HEAP-PULLBACK-HH");

    rtos_free(p);

    heap_snapshot_t after;
    snapshot(&after);

    TEST_EXPECT(after.free_high == before.free_high, "INV-HEAP-PULLBACK-HH");
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-PULLBACK-HH");
}

/* ── Case: coalesce_forward_lh ─────────────────────────────────────────── */
TEST_CASE(heap, coalesce_forward_lh)
{
    /* alloc A,B,C on LOW; free B then C. The boundary-pull-back path
     * coalesces B into a single free block, then pulls back when C
     * falls off the high-water mark. After both frees, LOW is restored
     * to its starting state and A is still held. */
    TEST_INV_DECLARE("INV-HEAP-COALESCE-FWD", 3);

    heap_snapshot_t before;
    snapshot(&before);

    void *A = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    void *B = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    void *C = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    TEST_ASSERT(A != NULL && B != NULL && C != NULL, "INV-HEAP-COALESCE-FWD");

    rtos_free(B);
    rtos_free(C);

    /* B+C should now have been returned to the gap (C frees adjacent to
     * top, pulls back, then B is also at top now → pulls back). Both
     * pull-backs return to gap, so LOW free list contains nothing extra. */
    heap_snapshot_t mid;
    snapshot(&mid);
    TEST_EXPECT(mid.free_low == before.free_low, "INV-HEAP-COALESCE-FWD");

    rtos_free(A);

    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-COALESCE-FWD");
}

/* ── Case: coalesce_backward_lh ────────────────────────────────────────── */
TEST_CASE(heap, coalesce_backward_lh)
{
    /* alloc A,B,C; free A then B. After free A: A is in the free list
     * (not at boundary since B,C still allocated). After free B: B
     * coalesces backward into A; LOW free list has one block of size A+B.
     * (Cannot pull back — C is still at the top.) */
    TEST_INV_DECLARE("INV-HEAP-COALESCE-BWD", 3);

    heap_snapshot_t before;
    snapshot(&before);

    void *A = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    void *B = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    void *C = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
    TEST_ASSERT(A != NULL && B != NULL && C != NULL, "INV-HEAP-COALESCE-BWD");

    rtos_free(A);
    rtos_free(B);

    /* LH free list now has at least the coalesced A+B block.
     * Largest-free-block(LOW) must be >= 2 * (SMALL_ALLOC) sized chunk. */
    size_t largest_low = rtos_memory_get_largest_free_block(RTOS_HEAP_LOW);
    TEST_EXPECT(largest_low >= SMALL_ALLOC * 2U, "INV-HEAP-COALESCE-BWD");

    rtos_free(C);

    /* Now everything frees, everything pulls back. */
    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-COALESCE-BWD");
}

/* ── Case: fragmentation_visible ───────────────────────────────────────── */
TEST_CASE(heap, fragmentation_visible)
{
    /* Alloc N blocks on LOW, free every other one. The kept blocks pin
     * the freed ones inside the LH region (no pull-back). LOW free-size
     * grows by the freed bytes; largest-free-block(LOW) reflects a
     * single freed slot, not the sum. */
    TEST_INV_DECLARE("INV-HEAP-FRAGMENT", 2);

    heap_snapshot_t before;
    snapshot(&before);

    enum { N = 6 };
    void *blocks[N];
    for (int i = 0; i < N; i++)
    {
        blocks[i] = rtos_malloc_from(RTOS_HEAP_LOW, SMALL_ALLOC);
        TEST_ASSERT(blocks[i] != NULL, "INV-HEAP-FRAGMENT");
    }

    /* Free every other (indices 1, 3, 5) — leaves 0, 2, 4 as pins. */
    for (int i = 1; i < N; i += 2)
    {
        rtos_free(blocks[i]);
    }

    size_t low_free  = rtos_memory_get_free_size(RTOS_HEAP_LOW);
    size_t low_largest = rtos_memory_get_largest_free_block(RTOS_HEAP_LOW);

    /* Largest free block on LOW is strictly less than total LOW free,
     * proving fragmentation is visible to diagnostics. */
    TEST_EXPECT(low_largest < low_free, "INV-HEAP-FRAGMENT");

    /* Cleanup: free the rest. After all frees + pull-backs the heap is
     * restored to its baseline. */
    for (int i = 0; i < N; i += 2)
    {
        rtos_free(blocks[i]);
    }

    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-FRAGMENT");
}

/* ── Case: largest_free_block_includes_gap ─────────────────────────────── */
TEST_CASE(heap, largest_free_block_includes_gap)
{
    /* With nothing internally fragmented, the gap dominates and BOTH's
     * largest free block should equal the gap (which equals total BOTH
     * free at baseline since LH and HH free lists are empty after init). */
    TEST_INV_DECLARE("INV-HEAP-LARGEST-GAP", 1);

    size_t both_free        = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    size_t both_largest     = rtos_memory_get_largest_free_block(RTOS_HEAP_BOTH);

    /* The gap is one contiguous chunk, so the largest free block can't
     * exceed total free — and at baseline should equal it. */
    TEST_EXPECT(both_largest == both_free, "INV-HEAP-LARGEST-GAP");
}

/* ── Case: min_ever_free_never_increases ───────────────────────────────── */
TEST_CASE(heap, min_ever_free_never_increases)
{
    TEST_INV_DECLARE("INV-HEAP-MIN-EVER", 3);

    size_t min0   = rtos_memory_get_min_ever_free_size(RTOS_HEAP_BOTH);
    size_t now0   = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    TEST_EXPECT(min0 <= now0, "INV-HEAP-MIN-EVER");

    /* Force min_ever lower by holding several allocations. */
    void *a = rtos_malloc(LARGE_ALLOC);
    void *b = rtos_malloc(LARGE_ALLOC);
    TEST_ASSERT(a != NULL && b != NULL, "INV-HEAP-MIN-EVER");

    size_t min1 = rtos_memory_get_min_ever_free_size(RTOS_HEAP_BOTH);
    TEST_EXPECT(min1 <= min0, "INV-HEAP-MIN-EVER");

    rtos_free(a);
    rtos_free(b);

    /* Free does NOT increase min-ever — it remains the historical low-water. */
    size_t min2 = rtos_memory_get_min_ever_free_size(RTOS_HEAP_BOTH);
    TEST_EXPECT(min2 == min1, "INV-HEAP-MIN-EVER");
}

/* ── Case: cross_heap_collision_clean ──────────────────────────────────── */
TEST_CASE(heap, cross_heap_collision_clean)
{
    /* Drain by alternating LOW/HIGH allocations until both fail. The
     * boundary check inside extend_into_gap() must stop each side
     * cleanly at the gap edge with no overlap. After draining, BOTH's
     * free size should be very small (≤ one max(LH min-block, HH min-block)
     * worth). Then free everything and verify recovery. */
    TEST_INV_DECLARE("INV-HEAP-COLLIDE", 2);

    heap_snapshot_t before;
    snapshot(&before);

    /* Drain by alternating LARGE_ALLOC requests on LOW and HIGH. With a
     * 16 KB pool, kernel allocations (~3 KB), and LARGE_ALLOC=512, we
     * exhaust the heap in ~25 requests. CAP=128 is generous headroom. */
    enum { CAP = 128 };
    void *ptrs[CAP];
    int   n = 0;

    bool low_alive  = true;
    bool high_alive = true;
    while ((low_alive || high_alive) && n < CAP)
    {
        if (low_alive)
        {
            void *p = rtos_malloc_from(RTOS_HEAP_LOW, LARGE_ALLOC);
            if (p == NULL) { low_alive = false; }
            else           { ptrs[n++] = p; }
        }
        if (n >= CAP) { break; }
        if (high_alive)
        {
            void *p = rtos_malloc_from(RTOS_HEAP_HIGH, LARGE_ALLOC);
            if (p == NULL) { high_alive = false; }
            else           { ptrs[n++] = p; }
        }
    }

    /* Both sides must have been killed by collision (not by hitting CAP). */
    TEST_EXPECT(!low_alive && !high_alive, "INV-HEAP-COLLIDE");

    /* After exhaustion, total free is below one more LARGE_ALLOC. */
    size_t both_after_drain = rtos_memory_get_free_size(RTOS_HEAP_BOTH);
    TEST_EXPECT(both_after_drain < LARGE_ALLOC, "INV-HEAP-COLLIDE");

    /* Free everything and verify recovery to baseline. */
    for (int i = 0; i < n; i++)
    {
        rtos_free(ptrs[i]);
    }

    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-COLLIDE");
}

/* ── Case: static_task_doesnt_touch_heap ───────────────────────────────── */
static uint32_t g_static_stack[STATIC_STACK / sizeof(uint32_t)] __attribute__((aligned(8)));

static void static_worker(void *p)
{
    (void) p;
    rtos_task_delete(NULL); /* self-delete; static stack stays untouched */
}

TEST_CASE(heap, static_task_doesnt_touch_heap)
{
    TEST_INV_DECLARE("INV-HEAP-STATIC-TASK", 3);

    heap_snapshot_t before;
    snapshot(&before);

    rtos_task_handle_t h = NULL;
    rtos_status_t      s = rtos_task_create_static(static_worker, "STK", g_static_stack, STATIC_STACK, NULL,
                                                   PRIO_WORKER, &h);
    TEST_ASSERT(s == RTOS_SUCCESS && h != NULL, "INV-HEAP-STATIC-TASK");

    /* HIGH heap unchanged: stack came from the static buffer, not the heap. */
    size_t high_after_create = rtos_memory_get_free_size(RTOS_HEAP_HIGH);
    TEST_EXPECT(high_after_create == before.free_high, "INV-HEAP-STATIC-TASK");

    /* Let the worker self-delete and the idle drain. */
    rtos_delay_ms(50);

    /* Still no heap delta (idle's pending-stack-free slot was NULL). */
    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-STATIC-TASK");
}

/* ── Case: static_timer_doesnt_touch_heap ──────────────────────────────── */
static void static_timer_cb(void *th, void *p)
{
    (void) th;
    (void) p;
}

static rtos_timer_t g_static_timer;

TEST_CASE(heap, static_timer_doesnt_touch_heap)
{
    TEST_INV_DECLARE("INV-HEAP-STATIC-TIMER", 3);

    heap_snapshot_t before;
    snapshot(&before);

    rtos_timer_handle_t th = NULL;
    rtos_status_t s = rtos_timer_create_static(&g_static_timer, "ST", 100, RTOS_TIMER_ONE_SHOT,
                                               static_timer_cb, NULL, &th);
    TEST_ASSERT(s == RTOS_SUCCESS && th == &g_static_timer, "INV-HEAP-STATIC-TIMER");

    heap_snapshot_t mid;
    snapshot(&mid);
    TEST_EXPECT(mid.free_both == before.free_both, "INV-HEAP-STATIC-TIMER");

    rtos_timer_delete(th);

    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-STATIC-TIMER");
}

/* ── Case: static_queue_doesnt_touch_heap ──────────────────────────────── */
static rtos_queue_t g_static_queue;
static uint32_t     g_static_queue_buf[8];

TEST_CASE(heap, static_queue_doesnt_touch_heap)
{
    TEST_INV_DECLARE("INV-HEAP-STATIC-QUEUE", 3);

    heap_snapshot_t before;
    snapshot(&before);

    rtos_queue_handle_t qh = NULL;
    rtos_status_t s = rtos_queue_create_static(&g_static_queue, g_static_queue_buf, 8U, sizeof(uint32_t), &qh);
    TEST_ASSERT(s == RTOS_SUCCESS && qh == &g_static_queue, "INV-HEAP-STATIC-QUEUE");

    heap_snapshot_t mid;
    snapshot(&mid);
    TEST_EXPECT(mid.free_both == before.free_both, "INV-HEAP-STATIC-QUEUE");

    rtos_queue_delete(qh);

    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-STATIC-QUEUE");
}

/* ── Case: dynamic_timer_delete_reclaims_heap ──────────────────────────── */
TEST_CASE(heap, dynamic_timer_delete_reclaims_heap)
{
    /* The original bug: rtos_timer_delete called rtos_free which was a
     * no-op, leaking the timer CB every cycle. Now it actually frees. */
    TEST_INV_DECLARE("INV-HEAP-DYNAMIC-TIMER-RECLAIM", 1);

    heap_snapshot_t before;
    snapshot(&before);

    rtos_timer_handle_t th = NULL;
    rtos_status_t       s  = rtos_timer_create("DYNT", 100, RTOS_TIMER_ONE_SHOT, static_timer_cb, NULL, &th);
    TEST_ASSERT(s == RTOS_SUCCESS, "INV-HEAP-DYNAMIC-TIMER-RECLAIM");

    rtos_timer_delete(th);

    heap_snapshot_t after;
    snapshot(&after);

    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-DYNAMIC-TIMER-RECLAIM");
}

/* ── Case: dynamic_queue_delete_reclaims_heap ──────────────────────────── */
TEST_CASE(heap, dynamic_queue_delete_reclaims_heap)
{
    TEST_INV_DECLARE("INV-HEAP-DYNAMIC-QUEUE-RECLAIM", 1);

    heap_snapshot_t before;
    snapshot(&before);

    rtos_queue_handle_t qh = NULL;
    rtos_status_t       s  = rtos_queue_create(&qh, 8U, sizeof(uint32_t));
    TEST_ASSERT(s == RTOS_SUCCESS, "INV-HEAP-DYNAMIC-QUEUE-RECLAIM");

    rtos_queue_delete(qh);

    heap_snapshot_t after;
    snapshot(&after);

    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-DYNAMIC-QUEUE-RECLAIM");
}

/* ── Case: dynamic_task_delete_reclaims_heap ───────────────────────────── */
static rtos_task_handle_t g_dyn_worker;
static volatile int       g_dyn_worker_ran;

static void dyn_worker_body(void *p)
{
    (void) p;
    g_dyn_worker_ran = 1;
    /* Spin until the runner deletes us (non-self delete avoids the idle-deferred
     * free path; the stack is reclaimed immediately by rtos_task_delete). */
    while (1)
    {
        rtos_delay_ms(10);
    }
}

TEST_CASE(heap, dynamic_task_delete_reclaims_heap)
{
    TEST_INV_DECLARE("INV-HEAP-DYNAMIC-TASK-RECLAIM", 2);

    heap_snapshot_t before;
    snapshot(&before);

    g_dyn_worker_ran = 0;
    rtos_status_t s  = rtos_task_create(dyn_worker_body, "DYNW", RTOS_DEFAULT_TASK_STACK_SIZE, NULL,
                                        PRIO_WORKER, &g_dyn_worker);
    TEST_ASSERT(s == RTOS_SUCCESS, "INV-HEAP-DYNAMIC-TASK-RECLAIM");

    /* Wait until the worker has run at least once (proves it really took its
     * stack). Worker is lower priority than runner; yield so it can run. */
    rtos_delay_ms(20);
    TEST_EXPECT(g_dyn_worker_ran == 1, "INV-HEAP-DYNAMIC-TASK-RECLAIM");

    /* Non-self delete — stack is freed synchronously. */
    rtos_task_delete(g_dyn_worker);

    /* Give the kernel a moment to settle (no idle drain needed for non-self). */
    rtos_delay_ms(20);

    heap_snapshot_t after;
    snapshot(&after);
    TEST_EXPECT(after.free_both == before.free_both, "INV-HEAP-DYNAMIC-TASK-RECLAIM");
}

/* ── Suite registration ──────────────────────────────────────────────── */

static const test_case_t *const g_heap_cases[] = {
    &TEST_CASE_REF(heap, init_sanity),
    &TEST_CASE_REF(heap, low_alloc_routes_to_low),
    &TEST_CASE_REF(heap, high_alloc_routes_to_high),
    &TEST_CASE_REF(heap, alloc_free_round_trip),
    &TEST_CASE_REF(heap, free_null_is_noop),
    &TEST_CASE_REF(heap, dual_heap_independence),
    &TEST_CASE_REF(heap, free_side_inferred_from_pointer),
    &TEST_CASE_REF(heap, gap_pullback_lh),
    &TEST_CASE_REF(heap, gap_pullback_hh),
    &TEST_CASE_REF(heap, coalesce_forward_lh),
    &TEST_CASE_REF(heap, coalesce_backward_lh),
    &TEST_CASE_REF(heap, fragmentation_visible),
    &TEST_CASE_REF(heap, largest_free_block_includes_gap),
    &TEST_CASE_REF(heap, min_ever_free_never_increases),
    &TEST_CASE_REF(heap, cross_heap_collision_clean),
    &TEST_CASE_REF(heap, static_task_doesnt_touch_heap),
    &TEST_CASE_REF(heap, static_timer_doesnt_touch_heap),
    &TEST_CASE_REF(heap, static_queue_doesnt_touch_heap),
    &TEST_CASE_REF(heap, dynamic_timer_delete_reclaims_heap),
    &TEST_CASE_REF(heap, dynamic_queue_delete_reclaims_heap),
    &TEST_CASE_REF(heap, dynamic_task_delete_reclaims_heap),
};

TEST_SUITE_DEFINE(heap, g_heap_cases,
                  .before = heap_before);

int main(void)
{
    return test_runtime_main(&test_suite_heap);
}
