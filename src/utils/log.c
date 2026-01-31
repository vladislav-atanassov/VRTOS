#include "log.h"

#include "stm32f4xx_hal.h" // IWYU pragma: keep

UART_HandleTypeDef g_huart2;
log_level_t        g_log_level = LOG_LEVEL_NONE;

void log_uart_init(log_level_t level)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin              = GPIO_PIN_2 | GPIO_PIN_3; /* PA2=TX, PA3=RX */
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

    /* Set initial log level */
    g_log_level = level;
}

/* Retarget printf */
int _write(int file, char *ptr, int len)
{
    __disable_irq();
    HAL_UART_Transmit(&g_huart2, (uint8_t *) ptr, len, HAL_MAX_DELAY);
    __enable_irq();
    return len;
}
