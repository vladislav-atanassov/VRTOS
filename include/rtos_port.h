#ifndef RTOS_PORT_H
#define RTOS_PORT_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Initialize the hardware port layer (NVIC priorities, FPU, etc.).
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_port_init(void);

/**
 * @brief Start the SysTick timer at the configured tick rate.
 */
void rtos_port_start_systick(void);

/**
 * @brief Load the first task and enter thread mode. Never returns.
 */
void rtos_port_start_first_task(void);

/**
 * @brief Initialize the exception frame for a new task stack.
 * @param stack_top Pointer to the top (highest address) of the allocated stack block.
 * @param task_function Task entry point.
 * @param parameter Argument passed to task_function.
 * @return Updated stack pointer after pushing the initial frame.
 */
uint32_t *rtos_port_init_task_stack(uint32_t *stack_top, rtos_task_function_t task_function, void *parameter);

/**
 * @brief Raise BASEPRI to mask task-level interrupts.
 * Priority-0 (critical hardware) IRQs can still preempt.
 */
void rtos_port_enter_critical(void);

/**
 * @brief Restore BASEPRI to 0, re-enabling task-level interrupts.
 */
void rtos_port_exit_critical(void);

/**
 * @brief ISR-safe critical section entry. Returns saved BASEPRI for later restoration.
 * @return Saved BASEPRI value to pass to rtos_port_exit_critical_from_isr().
 */
uint32_t rtos_port_enter_critical_from_isr(void);

/**
 * @brief ISR-safe critical section exit. Restores the saved BASEPRI value.
 * @param saved_priority Value returned by rtos_port_enter_critical_from_isr().
 */
void rtos_port_exit_critical_from_isr(uint32_t saved_priority);

/**
 * @brief Trigger a PendSV exception to request a context switch.
 */
void rtos_port_yield(void);

/**
 * @brief SysTick interrupt handler. Advances the tick counter and drives the scheduler.
 */
void rtos_port_systick_handler(void);

/**
 * @brief Return a microsecond-resolution uptime timestamp.
 * Combines the tick count (ms resolution) with the SysTick countdown for sub-tick precision.
 * Wraps at ~71 minutes. Safe to call from ISR or task context.
 * @return Microseconds since boot.
 */
uint32_t rtos_port_get_uptime_us(void);

/**
 * @brief Stop SysTick, sleep for up to expected_idle_ticks, then restore the tick count.
 * @param expected_idle_ticks Maximum number of ticks the CPU may sleep.
 */
void rtos_port_suppress_ticks_and_sleep(uint32_t expected_idle_ticks);

/**
 * @brief Return true if the caller is executing in interrupt/handler mode.
 * Use to assert against use of task-context-only APIs (e.g. rtos_malloc) from ISRs.
 */
bool rtos_port_in_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_PORT_H */
