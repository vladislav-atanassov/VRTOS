/*******************************************************************************
 * File: port/cortex_m4/port.c
 * Description: Cortex-M4 Porting Layer Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "../include/VRTOS/config.h"
#include "../src/core/kernel_priv.h"
#include "rtos_port.h"
#include "stm32f446xx.h"

/**
 * @file port.c
 * @brief Cortex-M4 Specific RTOS Porting Layer
 *
 * This file contains the hardware-specific implementation for STM32F446RE
 * (Cortex-M4) microcontroller.
 */

/* Cortex-M4 System Control Block registers */
#define NVIC_SYSTICK_CTRL_REG      (*((volatile uint32_t *)0xE000E010))
#define NVIC_SYSTICK_LOAD_REG      (*((volatile uint32_t *)0xE000E014))
#define NVIC_SYSTICK_VAL_REG       (*((volatile uint32_t *)0xE000E018))
#define NVIC_INT_CTRL_REG          (*((volatile uint32_t *)0xE000ED04))
#define NVIC_SYSPRI3_REG           (*((volatile uint32_t *)0xE000ED20))

/* SysTick Control Register bits */
#define NVIC_SYSTICK_ENABLE_BIT    (1UL << 0)
#define NVIC_SYSTICK_TICKINT_BIT   (1UL << 1)
#define NVIC_SYSTICK_CLKSOURCE_BIT (1UL << 2)

/* Interrupt Control Register bits */
#define NVIC_PENDSVSET_BIT         (1UL << 28)

/* System Priority Register values */
#define NVIC_PENDSV_PRI            (0xFFUL << 16) /* Lowest priority */
#define NVIC_SYSTICK_PRI           (0xFFUL << 24) /* Lowest priority */

/* Stack frame structure for Cortex-M4 */
typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
} cortex_m4_stack_frame_t;

/* Critical section nesting counter */
static volatile uint32_t g_critical_nesting = 0;
static volatile uint32_t g_critical_primask = 0;

/**
 * @brief Initialize the porting layer
 */
rtos_status_t rtos_port_init(void) {
    /* Set PendSV and SysTick to lowest priority */
    NVIC_SYSPRI3_REG |= NVIC_PENDSV_PRI;
    NVIC_SYSPRI3_REG |= NVIC_SYSTICK_PRI;

    g_critical_nesting = 0;

    return RTOS_SUCCESS;
}

/**
 * @brief Start system tick timer
 */
void rtos_port_start_systick(void) {
    /* Calculate reload value for desired tick rate */
    uint32_t reload_value = (RTOS_SYSTEM_CLOCK_HZ / RTOS_TICK_RATE_HZ) - 1;

    /* Configure SysTick */
    NVIC_SYSTICK_VAL_REG = 0;                            /* Clear current value */
    NVIC_SYSTICK_LOAD_REG = reload_value;                /* Set reload value */
    NVIC_SYSTICK_CTRL_REG = NVIC_SYSTICK_CLKSOURCE_BIT | /* Use processor clock */
                            NVIC_SYSTICK_TICKINT_BIT |   /* Enable interrupt */
                            NVIC_SYSTICK_ENABLE_BIT;     /* Enable SysTick */
}

/**
 * @brief Initialize task stack
 */
uint32_t *rtos_port_init_task_stack(uint32_t            *stack_top,
                                    rtos_task_function_t task_function,
                                    void                *parameter) {
    /* Stack pointer starts at top and grows down */
    uint32_t *stack_ptr = stack_top - 1;

    /* Initialize stack frame for initial context switch */
    /* These values will be popped by the initial context switch */

    /* Cortex-M4 automatically saves these registers on exception entry */
    *stack_ptr-- = 0x01000000UL;            /* PSR (Thumb bit set) */
    *stack_ptr-- = (uint32_t)task_function; /* PC (task function) */
    *stack_ptr-- = 0x00000000UL;            /* LR */
    *stack_ptr-- = 0x12121212UL;            /* R12 */
    *stack_ptr-- = 0x03030303UL;            /* R3 */
    *stack_ptr-- = 0x02020202UL;            /* R2 */
    *stack_ptr-- = 0x01010101UL;            /* R1 */
    *stack_ptr-- = (uint32_t)parameter;     /* R0 (task parameter) */

    /* Cortex-M4 manual save/restore registers */
    *stack_ptr-- = 0x11111111UL; /* R11 */
    *stack_ptr-- = 0x10101010UL; /* R10 */
    *stack_ptr-- = 0x09090909UL; /* R9 */
    *stack_ptr-- = 0x08080808UL; /* R8 */
    *stack_ptr-- = 0x07070707UL; /* R7 */
    *stack_ptr-- = 0x06060606UL; /* R6 */
    *stack_ptr-- = 0x05050505UL; /* R5 */
    *stack_ptr-- = 0x04040404UL; /* R4 */

    return stack_ptr;
}

/**
 * @brief Enter critical section
 */
void rtos_port_enter_critical(void) {
    /* Disable interrupts and save current PRIMASK */
    __asm volatile(
        "MRS %0, PRIMASK    \n"
        "CPSID I            \n"
        : "=r"(g_critical_primask)
        :
        : "memory");

    g_critical_nesting++;
}

/**
 * @brief Exit critical section
 */
void rtos_port_exit_critical(void) {
    if (g_critical_nesting > 0) {
        g_critical_nesting--;

        if (g_critical_nesting == 0) {
            /* Restore PRIMASK */
            __asm volatile("MSR PRIMASK, %0    \n" : : "r"(g_critical_primask) : "memory");
        }
    }
}

/**
 * @brief Force context switch
 */
void rtos_port_yield(void) {
    /* Trigger PendSV interrupt for context switch */
    NVIC_INT_CTRL_REG = NVIC_PENDSVSET_BIT;

    /* Memory barrier to ensure write completes */
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

/**
 * @brief Start first task (never returns)
 */
void rtos_port_start_first_task(void) {
    /* Set PSP to 0 to indicate we're starting fresh */
    __asm volatile(
        "MOV R0, #0         \n"
        "MSR PSP, R0        \n"
        "BL rtos_kernel_switch_context \n"
        "SVC 0              \n" /* Trigger SVC to start first task */
    );

    /* Should never reach here */
    while (1)
        ;
}

/**
 * @brief SysTick interrupt handler
 */
void SysTick_Handler(void) { rtos_port_systick_handler(); }

/**
 * @brief System tick interrupt handler
 */
void rtos_port_systick_handler(void) {
    /* Call kernel tick handler */
    rtos_kernel_tick_handler();
}

/**
 * @brief PendSV interrupt handler (context switching)
 */
void PendSV_Handler(void) { rtos_port_pendsv_handler(); }

/**
 * @brief SVC interrupt handler (start first task)
 */
void SVC_Handler(void) {
    /* This is called when starting the first task */
    __asm volatile(
        "LDR R0, =g_kernel          \n" /* Load kernel control block address */
        "LDR R1, [R0, #8]           \n" /* Load current_task (offset 8) */
        "LDR R0, [R1, #20]          \n" /* Load stack_pointer (offset 20) */
        "LDMIA R0!, {R4-R11}        \n" /* Pop R4-R11 */
        "MSR PSP, R0                \n" /* Set PSP to stack pointer */
        "ORR LR, LR, #0x04          \n" /* Ensure we return to task using PSP */
        "BX LR                      \n" /* Return to task */
    );
}
