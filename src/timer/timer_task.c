/**
 * Software timer service task.
 *
 * Activated by rtos_timer_task_init(). Until that call is made,
 * rtos_timer_dispatch_from_isr() runs callbacks synchronously from the
 * SysTick ISR — i.e. the kernel preserves its legacy "ISR-context callback"
 * contract for apps that never opt in.
 *
 * Once the task is created, every expired-timer callback is captured into a
 * single-producer/single-consumer ring (producer = SysTick ISR, consumer =
 * timer task) and dispatched from the task context. Callbacks may then call
 * blocking RTOS APIs.
 *
 * Snapshot vs. handle: we store callback + parameter at enqueue time rather
 * than re-reading them through the timer handle at dispatch. The timer may
 * be stopped, restarted, or have its callback replaced between enqueue and
 * dispatch — the snapshot makes the dispatch behave the same as the legacy
 * direct-ISR path (you can't cancel a callback that already started).
 *
 * Overflow: if the ring is full, enqueue silently increments
 * g_timer_dispatch_overflow_count and drops the dispatch. The auto-reload
 * re-insertion still runs in rtos_timer_tick(), so the next tick may
 * succeed. Sizing TIMER_TASK_QUEUE_LENGTH for the worst-case burst is the
 * application's responsibility.
 */

#include "KARTOS.h"
#include "config.h"
#include "klog.h"
#include "rtos_port.h"
#include "semaphore.h"
#include "task.h"
#include "timer.h"
#include "timer_priv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Diagnostic counter — readable from a test or app for sizing validation. */
volatile uint32_t g_timer_dispatch_overflow_count = 0U;

typedef struct
{
    rtos_timer_callback_t cb;
    void                 *handle;
    void                 *param;
} timer_dispatch_t;

static timer_dispatch_t   s_ring[RTOS_CONFIG_TIMER_TASK_QUEUE_LENGTH];
static volatile uint32_t  s_write_idx = 0U;
static volatile uint32_t  s_read_idx  = 0U;

static rtos_semaphore_t   s_wake_sem;
static rtos_task_handle_t s_timer_task = NULL;
static volatile bool      s_initialized = false;

static void timer_task_body(void *arg)
{
    (void) arg;
    for (;;)
    {
        /* Wait for the ISR to signal that something is in the ring. */
        rtos_semaphore_wait(&s_wake_sem, RTOS_SEM_MAX_WAIT);

        /* Drain everything pending; one signal may cover multiple enqueues. */
        while (s_read_idx != s_write_idx)
        {
            /* Pair with the dmb at the producer; consumer must observe the
             * payload write before the index advance it relied on. */
            __asm volatile("dmb" ::: "memory");
            timer_dispatch_t d = s_ring[s_read_idx];
            s_read_idx         = (s_read_idx + 1U) % RTOS_CONFIG_TIMER_TASK_QUEUE_LENGTH;

            if (d.cb != NULL)
            {
                d.cb(d.handle, d.param);
            }
        }
    }
}

void rtos_timer_dispatch_from_isr(rtos_timer_t *timer)
{
    if (timer == NULL || timer->callback == NULL)
    {
        return;
    }

    /* Legacy / pre-init path: caller wants ISR-context dispatch. */
    if (!s_initialized)
    {
        timer->callback((void *) timer, timer->parameter);
        return;
    }

    uint32_t w      = s_write_idx;
    uint32_t next_w = (w + 1U) % RTOS_CONFIG_TIMER_TASK_QUEUE_LENGTH;
    if (next_w == s_read_idx)
    {
        g_timer_dispatch_overflow_count++;
        return;
    }

    s_ring[w].cb     = timer->callback;
    s_ring[w].handle = timer;
    s_ring[w].param  = timer->parameter;
    /* Payload must be visible to the consumer before the index advance. */
    __asm volatile("dmb" ::: "memory");
    s_write_idx = next_w;

    rtos_semaphore_signal_from_isr(&s_wake_sem);
}

rtos_status_t rtos_timer_task_init(void)
{
    if (s_initialized)
    {
        return RTOS_ERROR_INVALID_STATE;
    }

    if (rtos_semaphore_init(&s_wake_sem, 0U, 0U) != RTOS_SEM_OK)
    {
        return RTOS_ERROR_GENERAL;
    }

    s_initialized = true;

    rtos_status_t st = rtos_task_create(timer_task_body, "TimerSvc",
                                        RTOS_CONFIG_TIMER_TASK_STACK_SIZE, NULL,
                                        RTOS_CONFIG_TIMER_TASK_PRIORITY,
                                        &s_timer_task);
    if (st != RTOS_SUCCESS)
    {
        s_initialized = false;
        s_timer_task  = NULL;
        return st;
    }

    KLOGI("Timer", "TimerTaskInit prio=%u qlen=%u",
          (uint32_t) RTOS_CONFIG_TIMER_TASK_PRIORITY,
          (uint32_t) RTOS_CONFIG_TIMER_TASK_QUEUE_LENGTH);
    return RTOS_SUCCESS;
}
