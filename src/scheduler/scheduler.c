#include "scheduler.h"

#include "cooperative.h"
#include "klog.h"
#include "preemptive_sp.h"
#include "profiling.h"
#include "round_robin.h"
#include "task_priv.h"

#include <string.h>

rtos_scheduler_instance_t g_scheduler_instance = {
    .vtable = NULL, .type = RTOS_SCHEDULER_TYPE, .private_data = NULL, .initialized = false};

/* Scheduler registry - add new schedulers here */
static const struct
{
    rtos_scheduler_type_t   type;
    const rtos_scheduler_t *vtable;
} g_scheduler_registry[] = {{RTOS_SCHEDULER_PREEMPTIVE_SP, &preemptive_sp_scheduler},
                            {RTOS_SCHEDULER_COOPERATIVE, &cooperative_scheduler},
                            {RTOS_SCHEDULER_ROUND_ROBIN, &round_robin_scheduler}};

#define SCHEDULER_REGISTRY_SIZE (sizeof(g_scheduler_registry) / sizeof(g_scheduler_registry[0]))

static const rtos_scheduler_t *rtos_scheduler_find_interface(rtos_scheduler_type_t scheduler_type);

/**
 * @brief Initialize the scheduler subsystem
 */
rtos_status_t rtos_scheduler_init(rtos_scheduler_type_t scheduler_type)
{
    if (g_scheduler_instance.initialized)
    {
        KLOGI("Scheduler", "SchedAlreadyInit type=%u", (uint32_t) scheduler_type);
        return RTOS_ERROR_INVALID_STATE;
    }

    const rtos_scheduler_t *interface = rtos_scheduler_find_interface(scheduler_type);
    if (interface == NULL)
    {
        KLOGE("Scheduler", "SchedInvalidType type=%u", (uint32_t) scheduler_type);
        return RTOS_ERROR_INVALID_PARAM;
    }

    g_scheduler_instance.vtable       = interface;
    g_scheduler_instance.type         = scheduler_type;
    g_scheduler_instance.private_data = NULL;
    g_scheduler_instance.initialized  = false; /* Will be set by interface init */

    rtos_status_t status = interface->init(&g_scheduler_instance);

    if (status == RTOS_SUCCESS)
    {
        g_scheduler_instance.initialized = true;
        KLOGI("Scheduler", "SchedInit type=%u", (uint32_t) scheduler_type);
    }
    else
    {
        g_scheduler_instance.vtable       = NULL;
        g_scheduler_instance.private_data = NULL;
        KLOGE("Scheduler", "SchedInitFail status=%u", (uint32_t) status);
    }

    return status;
}

/**
 * @brief Get the current scheduler type
 */
rtos_scheduler_type_t rtos_scheduler_get_type(void)
{
    return g_scheduler_instance.type;
}

/**
 * @brief Get the highest priority/earliest deadline ready task
 */
rtos_task_handle_t rtos_scheduler_get_next_task(void)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL)
    {
        KLOGE("Scheduler", "SchedNotInit");
        return NULL;
    }

    RTOS_SYS_PROFILE_START(scheduler);
    rtos_task_handle_t next = g_scheduler_instance.vtable->get_next_task(&g_scheduler_instance);
    RTOS_SYS_PROFILE_END(scheduler, &g_prof_scheduler);

    KLOGT("Scheduler", "GetNext picked=%s", (uint32_t) (next ? next->name : "none"));

    return next;
}

/**
 * @brief Check if scheduling decision needs to be made
 */
bool rtos_scheduler_should_preempt(rtos_task_handle_t new_task)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL)
    {
        return false;
    }

    bool preempt = g_scheduler_instance.vtable->should_preempt(&g_scheduler_instance, new_task);
    KLOGT("Scheduler", "ShouldPreempt new=%s result=%u",
          (uint32_t) (new_task ? new_task->name : "none"), (uint32_t) preempt);
    return preempt;
}

/**
 * @brief Handle task completion/yield
 */
void rtos_scheduler_task_completed(rtos_task_handle_t completed_task)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || completed_task == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->task_completed(&g_scheduler_instance, completed_task);
}

/**
 * @brief Add task to ready list via scheduler
 */
void rtos_scheduler_add_to_ready_list(rtos_task_handle_t task_handle)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->add_to_ready_list(&g_scheduler_instance, task_handle);
}

/**
 * @brief Remove task from ready list via scheduler
 */
void rtos_scheduler_remove_from_ready_list(rtos_task_handle_t task_handle)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->remove_from_ready_list(&g_scheduler_instance, task_handle);
}

/**
 * @brief Add task to delayed list via scheduler
 */
void rtos_scheduler_add_to_delayed_list(rtos_task_handle_t task_handle, rtos_tick_t delay_ticks)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->add_to_delayed_list(&g_scheduler_instance, task_handle, delay_ticks);
}

/**
 * @brief Remove task from delayed list via scheduler
 */
void rtos_scheduler_remove_from_delayed_list(rtos_task_handle_t task_handle)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->remove_from_delayed_list(&g_scheduler_instance, task_handle);
}

/**
 * @brief Update delayed tasks via scheduler
 */
void rtos_scheduler_update_delayed_tasks(void)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->update_delayed_tasks(&g_scheduler_instance);
}

/**
 * @brief Get expected idle ticks
 */
uint32_t rtos_scheduler_get_expected_idle_ticks(void)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || 
        g_scheduler_instance.vtable->get_expected_idle_ticks == NULL)
    {
        return 0;
    }

    return g_scheduler_instance.vtable->get_expected_idle_ticks(&g_scheduler_instance);
}

/**
 * @brief Get scheduler statistics
 */
size_t rtos_scheduler_get_statistics(void *stats_buffer, size_t buffer_size)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || stats_buffer == NULL ||
        buffer_size == 0)
    {
        return 0;
    }

    if (g_scheduler_instance.vtable->get_statistics != NULL)
    {
        return g_scheduler_instance.vtable->get_statistics(&g_scheduler_instance, stats_buffer, buffer_size);
    }

    return 0; /* Statistics not supported */
}

/**
 * @brief Print scheduler debug information
 */
void rtos_scheduler_debug_print(void)
{
    if (!g_scheduler_instance.initialized)
    {
        KLOGI("Scheduler", "SchedNotInit");
        return;
    }

    KLOGD("Scheduler", "SchedDebug type=%u", (uint32_t) g_scheduler_instance.type);

    uint8_t stats_buffer[128];
    size_t  stats_size = rtos_scheduler_get_statistics(stats_buffer, sizeof(stats_buffer));

    if (stats_size > 0)
    {
        KLOGD("Scheduler", "SchedStats size=%u", (uint32_t) stats_size);
    }
}

/**
 * @brief Find scheduler interface by type
 */
static const rtos_scheduler_t *rtos_scheduler_find_interface(rtos_scheduler_type_t scheduler_type)
{
    for (size_t i = 0; i < SCHEDULER_REGISTRY_SIZE; i++)
    {
        if (g_scheduler_registry[i].type == scheduler_type)
        {
            return g_scheduler_registry[i].vtable;
        }
    }
    return NULL;
}
