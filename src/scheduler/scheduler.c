/*******************************************************************************
 * File: src/scheduler/scheduler.c
 * Description: Updated RTOS Scheduler Manager with List Management
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "scheduler.h"
#include "cooperative.h"
#include "kernel_priv.h"
#include "log.h"
#include "preemptive_sp.h"
#include <string.h>

/**
 * @file scheduler.c
 * @brief RTOS Scheduler Manager Implementation with List Management
 *
 * This updated implementation provides a unified interface for scheduler
 * operations including list management, allowing different schedulers
 * to optimize their data structures while providing a consistent API.
 */

/* Global scheduler instance */
rtos_scheduler_instance_t g_scheduler_instance = {
    .vtable = NULL,
    .type = RTOS_SCHEDULER_COOPERATIVE,
    .private_data = NULL,
    .initialized = false};

/* Scheduler registry - add new schedulers here */
static const struct {
    rtos_scheduler_type_t   type;
    const rtos_scheduler_t *vtable;
} g_scheduler_registry[] = {
    {RTOS_SCHEDULER_PREEMPTIVE_SP, &preemptive_sp_scheduler},
    {RTOS_SCHEDULER_COOPERATIVE, &cooperative_scheduler}
    /* Add new schedulers here */
};

#define SCHEDULER_REGISTRY_SIZE                                                \
    (sizeof(g_scheduler_registry) / sizeof(g_scheduler_registry[0]))

/* Static function prototypes */
static const rtos_scheduler_t *rtos_scheduler_find_interface(
    rtos_scheduler_type_t scheduler_type);

/* ==================== Public API Implementation ==================== */

/**
 * @brief Initialize the scheduler subsystem
 */
rtos_status_t rtos_scheduler_init(rtos_scheduler_type_t scheduler_type) {
    /* Check if already initialized */
    if (g_scheduler_instance.initialized) {
        log_info("Scheduler already initialized");
        return RTOS_ERROR_INVALID_STATE;
    }

    /* Find scheduler interface */
    const rtos_scheduler_t *interface =
        rtos_scheduler_find_interface(scheduler_type);
    if (interface == NULL) {
        log_error("Unknown scheduler type: %d", scheduler_type);
        return RTOS_ERROR_INVALID_PARAM;
    }

    /* Initialize scheduler instance */
    g_scheduler_instance.vtable = interface;
    g_scheduler_instance.type = scheduler_type;
    g_scheduler_instance.private_data = NULL;
    g_scheduler_instance.initialized =
        false; /* Will be set by interface init */

    /* Initialize the specific scheduler */
    rtos_status_t status = interface->init(&g_scheduler_instance);

    if (status == RTOS_SUCCESS) {
        g_scheduler_instance.initialized = true;
        log_info("Scheduler initialized: %s",
                 scheduler_type == RTOS_SCHEDULER_PREEMPTIVE_SP
                     ? "Preemptive static priority-based"
                 : scheduler_type == RTOS_SCHEDULER_COOPERATIVE ? "COOPERATIVE"
                                                                : "Unknown");
    } else {
        /* Clean up on failure */
        g_scheduler_instance.vtable = NULL;
        g_scheduler_instance.private_data = NULL;
        log_error("Scheduler initialization failed: %d", status);
    }

    return status;
}

/**
 * @brief Get the current scheduler type
 */
rtos_scheduler_type_t rtos_scheduler_get_type(void) {
    return g_scheduler_instance.type;
}

/* ==================== Core Scheduling Operations ==================== */

/**
 * @brief Get the highest priority/earliest deadline ready task
 */
rtos_task_handle_t rtos_scheduler_get_next_task(void) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL) {
        log_error("Scheduler not initialized");
        return NULL;
    }

    return g_scheduler_instance.vtable->get_next_task(&g_scheduler_instance);
}

/**
 * @brief Check if scheduling decision needs to be made
 */
bool rtos_scheduler_should_preempt(rtos_task_handle_t new_task) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL) {
        return false;
    }

    return g_scheduler_instance.vtable->should_preempt(&g_scheduler_instance,
                                                       new_task);
}

/**
 * @brief Handle task completion/yield
 */
