/**
 * @file memory.c
 * @brief Bi-directional dual-ended heap allocator.
 *
 * Two independent first-fit allocators with adjacent-block coalescing share
 * one backing buffer and grow toward each other from opposite ends. LH
 * (low heap) carves from the buffer start growing up; HH (high heap) carves
 * from the buffer end growing down. The shared central gap is available to
 * either side. When a freed block is adjacent to its side's high-water mark,
 * the mark is pulled back and the memory is returned to the gap.
 *
 *   [LH region][... gap ...][HH region]
 *    ^        ^             ^         ^
 *    begin    g_lh_top      g_hh_bot  end
 *
 * Block header (8 bytes on M4): {next_free, size}. The MSB of size flags
 * allocated; the low bits hold the block size including header. Free blocks
 * are linked in per-side singly-linked lists sorted by address ascending.
 *
 * Thread safety: all public entry points take the kernel critical section.
 * Not ISR-safe — RTOS_ASSERT fires if rtos_malloc/free is called from an ISR.
 */

#include "memory.h"

#include "config.h"
#include "klog.h"
#include "rtos_assert.h"
#include "rtos_hooks.h"
#include "rtos_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ───────────────────────── Block header ────────────────────────────────── */

typedef struct rtos_heap_block
{
    struct rtos_heap_block *next_free; /**< Free-list link (NULL = end). Garbage when allocated. */
    size_t                  size;      /**< Block size incl. header. MSB = ALLOCATED flag. */
} rtos_heap_block_t;

#define HEAP_HEADER_SIZE   (sizeof(rtos_heap_block_t))
#define HEAP_ALLOCATED_BIT (((size_t) 1U) << ((sizeof(size_t) * 8U) - 1U))
#define HEAP_SIZE_MASK     (~HEAP_ALLOCATED_BIT)

RTOS_STATIC_ASSERT((HEAP_HEADER_SIZE % 8U) == 0U, "heap header must be 8-byte aligned");
RTOS_STATIC_ASSERT(RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE >= 16U, "min block size must accommodate header + payload");
RTOS_STATIC_ASSERT((RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE % 8U) == 0U, "min block size must be 8-byte aligned");

/* ───────────────────────── Per-side state ──────────────────────────────── */

typedef struct
{
    rtos_heap_block_t *free_list;      /**< Head of free list, sorted by address ascending. */
    size_t             free_bytes;     /**< Sum of block_size() over free_list (does NOT include gap). */
    size_t             min_free_bytes; /**< Low-water mark of free_bytes since boot. */
} heap_state_t;

/* Aligned heap pool. Aligned to 8 to keep block headers naturally aligned. */
static uint8_t g_heap_memory[RTOS_TOTAL_HEAP_SIZE] __attribute__((aligned(8)));

static heap_state_t g_lh;
static heap_state_t g_hh;

/* Boundaries: g_lh_top is the first byte past LH; g_hh_bot is the first byte of HH. */
static uint8_t *g_lh_top;
static uint8_t *g_hh_bot;
static size_t   g_gap_bytes;

/* Low-water mark of total free (LH free + HH free + gap) since boot. */
static size_t g_min_total_free_bytes;

/* ───────────────────────── Header bit helpers ──────────────────────────── */

static inline size_t block_size(const rtos_heap_block_t *b)
{
    return b->size & HEAP_SIZE_MASK;
}

static inline bool block_is_allocated(const rtos_heap_block_t *b)
{
    return (b->size & HEAP_ALLOCATED_BIT) != 0U;
}

static inline void block_set_allocated(rtos_heap_block_t *b)
{
    b->size |= HEAP_ALLOCATED_BIT;
}

static inline void block_clear_allocated(rtos_heap_block_t *b)
{
    b->size &= HEAP_SIZE_MASK;
}

static inline uint8_t *heap_begin(void)
{
    return g_heap_memory;
}

static inline uint8_t *heap_end(void)
{
    return g_heap_memory + RTOS_TOTAL_HEAP_SIZE;
}

static inline size_t total_free_now(void)
{
    return g_lh.free_bytes + g_hh.free_bytes + g_gap_bytes;
}

