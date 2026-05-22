#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Heap region identifier for the bi-directional dual-ended allocator.
 *
 * The heap consists of two independent first-fit/coalescing allocators that
 * share one backing buffer and grow toward each other from opposite ends.
 * Allocations to RTOS_HEAP_LOW grow up from the buffer start; allocations to
 * RTOS_HEAP_HIGH grow down from the buffer end. When a freed block sits at
 * its side's high-water mark, that mark is pulled back and the memory is
 * returned to the shared central gap, available to either side.
 *
 * Routing convention used by the RTOS itself:
 *   RTOS_HEAP_LOW  — small/short-lived control blocks (timer CB, queue CB).
 *   RTOS_HEAP_HIGH — large/long-lived buffers (task stacks, queue item storage).
 */
typedef enum
{
    RTOS_HEAP_LOW  = 0,
    RTOS_HEAP_HIGH = 1,
    RTOS_HEAP_BOTH = 2 /**< Aggregate (diagnostics only; not a valid alloc target). */
} rtos_heap_id_t;

/**
 * @brief Initialize the dual-ended heap. Must be called before rtos_malloc().
 */
void rtos_memory_init(void);

/**
 * @brief Allocate from a specific side of the dual-ended heap.
 * @param heap RTOS_HEAP_LOW or RTOS_HEAP_HIGH (passing RTOS_HEAP_BOTH is invalid).
 * @param size Bytes requested. Rounded up to 8-byte alignment internally.
 * @return Pointer to the allocated block (8-byte aligned), or NULL on OOM.
 *
 * @note Asserts if called from ISR context.
 */
void *rtos_malloc_from(rtos_heap_id_t heap, size_t size);

/**
 * @brief Allocate from the LOW heap. Convenience wrapper around rtos_malloc_from().
 * @param size Bytes requested.
 * @return Pointer to the allocated block, or NULL on OOM.
 */
void *rtos_malloc(size_t size);

/**
 * @brief Free a block previously returned by rtos_malloc()/rtos_malloc_from().
 * The side is inferred from the pointer's address. Coalesces with adjacent
 * free blocks; pulls back the high-water mark when possible.
 * @param ptr Pointer to free, or NULL (no-op).
 *
 * @note Asserts if called from ISR context, on double-free, or on a bogus pointer.
 */
void rtos_free(void *ptr);

/* ============================ Diagnostics ============================== */

/**
 * @brief Bytes still allocatable.
 * @param heap RTOS_HEAP_LOW, RTOS_HEAP_HIGH, or RTOS_HEAP_BOTH.
 *             For LOW/HIGH: bytes in that side's free list only.
 *             For BOTH: LOW free + HIGH free + shared gap (== total free memory).
 */
size_t rtos_memory_get_free_size(rtos_heap_id_t heap);

/**
 * @brief Low-water mark of free memory observed since boot. Useful for sizing
 *        RTOS_TOTAL_HEAP_SIZE. Per side or aggregate.
 */
size_t rtos_memory_get_min_ever_free_size(rtos_heap_id_t heap);

/**
 * @brief Size of the largest contiguous free block. Detects fragmentation
 *        (malloc fails despite total-free being large).
 *        For RTOS_HEAP_BOTH: max of (LOW free list max, HIGH free list max, shared gap).
 */
size_t rtos_memory_get_largest_free_block(rtos_heap_id_t heap);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_H
