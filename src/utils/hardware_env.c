/*******************************************************************************
 * File: src/utils/hardware_env.c
 * Description: Hardware Environment - Shared hardware initialization for tests
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "hardware_env.h"

#include "klog.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep

/* LED Configuration for STM32F446RE Nucleo */
#define LED_PORT     GPIOA
#define LED_PIN      5 /* PA5 - User LED (LD2) */
#define LED_PIN_MASK (1U << LED_PIN)

/* =================== LED Control =================== */

void led_toggle(void)
{
    if (LED_PORT->ODR & LED_PIN_MASK)
    {
        LED_PORT->BSRR = LED_PIN_MASK << 16; /* Reset bit */
    }
    else
    {
        LED_PORT->BSRR = LED_PIN_MASK; /* Set bit */
    }
}

void led_set(bool on)
{
    if (on)
    {
        LED_PORT->BSRR = LED_PIN_MASK; /* Set bit */
    }
    else
    {
        LED_PORT->BSRR = LED_PIN_MASK << 16; /* Reset bit */
    }
}

/* =================== System Failure =================== */

__attribute__((__noreturn__)) void indicate_system_failure(void)
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

/* =================== HAL Error Handler =================== */

__attribute__((__noreturn__)) void Error_Handler(void)
{
    __disable_irq();
    indicate_system_failure();
}

/* =================== Clock Configuration =================== */

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    /* Initialize the RCC Oscillators */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* Initialize the CPU, AHB and APB buses clocks */
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

/* =================== GPIO Configuration =================== */

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

/* =================== Test Environment Init =================== */

void hardware_env_config(void)
{
    /* Set vector table location */
    SCB->VTOR = FLASH_BASE;

    /* Configure system clock */
    SystemClock_Config();

    /* Initialize GPIO */
    MX_GPIO_Init();

    /* Enable global interrupts */
    __enable_irq();
}

/* =================== Fault Handlers =================== */

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

    KLOGF(KEVT_HARD_FAULT, pc, psr);
    KLOGF(KEVT_HARD_FAULT_REGS, r0, r1);

    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t psp  = __get_PSP();
    uint32_t msp  = __get_MSP();

    KLOGF(KEVT_HARD_FAULT_SCB, cfsr, hfsr);
    KLOGF(KEVT_HARD_FAULT_SP, psp, msp);

    /* Suppress unused variable warnings for registers we logged via arg0/arg1 */
    (void) r2;
    (void) r3;
    (void) r12;
    (void) lr;

    indicate_system_failure();
}
