/*******************************************************************************
 * File: examples/basic_blinky/main.c
 * Description: Basic LED Blinky Example
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "log.h"
#include "stm32f4xx_hal.h"
#include "task.h"

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
#define PRINT_TASK_PRIORITY (3U)

/* Task stack sizes */
#define TASK_STACK_SIZE (256U)

/* Blink & Print timing */
#define LED_BLINK_DELAY_MS (5000U)
#define PRINT_DELAY_MS (8000U)

void        SystemClock_Config(void);
static void MX_GPIO_Init(void);

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
    log_info("IN blink_task()");
    /* Task main loop */
    while (1) {
        /* Toggle LED */
        led_toggle();
        log_info("BLINK");
        /* Delay for specified time */
        rtos_delay_ms(LED_BLINK_DELAY_MS);
        // for (volatile int i = 0; i < 5000000; i++)
        //     ;
    }
}

/**
 * @brief Print UART task
 *
 * @param param Task parameter (unused)
 */
static void print_task(void *param) {
    (void)param; /* Suppress unused parameter warning */
    log_info("IN print_task()");
    /* Task main loop */
    while (1) {
        log_info("PRINT");
        /* Delay for specified time */
        rtos_delay_ms(PRINT_DELAY_MS);
        // for (volatile int i = 0; i < 5000000; i++)
        //     ;
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
    rtos_task_handle_t print_task_handle;

    /* System initializations */
    SCB->VTOR = FLASH_BASE; /* Set vector table location */
    SystemClock_Config();
    MX_GPIO_Init();
    __enable_irq(); /* Enable global interrupts */

    log_uart_init(LOG_LEVEL_ALL);

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
    status =
        rtos_task_create(blink_task, "BLINK", TASK_STACK_SIZE, NULL, BLINK_TASK_PRIORITY, &blink_task_handle);

    log_info("EXIT CODE OF rtos_task_create(): %d, expected: %d", status, RTOS_SUCCESS);

    if (status != RTOS_SUCCESS) {
        /* Task creation failed */
        while (1) {
            led_toggle();
            for (volatile uint32_t i = 0; i < 200000; i++)
                ;
        }
    }

    /* Create print task */
    status =
        rtos_task_create(print_task, "PRINT", TASK_STACK_SIZE, NULL, PRINT_TASK_PRIORITY, &print_task_handle);

    log_info("EXIT CODE OF rtos_task_create(): %d, expected: %d", status, RTOS_SUCCESS);

    if (status != RTOS_SUCCESS) {
        /* Task creation failed */
        while (1) {
            led_toggle();
            for (volatile uint32_t i = 0; i < 200000; i++)
                ;
        }
    }

    log_info("Blink task func: 0x%08X", (uint32_t)blink_task);
    log_info("Print task func: 0x%08X", (uint32_t)print_task);
    log_info("ENTERING rtos_start_scheduler()");

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

void Error_Handler(void) {
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {
    }
    /* USER CODE END Error_Handler_Debug */
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    /* Initializes the RCC Oscillators according to the specified parameters in the RCC_OscInitTypeDef structure. */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void HardFault_Handler(void) {
    /* Capture fault registers immediately */
    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar = SCB->BFAR;
    uint32_t psp = __get_PSP();
    uint32_t msp = __get_MSP();

    /* Direct register access for debugging */
    log_error("HardFault Registers:");
    log_error(" CFSR=0x%08X HFSR=0x%08X", cfsr, hfsr);
    log_error(" MMFAR=0x%08X BFAR=0x%08X", mmfar, bfar);
    log_error(" PSP=0x%08X MSP=0x%08X", psp, msp);

    /* Infinite loop with LED blink */
    while (1) {
        led_toggle();
        for (volatile int i = 0; i < 100000; i++)
            ;
    }
}
