#include "task.h"

#include "VRTOS.h"
#include "kernel_priv.h"
#include "klog.h"
#include "memory.h"
#include "mutex.h"
#include "port_common.h"
#include "rtos_port.h"
#include "scheduler.h"
#include "task_priv.h"
#include "utils.h"

#include <string.h>

rtos_tcb_t g_task_pool[RTOS_MAX_TASKS] = {0};
uint8_t    g_task_count                = 0;

/* Static function prototypes */
static rtos_tcb_t *rtos_task_allocate_tcb(void);
static uint32_t   *rtos_task_allocate_stack(rtos_stack_size_t size);

/**
 * @brief Initialize the task management system
 */
rtos_status_t rtos_task_init_system(void)
{
    memset(g_task_pool, 0, sizeof(g_task_pool));
    g_task_count = 0;
    KLOGD(KEVT_TASK_CREATE, RTOS_MAX_TASKS, RTOS_TOTAL_HEAP_SIZE);

    return RTOS_SUCCESS;
}

/**
 * @brief Create a new task
 */
rtos_status_t rtos_task_create(rtos_task_function_t task_function, const char *name, rtos_stack_size_t stack_size,
                               void *parameter, rtos_priority_t priority, rtos_task_handle_t *task_handle)
{
    if (task_function == NULL || task_handle == NULL)
    {
        KLOGE(KEVT_INVALID_PARAM, (uint32_t) task_function, (uint32_t) task_handle);
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (priority >= RTOS_MAX_TASK_PRIORITIES)
    {
        KLOGE(KEVT_INVALID_PARAM, priority, RTOS_MAX_TASK_PRIORITIES - 1);
        return RTOS_ERROR_INVALID_PARAM;
    }

    if (g_task_count >= RTOS_MAX_TASKS)
    {
        KLOGE(KEVT_ALLOC_FAIL, RTOS_MAX_TASKS, g_task_count);
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

    rtos_port_enter_critical();

    rtos_tcb_t *new_task = rtos_task_allocate_tcb();
    if (new_task == NULL)
    {
        rtos_port_exit_critical();
        KLOGE(KEVT_ALLOC_FAIL, 0, 0);
        return RTOS_ERROR_NO_MEMORY;
    }

    uint32_t *stack_memory = rtos_task_allocate_stack(stack_size);
    if (stack_memory == NULL)
    {
        new_task->task_function = NULL;
        rtos_port_exit_critical();
        KLOGE(KEVT_STACK_ALLOC_FAIL, stack_size, 0);
        return RTOS_ERROR_NO_MEMORY;
    }

    /* Write stack canary at bottom of stack for overflow detection */
#if RTOS_ENABLE_STACK_OVERFLOW_CHECK
    *stack_memory = PORT_STACK_CANARY_VALUE;
#endif

    new_task->task_id              = g_task_count;
    new_task->name                 = name;
    new_task->task_function        = task_function;
    new_task->parameter            = parameter;
    new_task->state                = RTOS_TASK_STATE_READY;
    new_task->priority             = priority;
    new_task->base_priority        = priority; /* Store original priority for inheritance */
    new_task->stack_base           = stack_memory;
    new_task->stack_size           = stack_size;
    new_task->stack_top            = stack_memory;
    new_task->delay_until          = 0;
    new_task->time_slice_remaining = RTOS_TIME_SLICE_TICKS;

    new_task->next             = NULL;
    new_task->prev             = NULL;
    new_task->next_waiting     = NULL;
    new_task->blocked_on       = NULL;
    new_task->blocked_on_type  = RTOS_SYNC_TYPE_NONE;
    new_task->held_mutex_list  = NULL;

    new_task->stack_pointer = rtos_port_init_task_stack(new_task->stack_top, task_function, parameter);
    rtos_scheduler_add_to_ready_list(new_task);

    g_task_count++;
    *task_handle = new_task;

    rtos_port_exit_critical();

    KLOGI(KEVT_TASK_CREATE, new_task->task_id, priority);

    return RTOS_SUCCESS;
}

/**
 * @brief Get the idle task
 */
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

/**
 * @brief Get current running task
 */
rtos_task_handle_t rtos_task_get_current(void)
{
    return g_kernel.current_task;
}

/**
 * @brief Get the current running task's ID
 * @return Task ID or 0xFF if no task is running
 * @note Used by KLog to populate the cpu_context field
 */
uint8_t rtos_get_current_task_id(void)
{
    if (g_kernel.current_task != NULL)
    {
        return g_kernel.current_task->task_id;
    }
    return 0xFF; /* No task running (pre-scheduler) */
}

const char *rtos_task_get_name(rtos_task_id_t task_id)
{
    if (task_id < g_task_count && g_task_pool[task_id].name != NULL)
    {
        return g_task_pool[task_id].name;
    }
    return "?";
}

/**
 * @brief Get task state
 */
rtos_task_state_t rtos_task_get_state(rtos_task_handle_t task_handle)
{
    if (task_handle == NULL)
    {
        return RTOS_TASK_STATE_DELETED;
    }
    return task_handle->state;
}

/**
 * @brief Get task priority
 */
rtos_priority_t rtos_task_get_priority(rtos_task_handle_t task_handle)
{
    if (task_handle == NULL)
    {
        return 0;
    }
    return task_handle->priority;
}

/**
 * @brief Get task by ID
 */
rtos_task_handle_t rtos_task_get_by_id(rtos_task_id_t task_id)
{
    if (task_id >= RTOS_MAX_TASKS)
    {
        return NULL;
    }

    rtos_tcb_t *task = &g_task_pool[task_id];
    if (task->task_function == NULL)
    {
        return NULL; /* Task slot is empty */
    }

    return task;
}

/**
 * @brief Get task by name
 */
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

/**
 * @brief Get total number of created tasks
 */
uint8_t rtos_task_get_count(void)
{
    return g_task_count;
}

/**
 * @brief Print task information for debugging
 */
void rtos_task_debug_print_all(void)
{
    KLOGD(KEVT_TASK_CREATE, g_task_count, RTOS_MAX_TASKS);

    for (uint8_t i = 0; i < RTOS_MAX_TASKS; i++)
    {
        rtos_tcb_t *task = &g_task_pool[i];
        if (task->task_function != NULL)
        {
            KLOGD(KEVT_TASK_CREATE, task->task_id, (uint32_t) task->state);
        }
    }
}

/**
 * @brief Idle task function
 */
__attribute__((__noreturn__)) void rtos_task_idle_function(void *param)
{
    (void) param; /* Unused parameter */

    KLOGD(KEVT_TASK_IDLE_START, 0, 0);

    while (1)
    {
        __asm volatile("wfi"); /* Wait for interrupt */

#if (RTOS_SCHEDULER_TYPE == RTOS_SCHEDULER_COOPERATIVE)
        rtos_yield();
#endif
    }
}

/**
 * @brief Check if a task's stack has overflowed
 * @param task_handle Task to check (NULL = check all tasks)
 * @return true if overflow detected, false if stack OK
 */
bool rtos_task_check_stack(rtos_task_handle_t task_handle)
{
#if RTOS_ENABLE_STACK_OVERFLOW_CHECK
    if (task_handle != NULL)
    {
        if (task_handle->stack_base != NULL)
        {
            if (*task_handle->stack_base != PORT_STACK_CANARY_VALUE)
            {
                KLOGE(KEVT_STACK_OVERFLOW, task_handle->task_id, 0);
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
                KLOGE(KEVT_STACK_OVERFLOW, task->task_id, 0);
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

/**
 * @brief Suspend a task
 * @param task_handle Task to suspend (NULL = suspend current task)
 * @return RTOS_SUCCESS on success, error code otherwise
 */
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

    KLOGD(KEVT_TASK_SUSPEND, task->task_id, 0);

    if (task == g_kernel.current_task)
    {
        rtos_port_exit_critical();
        rtos_yield();
        return RTOS_SUCCESS;
    }

    rtos_port_exit_critical();
    return RTOS_SUCCESS;
}

/**
 * @brief Resume a suspended task
 * @param task_handle Task to resume
 * @return RTOS_SUCCESS on success, error code otherwise
 */
rtos_status_t rtos_task_resume(rtos_task_handle_t task_handle)
{
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

    KLOGD(KEVT_TASK_RESUME, task_handle->task_id, 0);

    rtos_port_exit_critical();

    rtos_kernel_task_ready(task_handle);

    return RTOS_SUCCESS;
}

/**
 * @brief Delete a task and remove it from all scheduler and sync-object lists.
 */
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

    /* S1: Force-release all mutexes held by this task to prevent permanent
     * deadlocks.  For each mutex, transfer ownership to the highest-priority
     * waiter (if any) or mark it free. */
    while (task->held_mutex_list != NULL)
    {
        rtos_mutex_t *m   = task->held_mutex_list;
        task->held_mutex_list = m->next_held;
        m->next_held          = NULL;

        /* Try to hand off to the highest-priority waiter */
        rtos_tcb_t *waiter = m->waiting_list;
        if (waiter != NULL)
        {
            /* Pop head (highest priority due to ordered insertion) */
            m->waiting_list       = waiter->next_waiting;
            waiter->next_waiting  = NULL;
            waiter->blocked_on    = NULL;
            waiter->blocked_on_type = RTOS_SYNC_TYPE_NONE;

            m->owner      = waiter;
            m->lock_count = 1;
            m->next_held  = waiter->held_mutex_list;
            waiter->held_mutex_list = m;

            /* Unblock the new owner (will be made READY) */
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

    task->state           = RTOS_TASK_STATE_DELETED;

    KLOGI(KEVT_TASK_DELETE, task->task_id, 0);

    bool is_self = (task == g_kernel.current_task);
    if (is_self)
    {
        g_kernel.current_task = NULL;
    }

    rtos_port_exit_critical();

    if (is_self)
    {
        rtos_yield(); /* Never returns */
    }

    return RTOS_SUCCESS;
}

/**
 * @brief Allocate a TCB from the pool
 */
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

/**
 * @brief Allocate stack memory
 */
static uint32_t *rtos_task_allocate_stack(rtos_stack_size_t size)
{
    void *stack_block = rtos_malloc(size);

    if (stack_block == NULL)
    {
        KLOGE(KEVT_STACK_ALLOC_FAIL, size, 0);
        return NULL;
    }

    /**
     * Stack grows down on Cortex-M.
     * Return the TOP of the stack block (highest address).
     * rtos_malloc returns 8-byte aligned start address.
     * size is assumed to be 8-byte aligned by rtos_malloc (if not, it aligns up).
     * We just add size to get top.
     */
    uint8_t *stack_top = (uint8_t *) stack_block + size;

    /* Ensure 8-byte alignment of top (should be naturally aligned) */
    return (uint32_t *) ALIGN8_DOWN_VALUE((uint32_t) stack_top);
}
