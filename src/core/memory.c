#include "memory.h"

#include "config.h"
#include "log.h"
#include "utils.h"

#include <string.h>

/* Heap memory pool */
static uint8_t g_heap_memory[RTOS_TOTAL_HEAP_SIZE];
static size_t  g_heap_index = 0;

/**
 * @brief Initialize memory manager
 */
void rtos_memory_init(void)
{
    g_heap_index = 0;
    /* Clear heap */
    memset(g_heap_memory, 0, sizeof(g_heap_memory));
    log_debug("Memory manager initialized. Heap size: %u bytes", RTOS_TOTAL_HEAP_SIZE);
}

/**
 * @brief Allocate memory from the heap
 */
void *rtos_malloc(size_t size)
{
    /* Align size to 8 bytes */
    ALIGN8_UP(size);

    if (g_heap_index + size > RTOS_TOTAL_HEAP_SIZE)
    {
        log_error("Malloc failed: need %u, free %u", (unsigned int) size,
                  (unsigned int) (RTOS_TOTAL_HEAP_SIZE - g_heap_index));
        return NULL;
    }

    void *ptr = &g_heap_memory[g_heap_index];
    g_heap_index += size;

    return ptr;
}

/**
 * @brief Free memory
 */
void rtos_free(void *ptr)
{
    (void) ptr;
    /* Bump allocator cannot free individual blocks */
}
