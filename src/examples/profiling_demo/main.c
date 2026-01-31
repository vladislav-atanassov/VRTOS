/*******************************************************************************
 * File: examples/queue_demo/main.c
 * Description: Simple Producer-Consumer Queue Demonstration
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "log.h"
#include "profiling.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"

/* =================== Hardware Configuration =================== */

/* LED Configuration for STM32F446RE Nucleo */
#define LED_PORT     GPIOA
#define LED_PIN      5 /* PA5 - User LED (LD2) */
#define LED_PIN_MASK (1U << LED_PIN)

/* HAL handles */
void        SystemClock_Config(void);
static void MX_GPIO_Init(void);

/* =================== LED Control =================== */

/**
 * @brief Toggle LED state
 */
static void led_toggle(void)
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

/* =================== System Failure Handler =================== */

/**
 * @brief Indicate system failure by blinking LED rapidly
 */
__attribute__((__noreturn__)) static void indicate_system_failure(void)
{
    const uint32_t    delay_cycles = SystemCoreClock / 50;
    volatile uint32_t counter;

    while (1)
    {
        led_toggle();
        for (counter = 0; counter < delay_cycles; counter++)
        {
            __NOP();
        }
    }
}

/* =================== HAL Configuration =================== */

__attribute__((__noreturn__)) void Error_Handler(void)
{
    __disable_irq();
    indicate_system_failure();
}

void SystemClock_Config(void)
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

    log_error("HardFault: PC=0x%08lX PSR=0x%08lX", (unsigned long) pc, (unsigned long) psr);
    log_error("R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX", (unsigned long) r0, (unsigned long) r1, (unsigned long) r2,
              (unsigned long) r3);
    log_error("R12=0x%08lX LR=0x%08lX", (unsigned long) r12, (unsigned long) lr);

    uint32_t cfsr  = SCB->CFSR;
    uint32_t hfsr  = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar  = SCB->BFAR;
    uint32_t psp   = __get_PSP();
    uint32_t msp   = __get_MSP();

    log_error("CFSR=0x%08lX HFSR=0x%08lX", (unsigned long) cfsr, (unsigned long) hfsr);
    log_error("MMFAR=0x%08lX BFAR=0x%08lX", (unsigned long) mmfar, (unsigned long) bfar);
    log_error("PSP=0x%08lX MSP=0x%08lX", (unsigned long) psp, (unsigned long) msp);

    indicate_system_failure();
}

/* =================== Tasks =================== */

rtos_profile_stat_t prof_work = {UINT32_MAX, 0, 0, 0, "WorkBlock"};

/* Task that does some simulated work */
void WorkTask(void *param)
{
    volatile int i;
    while (1)
    {
        RTOS_USER_PROFILE_START(work);

        /* Simulate work: loop */
        for (i = 0; i < 10000; i++)
        {
            __asm volatile("nop");
        }

        led_toggle(); /* Blink to show activity */

        RTOS_USER_PROFILE_END(work, &prof_work);

        rtos_delay_ms(100);
    }
}

/* Task that prints reports */
void ReportTask(void *param)
{
    while (1)
    {
        rtos_delay_ms(5000); /* Report every 5s */

        log_info("============ PROFILING REPORT ============");

        /* User/Application profiling */
        log_info("--- User Application Stats ---");
        rtos_profiling_print_stat(&prof_work);

        /* RTOS System profiling (only if enabled) */
        rtos_profiling_report_system_stats();

        log_info("==========================================");
    }
}

int main(void)
{
    /* Hardware Initialization */
    SCB->VTOR = FLASH_BASE;
    SystemClock_Config();
    MX_GPIO_Init();
    __enable_irq();

    /* Board Drivers */
    log_uart_init(LOG_LEVEL_INFO);

    rtos_init();
    rtos_profiling_init();

    log_info("Starting Profiling Demo...");

    rtos_task_handle_t task_handle;

    rtos_task_create(WorkTask, "WORKER", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 1, &task_handle);
    rtos_task_create(ReportTask, "REPORTER", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, 2, &task_handle);

    rtos_start_scheduler();

    /* Should not reach here */
    while (1)
    {
    }
    return 0;
}
