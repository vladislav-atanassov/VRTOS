/*******************************************************************************
 * File: examples/basic_blinky/main.c
 * Description: Basic LED Blinky Example
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS/VRTOS.h"
#include "VRTOS/task.h"
#include "stm32f4xx_hal.h"

/**
 * @file main.c
 * @brief Basic LED Blinky RTOS Example
 *
 * This example demonstrates the basic functionality of the RTOS by creating
 * a simple task that blinks the onboard LED using direct register access.
 */

/* LED Configuration for STM32F446RE Nucleo */
#define LED_PORT GPIOA
#define LED_PIN 5 /* PA5 - User LED (LD2) */
#define LED_PIN_MASK (1U << LED_PIN)

/* Task priorities */
#define BLINK_TASK_PRIORITY (2U)

/* Task stack sizes */
#define BLINK_TASK_STACK_SIZE (256U)

/* Blink timing */
#define LED_BLINK_DELAY_MS (500U)

/* Function prototypes */
static void system_clock_config(void);
static void led_init(void);
static void led_toggle(void);
static void blink_task(void *param);

/**
 * @brief Configure system clock to 84MHz
 */
static void system_clock_config(void) {
    /* Enable HSE */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
        ;

    /* Configure PLL: HSE * (N/M) / P = 8MHz * (168/4) / 2 = 168MHz */
    /* But we want 84MHz, so: HSE * (168/4) / 4 = 84MHz */
    RCC->PLLCFGR = (4 << RCC_PLLCFGR_PLLM_Pos) |   /* M = 4 */
                   (168 << RCC_PLLCFGR_PLLN_Pos) | /* N = 168 */
                   (1 << RCC_PLLCFGR_PLLP_Pos) |   /* P = 4 (01 = /4) */
                   RCC_PLLCFGR_PLLSRC_HSE;         /* HSE as source */

    /* Enable PLL */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
        ;

    /* Configure Flash latency for 84MHz */
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    /* Configure AHB, APB1, APB2 prescalers */
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 |  /* AHB = SYSCLK */
                 RCC_CFGR_PPRE1_DIV2 | /* APB1 = AHB/2 = 42MHz */
                 RCC_CFGR_PPRE2_DIV1;  /* APB2 = AHB = 84MHz */

    /* Switch to PLL */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        ;

    /* Update SystemCoreClock variable */
    SystemCoreClock = 84000000;
}

/**
 * @brief Initialize LED GPIO using direct register access
 */
static void led_init(void) {
    /* Enable GPIOA clock */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    /* Configure PA5 as output */
    LED_PORT->MODER &= ~(3U << (LED_PIN * 2)); /* Clear mode bits */
    LED_PORT->MODER |= (1U << (LED_PIN * 2));  /* Set as output */

    /* Configure as push-pull */
    LED_PORT->OTYPER &= ~LED_PIN_MASK; /* Push-pull */

    /* Configure speed as low */
    LED_PORT->OSPEEDR &= ~(3U << (LED_PIN * 2)); /* Low speed */

    /* No pull-up/pull-down */
    LED_PORT->PUPDR &= ~(3U << (LED_PIN * 2)); /* No pull */

    /* Turn off LED initially */
    LED_PORT->BSRR = LED_PIN_MASK << 16; /* Reset bit */
}

/**
 * @brief Toggle LED state using direct register access
 */
static void led_toggle(void) {
    if (LED_PORT->ODR & LED_PIN_MASK) {
        /* LED is on, turn it off */
        LED_PORT->BSRR = LED_PIN_MASK << 16; /* Reset bit */
    } else {
        /* LED is off, turn it on */
        LED_PORT->BSRR = LED_PIN_MASK; /* Set bit */
    }
}

/**
 * @brief LED blink task
 *
 * @param param Task parameter (unused)
 */
static void blink_task(void *param) {
    (void)param; /* Suppress unused parameter warning */

    /* Task main loop */
    while (1) {
        /* Toggle LED */
        led_toggle();

        /* Delay for specified time */
        rtos_delay_ms(LED_BLINK_DELAY_MS);
    }
}

/**
 * @brief Main function
 *
 * @return Should never return
 */
int main(void) {
    rtos_status_t      status;
    rtos_task_handle_t blink_task_handle;

    /* Configure system clock */
    system_clock_config();

    /* Initialize LED */
    led_init();

    /* Initialize RTOS */
    status = rtos_init();
    if (status != RTOS_SUCCESS) {
        /* Initialization failed - indicate with LED */
        while (1) {
            led_toggle();
            for (volatile uint32_t i = 0; i < 100000; i++)
                ;
        }
    }

    /* Create blink task */
    status = rtos_task_create(blink_task, "BLINK", BLINK_TASK_STACK_SIZE, NULL, BLINK_TASK_PRIORITY,
                              &blink_task_handle);
    if (status != RTOS_SUCCESS) {
        /* Task creation failed */
        while (1) {
            led_toggle();
            for (volatile uint32_t i = 0; i < 200000; i++)
                ;
        }
    }

    /* Start the RTOS scheduler */
    status = rtos_start_scheduler();

    /* Should never reach here */
    while (1) {
        /* Error indication */
        led_toggle();
        for (volatile uint32_t i = 0; i < 50000; i++)
            ;
    }

    return 0;
}