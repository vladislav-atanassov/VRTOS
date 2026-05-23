#ifndef RTOS_TYPES_H
#define RTOS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t rtos_tick_t;
typedef uint8_t  rtos_priority_t;
typedef uint16_t rtos_stack_size_t;
typedef uint8_t  rtos_task_id_t;

typedef enum
{
    RTOS_SUCCESS = 0,
    RTOS_ERROR_INVALID_PARAM,
    RTOS_ERROR_NO_MEMORY,
    RTOS_ERROR_TASK_NOT_FOUND,
    RTOS_ERROR_INVALID_STATE,
    RTOS_ERROR_TIMEOUT,
    RTOS_ERROR_FULL,
    RTOS_ERROR_EMPTY,
    RTOS_ERROR_GENERAL,
    RTOS_ERROR_DEADLOCK,
    RTOS_ERROR_RECURSION
} rtos_status_t;

#define RTOS_MAX_DELAY ((rtos_tick_t) - 1)

typedef enum
{
    RTOS_TASK_STATE_READY = 0,
    RTOS_TASK_STATE_RUNNING,
    RTOS_TASK_STATE_BLOCKED,
    RTOS_TASK_STATE_SUSPENDED,
    RTOS_TASK_STATE_DELETED
} rtos_task_state_t;

typedef void (*rtos_task_function_t)(void *param);

typedef enum
{
    RTOS_SYNC_TYPE_NONE = 0,
    RTOS_SYNC_TYPE_MUTEX,
    RTOS_SYNC_TYPE_SEMAPHORE,
    RTOS_SYNC_TYPE_QUEUE,
    RTOS_SYNC_TYPE_NOTIFICATION,
    RTOS_SYNC_TYPE_EVENT_GROUP
} rtos_sync_type_t;

struct rtos_task_control_block;
typedef struct rtos_task_control_block rtos_tcb_t;

typedef rtos_tcb_t *rtos_task_handle_t;

#endif // RTOS_TYPES_H