static inline void touch_min_water(heap_state_t *S)
{
    if (S->free_bytes < S->min_free_bytes)
    {
        S->min_free_bytes = S->free_bytes;
    }
    size_t total = total_free_now();
    if (total < g_min_total_free_bytes)
    {
        g_min_total_free_bytes = total;
    }
}

/* ───────────────────────── Init ────────────────────────────────────────── */

void rtos_memory_init(void)
{
    memset(g_heap_memory, 0, sizeof(g_heap_memory));

    g_lh.free_list      = NULL;
    g_lh.free_bytes     = 0U;
    g_lh.min_free_bytes = 0U;

    g_hh.free_list      = NULL;
    g_hh.free_bytes     = 0U;
    g_hh.min_free_bytes = 0U;

    g_lh_top    = heap_begin();
    g_hh_bot    = heap_end();
    g_gap_bytes = (size_t) (g_hh_bot - g_lh_top);

    g_min_total_free_bytes = g_gap_bytes;

    KLOGD("Mem", "DualHeapInit total=%u", (unsigned) RTOS_TOTAL_HEAP_SIZE);
}

/* ───────────────────────── Allocation ──────────────────────────────────── */

/* Try to satisfy a request from the free list of one side. Returns the block
 * (with ALLOCATED bit set) on success, NULL if no fit. */
static rtos_heap_block_t *try_alloc_from_free_list(heap_state_t *S, size_t want)
{
    rtos_heap_block_t *prev = NULL;
    rtos_heap_block_t *cur  = S->free_list;
    while (cur != NULL && block_size(cur) < want)
    {
        prev = cur;
        cur  = cur->next_free;
    }
    if (cur == NULL)
    {
        return NULL;
    }

    const size_t found_size = block_size(cur);
    size_t       taken;

    if (found_size - want >= RTOS_CONFIG_HEAP_MIN_BLOCK_SIZE)
    {
        /* Split: the trailing remainder stays free. */
        rtos_heap_block_t *rem = (rtos_heap_block_t *) ((uint8_t *) cur + want);
        rem->size              = found_size - want;
        rem->next_free         = cur->next_free;
        cur->size              = want;
        if (prev != NULL)
        {
            prev->next_free = rem;
        }
        else
        {
            S->free_list = rem;
        }
        taken = want;
    }
    else
    {
        /* Take the whole block (don't bother splitting tiny leftover). */
        if (prev != NULL)
        {
            prev->next_free = cur->next_free;
        }
        else
        {
            S->free_list = cur->next_free;
        }
        taken = found_size;
    }

    S->free_bytes -= taken;
    block_set_allocated(cur);
    return cur;
}

/* Carve a fresh block out of the shared gap by extending one side's high-water
 * mark. Returns the new block (allocated bit set) or NULL on OOM. */
static rtos_heap_block_t *extend_into_gap(rtos_heap_id_t heap, size_t want)
{
    if (g_gap_bytes < want)
    {
        return NULL;
    }

    rtos_heap_block_t *block;
    if (heap == RTOS_HEAP_LOW)
    {
        block = (rtos_heap_block_t *) g_lh_top;
        g_lh_top += want;
    }
    else
    {
        g_hh_bot -= want;
        block = (rtos_heap_block_t *) g_hh_bot;
    }
    g_gap_bytes -= want;

    block->next_free = NULL;
    block->size      = want;
    block_set_allocated(block);
    return block;
}

