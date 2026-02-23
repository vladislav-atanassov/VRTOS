/*******************************************************************************
 * File: src/port/cortex_m4/port.c
 * Description: Cortex-M4F Porting Layer Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "config.h"
#include "kernel_priv.h"
#include "log.h"
#include "port_priv.h" /* chip-specific constants (must come first) */
#define PORT_VERIFY_CONTRACT
#include "port_common.h" /* contract checks + common types */
#include "rtos_port.h"
#include "task_priv.h"
#include "utils.h"

/* Include CMSIS for Cortex-M4 */
#include "stm32f4xx.h" // IWYU pragma: keep

/**
 * @file port.c
 * @brief Cortex-M4 Specific RTOS Porting Layer
 */

/* Critical section nesting counter */
static volatile uint32_t g_critical_nesting = 0;
static volatile uint32_t g_critical_basepri = 0;

/**
 * @brief Initialize the porting layer
 */
rtos_status_t rtos_port_init(void)
{
#if PORT_HAS_FPU
    /**
     * Enable lazy FPU context stacking.
     * ASPEN: Automatic State Preservation ENable — hardware reserves FPU
     *        stack space on exception entry when FPU was in use.
     * LSPEN: Lazy State Preservation ENable — defer the actual save of
     *        S0-S15/FPSCR until the ISR first touches the FPU.
     */
    FPU->FPCCR |= FPU_FPCCR_ASPEN_Msk | FPU_FPCCR_LSPEN_Msk;
#endif

    /* Configure interrupt priorities */
    NVIC_SetPriority(PendSV_IRQn, PORT_IRQ_PRIORITY_PENDSV >> 4);  /* Lowest */
    NVIC_SetPriority(SysTick_IRQn, PORT_IRQ_PRIORITY_KERNEL >> 4); /* Kernel level */

    /* Initialize BASEPRI to 0 (no masking) */
    __set_BASEPRI(0);

    g_critical_nesting = 0;
    g_critical_basepri = 0;

    log_info("Interrupt priorities configured:");
    log_info("  Critical:  0x%02X (never masked)", PORT_IRQ_PRIORITY_CRITICAL);
    log_info("  High:      0x%02X (preempts RTOS)", PORT_IRQ_PRIORITY_HIGH);
    log_info("  Kernel:    0x%02X (SysTick)", PORT_IRQ_PRIORITY_KERNEL);
    log_info("  PendSV:    0x%02X (context switch)", PORT_IRQ_PRIORITY_PENDSV);

    return RTOS_SUCCESS;
}

/**
 * @brief Start system tick timer
 */
void rtos_port_start_systick(void)
{
    /* Calculate reload value for desired tick rate */
    uint32_t reload_value = (SystemCoreClock / RTOS_TICK_RATE_HZ) - 1;

    /* Use CMSIS SysTick functions for reliability */
    if (SysTick_Config(reload_value) != 0)
    {
        log_error("SysTick configuration failed!");
        return;
    }

    /* Ensure proper priority */
    NVIC_SetPriority(SysTick_IRQn, 0xE0);
}

/**
 * @brief Initialize task stack
 */
