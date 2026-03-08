/*******************************************************************************
 * File: src/logging/uart_tx.c
 * Description: UART TX — Interrupt-Driven via SPSC Ring Buffer
 ******************************************************************************/

#include "uart_tx.h"

#include "stm32f4xx_hal.h" // IWYU pragma: keep

#ifndef UART_TX_BUF_SIZE
#define UART_TX_BUF_SIZE 512 /* Must be power of 2 */
#endif

UART_HandleTypeDef g_huart2;
log_level_t        g_log_level = LOG_LEVEL_NONE;

/* SPSC TX ring buffer: _write() produces, USART2_IRQHandler consumes */
static volatile uint8_t  tx_buf[UART_TX_BUF_SIZE];
static volatile uint32_t tx_head;
static volatile uint32_t tx_tail;

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
    g_huart2.Init.BaudRate     = 921600;
    g_huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    g_huart2.Init.StopBits     = UART_STOPBITS_1;
    g_huart2.Init.Parity       = UART_PARITY_NONE;
    g_huart2.Init.Mode         = UART_MODE_TX_RX;
    g_huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    g_huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_huart2);

    tx_head = 0;
    tx_tail = 0;

    HAL_NVIC_SetPriority(USART2_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    g_log_level = level;
}

/**
 * @brief Retarget for printf — copies to TX ring buffer, waits if full
 *
 * When the buffer is full, spin-waits for the TXE ISR to drain bytes.
 * Since only the lowest-priority flush task calls this, spinning does
 * not block higher-priority tasks — they preempt freely via PendSV.
 */
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

/**
 * @brief USART2 ISR — drains one byte per invocation from TX ring buffer
 *
 * Triggered when the UART TX data register is empty (TXE flag set).
 * Writes the next byte from the ring buffer to UART DR.
 * When the buffer is empty, disables TXE interrupt to stop firing.
 */
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
}

/**
 * @brief Blocking flush — drains the TX buffer by polling
 *
 * Use this:
 * - During pre-scheduler boot (before NVIC/interrupts are reliable)
 * - In hard fault handlers (interrupts may be disabled)
 * - After final printf before entering WFI
 */
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
