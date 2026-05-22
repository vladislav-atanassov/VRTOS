/*
 * Pass-through implementations of rtos_malloc / rtos_malloc_from / rtos_free
 * that delegate to libc. Used by host tests that need *some* allocator but
 * aren't exercising the real production allocator.
 *
 * Tests that DO link src/core/memory.c (e.g. test_heap_stress) must NOT link
 * this file, or they will get duplicate-symbol errors.
 */

#include "memory.h"

#include <stdlib.h>

void *rtos_malloc_from(rtos_heap_id_t heap, size_t size)
{
    (void) heap;
    return malloc(size);
}

void *rtos_malloc(size_t size)
{
    return rtos_malloc_from(RTOS_HEAP_LOW, size);
}

void rtos_free(void *ptr)
{
    free(ptr);
}
