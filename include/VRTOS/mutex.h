/*******************************************************************************
 * File: include/VRTOS/mutex.h
 * Description: Mutex header
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef MUTEX_H
#define MUTEX_H

#include "rtos_types.h"

/**
 * @file mutex.h
 * @brief Mutex definitions
 */

#define RTOS_MAX_WAIT ((rtos_tick_t) (-1))
#define RTOS_NO_WAIT  ((rtos_tick_t) (0))

#ifdef __cplusplus
extern "C"
{
#endif

/* Result codes specific to mutex API (maps to rtos_status_t) */
typedef enum
{
    RTOS_MUTEX_OK          = RTOS_SUCCESS,
    RTOS_MUTEX_ERR_INVALID = RTOS_ERROR_INVALID_PARAM,
    RTOS_MUTEX_ERR_NO_MEM  = RTOS_ERROR_NO_MEMORY,
    RTOS_MUTEX_ERR_TIMEOUT = RTOS_ERROR_TIMEOUT,
    RTOS_MUTEX_ERR_GENERAL = RTOS_ERROR_GENERAL
} rtos_mutex_status_t;

/* Minimal mutex structure. Keep fields intentionally small and extendable. */
typedef struct rtos_mutex
{
    rtos_tcb_t *owner;        /* current owner TCB (NULL if unlocked) */
    rtos_tcb_t *waiting_list; /* singly-linked list of waiting TCBs (uses tcb->next_waiting) */
    uint8_t     lock_count;   /* recursion depth for owner (future use) */
} rtos_mutex_t;

/**
 * Initialize a mutex object. Must be called before first use.
 * @param m Pointer to mutex object (non-NULL)
 * @return RTOS_MUTEX_OK on success, error otherwise
 */
rtos_mutex_status_t rtos_mutex_init(rtos_mutex_t *m);

/**
 * Lock/Acquire mutex. Blocks the calling task until mutex acquired or timeout expires.
 * @param m Pointer to mutex object
 * @param timeout_ticks Timeout in system ticks. 0 means try-once (non-blocking).
 *                      (use (rtos_tick_t)-1 to wait effectively forever)
 * @return RTOS_MUTEX_OK on success, RTOS_MUTEX_ERR_TIMEOUT if timed out, error otherwise
 */
rtos_mutex_status_t rtos_mutex_lock(rtos_mutex_t *m, rtos_tick_t timeout_ticks);

/**
 * Unlock/Release mutex. Only the owner may unlock.
 * @param m Pointer to mutex object
 * @return RTOS_MUTEX_OK on success, error otherwise
 */
rtos_mutex_status_t rtos_mutex_unlock(rtos_mutex_t *m);

#ifdef __cplusplus
}
#endif

#endif /* MUTEX_H */