uint32_t *rtos_port_init_task_stack(uint32_t *stack_top, rtos_task_function_t task_function, void *parameter)
{
    /* Convert to byte address for alignment */
    uint32_t *stack_ptr = (uint32_t *) ALIGN_DOWN((uint32_t) stack_top, PORT_STACK_ALIGNMENT);

    /* Initial exception frame */
    *--stack_ptr = PORT_INITIAL_XPSR;            /* xPSR (Thumb bit set) */
    *--stack_ptr = (uint32_t) task_function | 1; /* PC (task entry point) */
    *--stack_ptr = PORT_INITIAL_EXC_RETURN;      /* LR (EXC_RETURN to thread mode with PSP) */
    *--stack_ptr = 0;                            /* R12 */
    *--stack_ptr = 0;                            /* R3 */
    *--stack_ptr = 0;                            /* R2 */
    *--stack_ptr = 0;                            /* R1 */
    *--stack_ptr = (uint32_t) parameter;         /* R0 (task parameter) */

    /**
     * EXC_RETURN value — saved/restored per-task so each task carries its
     * own FPU-usage indication in bit 4.  Initial value = 0xFFFFFFFD:
     * thread mode, PSP, no FPU frame.
     */
    *--stack_ptr = PORT_INITIAL_EXC_RETURN;

    /* Manually-saved core registers (R4-R11) */
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

void rtos_port_enter_critical(void)
{
    /* Read current BASEPRI, set to kernel priority threshold */
    uint32_t basepri;

    __asm volatile("MRS %0, BASEPRI                    \n" /* Read current BASEPRI */
                   "MOV R1, %1                         \n" /* Load kernel priority */
                   "MSR BASEPRI, R1                    \n" /* Set BASEPRI threshold */
                   "DSB                                \n" /* Data sync barrier */
                   "ISB                                \n" /* Instruction sync barrier */
                   : "=r"(basepri)
                   : "i"(PORT_MAX_INTERRUPT_PRIORITY)
                   : "r1", "memory");

    g_critical_nesting++;

    if (g_critical_nesting == 1)
    {
        /* First entry - save original BASEPRI */
        g_critical_basepri = basepri;
    }
}

void rtos_port_exit_critical(void)
{
    if (g_critical_nesting > 0)
    {
        g_critical_nesting--;

        if (g_critical_nesting == 0)
        {
            /* Last exit - restore original BASEPRI */
            __asm volatile("MSR BASEPRI, %0            \n"
                           "DSB                        \n"
                           "ISB                        \n"
                           :
                           : "r"(g_critical_basepri)
                           : "memory");
        }
    }
}

uint32_t rtos_port_enter_critical_from_isr(void)
{
    uint32_t saved_basepri = __get_BASEPRI();
    __set_BASEPRI(PORT_MAX_INTERRUPT_PRIORITY);
    __DSB();
    __ISB();
    return saved_basepri;
}

void rtos_port_exit_critical_from_isr(uint32_t saved_priority)
{
    __set_BASEPRI(saved_priority);
    __DSB();
    __ISB();
}

/**
 * @brief Force context switch
 */
void rtos_port_yield(void)
{
    /* Trigger PendSV interrupt for context switch */
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    /* Memory barrier to ensure write completes */
    __DSB();
    __ISB();
}

/**
 * @brief Start first task
 */
__attribute__((__noreturn__)) void rtos_port_start_first_task(void)
{
    /* Ensure 8-byte stack alignment */
    uint32_t psp_val = ALIGN_DOWN((uint32_t) g_kernel.next_task->stack_pointer, PORT_STACK_ALIGNMENT);

    /* Set PSP to point to saved registers (R4-R11, R14) */
    __set_PSP(psp_val);
    __DSB();
    __ISB();

#if PORT_HAS_FPU
    /**
     * Clear the FPCA bit in CONTROL register to prevent the SVC exception
     * frame from including stale FPU state that may have been used before
     * the scheduler was started.
     */
    __asm volatile("MOV R0, #0       \n"
                   "MSR CONTROL, R0  \n"
                   "ISB              \n");
#endif

    /* Trigger SVC to start first task */
    __asm volatile("svc 0");

    log_error("Should never reach here");

    /* Should never reach here */
    while (1)
    {
    }
}

/**
 * @brief System tick interrupt handler
 */
void SysTick_Handler(void)
{
    rtos_port_systick_handler();
}

void rtos_port_systick_handler(void)
{
    rtos_kernel_tick_handler();
}

/**
 * @brief SVC interrupt handler
 *
 * This function should start the first task
 */
__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile("LDR  R3, =g_kernel       \n" /* Get current TCB address */
                   "LDR  R1, [R3]            \n"
                   "LDR  R0, [R1]            \n" /* First item = stack pointer */
                   "LDMIA R0!, {R4-R11, R14} \n" /* Restore R4-R11 + EXC_RETURN */
                   "MSR  PSP, R0             \n" /* Update PSP past restored regs */
                   "ISB                      \n"
                   "MOV  R0, #0              \n" /* Unmask interrupts (BASEPRI = 0) */
                   "MSR  BASEPRI, R0         \n"
                   "BX   R14                 \n" /* Return to thread mode */
                   ::
                       : "memory");
}

/**
 * @brief PendSV interrupt handler
 *
 * This function handles context switching.
 */
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile("MRS     R0, PSP                    \n" /* Get current PSP */
                   "ISB                                \n"

                   /* Save current task context */
                   "LDR     R3, =g_kernel              \n"
                   "LDR     R2, [R3]                   \n" /* R2 = current_task TCB */

#if PORT_HAS_FPU
                   /* Conditionally save S16-S31 (callee-saved VFP regs) */
                   "TST     R14, #0x10                 \n" /* Bit 4: 0 = FPU frame */
                   "BNE     1f                         \n"
                   "VSTMDB  R0!, {S16-S31}             \n"
                   "1:                                 \n"
#endif

                   /* Save core registers + EXC_RETURN */
                   "STMDB   R0!, {R4-R11, R14}         \n"

                   /* Store updated SP in current TCB */
                   "STR     R0, [R2]                   \n"

                   /* Call scheduler under BASEPRI protection */
                   "MOV     R0, %0                     \n"
                   "MSR     BASEPRI, R0                \n"
                   "DSB                                \n"
                   "ISB                                \n"
                   "BL      rtos_kernel_switch_context \n"
                   "MOV     R0, #0                     \n"
                   "MSR     BASEPRI, R0                \n"

                   /* Restore next task context */
                   "LDR     R3, =g_kernel              \n"
                   "LDR     R2, [R3]                   \n" /* R2 = (new) current_task */
                   "LDR     R0, [R2]                   \n" /* R0 = stack_pointer */

                   /* Restore core registers + EXC_RETURN */
                   "LDMIA   R0!, {R4-R11, R14}         \n"

#if PORT_HAS_FPU
                   /* Conditionally restore S16-S31 */
                   "TST     R14, #0x10                 \n"
                   "BNE     2f                         \n"
                   "VLDMIA  R0!, {S16-S31}             \n"
                   "2:                                 \n"
#endif

                   "MSR     PSP, R0                    \n"
                   "ISB                                \n"
                   "BX      R14                        \n" /* Per-task EXC_RETURN */
                   ::"i"(PORT_MAX_INTERRUPT_PRIORITY)
                   : "memory");
}
