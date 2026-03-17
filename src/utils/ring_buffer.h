#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Ring buffer control structure
 *
 * Power-of-2 sized circular buffer. The caller is responsible for providing
 * storage and ensuring the size is a power of 2. The buffer supports fixed-size
 * record writes (for binary logging) and variable-length byte writes.
 *
 * Thread-safety is the caller's responsibility:
 * - For ISR-safe use (KLog, ProfTrace): wrap calls with __disable_irq()/__enable_irq()
 * - For task-context use (ULog): protect with a mutex
 */
typedef struct
{
    volatile uint32_t head; /* Write index (producer) */
    volatile uint32_t tail; /* Read index (consumer) */
    uint8_t          *buf;  /* Backing storage */
    uint32_t          mask; /* size - 1, for index wrapping */
} ring_buffer_t;

/**
 * @brief Initialize a ring buffer
 * @param rb   Ring buffer control structure
 * @param buf  Backing storage (caller-allocated)
 * @param size Size in bytes, MUST be a power of 2
 */
void ring_buffer_init(ring_buffer_t *rb, uint8_t *buf, uint32_t size);

/**
 * @brief Write data to the ring buffer
 * @param rb   Ring buffer
 * @param data Source data
 * @param len  Number of bytes to write
 * @return true if written successfully, false if insufficient space (data is dropped)
 */
bool ring_buffer_write(ring_buffer_t *rb, const void *data, uint32_t len);

/**
 * @brief Read data from the ring buffer
 * @param rb      Ring buffer
 * @param data    Destination buffer
 * @param max_len Maximum bytes to read
 * @return Number of bytes actually read
 */
uint32_t ring_buffer_read(ring_buffer_t *rb, void *data, uint32_t max_len);

/**
 * @brief Check if the ring buffer is empty
 * @param rb Ring buffer
 * @return true if empty
 */
bool ring_buffer_is_empty(const ring_buffer_t *rb);

/**
 * @brief Get number of bytes available for reading
 * @param rb Ring buffer
 * @return Byte count
 */
uint32_t ring_buffer_count(const ring_buffer_t *rb);

/**
 * @brief Get number of bytes of free space available for writing
 * @param rb Ring buffer
 * @return Free byte count
 */
uint32_t ring_buffer_free(const ring_buffer_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_H */
