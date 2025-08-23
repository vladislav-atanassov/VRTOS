/*******************************************************************************
 * File: list.c
 * Description: Linked list utility functions for RTOS task management
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "list.h"
#include "config.h"
#include "kernel_priv.h"
#include "rtos_types.h"

/**
 * @file list.c
 * @brief Implementation of linked list operations for RTOS task scheduling.
 *
 * Implements FIFO and sorted insertion for managing ready and delayed task
 * lists. These functions form the backbone of task state transitions in the
 * scheduler.
 */

/**
 * @brief Add task to end of list (FIFO order)
 * @param list_head Pointer to the head of the list
 * @param task Task to add
 */
void rtos_list_add_tail(rtos_tcb_t **list_head, rtos_tcb_t *task) {
    if (task == NULL || list_head == NULL) {
        return;
    }

    task->next = NULL;
    task->prev = NULL;

    if (*list_head == NULL) {
        /* Empty list */
        *list_head = task;
        return;
    }

    /* Find tail and append */
    rtos_tcb_t *current = *list_head;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = task;
    task->prev = current;
}

/**
 * @brief Add task to list in sorted order by delay_until
 * @param list_head Pointer to the head of the list
 * @param task Task to add (must have delay_until set)
 */
void rtos_list_add_sorted(rtos_tcb_t **list_head, rtos_tcb_t *task) {
    if (task == NULL || list_head == NULL) {
        return;
    }

    task->next = NULL;
    task->prev = NULL;

    if (*list_head == NULL) {
        /* Empty list */
        *list_head = task;
        return;
    }

    rtos_tcb_t *current = *list_head;
    rtos_tcb_t *prev = NULL;

    /* Find insertion point */
    while (current != NULL && current->delay_until <= task->delay_until) {
        prev = current;
        current = current->next;
    }

    /* Insert task */
    task->next = current;
    task->prev = prev;

    if (prev == NULL) {
        /* Insert at head */
        *list_head = task;
    } else {
        prev->next = task;
    }

    if (current != NULL) {
        current->prev = task;
    }
}

/**
 * @brief Remove task from any list
 * @param list_head Pointer to the head of the list
 * @param task Task to remove
 */
void rtos_task_list_remove(rtos_tcb_t **list_head, rtos_tcb_t *task) {
    if (task == NULL || list_head == NULL) {
        return;
    }

    if (task->prev != NULL) {
        task->prev->next = task->next;
    } else {
        /* Task is at head of list */
        *list_head = task->next;
    }

    if (task->next != NULL) {
        task->next->prev = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;
}

/* High-level task management functions */

/**
 * @brief Add task to ready list
 */
void rtos_task_add_to_ready_list(rtos_tcb_t *task) {
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES) {
        return;
    }
    rtos_list_add_tail(&g_ready_list[task->priority], task);
}

/**
 * @brief Remove task from ready list
 */
void rtos_task_remove_from_ready_list(rtos_tcb_t *task) {
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES) {
        return;
    }
    rtos_task_list_remove(&g_ready_list[task->priority], task);
}

/**
 * @brief Add task to delayed list
 */
void rtos_task_add_to_delayed_list(rtos_tcb_t *task, rtos_tick_t delay_ticks) {
    if (task == NULL) {
        return;
    }

    task->delay_until = g_kernel.tick_count + delay_ticks;
    rtos_list_add_sorted(&g_delayed_task_list, task);
}

/**
 * @brief Remove task from delayed list
 */
void rtos_task_remove_from_delayed_list(rtos_tcb_t *task) {
    if (task == NULL || task->priority >= RTOS_MAX_TASK_PRIORITIES) {
        return;
    }
    rtos_task_list_remove(&g_delayed_task_list, task);
}
