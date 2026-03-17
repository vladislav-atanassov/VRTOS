#ifndef PORT_PRIV_H
#define PORT_PRIV_H

#include "config.h" // IWYU pragma: keep

/* These macros are enforced by port_common.h's PORT_VERIFY_CONTRACT check. */

/** Stack alignment requirement (AAPCS mandates 8-byte alignment). */
#define PORT_STACK_ALIGNMENT 8

/**
 * Initial EXC_RETURN pushed onto every new task stack.
 * 0xFFFFFFFD = return to Thread mode, use PSP, no FPU frame.
 */
#define PORT_INITIAL_EXC_RETURN 0xFFFFFFFD

/** This port has a hardware single-precision FPU. */
#define PORT_HAS_FPU 1

/* ======================== Interrupt Priorities =========================== */

/**
 * Cortex-M4 uses 4-bit priority (16 levels) in the upper nibble
 * of an 8-bit field. Lower numeric value = higher priority.
 */
#define PORT_IRQ_PRIORITY_CRITICAL (0x00) /**< Never masked (DMA, critical timers) */
#define PORT_IRQ_PRIORITY_HIGH     (0x40) /**< Can preempt RTOS (UART RX, SPI) */
#define PORT_IRQ_PRIORITY_KERNEL   (0x80) /**< SysTick */
#define PORT_IRQ_PRIORITY_LOW      (0xC0) /**< Non-critical peripherals */
#define PORT_IRQ_PRIORITY_PENDSV   (0xF0) /**< PendSV (lowest — late reschedule) */

/** BASEPRI threshold used to mask kernel-level and lower interrupts. */
#define PORT_MAX_INTERRUPT_PRIORITY PORT_IRQ_PRIORITY_KERNEL

/** DWT Cycle Count Register address (Cortex-M debug unit). */
#define PORT_DWT_CYCCNT_ADDR 0xE0001004

/** xPSR initial value (Thumb bit set). */
#define PORT_INITIAL_XPSR 0x01000000

#endif /* PORT_PRIV_H */
