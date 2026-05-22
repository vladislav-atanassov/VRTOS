#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct rtos_task_control_block;

#define RTOS_SEM_MAX_WAIT ((rtos_tick_t) 0xFFFFFFFFU)
#define RTOS_SEM_NO_WAIT  ((rtos_tick_t) 0U)

typedef enum
{
    RTOS_SEM_OK           = RTOS_SUCCESS,
    RTOS_SEM_ERR_INVALID  = RTOS_ERROR_INVALID_PARAM,
    RTOS_SEM_ERR_TIMEOUT  = RTOS_ERROR_TIMEOUT,
    RTOS_SEM_ERR_OVERFLOW = RTOS_ERROR_GENERAL
} rtos_sem_status_t;

typedef struct rtos_semaphore
{
    uint32_t                        count;
    uint32_t                        max_count;    /* 0 = unlimited, 1 = binary */
    struct rtos_task_control_block *waiting_list; /* priority-ordered */
} rtos_semaphore_t;

/**
 * @brief Initialize a semaphore.
 * @param sem Pointer to the semaphore object (must not be NULL).
 * @param initial_count Initial count value.
 * @param max_count Maximum count. 0 = unlimited, 1 = binary semaphore.
 * @return RTOS_SEM_OK on success, error code otherwise.
 */
rtos_sem_status_t rtos_semaphore_init(rtos_semaphore_t *sem, uint32_t initial_count, uint32_t max_count);

/**
 * @brief Decrement the semaphore count, blocking if the count is zero.
 * @param sem Pointer to the semaphore.
 * @param timeout_ticks Maximum ticks to wait. RTOS_SEM_NO_WAIT returns immediately.
 * @return RTOS_SEM_OK on success, RTOS_SEM_ERR_TIMEOUT if the count was not available in time.
 */
rtos_sem_status_t rtos_semaphore_wait(rtos_semaphore_t *sem, rtos_tick_t timeout_ticks);

/**
 * @brief Increment the semaphore count, unblocking the highest-priority waiter if any.
 * @param sem Pointer to the semaphore.
 * @return RTOS_SEM_OK on success, RTOS_SEM_ERR_OVERFLOW if max_count would be exceeded.
 */
rtos_sem_status_t rtos_semaphore_signal(rtos_semaphore_t *sem);

/**
 * @brief Try to decrement the semaphore count without blocking.
 * @param sem Pointer to the semaphore.
 * @return RTOS_SEM_OK if the count was available, RTOS_SEM_ERR_TIMEOUT otherwise.
 */
rtos_sem_status_t rtos_semaphore_try_wait(rtos_semaphore_t *sem);

/**
 * @brief Return the current semaphore count.
 * @param sem Pointer to the semaphore.
 * @return Current count value.
 */
uint32_t rtos_semaphore_get_count(rtos_semaphore_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* SEMAPHORE_H */
