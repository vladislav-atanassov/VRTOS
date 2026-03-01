/*******************************************************************************
 * File: src/utils/ring_buffer.c
 * Description: Generic Lock-Free Ring Buffer Implementation
 ******************************************************************************/

#include "ring_buffer.h"

void ring_buffer_init(ring_buffer_t *rb, uint8_t *buf, uint32_t size)
{
    rb->buf  = buf;
    rb->mask = size - 1; /* Requires power-of-2 size */
    rb->head = 0;
    rb->tail = 0;
}

bool ring_buffer_write(ring_buffer_t *rb, const void *data, uint32_t len)
{
    uint32_t free_space = ring_buffer_free(rb);
    if (len > free_space)
    {
        return false; /* Drop the record — never block */
    }

    const uint8_t *src  = (const uint8_t *) data;
    uint32_t       head = rb->head;

    for (uint32_t i = 0; i < len; i++)
    {
        rb->buf[(head + i) & rb->mask] = src[i];
    }

    rb->head = (head + len) & rb->mask;
    return true;
}

uint32_t ring_buffer_read(ring_buffer_t *rb, void *data, uint32_t max_len)
{
    uint32_t available = ring_buffer_count(rb);
    uint32_t to_read   = (max_len < available) ? max_len : available;

    uint8_t *dst  = (uint8_t *) data;
    uint32_t tail = rb->tail;

    for (uint32_t i = 0; i < to_read; i++)
    {
        dst[i] = rb->buf[(tail + i) & rb->mask];
    }

    rb->tail = (tail + to_read) & rb->mask;
    return to_read;
}

bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    return rb->head == rb->tail;
}

uint32_t ring_buffer_count(const ring_buffer_t *rb)
{
    return (rb->head - rb->tail) & rb->mask;
}

uint32_t ring_buffer_free(const ring_buffer_t *rb)
{
    /* Reserve one slot to distinguish full from empty */
    return rb->mask - ring_buffer_count(rb);
}
