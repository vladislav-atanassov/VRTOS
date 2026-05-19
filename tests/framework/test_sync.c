#include "test_sync.h"
#include "test_watchdog.h"

#include "KARTOS.h" /* rtos_get_tick_count */

#include <stddef.h>

/* ── Internal helper ─────────────────────────────────────────────────────── */

/*
 * Clamp timeout_ticks to the watchdog deadline so blocking calls return
 * when the phase budget expires. Uses signed arithmetic to handle tick-counter
 * wraparound correctly (deadline is always set in the near future).
 */
static rtos_tick_t clamp_to_deadline(rtos_tick_t timeout_ticks)
{
    if (g_test_watchdog_deadline == 0U)
    {
        return timeout_ticks;
    }

    rtos_tick_t now      = rtos_get_tick_count();
    int32_t     ticks_left = (int32_t)(g_test_watchdog_deadline - now);

    if (ticks_left <= 0)
    {
        /* Deadline already passed — caller should return immediately. */
        return 0U;
    }

    rtos_tick_t remaining = (rtos_tick_t) ticks_left;

    if (timeout_ticks == RTOS_SEM_MAX_WAIT || remaining < timeout_ticks)
    {
        return remaining;
    }
    return timeout_ticks;
}

/* ── test_signal_t ───────────────────────────────────────────────────────── */

void test_signal_init(test_signal_t *sig)
{
    rtos_semaphore_init(&sig->sem, 0U, 255U);
}

void test_signal_post(test_signal_t *sig)
{
    rtos_semaphore_signal(&sig->sem);
}

bool test_signal_wait(test_signal_t *sig, rtos_tick_t timeout_ticks)
{
    if (g_test_aborted)
    {
        return false;
    }

    rtos_tick_t       effective = clamp_to_deadline(timeout_ticks);
    rtos_sem_status_t ret       = rtos_semaphore_wait(&sig->sem, effective);

    /* Check abort again: timer may have fired during the wait. */
    if (g_test_aborted)
    {
        return false;
    }
    return ret == RTOS_SEM_OK;
}

void test_signal_reset(test_signal_t *sig)
{
    while (rtos_semaphore_try_wait(&sig->sem) == RTOS_SEM_OK)
    {
        /* drain */
    }
}

/* ── test_barrier_t ──────────────────────────────────────────────────────── */

void test_barrier_init(test_barrier_t *b, uint8_t n_parties)
{
    b->n_parties = n_parties;
    b->arrived   = 0U;
    /* gate starts closed; max count = n_parties so all can be released at once */
    rtos_semaphore_init(&b->gate, 0U, (uint32_t) n_parties);
    /* lock is a binary semaphore acting as a mutex for the arrived counter */
    rtos_semaphore_init(&b->lock, 1U, 1U);
}

bool test_barrier_wait(test_barrier_t *b, rtos_tick_t timeout_ticks)
{
    if (g_test_aborted)
    {
        return false;
    }

    /* Acquire the lock to safely increment the arrived counter. */
    rtos_tick_t effective = clamp_to_deadline(timeout_ticks);
    if (rtos_semaphore_wait(&b->lock, effective) != RTOS_SEM_OK)
    {
        return false;
    }

    b->arrived++;
    uint8_t arrived   = b->arrived;
    uint8_t n_parties = b->n_parties;
    rtos_semaphore_signal(&b->lock);

    if (arrived >= n_parties)
    {
        /* Last to arrive: reset counter and release all other waiters. */
        b->arrived = 0U;
        for (uint8_t i = 1U; i < n_parties; i++)
        {
            rtos_semaphore_signal(&b->gate);
        }
        return !g_test_aborted;
    }

    /* Not last: wait for the final party to release the gate. */
    effective                 = clamp_to_deadline(timeout_ticks);
    rtos_sem_status_t gate_ret = rtos_semaphore_wait(&b->gate, effective);

    if (g_test_aborted)
    {
        return false;
    }
    return gate_ret == RTOS_SEM_OK;
}

/* ── test_phase_token_t ──────────────────────────────────────────────────── */

void test_phase_token_init(test_phase_token_t *pt)
{
    rtos_event_group_init(&pt->eg);
}

void test_phase_set(test_phase_token_t *pt, uint32_t bits)
{
    rtos_event_group_set_bits(&pt->eg, bits);
}

bool test_phase_wait(test_phase_token_t *pt, uint32_t bits, bool wait_all,
                     bool clear_on_exit, rtos_tick_t timeout_ticks)
{
    if (g_test_aborted)
    {
        return false;
    }

    rtos_tick_t      effective = clamp_to_deadline(timeout_ticks);
    rtos_eg_status_t ret       = rtos_event_group_wait_bits(&pt->eg, bits, wait_all,
                                                            clear_on_exit, NULL, effective);
    if (g_test_aborted)
    {
        return false;
    }
    return ret == RTOS_EG_OK;
}
