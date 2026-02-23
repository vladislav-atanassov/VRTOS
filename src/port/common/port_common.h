/*******************************************************************************
 * File: src/port/common/port_common.h
 * Description: Common Port Layer Types and Helpers
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef PORT_COMMON_H
#define PORT_COMMON_H

#include <stdint.h>

/**
 * @file port_common.h
 * @brief Common types shared by all ports.
 *
 * See docs/porting_guide.md for full porting instructions.
 */

/* ======================== Common Type Aliases ============================ */

/** Stack element type (one word). */
typedef uint32_t port_stack_t;

/** Stack canary pattern for overflow detection. */
#define PORT_STACK_CANARY_VALUE 0xC0DEC0DEU

/* ======================== Contract Enforcement ============================ */

/**
 * Include this section ONLY from a chip's port.c, AFTER port_priv.h.
 * Define PORT_VERIFY_CONTRACT before including port_common.h to enable checks.
 */
#ifdef PORT_VERIFY_CONTRACT

#ifndef PORT_STACK_ALIGNMENT
#error "port_priv.h must define PORT_STACK_ALIGNMENT"
#endif

#ifndef PORT_INITIAL_EXC_RETURN
#error "port_priv.h must define PORT_INITIAL_EXC_RETURN"
#endif

#ifndef PORT_HAS_FPU
#error "port_priv.h must define PORT_HAS_FPU (0 or 1)"
#endif

#ifndef PORT_MAX_INTERRUPT_PRIORITY
#error "port_priv.h must define PORT_MAX_INTERRUPT_PRIORITY"
#endif

#ifndef PORT_INITIAL_XPSR
#error "port_priv.h must define PORT_INITIAL_XPSR"
#endif

#endif /* PORT_VERIFY_CONTRACT */

#endif /* PORT_COMMON_H */
