/*******************************************************************************
 * File: src/utils/ulog.c
 * Description: User Logger Implementation — Deferred Formatted Logging
 ******************************************************************************/

#include "ulog.h"

#include "ring_buffer.h"
#include "rtos_port.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ================================= BUFFER ================================= */

static uint8_t       ulog_buf[ULOG_BUFFER_SIZE];
static ring_buffer_t ulog_rb;
static ulog_level_t  ulog_min_level;

/* ================================== API =================================== */

void ulog_init(ulog_level_t level)
{
    ring_buffer_init(&ulog_rb, ulog_buf, ULOG_BUFFER_SIZE);
    ulog_min_level = level;
}

void ulog(ulog_level_t level, const char *fmt, ...)
{
    if (level > ulog_min_level || fmt == NULL)
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

    /* Clamp to buffer size and append \r\n */
    if ((uint32_t) len > sizeof(scratch) - 3)
    {
        len = sizeof(scratch) - 3;
    }
    scratch[len]     = '\r';
    scratch[len + 1] = '\n';
    len += 2;

    /* Write to ring buffer under critical section */
    rtos_port_enter_critical();
    ring_buffer_write(&ulog_rb, scratch, (uint32_t) len);
    rtos_port_exit_critical();
}

uint32_t ulog_drain(uint8_t *buf, uint32_t max_len)
{
    if (buf == NULL || max_len == 0)
    {
        return 0;
    }

    rtos_port_enter_critical();
    uint32_t bytes = ring_buffer_read(&ulog_rb, buf, max_len);
    rtos_port_exit_critical();

    return bytes;
}

uint32_t ulog_pending(void)
{
    return ring_buffer_count(&ulog_rb);
}
