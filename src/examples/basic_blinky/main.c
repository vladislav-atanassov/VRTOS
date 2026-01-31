/*******************************************************************************
 * File: examples/basic_blinky/main.c
 * Description: Basic LED Blinky Example
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "log.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"
#include "task_priv.h"

/**
 * @file main.c
 * @brief Basic LED Blinky RTOS Example
 *
 * This example demonstrates the basic functionality of the RTOS by creating
 * a simple task that blinks the onboard LED using direct register access.
 */

/* LED Configuration for STM32F446RE Nucleo */
#define LED_PORT     GPIOA
#define LED_PIN      5 /* PA5 - User LED (LD2) */
#define LED_PIN_MASK (1U << LED_PIN)

/* Task priorities */
#define BLINK_TASK_PRIORITY (2U)
#define PRINT_TASK_PRIORITY (3U)

/* Blink & Print timing */
#define LED_BLINK_DELAY_MS (1000U)
#define PRINT_DELAY_MS     (300U)

void        SystemClock_Config(void);
static void MX_GPIO_Init(void);

/**
 * @brief Toggle LED state using direct register access
 */
static void led_toggle(void)
{
    if (LED_PORT->ODR & LED_PIN_MASK)
    {
        /* LED is on, turn it off */
        LED_PORT->BSRR = LED_PIN_MASK << 16; /* Reset bit */
    }
    else
    {
        /* LED is off, turn it on */
        LED_PORT->BSRR = LED_PIN_MASK; /* Set bit */
    }
}

/**
 * @brief Indicate system failure by blinking the onboard LED.
 *
 * This function never returns.
 * It leaves interrupts enabled so that other fault handlers or logging
 * can still work, unless explicitly disabled beforehand.
 *
 * Works in both pre-RTOS and RTOS contexts.
 */
__attribute__((__noreturn__)) static void indicate_system_failure(void)
{
    const uint32_t    delay_cycles = SystemCoreClock / 50;
    volatile uint32_t counter;

    while (1)
    {
        led_toggle();

        /* crude software delay — does not block interrupts */
        for (counter = 0; counter < delay_cycles; counter++)
        {
            __NOP();
        }
    }
}

/**
 * @brief LED blink task
 *
 * @param param Task parameter (unused)
 */
static void blink_task(void *param)
{
    (void) param; /* Suppress unused parameter warning */
    log_debug("IN blink_task()");
    /* Task main loop */
    while (1)
    {
        /* Toggle LED */
        led_toggle();
        log_print("BLINK");
        /* Delay for specified time */
        rtos_delay_ms(LED_BLINK_DELAY_MS);
    }
}

/**
 * @brief Print task
 *
 * @param param Task parameter (unused)
 */
static void print_task(void *param)
{
    (void) param; /* Suppress unused parameter warning */
    log_debug("IN print_task()");
    /* Task main loop */
    while (1)
    {
        log_print("PRINT");
        /* Delay for specified time */
        rtos_delay_ms(PRINT_DELAY_MS);
    }
}

/**
 * @brief Print task
 *
 * @param param Task parameter (unused)
 */
static void memory_mang_task(void *param)
{
    (void) param; /* Suppress unused parameter warning */
    log_debug("IN memory_mang_task()");
    /* Task main loop */
    while (1)
    {
        rtos_task_debug_print_all();
        rtos_delay_ms(1500);
    }
}

/**
 * @brief Main function
 *
 * @return Should never return
 */
__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t      status;
    rtos_task_handle_t blink_task_handle;
    rtos_task_handle_t print_task_handle;
    rtos_task_handle_t memory_mang_task_handle;

    /* System initializations */
    SCB->VTOR = FLASH_BASE; /* Set vector table location */
    SystemClock_Config();
    MX_GPIO_Init();
    __enable_irq(); /* Enable global interrupts */

    log_uart_init(LOG_LEVEL_INFO);

    /* Initialize RTOS */
    status = rtos_init();

    if (status != RTOS_SUCCESS)
    {
        /* Initialization failed - indicate with LED */
        indicate_system_failure();
    }

    /* Create mem task */
    status = rtos_task_create(memory_mang_task, "MEM", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &memory_mang_task_handle);

    if (status != RTOS_SUCCESS)
    {
        /* Task creation failed */
        indicate_system_failure();
    }

    /* Create blink task */
    status = rtos_task_create(blink_task, "BLINK", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, BLINK_TASK_PRIORITY,
                              &blink_task_handle);

    if (status != RTOS_SUCCESS)
    {
        /* Task creation failed */
        indicate_system_failure();
    }

    /* Create print task */
    status = rtos_task_create(print_task, "PRINT", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, PRINT_TASK_PRIORITY,
                              &print_task_handle);

    if (status != RTOS_SUCCESS)
    {
        /* Task creation failed */
        indicate_system_failure();
    }

    /* Start the RTOS scheduler */
    status = rtos_start_scheduler();

    /* Should never reach here */
    while (1)
    {
    }
}

__attribute__((__noreturn__)) void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    /** 
     * Initializes the RCC Oscillators according to the specified parameters in
     * the RCC_OscInitTypeDef structure. 
     */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = GPIO_PIN_5;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("TST LR, #4              \n"
                   "ITE EQ                  \n"
                   "MRSEQ R0, MSP           \n"
                   "MRSNE R0, PSP           \n"
                   "MOV R1, R0              \n"
                   "B HardFault_Handler_C");
}

__attribute__((__noreturn__)) void HardFault_Handler_C(uint32_t *stack_frame)
{
    uint32_t r0  = stack_frame[0];
    uint32_t r1  = stack_frame[1];
    uint32_t r2  = stack_frame[2];
    uint32_t r3  = stack_frame[3];
    uint32_t r12 = stack_frame[4];
    uint32_t lr  = stack_frame[5];
    uint32_t pc  = stack_frame[6];
    uint32_t psr = stack_frame[7];

    log_error("HardFault: PC=0x%08lX PSR=0x%08lX", pc, psr);
    log_error("R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX", r0, r1, r2, r3);
    log_error("R12=0x%08lX LR=0x%08lX", r12, lr);

    uint32_t cfsr  = SCB->CFSR;
    uint32_t hfsr  = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar  = SCB->BFAR;
    uint32_t psp   = __get_PSP();
    uint32_t msp   = __get_MSP();

    /* Direct register access for debugging */
    log_error("HardFault Registers:");
    log_error("CFSR=0x%08lX HFSR=0x%08lX", cfsr, hfsr);
    log_error("MMFAR=0x%08lX BFAR=0x%08lX", mmfar, bfar);
    log_error("PSP=0x%08lX MSP=0x%08lX", psp, msp);

    indicate_system_failure();
}
