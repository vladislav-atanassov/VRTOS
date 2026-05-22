#include "KARTOS.h"
#include "assert.h"
#include "kernel_priv.h"
#include "klog.h"
#include "memory.h"
#include "profiling.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task.h"
#include "task_priv.h"
#include "test_hooks_priv.h"
#include "timer.h"

rtos_kernel_cb_t g_kernel = {.state               = RTOS_KERNEL_STATE_INACTIVE,
                             .tick_count          = 0,
                             .current_task        = NULL,
                             .next_task           = NULL,
                             .scheduler_suspended = 0};

/* Scheduler choice for this binary, supplied by a per-variant TU generated
 * by add_kartos_variant() (cmake/kartos_add_variant.cmake). Each ELF gets
 * exactly one definition; missing it is a link-time error. */
extern const rtos_scheduler_type_t kernel_scheduler_choice;

rtos_status_t rtos_init(void)
{
    rtos_status_t status;

    if (g_kernel.state != RTOS_KERNEL_STATE_INACTIVE)
    {
        return RTOS_ERROR_INVALID_STATE;
    }

#if RTOS_PROFILING_SYSTEM_ENABLED
    rtos_profiling_init();
#endif

    /* Initialize kernel logger after profiling (uses DWT for timestamps) */
    klog_init();

    g_kernel.state               = RTOS_KERNEL_STATE_INACTIVE;
    g_kernel.tick_count          = 0;
    g_kernel.current_task        = NULL;
    g_kernel.next_task           = NULL;
    g_kernel.scheduler_suspended = 0;

    rtos_memory_init();

    status = rtos_task_init_system();
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    status = rtos_scheduler_init(kernel_scheduler_choice);
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    status = rtos_port_init();
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    rtos_task_handle_t idle_task;
    status = rtos_task_create(rtos_task_idle_function, "IDLE", 0, NULL, RTOS_IDLE_TASK_PRIORITY, &idle_task);
    if (status != RTOS_SUCCESS)
    {
        return status;
    }

    g_kernel.state = RTOS_KERNEL_STATE_READY;
    return RTOS_SUCCESS;
}

rtos_status_t rtos_start_scheduler(void)
{
    if (g_kernel.state != RTOS_KERNEL_STATE_READY)
    {
        return RTOS_ERROR_INVALID_STATE;
    }

    g_kernel.next_task = rtos_scheduler_get_next_task();
    if (g_kernel.next_task == NULL)
    {
        return RTOS_ERROR_GENERAL;
    }

    g_kernel.current_task = g_kernel.next_task;

    if (g_kernel.current_task->state != RTOS_TASK_STATE_READY)
    {
        return RTOS_ERROR_INVALID_STATE;
    }

    g_kernel.current_task->state = RTOS_TASK_STATE_RUNNING;
    rtos_scheduler_remove_from_ready_list(g_kernel.current_task);

    g_kernel.state = RTOS_KERNEL_STATE_RUNNING;

    rtos_port_start_first_task();

    /* Should never return */
    return RTOS_ERROR_GENERAL;
}

rtos_tick_t rtos_get_tick_count(void)
{
    return g_kernel.tick_count;
}

void rtos_delay_ticks(rtos_tick_t ticks)
{
    if (ticks == 0)
        return;

    rtos_port_enter_critical();

    if (g_kernel.current_task == NULL)
    {
        KLOGE("Kernel", "NoCurrentTask");
        rtos_port_exit_critical();
        return;
    }

    g_kernel.current_task->state = RTOS_TASK_STATE_BLOCKED;
    rtos_scheduler_add_to_delayed_list(g_kernel.current_task, ticks);

    rtos_port_exit_critical();
    rtos_yield();
}

void rtos_delay_ms(uint32_t ms)
{
    rtos_tick_t ticks = ms / RTOS_TICK_PERIOD_MS;
    if (ticks == 0)
    {
        ticks = 1;
    }
    rtos_delay_ticks(ticks);
}

