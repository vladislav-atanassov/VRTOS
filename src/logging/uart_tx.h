#ifndef UART_TX_H
#define UART_TX_H

#include <stdint.h>
#include <stdio.h> // IWYU pragma: keep

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file uart_tx.h
 * @brief Low-level UART transport used by KLog / ULog and their flush task.
 *
 * The board-specific implementation lives in
 * boards/<board>/uart_tx.c.  This header is the interface KLog/ULog use to
 * reach the UART; verbosity / filtering / formatting are owned by the
 * higher-level loggers, not by this layer.
 */

/**
 * @brief Initialise UART hardware (pins, baud rate, interrupts).
 *
 * Must be called from main() before rtos_start_scheduler().  KLog/ULog write
 * into UART via uart_printf() and the newlib _write retarget which both
 * depend on this initialisation.
 */
void log_uart_init(void);

/**
 * @brief Blocking flush — drain the TX ring buffer by polling.  Use during
 *        pre-scheduler boot, fault handlers, or before WFI.
 */
void uart_tx_flush(void);

/**
 * @brief Drain any received bytes and dispatch known commands (e.g. LOG_MASK).
 *        Called periodically from the log flush task.
 */
void uart_rx_process_commands(void);

/**
 * @brief Direct printf replacement — formats with vsnprintf, then writes via
 *        the newlib _write retarget.  Bypasses newlib's stdio buffering.
 */
void uart_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif /* UART_TX_H */
