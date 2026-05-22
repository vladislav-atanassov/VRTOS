#include "task.h"

#include "KARTOS.h"
#include "kernel_priv.h"
#include "klog.h"
#include "memory.h"
#include "mutex.h"
#include "port_common.h"
#include "rtos_assert.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task_priv.h"
#include "utils.h"

#include <stdint.h>
#include <string.h>

rtos_tcb_t g_task_pool[RTOS_MAX_TASKS] = {0};
uint8_t    g_task_count                = 0;

/* Stack memory waiting to be reclaimed on behalf of a self-deleting task.
 * The self-deleting task can't free its own stack while running on it (PendSV
 * would clobber the freed memory during the context switch). Instead it
 * deposits the stack base here; the idle task drains it on its next pass.
 * If a second self-delete happens before the idle task runs, that stack leaks
 * — acceptable trade-off for the simplicity of a single-slot deferral. */
static void *volatile g_pending_stack_free = NULL;

static rtos_tcb_t   *rtos_task_allocate_tcb(void);
static uint32_t     *rtos_task_allocate_stack(rtos_stack_size_t size);
static rtos_status_t task_create_common(rtos_task_function_t task_function, const char *name, uint32_t *stack_base,
                                        rtos_stack_size_t stack_size, void *parameter, rtos_priority_t priority,
                                        bool stack_is_static, rtos_task_handle_t *task_handle);

rtos_status_t rtos_task_init_system(void)
{
    memset(g_task_pool, 0, sizeof(g_task_pool));
    g_task_count = 0;
    KLOGD("Task", "TaskSysInit tasks=%u heap=%u", RTOS_MAX_TASKS, RTOS_TOTAL_HEAP_SIZE);

    return RTOS_SUCCESS;
}

/* Shared TCB-population path used by both dynamic and static create variants.
 *
 * stack_base points at the LOW end of the stack buffer; stack_size is the
 * usable byte count starting at that address. The caller is responsible for
 * ensuring stack_base + stack_size fits the buffer it owns/allocated, and
 * that stack_base is 8-byte aligned.
 *
 * Takes the critical section internally. On failure, the TCB slot is
 * released back to the pool and the caller is responsible for freeing
 * heap-allocated stack memory (if any). */
static rtos_status_t task_create_common(rtos_task_function_t task_function, const char *name, uint32_t *stack_base,
                                        rtos_stack_size_t stack_size, void *parameter, rtos_priority_t priority,
                                        bool stack_is_static, rtos_task_handle_t *task_handle)
{
    rtos_port_enter_critical();

    rtos_tcb_t *new_task = rtos_task_allocate_tcb();
    if (new_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE("Task", "AllocFail");
        return RTOS_ERROR_NO_MEMORY;
    }

#if RTOS_ENABLE_STACK_OVERFLOW_CHECK
    *stack_base = PORT_STACK_CANARY_VALUE;
#endif

    /* Cortex-M stack grows downward; the initial stack pointer must sit at
     * the highest usable address. Round down to keep 8-byte alignment. */
    uint8_t  *raw_top   = (uint8_t *) stack_base + stack_size;
    uint32_t *stack_top = (uint32_t *) ALIGN8_DOWN_VALUE((uintptr_t) raw_top);

    new_task->task_id              = (uint8_t) (new_task - g_task_pool);
    new_task->name                 = name;
    new_task->task_function        = task_function;
    new_task->parameter            = parameter;
    new_task->state                = RTOS_TASK_STATE_READY;
    new_task->priority             = priority;
    new_task->base_priority        = priority;
    new_task->stack_base           = stack_base;
    new_task->stack_size           = stack_size;
    new_task->stack_top            = stack_top;
    new_task->stack_is_static      = stack_is_static ? 1U : 0U;
    new_task->delay_until          = 0;
    new_task->time_slice_remaining = RTOS_TIME_SLICE_TICKS;

    new_task->next            = NULL;
    new_task->prev            = NULL;
    new_task->next_waiting    = NULL;
    new_task->blocked_on      = NULL;
    new_task->blocked_on_type = RTOS_SYNC_TYPE_NONE;
    new_task->held_mutex_list = NULL;

    new_task->stack_pointer = rtos_port_init_task_stack(new_task->stack_top, task_function, parameter);
    rtos_scheduler_add_to_ready_list(new_task);

    g_task_count++;
    *task_handle = new_task;

    rtos_port_exit_critical();

    KLOGI("Task", "TaskCreate id=%u prio=%u %s", new_task->task_id, priority, stack_is_static ? "(static)" : "");
    return RTOS_SUCCESS;
}

