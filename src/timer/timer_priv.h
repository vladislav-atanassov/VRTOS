#ifndef TIMER_PRIV_H
#define TIMER_PRIV_H

#include "timer.h"

/* Global pointer to the list of active timers (sorted by expiry) */
extern rtos_timer_t *g_active_timers;

/* Internal helper to insert into sorted list */
void timer_insert_active_list(rtos_timer_t *timer);

/* Internal helper to remove from active list */
void timer_remove_active_list(rtos_timer_t *timer);

/*
 * Called by rtos_timer_tick() for each expired timer. If the timer service
 * task has been initialised (via rtos_timer_task_init()), the callback is
 * enqueued for task-context dispatch; otherwise it runs synchronously in the
 * caller's ISR context. Single source of truth for the dispatch branch.
 */
void rtos_timer_dispatch_from_isr(rtos_timer_t *timer);

#endif /* TIMER_PRIV_H */
