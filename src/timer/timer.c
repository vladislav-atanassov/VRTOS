/*******************************************************************************
 * File: src/timer/timer.c
 * Description: Timer API Implementation
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "timer.h"

#include "VRTOS.h"
#include "log.h"
#include "memory.h"
#include "rtos_port.h"
#include "timer_priv.h"

#include <stddef.h>

rtos_status_t rtos_timer_create(const char *name, rtos_tick_t period_ticks, rtos_timer_mode_t mode,
                                rtos_timer_callback_t callback, void *parameter,
                                rtos_timer_handle_t *timer_handle)
{
    if (timer_handle == NULL || callback == NULL || period_ticks == 0)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_timer_t *timer = (rtos_timer_t *) rtos_malloc(sizeof(rtos_timer_t));
    if (timer == NULL)
    {
        log_error("Failed to allocate timer");
        return RTOS_ERROR_NO_MEMORY;
    }

    timer->name        = name;
    timer->period      = period_ticks;
    timer->expiry_time = 0;
    timer->mode        = mode;
    timer->callback    = callback;
    timer->parameter   = parameter;
    timer->active      = false;
    timer->next        = NULL;

    *timer_handle = timer;
    log_info("Timer created: %s (period=%u)", name ? name : "unnamed", (unsigned int) period_ticks);

    return RTOS_SUCCESS;
}

rtos_status_t rtos_timer_start(rtos_timer_handle_t timer_handle)
{
    if (timer_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_timer_t *timer = (rtos_timer_t *) timer_handle;

    rtos_port_enter_critical();

    if (timer->active)
    {
        /* Remove first if already active to re-insert with new time */
        timer_remove_active_list(timer);
    }

    /* Set expiration */
    rtos_tick_t current_tick = rtos_get_tick_count();
    timer->expiry_time       = current_tick + timer->period;

    /**
     * Note: Tick wraparound is handled correctly by signed comparison in
     * rtos_timer_tick(). Expiry might wrap around, but (expiry - current)
     * as signed int32_t gives correct "time until expiry" value.
     */

    timer->active = true;
    timer_insert_active_list(timer);

    /* Capture values inside critical section for logging */
    const char *timer_name     = timer->name ? timer->name : "unnamed";
    rtos_tick_t expiry_for_log = timer->expiry_time;

    rtos_port_exit_critical();

    log_debug("Timer started: %s (expires at %u)", timer_name, (unsigned int) expiry_for_log);
    return RTOS_SUCCESS;
}

rtos_status_t rtos_timer_stop(rtos_timer_handle_t timer_handle)
{
    if (timer_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_timer_t *timer = (rtos_timer_t *) timer_handle;

    rtos_port_enter_critical();

    if (timer->active)
    {
        timer_remove_active_list(timer);
        timer->active = false;
    }

    rtos_port_exit_critical();
    log_debug("Timer stopped: %s", timer->name);
    return RTOS_SUCCESS;
}

rtos_status_t rtos_timer_change_period(rtos_timer_handle_t timer_handle,
                                       rtos_tick_t         new_period_ticks)
{
    if (timer_handle == NULL || new_period_ticks == 0)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_timer_t *timer = (rtos_timer_t *) timer_handle;

    rtos_port_enter_critical();

    timer->period = new_period_ticks;

    if (timer->active)
    {
        /* If active, restart with new period relative to NOW?
           Usually change_period implies reset.
        */
        timer_remove_active_list(timer);

        rtos_tick_t current_tick = rtos_get_tick_count();
        timer->expiry_time       = current_tick + timer->period;

        timer_insert_active_list(timer);
    }

    rtos_port_exit_critical();
    log_debug("Timer period changed: %s (new=%u)", timer->name, (unsigned int) new_period_ticks);
    return RTOS_SUCCESS;
}

rtos_status_t rtos_timer_delete(rtos_timer_handle_t timer_handle)
{
    if (timer_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_timer_stop(timer_handle);
    rtos_free(timer_handle);

    return RTOS_SUCCESS;
}
