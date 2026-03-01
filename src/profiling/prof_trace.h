/*******************************************************************************
 * File: src/utils/prof_trace.h
 * Description: Profiling Trace — ISR-Safe Event Timeline Records
 ******************************************************************************/

#ifndef PROF_TRACE_H
#define PROF_TRACE_H

#include "klog_events.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================= CONFIGURATION ============================== */

#ifndef PROF_TRACE_BUFFER_SIZE
#define PROF_TRACE_BUFFER_SIZE 512 /* Must be power of 2 */
#endif

/* ================================= TYPES ================================== */

/**
 * @brief Compact profiling trace record (8 bytes)
 *
 * Captures a timestamped event for timeline reconstruction on the host.
 * Lighter than klog_record_t — no level or extra args, just event + who + when.
 */
typedef struct __attribute__((packed))
{
    uint32_t cyccnt;    /* Raw DWT->CYCCNT */
    uint16_t event_id;  /* PEVT_* from log_event_ids.h */
    uint8_t  entity_id; /* Task ID or ISR number */
    uint8_t  _pad;      /* Alignment padding */
} prof_record_t;

/* ================================== API =================================== */

/**
 * @brief Initialize the profiling trace buffer
 * @note Called automatically by rtos_profiling_init() when enabled
 */
void prof_trace_init(void);

/**
 * @brief Emit a profiling trace event (ISR-safe)
 *
 * Briefly disables interrupts to atomically write an 8-byte record.
 * Never blocks, never allocates. Drops on full buffer.
 *
 * @param event_id PEVT_* event identifier
 * @param entity_id Task ID or ISR number
 */
void prof_trace_emit(uint16_t event_id, uint8_t entity_id);

/**
 * @brief Drain records from the profiling trace buffer
 * @param out         Destination array
 * @param max_records Maximum records to read
 * @return Number of records actually read
 */
uint32_t prof_trace_drain(prof_record_t *out, uint32_t max_records);

#ifdef __cplusplus
}
#endif

#endif /* PROF_TRACE_H */
