#include "timer.h"

#include "KARTOS.h"
#include "klog.h"
#include "memory.h"
#include "rtos_port.h"
#include "timer_priv.h"

#include <stdbool.h>
#include <stddef.h>

static void timer_init_fields(rtos_timer_t *timer, const char *name, rtos_tick_t period_ticks, rtos_timer_mode_t mode,
                              rtos_timer_callback_t callback, void *parameter, bool is_static)
{
    timer->name        = name;
    timer->period      = period_ticks;
    timer->expiry_time = 0;
    timer->mode        = mode;
    timer->callback    = callback;
    timer->parameter   = parameter;
    timer->active      = false;
    timer->is_static   = is_static;
    timer->next        = NULL;
}

rtos_status_t rtos_timer_create(const char *name, rtos_tick_t period_ticks, rtos_timer_mode_t mode,
                                rtos_timer_callback_t callback, void *parameter, rtos_timer_handle_t *timer_handle)
{
    if (timer_handle == NULL || callback == NULL || period_ticks == 0)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_timer_t *timer = (rtos_timer_t *) rtos_malloc_from(RTOS_HEAP_LOW, sizeof(rtos_timer_t));
    if (timer == NULL)
    {
        KLOGE("Timer", "AllocFail");
        return RTOS_ERROR_NO_MEMORY;
    }

    timer_init_fields(timer, name, period_ticks, mode, callback, parameter, false);

    *timer_handle = timer;
    KLOGI("Timer", "TimerCreate period=%u", (uint32_t) period_ticks);

    return RTOS_SUCCESS;
}

rtos_status_t rtos_timer_create_static(rtos_timer_t *buffer, const char *name, rtos_tick_t period_ticks,
                                       rtos_timer_mode_t mode, rtos_timer_callback_t callback, void *parameter,
                                       rtos_timer_handle_t *timer_handle)
{
    if (buffer == NULL || timer_handle == NULL || callback == NULL || period_ticks == 0)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    timer_init_fields(buffer, name, period_ticks, mode, callback, parameter, true);

    *timer_handle = buffer;
    KLOGI("Timer", "TimerCreateStatic period=%u", (uint32_t) period_ticks);

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
        /* Remove first if already active to re-insert with new expiry time */
        timer_remove_active_list(timer);
    }

    rtos_tick_t current_tick = rtos_get_tick_count();
    timer->expiry_time       = current_tick + timer->period;

    /* Tick wraparound is handled correctly by signed comparison in rtos_timer_tick().
     * Expiry may wrap around, but (expiry - current) as int32_t gives correct time-until-expiry. */

    timer->active = true;
    timer_insert_active_list(timer);

    rtos_tick_t expiry_for_log = timer->expiry_time;

    rtos_port_exit_critical();

    KLOGD("Timer", "TimerStart expiry=%u", (uint32_t) expiry_for_log);
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
    KLOGD("Timer", "TimerStop");
    return RTOS_SUCCESS;
}

rtos_status_t rtos_timer_change_period(rtos_timer_handle_t timer_handle, rtos_tick_t new_period_ticks)
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
        timer_remove_active_list(timer);

        rtos_tick_t current_tick = rtos_get_tick_count();
        timer->expiry_time       = current_tick + timer->period;

        timer_insert_active_list(timer);
    }

    rtos_port_exit_critical();
    KLOGD("Timer", "TimerPeriodChange period=%u", (uint32_t) new_period_ticks);
    return RTOS_SUCCESS;
}

rtos_status_t rtos_timer_delete(rtos_timer_handle_t timer_handle)
{
    if (timer_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_timer_stop(timer_handle);

    if (!timer_handle->is_static)
    {
        rtos_free(timer_handle);
    }

    return RTOS_SUCCESS;
}
