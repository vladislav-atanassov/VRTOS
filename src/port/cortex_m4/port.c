/*******************************************************************************
 * File: src/port/cortex_m4/port.c
 * Description: Cortex-M4 Porting Layer Implementation - FIXED
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS/config.h"
#include "VRTOS/rtos_assert.h"
#include "core/kernel_priv.h"
#include "log.h"
#include "rtos_port.h"
#include "task/task_priv.h"

// Include CMSIS for Cortex-M4
#include "stm32f4xx.h"

/**
 * @file port.c
 * @brief Cortex-M4 Specific RTOS Porting Layer - FIXED
 */

/* Defined in port_asm.s */
extern void PendSV_Handler(void);

void log_pendsv_entry(void) { log_info("PendSV triggered! PSP=0x%08X", __get_PSP()); }

/* Critical section nesting counter */
static volatile uint32_t g_critical_nesting = 0;
static volatile uint32_t g_critical_primask = 0;

/**
 * @brief Initialize the porting layer
 */
rtos_status_t rtos_port_init(void) {
    NVIC_SetPriority(PendSV_IRQn, 0xFF);  // Lowest priority
    NVIC_SetPriority(SysTick_IRQn, 0x00); // Higher than PendSV

    g_critical_nesting = 0;

    return RTOS_SUCCESS;
}

/**
 * @brief Start system tick timer
 */
void rtos_port_start_systick(void) {
    /* Calculate reload value for desired tick rate */
    uint32_t reload_value = (SystemCoreClock / RTOS_TICK_RATE_HZ) - 1;

    /* Use CMSIS SysTick functions for reliability */
    SysTick_Config(reload_value);

    /* Ensure SysTick has lowest priority */
    NVIC_SetPriority(SysTick_IRQn, 0xFF);
}

/**
 * @brief Initialize task stack
 */
uint32_t *rtos_port_init_task_stack(uint32_t *stack_top, rtos_task_function_t task_function, void *parameter) {
    /* Convert to byte address for alignment */
    uint32_t *stack_ptr = (uint32_t *)((uint32_t)stack_top & ~0x7);

    /* Initial exception frame (auto-saved by hardware) */
    *--stack_ptr = 0x01000000;              /* PSR (Thumb bit set) */
    *--stack_ptr = (uint32_t)task_function; /* PC */
    *--stack_ptr = 0xFFFFFFFD;              /* LR (EXC_RETURN: thread mode + PSP) */
    *--stack_ptr = 0;                       /* R12 */
    *--stack_ptr = 0;                       /* R3 */
    *--stack_ptr = 0;                       /* R2 */
    *--stack_ptr = 0;                       /* R1 */
    *--stack_ptr = (uint32_t)parameter;     /* R0 */

    /* Manual save registers (R4-R11) */
    *--stack_ptr = 0; /* R11 */
    *--stack_ptr = 0; /* R10 */
    *--stack_ptr = 0; /* R9 */
    *--stack_ptr = 0; /* R8 */
    *--stack_ptr = 0; /* R7 */
    *--stack_ptr = 0; /* R6 */
    *--stack_ptr = 0; /* R5 */
    *--stack_ptr = 0; /* R4 */

    log_info("Initialized SP: 0x%08X", (uint32_t)stack_ptr);

    return stack_ptr;
}

/**
 * @brief Enter critical section
 */
void rtos_port_enter_critical(void) {
    /* Disable interrupts and save current PRIMASK */
    __asm volatile("MRS %0, PRIMASK    \n"
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
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    /* Memory barrier to ensure write completes */
    __DSB();
    __ISB();
}

/**
 * @brief Start first task (never returns)
 */
void rtos_port_start_first_task(void) {
    /* Get next task's stack pointer */
    uint32_t psp_val = (uint32_t)g_kernel.next_task->stack_pointer;

    /* Critical: must be 8-byte aligned */
    psp_val = psp_val & ~0x7;
    log_info("Setting PSP to: 0x%08X", psp_val);

    __set_PSP(psp_val);

    /* Trigger SVC to start first task */
    __asm volatile("svc 0");

    log_info("Should never reach here");

    /* Should never reach here */
    while (1)
        ;
}

/**
 * @brief System tick interrupt handler
 */
void rtos_port_systick_handler(void) {
    /* Call kernel tick handler */
    rtos_kernel_tick_handler();
}

void SysTick_Handler(void) { rtos_port_systick_handler(); }

/**
 * @brief SVC interrupt handler (start first task)
 */
__attribute__((naked)) void SVC_Handler(void) {
    __asm volatile("MRS R0, PSP             \n" /* Get PSP */
                   "LDMIA R0!, {R4-R11}     \n" /* Restore R4-R11 */
                   "MSR PSP, R0             \n" /* Update PSP */

                   /* Set EXC_RETURN to thread mode with PSP */
                   "MOVW LR, #0xFFFD        \n" /* Use MOVW for 16-bit constant */
                   "MOVT LR, #0xFFFF        \n" /* Build 0xFFFFFFFD in two steps */

                   /* Return to thread mode */
                   "BX LR                   \n");
}