void rtos_scheduler_task_completed(rtos_task_handle_t completed_task) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL || completed_task == NULL) {
        return;
    }

    g_scheduler_instance.vtable->task_completed(&g_scheduler_instance,
                                                completed_task);
}

/* ==================== List Management Operations ==================== */

/**
 * @brief Add task to ready list via scheduler
 */
void rtos_scheduler_add_to_ready_list(rtos_task_handle_t task_handle) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL || task_handle == NULL) {
        return;
    }

    g_scheduler_instance.vtable->add_to_ready_list(&g_scheduler_instance,
                                                   task_handle);
}

/**
 * @brief Remove task from ready list via scheduler
 */
void rtos_scheduler_remove_from_ready_list(rtos_task_handle_t task_handle) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL || task_handle == NULL) {
        return;
    }

    g_scheduler_instance.vtable->remove_from_ready_list(&g_scheduler_instance,
                                                        task_handle);
}

/**
 * @brief Add task to delayed list via scheduler
 */
void rtos_scheduler_add_to_delayed_list(rtos_task_handle_t task_handle,
                                        rtos_tick_t        delay_ticks) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL || task_handle == NULL) {
        return;
    }

    g_scheduler_instance.vtable->add_to_delayed_list(&g_scheduler_instance,
                                                     task_handle, delay_ticks);
}

/**
 * @brief Remove task from delayed list via scheduler
 */
void rtos_scheduler_remove_from_delayed_list(rtos_task_handle_t task_handle) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL || task_handle == NULL) {
        return;
    }

    g_scheduler_instance.vtable->remove_from_delayed_list(&g_scheduler_instance,
                                                          task_handle);
}

/**
 * @brief Update delayed tasks via scheduler
 */
void rtos_scheduler_update_delayed_tasks(void) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL) {
        return;
    }

    g_scheduler_instance.vtable->update_delayed_tasks(&g_scheduler_instance);
}

/* ==================== Debug and Statistics ==================== */

/**
 * @brief Get scheduler statistics
 */
size_t rtos_scheduler_get_statistics(void *stats_buffer, size_t buffer_size) {
    if (!g_scheduler_instance.initialized ||
        g_scheduler_instance.vtable == NULL || stats_buffer == NULL ||
        buffer_size == 0) {
        return 0;
    }

    if (g_scheduler_instance.vtable->get_statistics != NULL) {
        return g_scheduler_instance.vtable->get_statistics(
            &g_scheduler_instance, stats_buffer, buffer_size);
    }

    return 0; /* Statistics not supported */
}

/**
 * @brief Print scheduler debug information
 */
void rtos_scheduler_debug_print(void) {
    if (!g_scheduler_instance.initialized) {
        log_info("Scheduler not initialized");
        return;
    }

    log_info("=== Scheduler Debug Information ===");
    log_info("Type: %s",
             g_scheduler_instance.type == RTOS_SCHEDULER_PREEMPTIVE_SP
                 ? "Preemptive static priority-based"
             : g_scheduler_instance.type == RTOS_SCHEDULER_COOPERATIVE
                 ? "COOPERATIVE"
                 : "Unknown");

    /* Try to get and print scheduler-specific statistics */
    uint8_t stats_buffer[128];
    size_t  stats_size = rtos_scheduler_get_statistics(stats_buffer, sizeof(stats_buffer));

    if (stats_size > 0) {
        log_info("Scheduler statistics (%zu bytes):", stats_size);
        /* Print as hex dump for debugging */
        for (size_t i = 0; i < stats_size; i += 16) {
            char  hex_line[64] = {0};
            char *ptr = hex_line;

            for (size_t j = 0; j < 16 && (i + j) < stats_size; j++) {
                ptr += sprintf(ptr, "%02X ", stats_buffer[i + j]);
            }
            log_info("  %04zX: %s", i, hex_line);
        }
    }

    log_info("===================================");
}

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Find scheduler interface by type
 */
static const rtos_scheduler_t *rtos_scheduler_find_interface(
    rtos_scheduler_type_t scheduler_type) {
    for (size_t i = 0; i < SCHEDULER_REGISTRY_SIZE; i++) {
        if (g_scheduler_registry[i].type == scheduler_type) {
            return g_scheduler_registry[i].vtable;
        }
    }
    return NULL;
}