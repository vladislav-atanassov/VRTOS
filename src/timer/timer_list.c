/*******************************************************************************
 * File: src/timer/timer_list.c
 * Description: Timer List Management and Tick Processing
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "rtos_port.h"
#include "timer.h"
#include "timer_priv.h"

#include <stddef.h>

/* Global list head */
rtos_timer_t *g_active_timers = NULL;

/*
 * Helper to comparison time with wraparound handling
 * Returns true if t1 is "before" t2
 */
static bool time_before(rtos_tick_t t1, rtos_tick_t t2)
{
    return ((int32_t) (t1 - t2) < 0);
}

void timer_insert_active_list(rtos_timer_t *timer)
{
    if (g_active_timers == NULL)
    {
        g_active_timers = timer;
        timer->next     = NULL;
        return;
    }

    /* Check if we should be at head */
    if (time_before(timer->expiry_time, g_active_timers->expiry_time))
    {
        timer->next     = g_active_timers;
        g_active_timers = timer;
        return;
    }

    /* Find insertion point */
    rtos_timer_t *current = g_active_timers;
    while (current->next != NULL)
    {
        if (time_before(timer->expiry_time, current->next->expiry_time))
        {
            /* Insert before next */
            break;
        }
        current = current->next;
    }

    /* Insert after current */
    timer->next   = current->next;
    current->next = timer;
}

void timer_remove_active_list(rtos_timer_t *timer)
{
    if (g_active_timers == NULL || timer == NULL)
    {
        return;
    }

    if (g_active_timers == timer)
    {
        g_active_timers = timer->next;
        timer->next     = NULL;
        return;
    }

    rtos_timer_t *current = g_active_timers;
    while (current->next != NULL && current->next != timer)
    {
        current = current->next;
    }

    if (current->next == timer)
    {
        current->next = timer->next;
        timer->next   = NULL;
    }
}

/**
 * @brief Process timer tick (Called from Kernel Tick Handler)
 * Checks for expired timers and executes callbacks.
 *
 * Note: Called from ISR context (SysTick), uses ISR-safe critical sections.
 */
void rtos_timer_tick(void)
{
    uint32_t saved_priority = rtos_port_enter_critical_from_isr();

    if (g_active_timers == NULL)
    {
        rtos_port_exit_critical_from_isr(saved_priority);
        return;
    }

    rtos_tick_t current_tick = rtos_get_tick_count();

    /* Process all expired timers */
    /* Note: Since list is sorted, we just check head until not expired */
    while (g_active_timers != NULL)
    {
        /* Check if expired (expiry <= current) using signed comparison for wraparound */
        if ((int32_t) (g_active_timers->expiry_time - current_tick) <= 0)
        {
            rtos_timer_t *expired = g_active_timers;

            /* Remove from list */
            g_active_timers = expired->next;
            expired->next   = NULL; /* Detach */

            /* Execute callback outside critical section for reduced latency */
            rtos_port_exit_critical_from_isr(saved_priority);

            if (expired->callback != NULL)
            {
                expired->callback((void *) expired, expired->parameter);
            }

            /* Re-enter critical section for list manipulation */
            saved_priority = rtos_port_enter_critical_from_isr();

            /* Handle Auto-Reload */
            if (expired->mode == RTOS_TIMER_AUTO_RELOAD)
            {
                /*
                 * Calculate next expiry using expiry_time + period to avoid drift.
                 * If callback took longer than one period, catch up by advancing
                 * expiry_time until it's in the future to prevent repeated fires.
                 */
                rtos_tick_t now = rtos_get_tick_count();
                do
                {
                    expired->expiry_time += expired->period;
                } while ((int32_t) (expired->expiry_time - now) <= 0);

                /* Re-insert into sorted active list */
                timer_insert_active_list(expired);
            }
            else
            {
                /* One-shot: mark inactive */
                expired->active = false;
            }
        }
        else
        {
            /* Head not expired, so nothing else is (sorted list) */
            break;
        }
    }

    rtos_port_exit_critical_from_isr(saved_priority);
}
