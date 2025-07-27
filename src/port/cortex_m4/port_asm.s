.syntax unified
.cpu cortex-m4
.thumb
.text

.extern g_kernel
.extern rtos_kernel_switch_context

.global PendSV_Handler
.type PendSV_Handler, %function
PendSV_Handler:
    PUSH {R0, LR}
    BL log_pendsv_entry             /* Implemented in port.c */
    POP {R0, LR}
    
    MRS     R0, PSP                 /* Get current PSP */
    CBZ     R0, pendsv_nosave       /* Skip save if first run */

    /* Save current context */
    STMDB   R0!, {R4-R11}           /* Push R4-R11 onto stack */

    /* Save stack pointer to current TCB */
    LDR     R1, =g_kernel
    LDR     R2, [R1, #8]            /* current_task offset (8) */
    STR     R0, [R2]                /* stack_pointer (first member) */

pendsv_nosave:
    /* Call scheduler to select next task */
    BL      rtos_kernel_switch_context

    /* Load next task context */
    LDR     R1, =g_kernel
    LDR     R2, [R1, #8]            /* current_task */
    LDR     R0, [R2]                /* stack_pointer */

    /* Restore next context */
    LDMIA   R0!, {R4-R11}           /* Pop R4-R11 */
    MSR     PSP, R0                 /* Update PSP */

    /* Set EXC_RETURN to use PSP */
    MOV     LR, #0xFFFFFFFD         /* EXC_RETURN value */
    
    BX      LR                      /* Return to next task */

.size PendSV_Handler, . - PendSV_Handler