rtos_status_t rtos_task_create(rtos_task_function_t task_function, const char *name, rtos_stack_size_t stack_size,
                               void *parameter, rtos_priority_t priority, rtos_task_handle_t *task_handle)
{
    RTOS_ASSERT_PARAM(task_function != NULL);
    RTOS_ASSERT_PARAM(task_handle != NULL);
    if (task_function == NULL || task_handle == NULL)
    {
        KLOGE("Task", "InvalidParam fn=%u hdl=%u", (uint32_t) task_function, (uint32_t) task_handle);
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        KLOGE("Task", "InvalidParam prio=%u max=%u", priority, RTOS_MAX_TASK_PRIORITIES - 1);
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (g_task_count >= RTOS_MAX_TASKS)
    {
        KLOGE("Task", "AllocFail max=%u count=%u", RTOS_MAX_TASKS, g_task_count);
        return RTOS_ERROR_NO_MEMORY;
    }

    if (stack_size == 0)
    {
        stack_size = RTOS_DEFAULT_TASK_STACK_SIZE;
    }

    if (stack_size < RTOS_MINIMUM_TASK_STACK_SIZE)
    {
        stack_size = RTOS_MINIMUM_TASK_STACK_SIZE;
    }

    ALIGN8_UP(stack_size);

    uint32_t *stack_memory = rtos_task_allocate_stack(stack_size);
    if (stack_memory == NULL)
    {
        return RTOS_ERROR_NO_MEMORY;
    }

    rtos_status_t status =
        task_create_common(task_function, name, stack_memory, stack_size, parameter, priority, false, task_handle);
    if (status != RTOS_SUCCESS)
    {
        rtos_free(stack_memory);
    }
    return status;
}

rtos_status_t rtos_task_create_static(rtos_task_function_t task_function, const char *name, uint32_t *stack_buffer,
                                      rtos_stack_size_t stack_size, void *parameter, rtos_priority_t priority,
                                      rtos_task_handle_t *task_handle)
{
    RTOS_ASSERT_PARAM(task_function != NULL);
    RTOS_ASSERT_PARAM(task_handle != NULL);
    RTOS_ASSERT_PARAM(stack_buffer != NULL);
    if (task_function == NULL || task_handle == NULL || stack_buffer == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (stack_size < RTOS_MINIMUM_TASK_STACK_SIZE)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (((uintptr_t) stack_buffer & 7U) != 0U)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (g_task_count >= RTOS_MAX_TASKS)
    {
        return RTOS_ERROR_NO_MEMORY;
    }

    return task_create_common(task_function, name, stack_buffer, stack_size, parameter, priority, true, task_handle);
}

static rtos_tcb_t *g_idle_task_cache = NULL;

rtos_tcb_t *rtos_task_get_idle_task(void)
{
    if (g_idle_task_cache != NULL)
    {
        return g_idle_task_cache;
    }

    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        if (g_task_pool[i].task_function != NULL && g_task_pool[i].priority == RTOS_IDLE_TASK_PRIORITY)
        {
            g_idle_task_cache = &g_task_pool[i];
            return g_idle_task_cache;
        }
    }
    return NULL;
}

rtos_task_handle_t rtos_task_get_current(void)
{
    return g_kernel.current_task;
}

/* Returns 0xFF when no task is running (pre-scheduler); used by KLog for cpu_context */
uint8_t rtos_get_current_task_id(void)
{
    if (g_kernel.current_task != NULL)
    {
        return g_kernel.current_task->task_id;
    }
    return 0xFF;
}

const char *rtos_task_get_name(rtos_task_id_t task_id)
{
    if (task_id < g_task_count && g_task_pool[task_id].name != NULL)
    {
        return g_task_pool[task_id].name;
    }
    return "?";
}

const char *rtos_get_current_task_name(void)
{
    return (g_kernel.current_task != NULL && g_kernel.current_task->name != NULL) ? g_kernel.current_task->name
                                                                                  : "none";
}

rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task_handle)
{
    if (task_handle == NULL)
    {
        return RTOS_TASK_STATE_DELETED;
    }
    return task_handle->state;
}

rtos_priority_t rtos_task_get_priority(rtos_task_handle_t task_handle)
{
    if (task_handle == NULL)
    {
        return 0;
    }
    return task_handle->priority;
}

rtos_task_handle_t rtos_task_get_by_id(rtos_task_id_t task_id)
{
    if (task_id >= RTOS_MAX_TASKS)
    {
        return NULL;
    }

    rtos_tcb_t *task = &g_task_pool[task_id];
    if (task->task_function == NULL)
    {
        return NULL;
    }

    return task;
}

rtos_task_handle_t rtos_task_get_by_name(const char *name)
{
    if (name == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        rtos_tcb_t *task = &g_task_pool[i];
        if (task->task_function != NULL && task->name != NULL)
        {
            if (strcmp(task->name, name) == 0)
            {
                return task;
            }
        }
    }

    return NULL;
}

