#include "test_verdict_io.h"

#include "rtos_port.h"  /* rtos_port_enter_critical / exit_critical */
#include "stm32f4xx_hal.h"
#include "uart_tx.h"    /* uart_tx_flush */

void test_verdict_emit_line(const char *line, size_t len)
{
    if (line == NULL || len == 0)
    {
        return;
    }

    /*
     * Same approach as the legacy TEST_EMIT_VERDICT in test_common.h:
     *
     *   1. Enter a critical section so the log flush task cannot interleave
     *      its _write() (which uses the interrupt-driven TX ring buffer).
     *   2. Drain the TX ring by polling so any pending log output is on the
     *      wire before our verdict line.
     *   3. Write the verdict bytes directly to USART2->DR by polling TXE.
     *   4. Wait for TC so the line is fully clocked out before returning.
     */
    rtos_port_enter_critical();
    uart_tx_flush();

    for (size_t i = 0; i < len; i++)
    {
        while (!(USART2->SR & USART_SR_TXE))
        {
        }
        USART2->DR = (uint8_t) line[i];
    }
    while (!(USART2->SR & USART_SR_TC))
    {
    }

    rtos_port_exit_critical();
}
