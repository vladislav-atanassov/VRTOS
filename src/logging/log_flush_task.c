#include "log_flush_task.h"

#include "KARTOS.h"
#include "log.h"
#include "uart_tx.h"

/* Flush period in milliseconds */
#define KLOG_FLUSH_PERIOD_MS 100

void log_flush_task(void *param)
{
    (void) param;

    while (1)
    {
        uart_rx_process_commands();
        log_flush_drain();
        rtos_delay_ms(KLOG_FLUSH_PERIOD_MS);
    }
}
