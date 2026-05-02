#include "uart_tx.h"

#include "klog.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep

#include <stdarg.h>
#include <stdio.h>

#ifndef UART_TX_BUF_SIZE
#define UART_TX_BUF_SIZE 512 /* Must be power of 2 */
#endif

#define UART_RX_BUF_SIZE 64 /* Must be power of 2 */

UART_HandleTypeDef g_huart2;
log_level_t        g_log_level = LOG_LEVEL_NONE;

/* SPSC TX ring buffer: _write() produces, USART2_IRQHandler consumes */
static volatile uint8_t  tx_buf[UART_TX_BUF_SIZE];
static volatile uint32_t tx_head;
static volatile uint32_t tx_tail;

/* SPSC RX ring buffer: USART2_IRQHandler produces, uart_rx_process_commands consumes */
static volatile uint8_t  rx_buf[UART_RX_BUF_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;

static inline uint32_t tx_count(void)
{
    return (tx_head - tx_tail) & (UART_TX_BUF_SIZE - 1);
}

static inline uint32_t tx_free(void)
{
    return (UART_TX_BUF_SIZE - 1) - tx_count();
}

void log_uart_init(log_level_t level)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin              = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode             = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate        = GPIO_AF7_USART2;
    GPIO_InitStruct.Pull             = GPIO_NOPULL;
    GPIO_InitStruct.Speed            = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    g_huart2.Instance          = USART2;
    g_huart2.Init.BaudRate     = 115200;
    g_huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    g_huart2.Init.StopBits     = UART_STOPBITS_1;
    g_huart2.Init.Parity       = UART_PARITY_NONE;
    g_huart2.Init.Mode         = UART_MODE_TX_RX;
    g_huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    g_huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_huart2);

    tx_head = 0;
    tx_tail = 0;
    rx_head = 0;
    rx_tail = 0;

    HAL_NVIC_SetPriority(USART2_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    /* Enable both TX-empty and RX-not-empty interrupts */
    USART2->CR1 |= USART_CR1_RXNEIE;

    /* Disable stdout buffering — newlib block-buffers stdout when isatty()==0,
     * so printf output otherwise sits in libc's buffer and never reaches _write. */
    setvbuf(stdout, NULL, _IONBF, 0);

    g_log_level = level;
}

/* printf retarget — copies to TX ring buffer, spins if full.
 * Only the lowest-priority flush task calls this, so spinning
 * doesn't block higher-priority tasks. */
int _write(int file, char *ptr, int len)
{
    (void) file;

    for (int i = 0; i < len; i++)
    {
        while (tx_free() == 0)
        {
            USART2->CR1 |= USART_CR1_TXEIE;
        }

        tx_buf[tx_head & (UART_TX_BUF_SIZE - 1)] = (uint8_t) ptr[i];
        tx_head++;
    }

    USART2->CR1 |= USART_CR1_TXEIE;

    return len;
}

/* TXE + RXNE ISR */
void USART2_IRQHandler(void)
{
    if ((USART2->SR & USART_SR_TXE) && (USART2->CR1 & USART_CR1_TXEIE))
    {
        if (tx_head != tx_tail)
        {
            USART2->DR = tx_buf[tx_tail & (UART_TX_BUF_SIZE - 1)];
            tx_tail++;
        }
        else
        {
            USART2->CR1 &= ~USART_CR1_TXEIE;
        }
    }

    if (USART2->SR & USART_SR_RXNE)
    {
        uint8_t  ch   = (uint8_t)(USART2->DR & 0xFF);
        uint32_t next = (rx_head + 1) & (UART_RX_BUF_SIZE - 1);
        if (next != rx_tail)
        {
            rx_buf[rx_head] = ch;
            rx_head         = next;
        }
    }
}

/* Blocking flush — drains TX buffer by polling.
 * Use during pre-scheduler boot, fault handlers, or before WFI. */
void uart_tx_flush(void)
{
    USART2->CR1 &= ~USART_CR1_TXEIE;

    while (tx_head != tx_tail)
    {
        while (!(USART2->SR & USART_SR_TXE))
        {
        }
        USART2->DR = tx_buf[tx_tail & (UART_TX_BUF_SIZE - 1)];
        tx_tail++;
    }

    while (!(USART2->SR & USART_SR_TC))
    {
    }
}

/* Direct printf replacement — formats with vsnprintf, then writes via _write.
 * Bypasses newlib's stdio buffering entirely. */
void uart_printf(const char *fmt, ...)
{
    char    buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return;
    }
    if ((size_t) n >= sizeof(buf))
    {
        n = (int) sizeof(buf) - 1;
    }
    _write(1, buf, n);
}

/* Drain RX ring buffer into a line accumulator and parse LOG_MASK commands.
 * Call periodically from the log flush task. */
void uart_rx_process_commands(void)
{
    static char    line[32];
    static uint8_t pos = 0;

    while (rx_tail != rx_head)
    {
        char ch = (char) rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & (UART_RX_BUF_SIZE - 1);

        if (ch == '\n' || ch == '\r')
        {
            if (pos > 0)
            {
                line[pos] = '\0';
                unsigned int lvl;
                if (sscanf(line, "LOG_MASK %u", &lvl) == 1 && lvl <= KLOG_LEVEL_TRACE)
                {
                    klog_set_verbosity((uint8_t) lvl);
                    NVIC_SystemReset();
                }
                pos = 0;
            }
        }
        else if (pos < (uint8_t)(sizeof(line) - 1))
        {
            line[pos++] = ch;
        }
    }
}
