#include "hardware_env.h"

#include "config.h" // IWYU pragma: keep
#include "klog.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep

/* LED Configuration for STM32F446RE Nucleo */
#define LED_PORT     GPIOA
#define LED_PIN      5 /* PA5 - User LED (LD2) */
#define LED_PIN_MASK (1U << LED_PIN)

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

uint32_t hardware_env_cpu_clock_hz(void)
{
    return SystemCoreClock;
}

__attribute__((__noreturn__)) void indicate_system_failure(void)
{
    const uint32_t    delay_cycles = hardware_env_cpu_clock_hz() / 50;
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

__attribute__((__noreturn__)) void Error_Handler(void)
{
    __disable_irq();
    indicate_system_failure();
}

/* Compile-time clock configuration calculations based on RTOS_SYSTEM_CLOCK_HZ */
#if RTOS_SYSTEM_CLOCK_HZ == 16000000U
    #define HW_USE_PLL 0
    #define HW_FLASH_LATENCY FLASH_LATENCY_0
    #define HW_APB1_DIV      RCC_HCLK_DIV1
    #define HW_APB2_DIV      RCC_HCLK_DIV1
#else
    #define HW_USE_PLL 1
    #define HW_PLLM    8 /* VCO input = 16MHz / 8 = 2MHz */

    #if RTOS_SYSTEM_CLOCK_HZ < 50000000U
        #define HW_PLLP RCC_PLLP_DIV4
        #define HW_PLLN ((RTOS_SYSTEM_CLOCK_HZ * 4) / 2000000U)
        #define HW_PLLP_VAL 4
    #else
        #define HW_PLLP RCC_PLLP_DIV2
        #define HW_PLLN ((RTOS_SYSTEM_CLOCK_HZ * 2) / 2000000U)
        #define HW_PLLP_VAL 2
    #endif

    #if RTOS_SYSTEM_CLOCK_HZ <= 30000000U
        #define HW_FLASH_LATENCY FLASH_LATENCY_0
    #elif RTOS_SYSTEM_CLOCK_HZ <= 60000000U
        #define HW_FLASH_LATENCY FLASH_LATENCY_1
    #elif RTOS_SYSTEM_CLOCK_HZ <= 90000000U
        #define HW_FLASH_LATENCY FLASH_LATENCY_2
    #elif RTOS_SYSTEM_CLOCK_HZ <= 120000000U
        #define HW_FLASH_LATENCY FLASH_LATENCY_3
    #elif RTOS_SYSTEM_CLOCK_HZ <= 150000000U
        #define HW_FLASH_LATENCY FLASH_LATENCY_4
    #else
        #define HW_FLASH_LATENCY FLASH_LATENCY_5
    #endif

    #if RTOS_SYSTEM_CLOCK_HZ > 90000000U
        #define HW_APB1_DIV RCC_HCLK_DIV4
        #define HW_APB2_DIV RCC_HCLK_DIV2
    #elif RTOS_SYSTEM_CLOCK_HZ > 45000000U
        #define HW_APB1_DIV RCC_HCLK_DIV2
        #define HW_APB2_DIV RCC_HCLK_DIV1
    #else
        #define HW_APB1_DIV RCC_HCLK_DIV1
        #define HW_APB2_DIV RCC_HCLK_DIV1
    #endif

    /* Validate that the requested frequency is exactly achievable */
    #if (((16000000U / HW_PLLM) * HW_PLLN) / HW_PLLP_VAL) != RTOS_SYSTEM_CLOCK_HZ
        #error "RTOS_SYSTEM_CLOCK_HZ cannot be accurately generated from 16MHz HSI with this simple algorithm."
    #endif
#endif

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();
    /* SCALE1 supports up to 180MHz */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* Initialize the RCC Oscillators */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

#if !HW_USE_PLL
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
#else
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = HW_PLLM;
    RCC_OscInitStruct.PLL.PLLN            = HW_PLLN;
    RCC_OscInitStruct.PLL.PLLP            = HW_PLLP;
    RCC_OscInitStruct.PLL.PLLQ            = 7;
    RCC_OscInitStruct.PLL.PLLR            = 2;
#endif

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* Initialize the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;

#if !HW_USE_PLL
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
#else
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
#endif

    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = HW_APB1_DIV;
    RCC_ClkInitStruct.APB2CLKDivider = HW_APB2_DIV;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, HW_FLASH_LATENCY) != HAL_OK)
    {
        Error_Handler();
    }

    /* Update SystemCoreClock variable to match new frequency */
    SystemCoreClockUpdate();
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

void hardware_env_config(void)
{
    SCB->VTOR = FLASH_BASE;
    SystemClock_Config();
    MX_GPIO_Init();
    __enable_irq();
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

    KLOGF("HwEnv", "HardFault pc=0x%08x psr=0x%08x", pc, psr);
    KLOGF("HwEnv", "HardFaultRegs r0=0x%08x r1=0x%08x", r0, r1);

    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t psp  = __get_PSP();
    uint32_t msp  = __get_MSP();

    KLOGF("HwEnv", "HardFaultSCB cfsr=0x%08x hfsr=0x%08x", cfsr, hfsr);
    KLOGF("HwEnv", "HardFaultSP psp=0x%08x msp=0x%08x", psp, msp);

    /* Suppress unused variable warnings for registers we logged via arg0/arg1 */
    (void) r2;
    (void) r3;
    (void) r12;
    (void) lr;

    indicate_system_failure();
}
