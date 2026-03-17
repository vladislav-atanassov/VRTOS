#ifndef PROF_TRACE_H
#define PROF_TRACE_H

#include "klog_events.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef PROF_TRACE_BUFFER_SIZE
#define PROF_TRACE_BUFFER_SIZE 512 /* Must be power of 2 */
#endif

/* Compact trace record (8 bytes): timestamp + event + entity, no level or extra args. */
typedef struct __attribute__((packed))
{
    uint32_t cyccnt;    /* Raw DWT->CYCCNT */
    uint16_t event_id;  /* PEVT_* from log_event_ids.h */
    uint8_t  entity_id; /* Task ID or ISR number */
    uint8_t  _pad;      /* Alignment padding */
} prof_record_t;

void prof_trace_init(void);

/* ISR-safe. Disables interrupts to write atomically. Drops on full buffer. */
void prof_trace_emit(uint16_t event_id, uint8_t entity_id);

uint32_t prof_trace_drain(prof_record_t *out, uint32_t max_records);

#ifdef __cplusplus
}
#endif

#endif /* PROF_TRACE_H */