void *rtos_malloc_from(rtos_heap_id_t heap, size_t size)
{
    RTOS_ASSERT(!rtos_port_in_isr());
    RTOS_ASSERT(heap == RTOS_HEAP_LOW || heap == RTOS_HEAP_HIGH);

    if (size == 0U)
    {
        return NULL;
    }

    /* Total wanted block size = aligned payload + header. */
    size_t want = size + HEAP_HEADER_SIZE;
    /* Round up to 8 to keep all returned pointers aligned. */
    want = (want + 7U) & ~((size_t) 7U);

    /* Reject obviously impossible requests up front. */
    if (want > RTOS_TOTAL_HEAP_SIZE)
    {
        KLOGE("Mem", "AllocTooLarge want=%u", (unsigned) want);
        rtos_application_malloc_failed_hook(size, heap);
        return NULL;
    }

    heap_state_t *S = (heap == RTOS_HEAP_LOW) ? &g_lh : &g_hh;

    rtos_port_enter_critical();

    rtos_heap_block_t *block = try_alloc_from_free_list(S, want);
    if (block == NULL)
    {
        block = extend_into_gap(heap, want);
    }

    if (block == NULL)
    {
        rtos_port_exit_critical();
        KLOGE("Mem", "AllocFail heap=%u want=%u gap=%u lh=%u hh=%u", (unsigned) heap, (unsigned) want,
              (unsigned) g_gap_bytes, (unsigned) g_lh.free_bytes, (unsigned) g_hh.free_bytes);
        rtos_application_malloc_failed_hook(size, heap);
        return NULL;
    }

    touch_min_water(S);

    rtos_port_exit_critical();

    return (void *) ((uint8_t *) block + HEAP_HEADER_SIZE);
}

void *rtos_malloc(size_t size)
{
    return rtos_malloc_from(RTOS_HEAP_LOW, size);
}

/* ───────────────────────── Free ────────────────────────────────────────── */

/* Insert+coalesce+pull-back. Caller holds the critical section. */
static void heap_free_side(heap_state_t *S, rtos_heap_block_t *block, rtos_heap_id_t heap)
{
    size_t sz = block_size(block);
    S->free_bytes += sz;

    /* Locate insertion point: pred = greatest free block with addr < block. */
    rtos_heap_block_t *pred = NULL;
    rtos_heap_block_t *cur  = S->free_list;
    while (cur != NULL && cur < block)
    {
        pred = cur;
        cur  = cur->next_free;
    }
    rtos_heap_block_t *next = cur; /* first free block at addr >= block, or NULL */

    /* Defensive: detect overlap with neighbouring free blocks (corruption). */
    RTOS_ASSERT(pred == NULL || (uint8_t *) pred + block_size(pred) <= (uint8_t *) block);
    RTOS_ASSERT(next == NULL || (uint8_t *) block + sz <= (uint8_t *) next);

    /* Backward coalesce: if pred sits exactly against block, fold block into pred. */
    if (pred != NULL && (uint8_t *) pred + block_size(pred) == (uint8_t *) block)
    {
        pred->size = block_size(pred) + sz;
        block      = pred;
        sz         = block_size(block);
        /* block now sits in the list in pred's slot; do NOT re-insert. */
    }
    else
    {
        /* Normal insert between pred and next. */
        block->next_free = next;
        if (pred != NULL)
        {
            pred->next_free = block;
        }
        else
        {
            S->free_list = block;
        }
    }

    /* Forward coalesce: if block now touches next, absorb next. */
    if (next != NULL && (uint8_t *) block + sz == (uint8_t *) next)
    {
        block->next_free = next->next_free;
        block->size      = sz + block_size(next);
        sz               = block_size(block);
    }

    /* Pull back the high-water mark if the resulting free block sits at it. */
    bool at_boundary = (heap == RTOS_HEAP_LOW) ? ((uint8_t *) block + sz == g_lh_top) : ((uint8_t *) block == g_hh_bot);
    if (at_boundary)
    {
        /* Unlink block from the free list. After coalescing block may be at
         * a different list position than where we inserted, so walk to find
         * its predecessor. */
        rtos_heap_block_t **link = &S->free_list;
        while (*link != NULL && *link != block)
        {
            link = &(*link)->next_free;
        }
        RTOS_ASSERT(*link == block);
        *link = block->next_free;

        if (heap == RTOS_HEAP_LOW)
        {
            g_lh_top -= sz;
        }
        else
        {
            g_hh_bot += sz;
        }
        g_gap_bytes += sz;
        S->free_bytes -= sz;
    }

    touch_min_water(S);
}

