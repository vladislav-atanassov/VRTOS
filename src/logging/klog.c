#include "klog.h"

#include "ring_buffer.h"
#include "rtos_port.h"

#include <stddef.h>

/* Forward declaration — defined in task.c */
extern uint8_t rtos_get_current_task_id(void);

/* CMSIS for __get_IPSR, __disable_irq, __set_PRIMASK */
#include "stm32f4xx.h" // IWYU pragma: keep

/* Buffer + verbosity in .noinit — survives NVIC_SystemReset() */
static volatile uint8_t  klog_buf[KLOG_BUFFER_SIZE] __attribute__((section(".noinit")));
static ring_buffer_t     klog_rb;

#define KLOG_NOINIT_MAGIC 0xB007CA11u

static volatile uint32_t klog_noinit_magic __attribute__((section(".noinit")));
volatile uint8_t          klog_verbosity    __attribute__((section(".noinit")));

void klog_init(void)
{
    ring_buffer_init(&klog_rb, (uint8_t *) klog_buf, KLOG_BUFFER_SIZE);
    if (klog_noinit_magic != KLOG_NOINIT_MAGIC || klog_verbosity > KLOG_LEVEL_TRACE)
    {
        klog_noinit_magic = KLOG_NOINIT_MAGIC;
        klog_verbosity    = (uint8_t) KLOG_MIN_LEVEL;
    }
}

void klog_set_verbosity(uint8_t level)
{
    klog_verbosity = level;
}

uint8_t klog_get_verbosity(void)
{
    return klog_verbosity;
}

/* ISR-safe. Never blocks, never allocates. Drops silently on full buffer. */
void klog_write(klog_level_t level, const char *module, const char *file, uint16_t line,
                const char *fmt, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    log_packet_t pkt;

    pkt.timestamp_us = rtos_port_get_uptime_us();
    pkt.level        = (uint8_t) level;
    pkt.line         = line;
    pkt.module       = module;
    pkt.file         = file;
    pkt.fmt          = fmt;
    pkt.args[0]      = a0;
    pkt.args[1]      = a1;
    pkt.args[2]      = a2;
    pkt.args[3]      = a3;

    /* cpu_context encoding:
     *   bit 7 = 1 → ISR; low 7 bits = exception number (IPSR)
     *   bit 7 = 0 → task; low 7 bits = task_id (0xFF = pre-scheduler) */
    uint32_t ipsr   = __get_IPSR();
    pkt.cpu_context = (ipsr != 0) ? (uint8_t) (0x80 | (ipsr & 0x7F)) : rtos_get_current_task_id();

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    ring_buffer_write(&klog_rb, &pkt, sizeof(pkt));
    __set_PRIMASK(primask);
}

uint32_t klog_drain(log_packet_t *out, uint32_t max_records)
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
        uint32_t bytes = ring_buffer_read(&klog_rb, &out[count], sizeof(log_packet_t));
        if (bytes < sizeof(log_packet_t))
        {
            break;
        }
        count++;
    }

    __set_PRIMASK(primask);

    return count;
}
