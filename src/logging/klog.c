#include "klog.h"

#include "config.h"
#include "log_common.h"
#include "ring_buffer.h"
#include "rtos_port.h"
#include "task.h"
#include "uart_tx.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

void klog_backend_init(void)
{
    ring_buffer_init(&klog_rb, (uint8_t *) klog_buf, KLOG_BUFFER_SIZE);
    if (klog_noinit_magic != KLOG_NOINIT_MAGIC || klog_verbosity > KLOG_LEVEL_TRACE)
    {
        klog_noinit_magic = KLOG_NOINIT_MAGIC;
        klog_verbosity    = (uint8_t) RTOS_KLOG_MIN_LEVEL;
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

/* ── Emit helpers ─────────────────────────────────────────────────────────── */

static char level_char(uint8_t lvl)
{
    static const char t[] = {'F', 'E', 'W', 'I', 'D', 'T'};
    return (lvl < 6) ? t[lvl] : '?';
}

#define KLOG_FLUSH_BATCH 8

static log_packet_t s_batch[KLOG_FLUSH_BATCH];
static char         s_fmt_buf[160];

void klog_drain_and_emit(void)
{
    uint32_t n = klog_drain(s_batch, KLOG_FLUSH_BATCH);

    for (uint32_t i = 0; i < n; i++)
    {
        const log_packet_t *p         = &s_batch[i];
        const char         *task_name = (p->cpu_context & 0x80) ? "ISR" : rtos_task_get_name(p->cpu_context);
        const char         *filename  = log_basename(p->file);

        /* Decompose us into seconds.ms.us for human-readable timestamp. */
        uint32_t us_total = p->timestamp_us;
        uint32_t s        = us_total / 1000000U;
        uint32_t ms       = (us_total / 1000U) % 1000U;
        uint32_t us       = us_total % 1000U;

        int hlen = snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                            "%04lu.%03lu.%03lu [%-12s] [%-9s] %c %s:%-4u | ", (unsigned long) s,
                            (unsigned long) ms, (unsigned long) us, task_name, p->module,
                            level_char(p->level), filename, (unsigned) p->line);

        if (hlen > 0 && hlen < (int) sizeof(s_fmt_buf))
        {
            snprintf(s_fmt_buf + hlen, sizeof(s_fmt_buf) - (size_t) hlen, p->fmt,
                     p->args[0], p->args[1], p->args[2], p->args[3]);
        }

        strncat(s_fmt_buf, "\r\n", sizeof(s_fmt_buf) - strlen(s_fmt_buf) - 1);
        uart_printf("%s", s_fmt_buf);
    }
}
