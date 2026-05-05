#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

/* ── Enable only the HAL modules KARTOS actually uses ─────────────────────── */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED    /* required by HAL_UART */
#define HAL_FLASH_MODULE_ENABLED  /* required by HAL base */
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* ── Oscillator / clock defaults ─────────────────────────────────────────── */
#define HSE_VALUE              8000000U  /* 8 MHz HSE on Nucleo-F446RE */
#define HSE_STARTUP_TIMEOUT      100U
#define HSI_VALUE             16000000U
#define LSI_VALUE                32000U
#define LSE_VALUE                32768U
#define LSE_STARTUP_TIMEOUT      5000U
#define EXTERNAL_CLOCK_VALUE  12288000U

/* ── System / VDD ────────────────────────────────────────────────────────── */
#define VDD_VALUE                 3300U
#define TICK_INT_PRIORITY           15U  /* lowest priority for HAL tick */
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U
#define INSTRUCTION_CACHE_ENABLE     1U
#define DATA_CACHE_ENABLE            1U

/* ── Callback registration (disabled — not used) ─────────────────────────── */
#define USE_HAL_UART_REGISTER_CALLBACKS  0U

/* ── Disable HAL assert (no-op in production) ───────────────────────────── */
#define assert_param(expr)  ((void)0U)

/* ── Module header includes (required by this HAL version) ──────────────── */
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f4xx_hal_rcc.h"
  #include "stm32f4xx_hal_rcc_ex.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f4xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f4xx_hal_dma.h"
  #include "stm32f4xx_hal_dma_ex.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f4xx_hal_cortex.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32f4xx_hal_pwr.h"
  #include "stm32f4xx_hal_pwr_ex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f4xx_hal_flash.h"
  #include "stm32f4xx_hal_flash_ex.h"
  #include "stm32f4xx_hal_flash_ramfunc.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32f4xx_hal_uart.h"
#endif

#endif /* __STM32F4xx_HAL_CONF_H */
