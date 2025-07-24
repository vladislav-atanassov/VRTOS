/*******************************************************************************
 * File: src/utils/rtos_assert.c
 * Description: RTOS Assertion Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "rtos_assert.h"
#include "config.h"
#include <stddef.h>
#include <stdint.h>
#include <stm32f4xx_hal.h>

/* Include CMSIS core for ARM intrinsics */
#if defined(__GNUC__)
    #include "core_cm4.h"
#endif

/**
 * @file rtos_assert.c
 * @brief RTOS Assertion Implementation
 * 
 * This file contains the implementation of assertion handling for the RTOS.
 */

#if RTOS_ASSERT_ENABLED

/**
 * @brief RTOS assertion failure handler
 * 
 * This function is called when an assertion fails. In a production system,
 * this might log the error and reset the system. For development, it provides
 * debugging information.
 * 
 * @param file Source file where assertion failed
 * @param line Line number where assertion failed
 * @param func Function name where assertion failed
 * @param expr Expression that failed
 */
void rtos_assert_failed(const char *file, uint32_t line, const char *func, const char *expr)
{
    /* Disable interrupts to prevent further issues */
    #if defined(__GNUC__)
        __disable_irq();
    #else
        __asm volatile ("cpsid i" ::: "memory");
    #endif
    
    /* Store assertion information for debugging - suppress unused warnings */
    static volatile const char *assert_file = NULL;
    static volatile uint32_t assert_line = 0;
    static volatile const char *assert_func = NULL;
    static volatile const char *assert_expr = NULL;
    
    /* Use variables to prevent optimization and suppress warnings */
    assert_file = file;
    assert_line = line;
    assert_func = func;
    assert_expr = expr;
    
    /* Prevent compiler from optimizing away the variables */
    (void)assert_file;
    (void)assert_line;
    (void)assert_func;
    (void)assert_expr;
    
    /* In debug builds, break to debugger */
    #ifdef DEBUG
        #if defined(__GNUC__)
            __BKPT(0);
        #else
            __asm volatile ("bkpt #0");
        #endif
    #endif
    
    /* Infinite loop to halt execution */
    while (1) {
        /* Could implement watchdog reset here */
        #if defined(__GNUC__)
            __NOP();
        #else
            __asm volatile ("nop");
        #endif
    }
}

#endif /* RTOS_ASSERT_ENABLED */