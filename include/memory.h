#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Initialize memory manager
 */
void rtos_memory_init(void);

/**
 * @brief Allocate memory from the heap
 *
 * @param size Size in bytes
 * @return void* Pointer to allocated memory or NULL
 */
void *rtos_malloc(size_t size);

/**
 * @brief Free memory (Not implemented in simple bump allocator)
 *
 * @param ptr Pointer to memory
 */
void rtos_free(void *ptr);

#endif // MEMORY_H