uint8_t rtos_task_get_count(void)
{
    return g_task_count;
}

void rtos_task_debug_print_all(void)
{
    KLOGD("Task", "TaskDebug count=%u max=%u", g_task_count, RTOS_MAX_TASKS);

    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        rtos_tcb_t *task = &g_task_pool[i];
        if (task->task_function != NULL)
        {
            KLOGD("Task", "TaskDebug id=%u state=%u", task->task_id, (uint32_t) task->state);
        }
    }
}

static inline void handle_idle_sleep(void)
{
#if (RTOS_CONFIG_USE_TICKLESS_IDLE == 1)
    uint32_t expected_idle_ticks = rtos_scheduler_get_expected_idle_ticks();

    if (expected_idle_ticks >= RTOS_CONFIG_EXPECTED_IDLE_TIME_BEFORE_SLEEP)
    {
        rtos_port_suppress_ticks_and_sleep(expected_idle_ticks);
        return;
    }
#endif
    __asm volatile("wfi");
}

static inline void handle_idle_yield(void)
{
    if (rtos_scheduler_get_type() == RTOS_SCHEDULER_COOPERATIVE)
    {
        rtos_yield();
    }
}

__attribute__((__noreturn__)) void rtos_task_idle_function(void *param)
{
    (void) param;

    KLOGD("Task", "IdleStart");

    while (1)
    {
        /* Reclaim stack memory deferred by a self-deleting task. */
        if (g_pending_stack_free != NULL)
        {
            rtos_port_enter_critical();
            void *to_free        = g_pending_stack_free;
            g_pending_stack_free = NULL;
            rtos_port_exit_critical();
            if (to_free != NULL)
            {
                rtos_free(to_free);
            }
        }

        handle_idle_sleep();
        handle_idle_yield();
    }
}

bool rtos_task_check_stack(rtos_task_handle_t task_handle)
{
#if RTOS_ENABLE_STACK_OVERFLOW_CHECK
    if (task_handle != NULL)
    {
        if (task_handle->stack_base != NULL)
        {
            if (*task_handle->stack_base != PORT_STACK_CANARY_VALUE)
            {
                KLOGE("Task", "StackOverflow id=%u", task_handle->task_id);
                return true;
            }
        }
        return false;
    }

    bool overflow_found = false;
    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        rtos_tcb_t *task = &g_task_pool[i];
        if (task->task_function != NULL && task->stack_base != NULL)
        {
            if (*task->stack_base != PORT_STACK_CANARY_VALUE)
            {
                KLOGE("Task", "StackOverflow id=%u", task->task_id);
                overflow_found = true;
            }
        }
    }
    return overflow_found;
#else
    (void) task_handle;
    return false;
#endif /* RTOS_ENABLE_STACK_OVERFLOW_CHECK */
}

rtos_status_t rtos_task_suspend(rtos_task_handle_t task_handle)
{
    rtos_port_enter_critical();

    rtos_tcb_t *task = (task_handle != NULL) ? task_handle : g_kernel.current_task;

    if (task == NULL)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (!rtos_kernel_validate_transition(task, RTOS_TASK_STATE_SUSPENDED))
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_STATE;
    }

    if (task->state == RTOS_TASK_STATE_READY)
    {
        rtos_scheduler_remove_from_ready_list(task);
    }

    if (task->state == RTOS_TASK_STATE_BLOCKED)
    {
        rtos_scheduler_remove_from_delayed_list(task);
    }

    task->state = RTOS_TASK_STATE_SUSPENDED;

    KLOGD("Task", "TaskSuspend id=%u", task->task_id);

    if (task == g_kernel.current_task)
    {
        rtos_port_exit_critical();
        rtos_yield();
        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();
    return RTOS_SUCCESS;
}

rtos_status_t rtos_task_resume(rtos_task_handle_t task_handle)
{
    RTOS_ASSERT_PARAM(task_handle != NULL);
    if (task_handle == NULL)
    {
        return RTOS_ERROR_INVALID_PARAM;
    }

    rtos_port_enter_critical();

    if (task_handle->state != RTOS_TASK_STATE_SUSPENDED)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_STATE;
    }

    if (task_handle->blocked_on != NULL)
    {
        task_handle->state = RTOS_TASK_STATE_BLOCKED;
        rtos_port_exit_critical();
        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();

    rtos_kernel_task_ready(task_handle);

    return RTOS_SUCCESS;
}