void rtos_delay_until(rtos_tick_t *const prev_wake_time, rtos_tick_t time_increment)
{
    if (prev_wake_time == NULL || time_increment == 0)
        return;

    rtos_port_enter_critical();

    rtos_tick_t current_time = g_kernel.tick_count;
    /* Use signed comparison for tick wraparound safety */
    int32_t elapsed      = (int32_t) (current_time - *prev_wake_time);
    bool    should_delay = false;

    if (elapsed >= 0 && (uint32_t) elapsed < time_increment)
    {
        rtos_tick_t ticks_to_delay = time_increment - (uint32_t) elapsed;

        if (g_kernel.current_task == NULL)
        {
            KLOGE("Kernel", "NoCurrentTask");
        }
        else
        {
            g_kernel.current_task->state = RTOS_TASK_STATE_BLOCKED;
            rtos_scheduler_add_to_delayed_list(g_kernel.current_task, ticks_to_delay);
            should_delay = true;
        }
    }

    *prev_wake_time = *prev_wake_time + time_increment;

    rtos_port_exit_critical();

    if (should_delay)
    {
        rtos_yield();
    }
}

void rtos_yield(void)
{
    KLOGT("Kernel", "Yield from=%s", (uint32_t) rtos_get_current_task_name());
    rtos_port_yield();
}

void rtos_kernel_tick_handler(void)
{
    RTOS_SYS_PROFILE_START(tick);
    g_kernel.tick_count++;

    RTOS_TEST_HOOK_FIRE(RTOS_HOOK_TICK, {
        _ctx_.u.tick_val = g_kernel.tick_count;
    });

    rtos_timer_tick();

    if (g_kernel.state == RTOS_KERNEL_STATE_RUNNING && g_scheduler_instance.initialized)
    {
        rtos_port_enter_critical();

        rtos_scheduler_update_delayed_tasks();
        rtos_task_handle_t next_task = rtos_scheduler_get_next_task();

        if (rtos_scheduler_should_preempt(next_task))
        {
            KLOGT("Kernel", "TickPreempt next=%s tick=%u", (uint32_t) (next_task ? next_task->name : "none"),
                  g_kernel.tick_count);
            rtos_port_exit_critical();
            RTOS_SYS_PROFILE_END(tick, &g_prof_tick);
            rtos_yield();
            return;
        }

        rtos_port_exit_critical();
    }
    RTOS_SYS_PROFILE_END(tick, &g_prof_tick);
}

void rtos_kernel_step_tick(uint32_t ticks_slept)
{
    if (ticks_slept == 0)
    {
        return;
    }

    /* Fast-forward the OS tick count */
    rtos_port_enter_critical();
    g_kernel.tick_count += ticks_slept;
    rtos_port_exit_critical();

    rtos_timer_tick();

    if (g_kernel.state == RTOS_KERNEL_STATE_RUNNING && g_scheduler_instance.initialized)
    {
        rtos_port_enter_critical();

        rtos_scheduler_update_delayed_tasks();

        rtos_task_handle_t next_task = rtos_scheduler_get_next_task();
        if (rtos_scheduler_should_preempt(next_task))
        {
            rtos_port_exit_critical();
            rtos_yield();
            return;
        }

        rtos_port_exit_critical();
    }
}

