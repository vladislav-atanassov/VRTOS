/*******************************************************************************
 * File: src/timer/timer_priv.h
 * Description: Private Timer Structure and Internal helper declarations
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#ifndef TIMER_PRIV_H
#define TIMER_PRIV_H

#include "timer.h"

#include <stdbool.h>


/* Internal Timer Structure */
struct rtos_timer
{
    const char           *name;
    rtos_tick_t           period;
    rtos_tick_t           expiry_time; /* Absolute tick count for next expiry */
    rtos_timer_mode_t     mode;
    rtos_timer_callback_t callback;
    void                 *parameter;
    bool                  active;

    struct rtos_timer *next; /* Next timer in active list */
};

/* Global pointer to the list of active timers (sorted by expiry) */
extern rtos_timer_t *g_active_timers;

/* Internal helper to insert into sorted list */
void timer_insert_active_list(rtos_timer_t *timer);

/* Internal helper to remove from active list */
void timer_remove_active_list(rtos_timer_t *timer);

#endif /* TIMER_PRIV_H */