rtos_status_t rtos_task_delete(rtos_task_handle_t task_handle)
{
    rtos_port_enter_critical();

    rtos_tcb_t *task = (task_handle != NULL) ? task_handle : g_kernel.current_task;

    if (task == NULL)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (task->state == RTOS_TASK_STATE_DELETED)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_STATE;
    }

    if (task->priority == RTOS_IDLE_TASK_PRIORITY)
    {
        rtos_port_exit_critical();
        return RTOS_ERROR_INVALID_STATE;
    }

    if (task->state == RTOS_TASK_STATE_READY)
    {
        rtos_scheduler_remove_from_ready_list(task);
    }
    else if (task->state == RTOS_TASK_STATE_BLOCKED)
    {
        rtos_scheduler_remove_from_delayed_list(task);

        if (task->blocked_on != NULL)
        {
            switch (task->blocked_on_type)
            {
                case RTOS_SYNC_TYPE_MUTEX:
                    rtos_mutex_remove_task_from_wait(task->blocked_on, task);
                    break;
                case RTOS_SYNC_TYPE_SEMAPHORE:
                    rtos_sem_remove_task_from_wait(task->blocked_on, task);
                    break;
                case RTOS_SYNC_TYPE_QUEUE:
                    rtos_queue_remove_task_from_wait(task->blocked_on, task);
                    break;
                case RTOS_SYNC_TYPE_NOTIFICATION:
                    /* Self-pointer sentinel — no external list, cleared below */
                    break;
                case RTOS_SYNC_TYPE_EVENT_GROUP:
                    rtos_event_group_remove_task_from_wait(task->blocked_on, task);
                    break;
                default:
                    break;
            }
        }
    }
    /* RTOS_TASK_STATE_RUNNING / SUSPENDED: not in any list */

    task->next_waiting    = NULL;
    task->blocked_on      = NULL;
    task->blocked_on_type = RTOS_SYNC_TYPE_NONE;

    /* Force-release all mutexes held by this task to prevent permanent deadlocks.
     * For each mutex, transfer ownership to the highest-priority waiter (if any) or mark it free. */
    while (task->held_mutex_list != NULL)
    {
        rtos_mutex_t *m       = task->held_mutex_list;
        task->held_mutex_list = m->next_held;
        m->next_held          = NULL;

        rtos_tcb_t *waiter = m->waiting_list;
        if (waiter != NULL)
        {
            m->waiting_list         = waiter->next_waiting;
            waiter->next_waiting    = NULL;
            waiter->blocked_on      = NULL;
            waiter->blocked_on_type = RTOS_SYNC_TYPE_NONE;

            m->owner                = waiter;
            m->lock_count           = 1;
            m->next_held            = waiter->held_mutex_list;
            waiter->held_mutex_list = m;

            rtos_scheduler_remove_from_delayed_list(waiter);
            waiter->state = RTOS_TASK_STATE_READY;
            rtos_scheduler_add_to_ready_list(waiter);
        }
        else
        {
            m->owner      = NULL;
            m->lock_count = 0;
        }
    }

    task->state         = RTOS_TASK_STATE_DELETED;
    task->task_function = NULL;

    if (g_task_count > 0)
    {
        g_task_count--;
    }

    KLOGI("Task", "TaskDelete id=%u", task->task_id);

    bool  is_self            = (task == g_kernel.current_task);
    bool  reclaim_stack      = (task->stack_is_static == 0U) && (task->stack_base != NULL);
    void *stack_to_free      = reclaim_stack ? (void *) task->stack_base : NULL;
    task->stack_base         = NULL;

    if (is_self)
    {
        g_kernel.current_task = NULL;
        if (stack_to_free != NULL)
        {
            /* Defer until idle: we're still executing on this stack. */
            if (g_pending_stack_free == NULL)
            {
                g_pending_stack_free = stack_to_free;
                stack_to_free        = NULL;
            }
            else
            {
                /* Idle hasn't drained the previous one yet — leak this stack
                 * rather than risk losing the older deferred free. */
                KLOGW("Task", "StackLeak id=%u (pending slot busy)", task->task_id);
                stack_to_free = NULL;
            }
        }
    }

    rtos_port_exit_critical();

    if (stack_to_free != NULL)
    {
        rtos_free(stack_to_free);
    }

    if (is_self)
    {
        rtos_yield(); /* Never returns */
    }

    return RTOS_SUCCESS;
}

static rtos_tcb_t *rtos_task_allocate_tcb(void)
{
    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        if (g_task_pool[i].task_function == NULL)
        {
            return &g_task_pool[i];
        }
    }
    return NULL;
}

/* Returns the base (low-address) of a newly heap-allocated stack block,
 * routed to the HIGH heap so that long-lived task stacks cluster apart from
 * short-lived control-block allocations. task_create_common derives the
 * stack-top from this base. */
static uint32_t *rtos_task_allocate_stack(rtos_stack_size_t size)
{
    void *stack_block = rtos_malloc_from(RTOS_HEAP_HIGH, size);

    if (stack_block == NULL)
    {
        KLOGE("Task", "StackAllocFail size=%u", size);
        return NULL;
    }

    return (uint32_t *) stack_block;
}
