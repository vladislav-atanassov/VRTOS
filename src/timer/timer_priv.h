#ifndef TIMER_PRIV_H
#define TIMER_PRIV_H

#include "timer.h"

/* Global pointer to the list of active timers (sorted by expiry) */
extern rtos_timer_t *g_active_timers;

/* Internal helper to insert into sorted list */
void timer_insert_active_list(rtos_timer_t *timer);

/* Internal helper to remove from active list */
void timer_remove_active_list(rtos_timer_t *timer);

#endif /* TIMER_PRIV_H */
