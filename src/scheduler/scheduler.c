#include "scheduler.h"

#include "cooperative.h"
#include "klog.h"
#include "preemptive_sp.h"
#include "profiling.h"
#include "round_robin.h"
#include "task.h"
#include "task_priv.h"
#include "test_hooks_priv.h"

#include <string.h>

/* .type is set by rtos_scheduler_init() once the variant's
 * kernel_scheduler_choice symbol has been resolved at link time. */
rtos_scheduler_instance_t g_scheduler_instance = {
    .vtable = NULL, .private_data = NULL, .initialized = false};

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
    g_scheduler_instance.initialized  = false;

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

rtos_scheduler_type_t rtos_scheduler_get_type(void)
{
    return g_scheduler_instance.type;
}

rtos_task_handle_t rtos_scheduler_get_next_task(void)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL)
    {
        KLOGE("Scheduler", "SchedNotInit");
        return NULL;
    }

    rtos_task_handle_t next = g_scheduler_instance.vtable->get_next_task(&g_scheduler_instance);

    KLOGT("Scheduler", "GetNext picked=%s", (uint32_t) (next ? next->name : "none"));

    RTOS_TEST_HOOK_FIRE(RTOS_HOOK_CTX_SWITCH, {
        _ctx_.u.ctx_switch.out_task = rtos_task_get_current();
        _ctx_.u.ctx_switch.in_task  = next;
    });

    return next;
}

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

void rtos_scheduler_task_completed(rtos_task_handle_t completed_task)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || completed_task == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->task_completed(&g_scheduler_instance, completed_task);
}

void rtos_scheduler_add_to_ready_list(rtos_task_handle_t task_handle)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->add_to_ready_list(&g_scheduler_instance, task_handle);
}

void rtos_scheduler_remove_from_ready_list(rtos_task_handle_t task_handle)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->remove_from_ready_list(&g_scheduler_instance, task_handle);
}

void rtos_scheduler_add_to_delayed_list(rtos_task_handle_t task_handle, rtos_tick_t delay_ticks)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->add_to_delayed_list(&g_scheduler_instance, task_handle, delay_ticks);
}

void rtos_scheduler_remove_from_delayed_list(rtos_task_handle_t task_handle)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL || task_handle == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->remove_from_delayed_list(&g_scheduler_instance, task_handle);
}

void rtos_scheduler_update_delayed_tasks(void)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL)
    {
        return;
    }

    g_scheduler_instance.vtable->update_delayed_tasks(&g_scheduler_instance);
}

uint32_t rtos_scheduler_get_expected_idle_ticks(void)
{
    if (!g_scheduler_instance.initialized || g_scheduler_instance.vtable == NULL ||
        g_scheduler_instance.vtable->get_expected_idle_ticks == NULL)
    {
        return 0;
    }

    return g_scheduler_instance.vtable->get_expected_idle_ticks(&g_scheduler_instance);
}

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
