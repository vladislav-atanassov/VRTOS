#ifndef TEST_VERDICT_IO_H
#define TEST_VERDICT_IO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_verdict_io.h
 * @brief Direct-UART line emitter for test verdicts.
 *
 * Verdicts must reach the host even when scheduling is broken or the log
 * flush task can't run. This bypasses the ulog ring + interrupt-driven TX
 * and writes directly to the UART data register inside a critical section.
 *
 * Board-specific implementation lives in test_verdict_io.c. Today only
 * the STM32F4 USART2 path is provided; a port to another board adds a new
 * implementation file behind the same prototype.
 */
void test_verdict_emit_line(const char *line, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TEST_VERDICT_IO_H */
