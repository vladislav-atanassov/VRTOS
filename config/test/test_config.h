/*******************************************************************************
 * File: config/test/test_config.h
 * Description: Test Configuration Parameters
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/**
 * @file test_config.h
 * @brief Configuration for scheduler and synchronization tests
 *
 * These parameters define the test duration and task timing.
 * Task delays are chosen to create overlapping execution patterns
 * that clearly demonstrate scheduler behavior.
 */

/* Test duration in milliseconds (captures full test cycle) */
#define TEST_DURATION_MS (7500U)

/* Task delays in milliseconds (chosen for frequent overlap) */
#define TEST_TASK1_DELAY_MS (200U) /* Fast task */
#define TEST_TASK2_DELAY_MS (300U) /* Medium task */
#define TEST_TASK3_DELAY_MS (400U) /* Slow task */

/* Number of iterations each task should complete */
/* Calculated as ~7.5 * largest delay / own delay */
#define TEST_TASK1_ITERATIONS (15U) /* 3000ms / 200ms */
#define TEST_TASK2_ITERATIONS (10U) /* 3000ms / 300ms */
#define TEST_TASK3_ITERATIONS (8U)  /* 3000ms / 400ms */

/* Test timeout buffer (extra time after expected completion) */
#define TEST_TIMEOUT_BUFFER_MS (500U)

/* Total test time including buffer */
#define TEST_TOTAL_TIMEOUT_MS (TEST_DURATION_MS + TEST_TIMEOUT_BUFFER_MS)

#endif /* TEST_CONFIG_H */
