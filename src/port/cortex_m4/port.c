/*******************************************************************************
 * File: src/port/cortex_m4/port.c
 * Description: Cortex-M4 Porting Layer Implementation - FIXED
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "config.h"
#include "rtos_assert.h"
#include "kernel_priv.h"
#include "log.h"
#include "utils.h"
#include "rtos_port.h"
#include "task_priv.h"

// Include CMSIS for Cortex-M4
#include "stm32f4xx.h"

/**
 * @file port.c
 * @brief Cortex-M4 Specific RTOS Porting Layer
 */

/* Critical section nesting counter */
static volatile uint32_t g_critical_nesting = 0;
static volatile uint32_t g_critical_primask = 0;

/**
 * @brief Initialize the porting layer
 */
rtos_status_t rtos_port_init(void) {
    /* Set interrupt priorities */
    NVIC_SetPriority(PendSV_IRQn, 0xFF);  /* Lowest priority */
    NVIC_SetPriority(SysTick_IRQn, 0xE0); /* Higher priority */

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
    if (SysTick_Config(reload_value) != 0) {
        log_error("SysTick configuration failed!");
        return;
    }

    /* Ensure proper priority */
    NVIC_SetPriority(SysTick_IRQn, 0xE0);
}

/**
 * @brief Initialize task stack
 */
uint32_t *rtos_port_init_task_stack(uint32_t *stack_top, rtos_task_function_t task_function, void *parameter) {
    /* Convert to byte address for alignment */
    uint32_t *stack_ptr = (uint32_t *)ALIGN8_DOWN_VALUE((uint32_t)stack_top);

    /* Initial exception frame */
    *--stack_ptr = 0x01000000;                  /* xPSR (Thumb bit set) */
    *--stack_ptr = (uint32_t)task_function | 1; /* PC (task entry point) */
    *--stack_ptr = 0xFFFFFFFD;                  /* LR (EXC_RETURN to thread mode with PSP) */
    *--stack_ptr = 0;                           /* R12 */
    *--stack_ptr = 0;                           /* R3 */
    *--stack_ptr = 0;                           /* R2 */
    *--stack_ptr = 0;                           /* R1 */
    *--stack_ptr = (uint32_t)parameter;         /* R0 (task parameter) */

    /* Manual save registers (R4-R11) */
    *--stack_ptr = 0; /* R11 */
    *--stack_ptr = 0; /* R10 */
    *--stack_ptr = 0; /* R9 */
    *--stack_ptr = 0; /* R8 */
    *--stack_ptr = 0; /* R7 */
    *--stack_ptr = 0; /* R6 */
    *--stack_ptr = 0; /* R5 */
    *--stack_ptr = 0; /* R4 */

    return stack_ptr;
}

/**
 * @brief Enter critical section
 */
void rtos_port_enter_critical(void) {
    /* Disable interrupts and save current PRIMASK */
    uint32_t primask;

    __asm volatile("MRS %0, PRIMASK    \n"
                   "CPSID I            \n"
                   : "=r"(primask)
                   :
                   : "memory");

    g_critical_nesting++;

    if (g_critical_nesting == 1) {
        g_critical_primask = primask;
    }
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
 * @brief Start first task
 */
__attribute__((__noreturn__))
void rtos_port_start_first_task(void) {
    /* Ensure 8-byte stack alignment */
    uint32_t psp_val = ALIGN8_DOWN_VALUE((uint32_t)g_kernel.next_task->stack_pointer);

    /* Set PSP to point to saved registers (R4-R11) */
    __set_PSP(psp_val);
    __DSB();
    __ISB();

    /* Trigger SVC to start first task */
    __asm volatile("svc 0");

    log_error("Should never reach here");

    /* Should never reach here */
    while (1) {
    }
}

/**
 * @brief System tick interrupt handler
 */
void SysTick_Handler(void) { rtos_port_systick_handler(); }

void rtos_port_systick_handler(void) { rtos_kernel_tick_handler(); }

void log_svc_handler(void) { log_debug("Triggered SVC handler, PSP=%08X", __get_PSP()); }

/**
 * @brief SVC interrupt handler
 * 
 * This function should start the first task
 */
__attribute__((naked))
void SVC_Handler(void) {
    __asm volatile(
        "BL log_svc_handler      \n" /* Debug log */
        "MRS R0, PSP             \n" /* Get PSP */
        "LDMIA R0!, {R4-R11}     \n" /* Restore R4-R11 from stack */
        "MSR PSP, R0             \n" /* Update PSP to point after registers */
        "ISB                     \n" /* Instruction barrier */
        "LDR LR, =0xFFFFFFFD     \n" /* EXC_RETURN: thread mode + PSP */
        "BX LR                   \n" /* Return to thread mode */
        ::: "memory");
}

void log_pendsv_handler(void) { log_debug("Triggered PendSV handler, PSP=%08X", __get_PSP()); }

/**
 * @brief PendSV interrupt handler
 *
 * This function handles context switching.
 */
__attribute__((naked))
void PendSV_Handler(void) {
    __asm volatile(
        "BL log_pendsv_handler              \n" /* Debug log */
        "MRS     R0, PSP                    \n" /* Get current PSP */
        "CBZ     R0, pendsv_nosave          \n" /* Skip save if first run */

        "STMDB   R0!, {R4-R11}              \n" /* Save current context - R4-R11 */
        "LDR     R1, =g_kernel              \n" /* Save stack pointer to TCB */
        "LDR     R2, [R1, #0]               \n" /* current_task */
        "STR     R0, [R2]                   \n" /* stack_pointer */

        "pendsv_nosave:                     \n"
        "PUSH    {R3, LR}                   \n" /* Preserve LR (EXC_RETURN) */
        "BL      rtos_kernel_switch_context \n" /* Call scheduler */
        "POP     {R3, LR}                   \n" /* Restore LR */

        "LDR     R1, =g_kernel              \n" /* Load next task context */
        "LDR     R2, [R1, #0]               \n" /* current_task */
        "LDR     R0, [R2]                   \n" /* stack_pointer */

        "LDMIA   R0!, {R4-R11}              \n" /* Restore R4-R11 */

        "MSR     PSP, R0                    \n" /* Update PSP */
        "MOV     LR, #0xFFFFFFFD            \n" /* Set EXC_RETURN value, Thread mode + PSP */

        "BX      LR                         \n" /* Return to thread mode */
        ::: "r0", "r1", "r2", "memory");
}
