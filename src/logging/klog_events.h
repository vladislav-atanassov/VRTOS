/*******************************************************************************
 * File: src/utils/log_event_ids.h
 * Description: Shared Event IDs for KLog and ProfTrace
 ******************************************************************************/

#ifndef LOG_EVENT_IDS_H
#define LOG_EVENT_IDS_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Unified event ID enum for kernel logging and profiling trace.
 *
 * Kernel events (KEVT_*) are emitted by KLog.
 * Profiling events (PEVT_*) are emitted by ProfTrace.
 * The host-side decoder uses these IDs to reconstruct human-readable output.
 */
typedef enum
{
    /* ===== Kernel Events (KLog) ===== */

    /* Task lifecycle */
    KEVT_TASK_CREATE = 0x0001,
    KEVT_TASK_SWITCH,
    KEVT_TASK_SUSPEND,
    KEVT_TASK_RESUME,
    KEVT_TASK_READY,
    KEVT_TASK_BLOCK,
    KEVT_TASK_UNBLOCK,
    KEVT_TASK_DELETE,
    KEVT_TASK_IDLE_START,

    /* Scheduler */
    KEVT_SCHEDULER_INIT = 0x0020,
    KEVT_SCHEDULER_NOT_INIT,

    /* Synchronization */
    KEVT_MUTEX_INIT = 0x0040,
    KEVT_MUTEX_LOCK,
    KEVT_MUTEX_UNLOCK,
    KEVT_MUTEX_PIP_BOOST,
    KEVT_MUTEX_PIP_RESTORE,
    KEVT_MUTEX_RECURSIVE,
    KEVT_MUTEX_MAX_RECURSION,
    KEVT_MUTEX_DEADLOCK,
    KEVT_MUTEX_BLOCK,
    KEVT_MUTEX_TIMEOUT,
    KEVT_SEM_INIT,
    KEVT_SEM_ACQUIRE,
    KEVT_SEM_SIGNAL,
    KEVT_SEM_BLOCK,
    KEVT_SEM_TIMEOUT,
    KEVT_SEM_OVERFLOW,
    KEVT_SEM_WAKE,

    /* Timers */
    KEVT_TIMER_CREATE = 0x0060,
    KEVT_TIMER_START,
    KEVT_TIMER_STOP,
    KEVT_TIMER_PERIOD_CHANGE,

    /* Port / Hardware */
    KEVT_PORT_INIT = 0x0080,
    KEVT_SYSTICK_FAIL,

    /* Errors and faults */
    KEVT_HARD_FAULT = 0x00A0,
    KEVT_HARD_FAULT_REGS,
    KEVT_HARD_FAULT_SCB,
    KEVT_HARD_FAULT_SP,
    KEVT_STACK_OVERFLOW,
    KEVT_INVALID_TRANSITION,
    KEVT_ASSERT_FAIL,
    KEVT_ERROR_GENERIC,
    KEVT_ALLOC_FAIL,
    KEVT_INVALID_PARAM,
    KEVT_NO_CURRENT_TASK,

    /* Queue */
    KEVT_QUEUE_INIT = 0x00D0,
    KEVT_QUEUE_CREATE,
    KEVT_QUEUE_SEND,
    KEVT_QUEUE_SEND_BLOCK,
    KEVT_QUEUE_SEND_TIMEOUT,
    KEVT_QUEUE_SEND_FULL,
    KEVT_QUEUE_RECV,
    KEVT_QUEUE_RECV_BLOCK,
    KEVT_QUEUE_RECV_TIMEOUT,
    KEVT_QUEUE_RECV_EMPTY,
    KEVT_QUEUE_WAKE_RECV,
    KEVT_QUEUE_WAKE_SEND,
    KEVT_QUEUE_RESET,

    /* Scheduler internals */
    KEVT_SCHED_TASK_READY = 0x00E0,
    KEVT_SCHED_TASK_DELAYED,
    KEVT_SCHED_TASK_DELAY_EXPIRED,
    KEVT_SCHED_TIME_SLICE,
    KEVT_SCHED_ROTATE,

    /* Memory */
    KEVT_STACK_ALLOC_FAIL,

    /* ===== Profiling Events (ProfTrace) ===== */

    PEVT_CTX_SWITCH = 0x1001,
    PEVT_TICK,
    PEVT_TASK_ENTER,
    PEVT_MUTEX_ACQUIRE,
    PEVT_MUTEX_RELEASE,
    PEVT_USER_MARK,
    PEVT_ISR_ENTER,
    PEVT_ISR_EXIT,
} log_event_id_t;

#ifdef __cplusplus
}
#endif

#endif /* LOG_EVENT_IDS_H */
