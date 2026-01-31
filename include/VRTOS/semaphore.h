/*******************************************************************************
 * File: include/VRTOS/semaphore.h
 * Description: Counting Semaphore API
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "rtos_types.h"

/**
 * @file semaphore.h
 * @brief Counting Semaphore API
 *
 * Provides counting semaphores for task synchronization.
 * Binary semaphores can be created by setting max_count = 1.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/* Forward declaration for TCB */
struct rtos_task_control_block;

/* Semaphore wait timeout values */
#define RTOS_SEM_MAX_WAIT ((rtos_tick_t) 0xFFFFFFFFU)
#define RTOS_SEM_NO_WAIT  ((rtos_tick_t) 0U)

/**
 * @brief Semaphore status codes
 */
typedef enum
{
    RTOS_SEM_OK           = RTOS_SUCCESS,
    RTOS_SEM_ERR_INVALID  = RTOS_ERROR_INVALID_PARAM,
    RTOS_SEM_ERR_TIMEOUT  = RTOS_ERROR_TIMEOUT,
    RTOS_SEM_ERR_OVERFLOW = RTOS_ERROR_GENERAL
} rtos_sem_status_t;

/**
 * @brief Semaphore structure
 */
typedef struct rtos_semaphore
{
    uint32_t                        count;        /**< Current count */
    uint32_t                        max_count;    /**< Maximum count (0 = unlimited) */
    struct rtos_task_control_block *waiting_list; /**< Head of waiting task list (priority-ordered) */
} rtos_semaphore_t;

/**
 * @brief Initialize a semaphore
 * @param sem Pointer to semaphore structure
 * @param initial_count Initial count value
 * @param max_count Maximum count (0 = unlimited, 1 = binary semaphore)
 * @return RTOS_SEM_OK on success
 */
rtos_sem_status_t rtos_semaphore_init(rtos_semaphore_t *sem, uint32_t initial_count, uint32_t max_count);

/**
 * @brief Wait on a semaphore (decrements count or blocks)
 * @param sem Pointer to semaphore
 * @param timeout_ticks Timeout in ticks (0 = no wait, RTOS_SEM_MAX_WAIT = forever)
 * @return RTOS_SEM_OK if acquired, RTOS_SEM_ERR_TIMEOUT if timed out
 */
rtos_sem_status_t rtos_semaphore_wait(rtos_semaphore_t *sem, rtos_tick_t timeout_ticks);

/**
 * @brief Signal a semaphore (increments count or wakes waiter)
 * @param sem Pointer to semaphore
 * @return RTOS_SEM_OK on success, RTOS_SEM_ERR_OVERFLOW if at max
 */
rtos_sem_status_t rtos_semaphore_signal(rtos_semaphore_t *sem);

/**
 * @brief Try to acquire semaphore without blocking
 * @param sem Pointer to semaphore
 * @return RTOS_SEM_OK if acquired, RTOS_SEM_ERR_TIMEOUT if not available
 */
rtos_sem_status_t rtos_semaphore_try_wait(rtos_semaphore_t *sem);

/**
 * @brief Get current semaphore count
 * @param sem Pointer to semaphore
 * @return Current count value
 */
uint32_t rtos_semaphore_get_count(rtos_semaphore_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* SEMAPHORE_H */
