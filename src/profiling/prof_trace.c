/*******************************************************************************
 * File: src/utils/prof_trace.c
 * Description: Profiling Trace Implementation — ISR-Safe Event Timeline
 ******************************************************************************/

#include "prof_trace.h"

#include "ring_buffer.h"

#include <stddef.h>

/* CMSIS for DWT, __disable_irq, __enable_irq */
#include "stm32f4xx.h" // IWYU pragma: keep

/* ================================= BUFFER ================================= */

/* Buffer in .noinit — survives soft reset for post-mortem analysis */
static volatile uint8_t prof_buf[PROF_TRACE_BUFFER_SIZE] __attribute__((section(".noinit")));
static ring_buffer_t    prof_rb;

/* ================================== API =================================== */

void prof_trace_init(void)
{
    ring_buffer_init(&prof_rb, (uint8_t *) prof_buf, PROF_TRACE_BUFFER_SIZE);
}

void prof_trace_emit(uint16_t event_id, uint8_t entity_id)
{
    prof_record_t record;

    record.cyccnt    = DWT->CYCCNT;
    record.event_id  = event_id;
    record.entity_id = entity_id;
    record._pad      = 0;

    /* Atomic write under interrupt disable */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    ring_buffer_write(&prof_rb, &record, sizeof(record));

    __set_PRIMASK(primask);
}

uint32_t prof_trace_drain(prof_record_t *out, uint32_t max_records)
{
    if (out == NULL || max_records == 0)
    {
        return 0;
    }

    uint32_t count = 0;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    while (count < max_records)
    {
        uint32_t bytes = ring_buffer_read(&prof_rb, &out[count], sizeof(prof_record_t));
        if (bytes < sizeof(prof_record_t))
        {
            break;
        }
        count++;
    }

    __set_PRIMASK(primask);

    return count;
}