void rtos_kernel_switch_context(void)
{
    if (g_kernel.scheduler_suspended > 0)
    {
        return;
    }

    RTOS_SYS_PROFILE_START(ctx_switch);

    rtos_port_enter_critical();

    const char *from_name = rtos_get_current_task_name();

    if (g_kernel.current_task != NULL)
    {
        if (g_kernel.current_task->state == RTOS_TASK_STATE_RUNNING)
        {
            g_kernel.current_task->state = RTOS_TASK_STATE_READY;
            rtos_scheduler_add_to_ready_list(g_kernel.current_task);
        }

        rtos_scheduler_task_completed(g_kernel.current_task);
    }

    g_kernel.next_task = rtos_scheduler_get_next_task();

    if (g_kernel.next_task != NULL)
    {
        rtos_scheduler_remove_from_ready_list(g_kernel.next_task);

#if RTOS_PROFILING_SYSTEM_ENABLED
        /* Only track scheduling latency for tasks above idle priority.
         * Priority-0 tasks (e.g. log flush) naturally wait for all others,
         * producing high but expected latency that skews the aggregate. */
        if (g_kernel.next_task->ready_timestamp != 0)
        {
            uint32_t latency = rtos_profiling_get_cycles() - g_kernel.next_task->ready_timestamp;
            rtos_profiling_record(&g_prof_scheduling_latency, latency);
            g_kernel.next_task->ready_timestamp = 0;
        }

        /* Full PendSV duration captured in assembly */
        if (g_pendsv_cycles != 0)
        {
            rtos_profiling_record(&g_prof_pendsv_full, g_pendsv_cycles);
            g_pendsv_cycles = 0;
        }
#endif

        g_kernel.next_task->state = RTOS_TASK_STATE_RUNNING;
        g_kernel.current_task     = g_kernel.next_task;
    }
    else
    {
        g_kernel.current_task = rtos_task_get_idle_task();
        if (g_kernel.current_task != NULL)
        {
            rtos_scheduler_remove_from_ready_list(g_kernel.current_task);
            g_kernel.current_task->state = RTOS_TASK_STATE_RUNNING;
        }
        else
        {
            KLOGE("Kernel", "NoIdleTask");
            while (1)
            {
                __asm volatile("wfi");
            }
        }
    }

    KLOGT("Kernel", "CtxSwitch from=%s to=%s", (uint32_t) from_name, (uint32_t) rtos_get_current_task_name());

    rtos_port_exit_critical();

    RTOS_SYS_PROFILE_END(ctx_switch, &g_prof_context_switch);
}

/* Valid state transitions:
 *   READY     -> RUNNING, SUSPENDED, DELETED
 *   RUNNING   -> READY, BLOCKED, SUSPENDED, DELETED
 *   BLOCKED   -> READY, SUSPENDED, DELETED
 *   SUSPENDED -> READY, DELETED
 *   DELETED   -> (none) */
bool rtos_kernel_validate_transition(rtos_task_handle_t task, rtos_task_state_t new_state)
{
    if (task == NULL)
    {
        return false;
    }

    rtos_task_state_t old_state = task->state;
    bool              valid     = false;

    switch (old_state)
    {
        case RTOS_TASK_STATE_READY:
            valid = (new_state == RTOS_TASK_STATE_RUNNING || new_state == RTOS_TASK_STATE_SUSPENDED ||
                     new_state == RTOS_TASK_STATE_DELETED);
            break;

        case RTOS_TASK_STATE_RUNNING:
            valid = (new_state == RTOS_TASK_STATE_READY || new_state == RTOS_TASK_STATE_BLOCKED ||
                     new_state == RTOS_TASK_STATE_SUSPENDED || new_state == RTOS_TASK_STATE_DELETED);
            break;

        case RTOS_TASK_STATE_BLOCKED:
            valid = (new_state == RTOS_TASK_STATE_READY || new_state == RTOS_TASK_STATE_SUSPENDED ||
                     new_state == RTOS_TASK_STATE_DELETED);
            break;

        case RTOS_TASK_STATE_SUSPENDED:
            valid = (new_state == RTOS_TASK_STATE_READY || new_state == RTOS_TASK_STATE_DELETED);
            break;

        case RTOS_TASK_STATE_DELETED:
            valid = false;
            break;

        default:
            valid = false;
            break;
    }

    if (!valid)
    {
        KLOGE("Kernel", "InvalidTransition from=%u to=%u", (uint32_t) old_state, (uint32_t) new_state);
    }

    return valid;
}

