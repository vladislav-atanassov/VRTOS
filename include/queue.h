#ifndef QUEUE_H
#define QUEUE_H

#include "rtos_types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct rtos_queue *rtos_queue_handle_t;

rtos_status_t rtos_queue_create(rtos_queue_handle_t *queue_handle, uint32_t item_count, uint32_t item_size);

/* timeout_ticks: 0=non-blocking, RTOS_MAX_DELAY=forever */
rtos_status_t rtos_queue_send(rtos_queue_handle_t queue_handle, const void *item_ptr, rtos_tick_t timeout_ticks);

/* timeout_ticks: 0=non-blocking, RTOS_MAX_DELAY=forever */
rtos_status_t rtos_queue_receive(rtos_queue_handle_t queue_handle, void *buffer, rtos_tick_t timeout_ticks);

uint32_t rtos_queue_messages_waiting(rtos_queue_handle_t queue_handle);
uint32_t rtos_queue_spaces_available(rtos_queue_handle_t queue_handle);
bool     rtos_queue_is_full(rtos_queue_handle_t queue_handle);
bool     rtos_queue_is_empty(rtos_queue_handle_t queue_handle);

/* Warning: does not wake waiting receivers. All data is discarded. */
rtos_status_t rtos_queue_reset(rtos_queue_handle_t queue_handle);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */