/*******************************************************************************
 * File: port/cortex_m4/port_asm.s
 * Description: Assembly Context Switch Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

/**
 * @file port_asm.s
 * @brief Assembly routines for context switching
 * 
 * This file contains the low-level assembly implementation for context
 * switching on Cortex-M4.
 */

.syntax unified
.cpu cortex-m4
.thumb
.text

.extern g_kernel
.extern rtos_kernel_switch_context

/**
 * @brief PendSV interrupt handler for context switching
 */
.global rtos_port_pendsv_handler
.type rtos_port_pendsv_handler, %function
rtos_port_pendsv_handler:
    MRS     R0, PSP                     /* Get current PSP */
    CBZ     R0, pendsv_handler_nosave   /* Skip save if PSP is 0 (first run) */
    
    /* Save context of current task */
    STMDB   R0!, {R4-R11}               /* Push R4-R11 onto PSP stack */
    
    /* Save stack pointer in current task's TCB */
    LDR     R1, =g_kernel               /* Load kernel control block address */
    LDR     R2, [R1, #8]                /* Load current_task pointer (offset 8) */
    STR     R0, [R2, #20]               /* Save PSP in stack_pointer (offset 20) */

pendsv_handler_nosave:
    /* Call scheduler to find next task */
    BL      rtos_kernel_switch_context
    
    /* Load context of next task */
    LDR     R0, =g_kernel               /* Load kernel control block address */
    LDR     R1, [R0, #8]                /* Load current_task pointer */
    LDR     R0, [R1, #20]               /* Load stack_pointer from TCB */
    
    LDMIA   R0!, {R4-R11}               /* Pop R4-R11 from task's stack */
    MSR     PSP, R0                     /* Set PSP to new stack pointer */
    
    ORR     LR, LR, #0x04               /* Ensure exception return uses PSP */
    BX      LR                          /* Return to task */

.size rtos_port_pendsv_handler, . - rtos_port_pendsv_handler

.end