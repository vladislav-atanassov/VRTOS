/*******************************************************************************
 * File: include/rtos/rtos_assert.h
 * Description: RTOS Assertion Macros
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef RTOS_ASSERT_H
#define RTOS_ASSERT_H

#include "config.h"

#include <assert.h>
#include <stdint.h>

/**
 * @file rtos_assert.h
 * @brief RTOS Assertion Macros
 *
 * This file provides assertion macros for debugging and error checking.
 */

#if RTOS_ASSERT_ENABLED

/**
 * @brief Assertion failure handler
 *
 * @param file Source file name
 * @param line Line number
 * @param func Function name
 * @param expr Failed expression
 */
void rtos_assert_failed(const char *file, uint32_t line, const char *func, const char *expr);

/**
 * @brief Main assertion macro
 *
 * @param expr Expression to evaluate
 */
#define RTOS_ASSERT(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expr))                                                                               \
        {                                                                                          \
            rtos_assert_failed(__FILE__, __LINE__, __func__, #expr);                               \
        }                                                                                          \
    } while (0)

/**
 * @brief Parameter validation assertion
 *
 * @param expr Expression to evaluate
 */
#define RTOS_ASSERT_PARAM(expr) RTOS_ASSERT(expr)

/**
 * @brief Critical assertion that should never fail
 *
 * @param expr Expression to evaluate
 */
#define RTOS_ASSERT_CRITICAL(expr) RTOS_ASSERT(expr)

#else /* RTOS_ASSERT_ENABLED */

/* Assertions disabled - define as empty macros */
#define RTOS_ASSERT(expr)          ((void) 0)
#define RTOS_ASSERT_PARAM(expr)    ((void) 0)
#define RTOS_ASSERT_CRITICAL(expr) ((void) 0)

#endif /* RTOS_ASSERT_ENABLED */

/**
 * @brief Compile-time assertion
 *
 * @param expr Expression to evaluate at compile time
 * @param msg Error message
 */
#define RTOS_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)

#endif /* RTOS_ASSERT_H */