#include "klog.h"

#include "ring_buffer.h"

#include <stddef.h>

/* CMSIS for DWT, __disable_irq, __enable_irq, IPSR */
#include "stm32f4xx.h" // IWYU pragma: keep

/* Buffer in .noinit — survives soft reset for post-mortem inspection */
static volatile uint8_t klog_buf[KLOG_BUFFER_SIZE] __attribute__((section(".noinit")));
static ring_buffer_t    klog_rb;

/* Forward declaration — defined in kernel or stub */
extern uint8_t rtos_get_current_task_id(void);

void klog_init(void)
{
    ring_buffer_init(&klog_rb, (uint8_t *) klog_buf, KLOG_BUFFER_SIZE);
}

void klog_write(klog_level_t level, uint16_t event_id, uint32_t arg0, uint32_t arg1)
{
    klog_record_t record;

    record.timestamp_cycles = DWT->CYCCNT;
    record.event_id         = event_id;
    record.level            = (uint8_t) level;
    record.arg0             = arg0;
    record.arg1             = arg1;

    /* Detect ISR vs task context via IPSR */
    uint32_t ipsr = __get_IPSR();
    if (ipsr != 0)
    {
        record.cpu_context = (uint8_t) (ipsr & 0xFF); /* ISR number */
    }
    else
    {
        record.cpu_context = rtos_get_current_task_id();
    }

    /* Atomic write: disable interrupts for the brief ring buffer operation */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    ring_buffer_write(&klog_rb, &record, sizeof(record));

    __set_PRIMASK(primask);
}

uint32_t klog_drain(klog_record_t *out, uint32_t max_records)
{
    if (out == NULL || max_records == 0)
    {
        return 0;
    }

    uint32_t count = 0;

    /* Drain under interrupt protection to ensure record integrity */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    while (count < max_records)
    {
        uint32_t bytes = ring_buffer_read(&klog_rb, &out[count], sizeof(klog_record_t));
        if (bytes < sizeof(klog_record_t))
        {
            break;
        }
        count++;
    }

    __set_PRIMASK(primask);

    return count;
}
