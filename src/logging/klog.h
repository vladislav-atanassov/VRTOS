/*******************************************************************************
 * File: src/utils/klog.h
 * Description: Kernel Logger — ISR-Safe Binary Logging
 ******************************************************************************/

#ifndef KLOG_H
#define KLOG_H

#include "klog_events.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================= CONFIGURATION ============================== */

#ifndef KLOG_BUFFER_SIZE
#define KLOG_BUFFER_SIZE 2048 /* Must be power of 2 */
#endif

#ifndef KLOG_MIN_LEVEL
#define KLOG_MIN_LEVEL KLOG_LEVEL_INFO
#endif

/* ================================= TYPES ================================== */

typedef enum
{
    KLOG_LEVEL_FAULT = 0, /* Always logged, never filtered */
    KLOG_LEVEL_ERROR,
    KLOG_LEVEL_WARN,
    KLOG_LEVEL_INFO,
    KLOG_LEVEL_DEBUG,
    KLOG_LEVEL_TRACE,
} klog_level_t;

/**
 * @brief Fixed-size binary log record (16 bytes)
 *
 * No strings, no formatting — just data. A host-side decoder maps event_id
 * to human-readable descriptions using log_event_ids.h.
 */
typedef struct __attribute__((packed))
{
    uint32_t timestamp_cycles; /* Raw DWT->CYCCNT */
    uint16_t event_id;         /* log_event_id_t */
    uint8_t  level;            /* klog_level_t */
    uint8_t  cpu_context;      /* Current task ID or ISR number */
    uint32_t arg0;
    uint32_t arg1;
} klog_record_t;

/* ================================== API =================================== */

/**
 * @brief Initialize the kernel logger
 * @note Safe to call before the scheduler is running
 */
void klog_init(void);

/**
 * @brief Write a binary log record (ISR-safe)
 *
 * Briefly disables interrupts (~10 cycles) to atomically write a 16-byte record
 * to the ring buffer. Never blocks, never calls printf, never allocates.
 * If the buffer is full, the record is silently dropped.
 */
void klog_write(klog_level_t level, uint16_t event_id, uint32_t arg0, uint32_t arg1);

/**
 * @brief Drain records from the KLog buffer
 * @param out         Destination array
 * @param max_records Maximum records to read
 * @return Number of records actually read
 */
uint32_t klog_drain(klog_record_t *out, uint32_t max_records);

/* ================================= MACROS ================================= */

/**
 * @brief Core logging macro with compile-time level filtering
 *
 * Records filtered at compile time (level > KLOG_MIN_LEVEL) produce zero code.
 */
#define KLOG(level, event_id, a0, a1)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((level) <= KLOG_MIN_LEVEL)                                                                                 \
        {                                                                                                              \
            klog_write((level), (event_id), (a0), (a1));                                                               \
        }                                                                                                              \
    } while (0)

/* Shorthand wrappers — avoid repeating the level on every call */
#define KLOGF(evt, a0, a1) KLOG(KLOG_LEVEL_FAULT, (evt), (a0), (a1))
#define KLOGE(evt, a0, a1) KLOG(KLOG_LEVEL_ERROR, (evt), (a0), (a1))
#define KLOGW(evt, a0, a1) KLOG(KLOG_LEVEL_WARN, (evt), (a0), (a1))
#define KLOGI(evt, a0, a1) KLOG(KLOG_LEVEL_INFO, (evt), (a0), (a1))
#define KLOGD(evt, a0, a1) KLOG(KLOG_LEVEL_DEBUG, (evt), (a0), (a1))
#define KLOGT(evt, a0, a1) KLOG(KLOG_LEVEL_TRACE, (evt), (a0), (a1))

#ifdef __cplusplus
}
#endif

#endif /* KLOG_H */
