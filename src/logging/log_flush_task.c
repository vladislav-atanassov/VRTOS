#include "log_flush_task.h"

#include "KARTOS.h"
#include "klog.h"
#include "log_common.h"
#include "task.h"
#include "uart_tx.h"
#include "ulog.h"

#include <stdio.h>
#include <string.h>

static char level_char(uint8_t lvl)
{
    static const char t[] = {'F', 'E', 'W', 'I', 'D', 'T'};
    return (lvl < 6) ? t[lvl] : '?';
}

/* Drain batch size — how many records to pull per iteration */
#define KLOG_FLUSH_BATCH 8

/* Flush period in milliseconds */
#define KLOG_FLUSH_PERIOD_MS 100

/* ULog drain chunk size */
#define ULOG_FLUSH_CHUNK 128

void log_flush_task(void *param)
{
    (void) param;

    log_packet_t batch[KLOG_FLUSH_BATCH];

    while (1)
    {
        /* 0. Process any incoming UART commands (e.g. LOG_MASK N) */
        uart_rx_process_commands();

        /* 1. Drain KLog — format and transmit each packet */
        uint32_t n = klog_drain(batch, KLOG_FLUSH_BATCH);

        for (uint32_t i = 0; i < n; i++)
        {
            const log_packet_t *p         = &batch[i];
            const char         *task_name = (p->cpu_context & 0x80) ? "ISR" : rtos_task_get_name(p->cpu_context);
            const char         *filename  = log_basename(p->file);
            char                buf[160];

            /* Decompose us into seconds.ms.us for human-readable timestamp.
             * Wraps every ~71 minutes (uint32_t us). */
            uint32_t us_total = p->timestamp_us;
            uint32_t s        = us_total / 1000000U;
            uint32_t ms       = (us_total / 1000U) % 1000U;
            uint32_t us       = us_total % 1000U;

            /* Format: "0123.456.789 [IdleTask    ] [Kernel   ] I kernel.c:45 | message"
             *   %04lu.%03lu.%03lu — seconds.ms.us (12 chars)
             *   %-12s             — task name, left-aligned in 12-char field
             *   %-9s              — module tag, left-aligned in 9-char field
             *   %c                — single level character
             *   %s                — filename (stripped)
             *   %-4u              — line number, left-aligned in 4-char field */
            int hlen = snprintf(buf, sizeof(buf), "%04lu.%03lu.%03lu [%-12s] [%-9s] %c %s:%-4u | ", (unsigned long) s,
                                (unsigned long) ms, (unsigned long) us, task_name, p->module, level_char(p->level),
                                filename, (unsigned) p->line);

            if (hlen > 0 && hlen < (int) sizeof(buf))
            {
                snprintf(buf + hlen, sizeof(buf) - (size_t) hlen, p->fmt, p->args[0], p->args[1], p->args[2],
                         p->args[3]);
            }

            strncat(buf, "\r\n", sizeof(buf) - strlen(buf) - 1);
            uart_printf("%s", buf);
        }

        /* 2. Drain ULog — pre-formatted strings, write directly to UART */
        {
            uint8_t  ulog_chunk[ULOG_FLUSH_CHUNK];
            uint32_t ulog_bytes;

            while ((ulog_bytes = ulog_drain(ulog_chunk, sizeof(ulog_chunk))) > 0)
            {
                extern int _write(int file, char *ptr, int len);
                _write(1, (char *) ulog_chunk, (int) ulog_bytes);
            }
        }

        rtos_delay_ms(KLOG_FLUSH_PERIOD_MS);
    }
}