void rtos_kernel_task_ready(rtos_task_handle_t task)
{
    if (task == NULL)
    {
        return;
    }

    rtos_port_enter_critical();

    if (!rtos_kernel_validate_transition(task, RTOS_TASK_STATE_READY))
    {
        rtos_port_exit_critical();
        return;
    }

#if RTOS_TEST_HOOKS_ENABLED
    rtos_task_state_t _old_state_ready = task->state;
#endif
    task->state = RTOS_TASK_STATE_READY;

    RTOS_TEST_HOOK_FIRE(RTOS_HOOK_TASK_STATE, {
        _ctx_.u.task_state.task      = task;
        _ctx_.u.task_state.old_state = (uint8_t) _old_state_ready;
        _ctx_.u.task_state.new_state = (uint8_t) RTOS_TASK_STATE_READY;
    });

#if RTOS_PROFILING_SYSTEM_ENABLED
    /* Only track scheduling latency for tasks above idle priority.
     * Priority-0 tasks (e.g. log flush) naturally wait for all others,
     * producing high but expected latency that skews the aggregate. */
    if (task->priority > 0)
    {
        task->ready_timestamp = rtos_profiling_get_cycles();
    }
#endif

    rtos_scheduler_add_to_ready_list(task);

    KLOGT("Kernel", "TaskReady name=%s prio=%u", (uint32_t) task->name, task->priority);

    if (g_kernel.state == RTOS_KERNEL_STATE_RUNNING)
    {
        if (rtos_scheduler_should_preempt(task))
        {
            rtos_port_exit_critical();
            rtos_yield();
            return;
        }
    }

    rtos_port_exit_critical();
}

/* delay_ticks = 0 means indefinite block (no delayed-list entry) */
void rtos_kernel_task_block(rtos_task_handle_t task, rtos_tick_t delay_ticks)
{
    if (task == NULL)
    {
        return;
    }

    rtos_port_enter_critical();

    if (!rtos_kernel_validate_transition(task, RTOS_TASK_STATE_BLOCKED))
    {
        rtos_port_exit_critical();
        return;
    }

    /* Only remove from ready list if task was actually in READY state.
     * RUNNING tasks are not in the ready list. */
    if (task->state == RTOS_TASK_STATE_READY)
    {
        rtos_scheduler_remove_from_ready_list(task);
    }

#if RTOS_TEST_HOOKS_ENABLED
    rtos_task_state_t _old_state_block = task->state;
#endif
    task->state = RTOS_TASK_STATE_BLOCKED;

    RTOS_TEST_HOOK_FIRE(RTOS_HOOK_TASK_STATE, {
        _ctx_.u.task_state.task      = task;
        _ctx_.u.task_state.old_state = (uint8_t) _old_state_block;
        _ctx_.u.task_state.new_state = (uint8_t) RTOS_TASK_STATE_BLOCKED;
    });

    if (delay_ticks > 0)
    {
        rtos_scheduler_add_to_delayed_list(task, delay_ticks);
    }

    KLOGT("Kernel", "TaskBlock name=%s delay=%u", (uint32_t) task->name, (uint32_t) delay_ticks);

    if (task == g_kernel.current_task)
    {
        rtos_port_exit_critical();
        rtos_yield();
        return;
    }

    rtos_port_exit_critical();
}

void rtos_kernel_task_unblock(rtos_task_handle_t task)
{
    if (task == NULL || task->state != RTOS_TASK_STATE_BLOCKED)
    {
        return;
    }

    KLOGT("Kernel", "TaskUnblock name=%s", (uint32_t) task->name);

    /* rtos_kernel_task_ready() enters/exits its own critical section and may
     * call rtos_yield() on the preemption path.  Wrapping an additional
     * critical section here would keep BASEPRI elevated across the yield,
     * preventing PendSV from firing.  Delegate entirely. */
    rtos_scheduler_remove_from_delayed_list(task);
    rtos_kernel_task_ready(task);
}
