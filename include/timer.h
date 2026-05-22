#ifndef RTOS_TIMER_H
#define RTOS_TIMER_H

#include "rtos_types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    RTOS_TIMER_ONE_SHOT    = 0,
    RTOS_TIMER_AUTO_RELOAD = 1
} rtos_timer_mode_t;

/*
 * Callbacks execute in ISR context (SysTick handler). Must NOT call blocking RTOS APIs
 * (rtos_mutex_lock, rtos_semaphore_wait, rtos_delay_ms, etc.). Keep callbacks short to avoid
 * tick jitter. Use ISR-safe APIs only (e.g. rtos_event_group_set_bits_from_isr, rtos_task_notify).
 */
typedef void (*rtos_timer_callback_t)(void *timer_handle, void *parameter);

/* Timer control block. Public for static allocation via rtos_timer_create_static().
 * Treat fields as opaque — use the rtos_timer_* API instead of accessing directly. */
typedef struct rtos_timer
{
    const char           *name;
    rtos_tick_t           period;
    rtos_tick_t           expiry_time; /* Absolute tick count for next expiry */
    rtos_timer_mode_t     mode;
    rtos_timer_callback_t callback;
    void                 *parameter;
    bool                  active;
    bool                  is_static; /* True if storage is caller-owned (do not free on delete). */

    struct rtos_timer *next; /* Next timer in active list */
} rtos_timer_t;

typedef rtos_timer_t *rtos_timer_handle_t;

/**
 * @brief Create a software timer.
 * @param name         Timer name string (not copied; may be NULL).
 * @param period_ticks Timer period in ticks (must be > 0).
 * @param mode         RTOS_TIMER_ONE_SHOT or RTOS_TIMER_AUTO_RELOAD.
 * @param callback     Function called on expiry (executes in ISR context).
 * @param parameter    Passed as the second argument to callback.
 * @param timer_handle Output handle for the created timer.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_timer_create(const char *name, rtos_tick_t period_ticks, rtos_timer_mode_t mode,
                                rtos_timer_callback_t callback, void *parameter, rtos_timer_handle_t *timer_handle);

/**
 * @brief Create a software timer using caller-provided storage (no heap allocation).
 * @param buffer       Pointer to caller-owned storage for the timer control block.
 *                     Must remain valid for the lifetime of the timer.
 * @param name         Timer name string (not copied; may be NULL).
 * @param period_ticks Timer period in ticks (must be > 0).
 * @param mode         RTOS_TIMER_ONE_SHOT or RTOS_TIMER_AUTO_RELOAD.
 * @param callback     Function called on expiry (executes in ISR context).
 * @param parameter    Passed as the second argument to callback.
 * @param timer_handle Output handle (will equal buffer on success).
 * @return RTOS_SUCCESS or an error code.
 *
 * @note rtos_timer_delete() on a statically-created timer stops it but does not free storage.
 */
rtos_status_t rtos_timer_create_static(rtos_timer_t *buffer, const char *name, rtos_tick_t period_ticks,
                                       rtos_timer_mode_t mode, rtos_timer_callback_t callback, void *parameter,
                                       rtos_timer_handle_t *timer_handle);

/**
 * @brief Start or restart a timer. If already active, the expiry time is reset.
 * @param timer_handle Handle of the timer to start.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_timer_start(rtos_timer_handle_t timer_handle);

/**
 * @brief Stop a running timer. Has no effect if the timer is already stopped.
 * @param timer_handle Handle of the timer to stop.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_timer_stop(rtos_timer_handle_t timer_handle);

/**
 * @brief Change the period of a timer. If the timer is active, it is restarted with the new period.
 * @param timer_handle     Handle of the timer to modify.
 * @param new_period_ticks New period in ticks (must be > 0).
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_timer_change_period(rtos_timer_handle_t timer_handle, rtos_tick_t new_period_ticks);

/**
 * @brief Stop and free a timer. The handle is invalid after this call.
 * @param timer_handle Handle of the timer to delete.
 * @return RTOS_SUCCESS or an error code.
 */
rtos_status_t rtos_timer_delete(rtos_timer_handle_t timer_handle);

/**
 * @brief Process expired timers. Called by the kernel tick handler from SysTick ISR context.
 */
void rtos_timer_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_TIMER_H */