void rtos_free(void *ptr)
{
    RTOS_ASSERT(!rtos_port_in_isr());

    if (ptr == NULL)
    {
        return;
    }

    rtos_heap_block_t *block = (rtos_heap_block_t *) ((uint8_t *) ptr - HEAP_HEADER_SIZE);

    /* Pointer-range sanity. The actual side check requires the critical section
     * because g_lh_top / g_hh_bot move. Do a cheap range check first. */
    RTOS_ASSERT((uint8_t *) block >= heap_begin() && (uint8_t *) block < heap_end());
    RTOS_ASSERT(block_is_allocated(block));
    RTOS_ASSERT((block_size(block) % 8U) == 0U);
    RTOS_ASSERT(block_size(block) >= HEAP_HEADER_SIZE);

    rtos_port_enter_critical();

    /* Determine which side. Must be inside [begin, g_lh_top) for LH or
     * [g_hh_bot, end) for HH; anything in the gap means corruption. */
    rtos_heap_id_t heap;
    if ((uint8_t *) block < g_lh_top)
    {
        heap = RTOS_HEAP_LOW;
    }
    else if ((uint8_t *) block >= g_hh_bot)
    {
        heap = RTOS_HEAP_HIGH;
    }
    else
    {
        rtos_port_exit_critical();
        RTOS_ASSERT(0); /* ptr is inside the gap — pointer is bogus */
        return;
    }

    /* End of block must also be inside the same side's region. */
    RTOS_ASSERT(heap != RTOS_HEAP_LOW || (uint8_t *) block + block_size(block) <= g_lh_top);
    RTOS_ASSERT(heap != RTOS_HEAP_HIGH || (uint8_t *) block + block_size(block) <= heap_end());

    block_clear_allocated(block);
    block->next_free = NULL;

    heap_state_t *S = (heap == RTOS_HEAP_LOW) ? &g_lh : &g_hh;
    heap_free_side(S, block, heap);

    rtos_port_exit_critical();
}

/* ───────────────────────── Diagnostics ─────────────────────────────────── */

size_t rtos_memory_get_free_size(rtos_heap_id_t heap)
{
    size_t result;
    rtos_port_enter_critical();
    switch (heap)
    {
        case RTOS_HEAP_LOW:
            result = g_lh.free_bytes;
            break;
        case RTOS_HEAP_HIGH:
            result = g_hh.free_bytes;
            break;
        case RTOS_HEAP_BOTH:
            result = total_free_now();
            break;
        default:
            result = 0U;
            break;
    }
    rtos_port_exit_critical();
    return result;
}

size_t rtos_memory_get_min_ever_free_size(rtos_heap_id_t heap)
{
    size_t result;
    rtos_port_enter_critical();
    switch (heap)
    {
        case RTOS_HEAP_LOW:
            result = g_lh.min_free_bytes;
            break;
        case RTOS_HEAP_HIGH:
            result = g_hh.min_free_bytes;
            break;
        case RTOS_HEAP_BOTH:
            result = g_min_total_free_bytes;
            break;
        default:
            result = 0U;
            break;
    }
    rtos_port_exit_critical();
    return result;
}

static size_t side_largest_free_block(const heap_state_t *S)
{
    size_t largest = 0U;
    for (const rtos_heap_block_t *b = S->free_list; b != NULL; b = b->next_free)
    {
        size_t s = block_size(b);
        if (s > largest)
        {
            largest = s;
        }
    }
    return largest;
}

size_t rtos_memory_get_largest_free_block(rtos_heap_id_t heap)
{
    size_t result;
    rtos_port_enter_critical();
    switch (heap)
    {
        case RTOS_HEAP_LOW:
            result = side_largest_free_block(&g_lh);
            break;
        case RTOS_HEAP_HIGH:
            result = side_largest_free_block(&g_hh);
            break;
        case RTOS_HEAP_BOTH:
        {
            size_t lh  = side_largest_free_block(&g_lh);
            size_t hh  = side_largest_free_block(&g_hh);
            size_t gap = g_gap_bytes;
            result     = (lh > hh) ? lh : hh;
            if (gap > result)
            {
                result = gap;
            }
            break;
        }
        default:
            result = 0U;
            break;
    }
    rtos_port_exit_critical();
    return result;
}
