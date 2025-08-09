/*******************************************************************************
 * File: include/rtos_port.h
 * Description: Porting Layer Interface
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_PORT_H
#define RTOS_PORT_H

#include "rtos_types.h"

/**
 * @file rtos_port.h
 * @brief RTOS Porting Layer Interface
 *
 * This file defines the interface between the RTOS core and the hardware platform.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the porting layer
 *
 * @return RTOS_SUCCESS if successful, error code otherwise
 */
rtos_status_t rtos_port_init(void);

/**
 * @brief Start the system tick timer
 */
void rtos_port_start_systick(void);

/**
 * @brief Start the first task
 *
 * This function should never return.
 */
void rtos_port_start_first_task(void);

/**
 * @brief Initialize task stack
 *
 * @param stack_top Top of the stack memory
 * @param task_function Task function to execute
 * @param parameter Parameter to pass to task function
 * @return Initial stack pointer value
 */
uint32_t *rtos_port_init_task_stack(uint32_t *stack_top, rtos_task_function_t task_function, void *parameter);

/**
 * @brief Enter critical section (disable interrupts)
 */
void rtos_port_enter_critical(void);

/**
 * @brief Exit critical section (enable interrupts)
 */
void rtos_port_exit_critical(void);

/**
 * @brief Force a context switch
 */
void rtos_port_yield(void);

/**
 * @brief System tick interrupt handler
 *
 * This function should be called from the SysTick interrupt handler.
 */
void rtos_port_systick_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_PORT_H */
