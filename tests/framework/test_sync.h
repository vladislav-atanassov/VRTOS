#ifndef TEST_SYNC_H
#define TEST_SYNC_H

#include "event_group.h"
#include "rtos_types.h"
#include "semaphore.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @file test_sync.h
 * @brief Event-driven test synchronisation primitives.
 *
 * Wraps RTOS semaphore and event-group objects with abort-aware wait functions.
 * When the watchdog fires (g_test_aborted = true) or the watchdog deadline
 * passes, all blocking waits return false immediately without consuming a
 * signal or clearing an event bit.
 *
 * Three types are provided:
 *   - test_signal_t   — counting semaphore (one producer / one consumer
 *                       handshake, or multi-signal accumulation).
 *   - test_barrier_t  — N-party rendezvous: all tasks block until the last
 *                       one arrives, then all proceed together.
 *   - test_phase_token_t — event-group wrapper for accumulating multiple
 *                          independent phase-complete signals in one object.
 *
 * Initialise objects from the suite's before() hook (or case body) so they
 * start clean for every case.
 */

/* ── test_signal_t ───────────────────────────────────────────────────────── */

/** Counting semaphore wrapper. Initial count = 0, max = 255. */
typedef struct
{
    rtos_semaphore_t sem;
} test_signal_t;

/**
 * @brief Initialise a test signal (count = 0).
 * Call from before() or case body before spawning test tasks.
 */
void test_signal_init(test_signal_t *sig);

/**
 * @brief Increment the signal count and wake one blocked waiter.
 * Safe to call from any task context (not from ISR).
 */
void test_signal_post(test_signal_t *sig);

/**
 * @brief Wait for the signal count to become non-zero, then decrement it.
 *
 * If a watchdog is armed, the effective timeout is clamped to the watchdog
 * deadline so the call returns when the phase budget expires rather than
 * blocking until @p timeout_ticks. After the wait, g_test_aborted is
 * re-checked — if it is set the call returns false even if the semaphore
 * was acquired.
 *
 * @param sig           Signal to wait on.
 * @param timeout_ticks RTOS_SEM_MAX_WAIT for "wait up to watchdog deadline",
 *                      or a specific tick count for a hard upper bound.
 * @return true if the signal was received, false on timeout or abort.
 */
bool test_signal_wait(test_signal_t *sig, rtos_tick_t timeout_ticks);

/**
 * @brief Drain any pending counts to zero (non-blocking).
 * Call from before() to reset inter-case state.
 */
void test_signal_reset(test_signal_t *sig);

/* ── test_barrier_t ──────────────────────────────────────────────────────── */

/**
 * N-party barrier. All n_parties tasks block at test_barrier_wait() until the
 * last one arrives; then all are released simultaneously.
 */
typedef struct
{
    rtos_semaphore_t gate;     /**< Counting gate: released n-1 times by the last arrival. */
    rtos_semaphore_t lock;     /**< Binary semaphore protecting the arrived counter. */
    volatile uint8_t arrived;
    uint8_t          n_parties;
} test_barrier_t;

/**
 * @brief Initialise an N-party barrier.
 * @param b         Barrier to initialise.
 * @param n_parties Number of tasks that must call test_barrier_wait before any proceed.
 */
void test_barrier_init(test_barrier_t *b, uint8_t n_parties);

/**
 * @brief Arrive at the barrier and wait for all other parties.
 *
 * @param b             Barrier to synchronise on.
 * @param timeout_ticks Per-task wait limit; clamped to the watchdog deadline.
 * @return true if all parties arrived within the timeout, false otherwise.
 */
bool test_barrier_wait(test_barrier_t *b, rtos_tick_t timeout_ticks);

/* ── test_phase_token_t ──────────────────────────────────────────────────── */

/**
 * Event-group wrapper for accumulating multiple named-phase completions in a
 * single object. Each phase occupies one bit of the underlying event group
 * (up to 24 usable bits on a 32-bit event group).
 */
typedef struct
{
    rtos_event_group_t eg;
} test_phase_token_t;

/**
 * @brief Initialise a phase token (all bits clear).
 */
void test_phase_token_init(test_phase_token_t *pt);

/**
 * @brief Mark one or more phase bits as complete.
 * Safe to call from any task context.
 * @param pt   Phase token.
 * @param bits Bitmask of phases to mark done.
 */
void test_phase_set(test_phase_token_t *pt, uint32_t bits);

/**
 * @brief Wait for the specified phase bits to be set.
 *
 * @param pt            Phase token.
 * @param bits          Bitmask to wait for.
 * @param wait_all      true = all bits must be set; false = any one bit suffices.
 * @param clear_on_exit true = clear the matched bits after the condition is met.
 * @param timeout_ticks Timeout; clamped to the watchdog deadline if armed.
 * @return true if the condition was met, false on timeout or abort.
 */
bool test_phase_wait(test_phase_token_t *pt, uint32_t bits, bool wait_all,
                     bool clear_on_exit, rtos_tick_t timeout_ticks);

#ifdef __cplusplus
}
#endif

#endif /* TEST_SYNC_H */
