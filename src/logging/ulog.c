#include "ulog.h"

#include "config.h"
#include "ring_buffer.h"
#include "rtos_port.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint8_t       ulog_buf[ULOG_BUFFER_SIZE];
static ring_buffer_t ulog_rb;

/* Verbosity + magic in .noinit — survives NVIC_SystemReset() */
#define ULOG_NOINIT_MAGIC 0xB007CA22u

static volatile uint32_t ulog_noinit_magic __attribute__((section(".noinit")));
volatile uint8_t          ulog_verbosity    __attribute__((section(".noinit")));

void ulog_backend_init(void)
{
    ring_buffer_init(&ulog_rb, ulog_buf, ULOG_BUFFER_SIZE);
    if (ulog_noinit_magic != ULOG_NOINIT_MAGIC || ulog_verbosity > LOG_LEVEL_TRACE)
    {
        ulog_noinit_magic = ULOG_NOINIT_MAGIC;
        ulog_verbosity    = (uint8_t) RTOS_ULOG_MIN_LEVEL;
    }
}

void ulog_set_verbosity(uint8_t level)
{
    ulog_verbosity = level;
}

uint8_t ulog_get_verbosity(void)
{
    return ulog_verbosity;
}

void ulog(ulog_level_t level, const char *fmt, ...)
{
    /* Backstop for direct callers that bypass the macros. */
    if (level > ulog_verbosity || fmt == NULL)
    {
        return;
    }

    /* Format into stack-local scratch buffer — no malloc */
    char    scratch[ULOG_LINE_MAX];
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(scratch, sizeof(scratch) - 2, fmt, args);
    va_end(args);

    if (len <= 0)
    {
        return;
    }

    if ((uint32_t) len > sizeof(scratch) - 3)
    {
        len = sizeof(scratch) - 3;
    }
    scratch[len]     = '\r';
    scratch[len + 1] = '\n';
    len += 2;

    rtos_port_enter_critical();
    ring_buffer_write(&ulog_rb, scratch, (uint32_t) len);
    rtos_port_exit_critical();
}

/* ── Emit helper ──────────────────────────────────────────────────────────── */

#define ULOG_FLUSH_CHUNK 128

static uint8_t s_ulog_chunk[ULOG_FLUSH_CHUNK];

void ulog_drain_and_emit(void)
{
    uint32_t bytes;
    do
    {
        rtos_port_enter_critical();
        bytes = ring_buffer_read(&ulog_rb, s_ulog_chunk, sizeof(s_ulog_chunk));
        rtos_port_exit_critical();

        if (bytes > 0)
        {
            extern int _write(int file, char *ptr, int len);
            _write(1, (char *) s_ulog_chunk, (int) bytes);
        }
    } while (bytes > 0);
}
