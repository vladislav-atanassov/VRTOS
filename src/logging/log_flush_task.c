/*******************************************************************************
 * File: src/utils/log_flush_task.c
 * Description: Log Flush Task — drains KLog + ULog to UART
 ******************************************************************************/

#include "log_flush_task.h"

#include "VRTOS.h"
#include "klog.h"
#include "klog_events.h"
#include "task.h"
#include "uart_tx.h"
#include "ulog.h"

/** ============================================================================
 * EVENT ID → HUMAN-READABLE FORMAT
 *
 * Instead of printing raw hex args, each event type gets a contextual format
 * so the output reads like a narrative.
 * ============================================================================= */

/**
 * @brief Format a single KLog record into a human-readable line.
 *
 * Output examples:
 *   [K/I] TaskCreate    id=1 prio=2               (T00)
 *   [K/D] IdleStart                                (T00)
 *   [K/I] SchedInit                                (ISR)
 *   [K/T] SchedDelayed  id=3 wake=0x0042           (T03)
 *   [K/F] HardFault     lr=0x08001234 psr=0x010000 (ISR)
 */
static void klog_format_record(const klog_record_t *r)
{
    const char *lvl;

    switch (r->level)
    {
        case KLOG_LEVEL_FAULT:
            lvl = "F";
            break;
        case KLOG_LEVEL_ERROR:
            lvl = "E";
            break;
        case KLOG_LEVEL_WARN:
            lvl = "W";
            break;
        case KLOG_LEVEL_INFO:
            lvl = "I";
            break;
        case KLOG_LEVEL_DEBUG:
            lvl = "D";
            break;
        case KLOG_LEVEL_TRACE:
            lvl = "T";
            break;
        default:
            lvl = "?";
            break;
    }

    /* Context string: "ISR" or task name like "WORKER" */
    const char *ctx;
    if (r->cpu_context >= 0xF0)
    {
        ctx = "ISR";
    }
    else
    {
        ctx = rtos_task_get_name(r->cpu_context);
    }

    /* Event-specific human-readable formatting */
    switch ((log_event_id_t) r->event_id)
    {
        /* ---- Task lifecycle ---- */
        case KEVT_TASK_CREATE:
            log_print("[K/%s] %-14s %-6s stk=%lu (%s)", lvl, "TaskCreate", rtos_task_get_name((uint8_t) r->arg0),
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_TASK_SWITCH:
            log_print("[K/%s] %-14s %s -> %s (%s)", lvl, "TaskSwitch", rtos_task_get_name((uint8_t) r->arg0),
                      rtos_task_get_name((uint8_t) r->arg1), ctx);
            break;
        case KEVT_TASK_SUSPEND:
            log_print("[K/%s] %-14s %s (%s)", lvl, "TaskSuspend", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_TASK_RESUME:
            log_print("[K/%s] %-14s %s (%s)", lvl, "TaskResume", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_TASK_READY:
            log_print("[K/%s] %-14s %s (%s)", lvl, "TaskReady", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_TASK_BLOCK:
            log_print("[K/%s] %-14s %s (%s)", lvl, "TaskBlock", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_TASK_UNBLOCK:
            log_print("[K/%s] %-14s %s (%s)", lvl, "TaskUnblock", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_TASK_DELETE:
            log_print("[K/%s] %-14s %s (%s)", lvl, "TaskDelete", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_TASK_IDLE_START:
            log_print("[K/%s] %-14s (%s)", lvl, "IdleStart", ctx);
            break;

        /* ---- Scheduler ---- */
        case KEVT_SCHEDULER_INIT:
            log_print("[K/%s] %-14s (%s)", lvl, "SchedInit", ctx);
            break;
        case KEVT_SCHEDULER_NOT_INIT:
            log_print("[K/%s] %-14s (%s)", lvl, "SchedNotInit", ctx);
            break;

        /* ---- Scheduler internals ---- */
        case KEVT_SCHED_TASK_READY:
            log_print("[K/%s] %-14s %s cnt=%lu (%s)", lvl, "SchedReady", rtos_task_get_name((uint8_t) r->arg0),
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_SCHED_TASK_DELAYED:
            log_print("[K/%s] %-14s %s wake=%lu (%s)", lvl, "SchedDelayed", rtos_task_get_name((uint8_t) r->arg0),
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_SCHED_TASK_DELAY_EXPIRED:
            log_print("[K/%s] %-14s %s (%s)", lvl, "SchedDelayExp", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_SCHED_TIME_SLICE:
            log_print("[K/%s] %-14s %s (%s)", lvl, "SchedSlice", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_SCHED_ROTATE:
            log_print("[K/%s] %-14s (%s)", lvl, "SchedRotate", ctx);
            break;

        /* ---- Mutex ---- */
        case KEVT_MUTEX_INIT:
            log_print("[K/%s] %-14s (%s)", lvl, "MtxInit", ctx);
            break;
        case KEVT_MUTEX_LOCK:
            log_print("[K/%s] %-14s owner=%lu (%s)", lvl, "MtxLock", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_MUTEX_UNLOCK:
            log_print("[K/%s] %-14s owner=%lu (%s)", lvl, "MtxUnlock", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_MUTEX_PIP_BOOST:
            log_print("[K/%s] %-14s old=%lu new=%lu (%s)", lvl, "MtxPIPBoost", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_MUTEX_PIP_RESTORE:
            log_print("[K/%s] %-14s prio=%lu (%s)", lvl, "MtxPIPRestore", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_MUTEX_RECURSIVE:
            log_print("[K/%s] %-14s depth=%lu (%s)", lvl, "MtxRecursive", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_MUTEX_MAX_RECURSION:
            log_print("[K/%s] %-14s (%s)", lvl, "MtxMaxRecur", ctx);
            break;
        case KEVT_MUTEX_DEADLOCK:
            log_print("[K/%s] %-14s (%s)", lvl, "MtxDeadlock!", ctx);
            break;
        case KEVT_MUTEX_BLOCK:
            log_print("[K/%s] %-14s owner=%lu (%s)", lvl, "MtxBlock", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_MUTEX_TIMEOUT:
            log_print("[K/%s] %-14s (%s)", lvl, "MtxTimeout", ctx);
            break;

        /* ---- Semaphore ---- */
        case KEVT_SEM_INIT:
            log_print("[K/%s] %-14s init=%lu max=%lu (%s)", lvl, "SemInit", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_SEM_ACQUIRE:
            log_print("[K/%s] %-14s cnt=%lu (%s)", lvl, "SemAcquire", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_SEM_SIGNAL:
            log_print("[K/%s] %-14s cnt=%lu (%s)", lvl, "SemSignal", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_SEM_BLOCK:
            log_print("[K/%s] %-14s (%s)", lvl, "SemBlock", ctx);
            break;
        case KEVT_SEM_TIMEOUT:
            log_print("[K/%s] %-14s (%s)", lvl, "SemTimeout", ctx);
            break;
        case KEVT_SEM_OVERFLOW:
            log_print("[K/%s] %-14s max=%lu (%s)", lvl, "SemOverflow", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_SEM_WAKE:
            log_print("[K/%s] %-14s %s (%s)", lvl, "SemWake", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;

        /* ---- Timer ---- */
        case KEVT_TIMER_CREATE:
            log_print("[K/%s] %-14s period=%lu (%s)", lvl, "TimerCreate", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_TIMER_START:
            log_print("[K/%s] %-14s (%s)", lvl, "TimerStart", ctx);
            break;
        case KEVT_TIMER_STOP:
            log_print("[K/%s] %-14s (%s)", lvl, "TimerStop", ctx);
            break;
        case KEVT_TIMER_PERIOD_CHANGE:
            log_print("[K/%s] %-14s old=%lu new=%lu (%s)", lvl, "TimerPeriod", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;

        /* ---- Port ---- */
        case KEVT_PORT_INIT:
            log_print("[K/%s] %-14s systick=%luHz (%s)", lvl, "PortInit", (unsigned long) r->arg1, ctx);
            break;
        case KEVT_SYSTICK_FAIL:
            log_print("[K/%s] %-14s (%s)", lvl, "SysTickFail!", ctx);
            break;

        /* ---- Errors ---- */
        case KEVT_HARD_FAULT:
            log_print("[K/%s] %-14s lr=0x%08lX psr=0x%08lX (%s)", lvl, "HARDFAULT!", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_HARD_FAULT_REGS:
            log_print("[K/%s] %-14s r0=0x%08lX r1=0x%08lX (%s)", lvl, "HFault-Regs", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_HARD_FAULT_SCB:
            log_print("[K/%s] %-14s cfsr=0x%08lX hfsr=0x%08lX (%s)", lvl, "HFault-SCB", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_HARD_FAULT_SP:
            log_print("[K/%s] %-14s sp=0x%08lX (%s)", lvl, "HFault-SP", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_STACK_OVERFLOW:
            log_print("[K/%s] %-14s %s (%s)", lvl, "StackOverflow!", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_INVALID_TRANSITION:
            log_print("[K/%s] %-14s from=%lu to=%lu (%s)", lvl, "InvalidTrans!", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_ASSERT_FAIL:
            log_print("[K/%s] %-14s (%s)", lvl, "AssertFail!", ctx);
            break;
        case KEVT_ERROR_GENERIC:
            log_print("[K/%s] %-14s code=%lu (%s)", lvl, "Error", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_ALLOC_FAIL:
            log_print("[K/%s] %-14s size=%lu (%s)", lvl, "AllocFail!", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_INVALID_PARAM:
            log_print("[K/%s] %-14s (%s)", lvl, "InvalidParam", ctx);
            break;
        case KEVT_NO_CURRENT_TASK:
            log_print("[K/%s] %-14s (%s)", lvl, "NoCurrentTask", ctx);
            break;

        /* ---- Queue ---- */
        case KEVT_QUEUE_INIT:
            log_print("[K/%s] %-14s (%s)", lvl, "QueueInit", ctx);
            break;
        case KEVT_QUEUE_CREATE:
            log_print("[K/%s] %-14s cap=%lu item=%lu (%s)", lvl, "QueueCreate", (unsigned long) r->arg0,
                      (unsigned long) r->arg1, ctx);
            break;
        case KEVT_QUEUE_SEND:
            log_print("[K/%s] %-14s cnt=%lu (%s)", lvl, "QueueSend", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_QUEUE_SEND_BLOCK:
            log_print("[K/%s] %-14s tmo=%lu (%s)", lvl, "QueueSendBlk", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_QUEUE_SEND_TIMEOUT:
            log_print("[K/%s] %-14s (%s)", lvl, "QueueSendTmo", ctx);
            break;
        case KEVT_QUEUE_SEND_FULL:
            log_print("[K/%s] %-14s (%s)", lvl, "QueueFull", ctx);
            break;
        case KEVT_QUEUE_RECV:
            log_print("[K/%s] %-14s cnt=%lu (%s)", lvl, "QueueRecv", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_QUEUE_RECV_BLOCK:
            log_print("[K/%s] %-14s tmo=%lu (%s)", lvl, "QueueRecvBlk", (unsigned long) r->arg0, ctx);
            break;
        case KEVT_QUEUE_RECV_TIMEOUT:
            log_print("[K/%s] %-14s (%s)", lvl, "QueueRecvTmo", ctx);
            break;
        case KEVT_QUEUE_RECV_EMPTY:
            log_print("[K/%s] %-14s (%s)", lvl, "QueueEmpty", ctx);
            break;
        case KEVT_QUEUE_WAKE_RECV:
            log_print("[K/%s] %-14s %s (%s)", lvl, "QueueWakeRecv", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_QUEUE_WAKE_SEND:
            log_print("[K/%s] %-14s %s (%s)", lvl, "QueueWakeSend", rtos_task_get_name((uint8_t) r->arg0), ctx);
            break;
        case KEVT_QUEUE_RESET:
            log_print("[K/%s] %-14s (%s)", lvl, "QueueReset", ctx);
            break;

        /* ---- Memory ---- */
        case KEVT_STACK_ALLOC_FAIL:
            log_print("[K/%s] %-14s size=%lu (%s)", lvl, "StackAllocFail!", (unsigned long) r->arg0, ctx);
            break;

        /* ---- Profiling events (shouldn't appear in KLog, but handle gracefully) ---- */
        case PEVT_CTX_SWITCH:
        case PEVT_TICK:
        case PEVT_TASK_ENTER:
        case PEVT_MUTEX_ACQUIRE:
        case PEVT_MUTEX_RELEASE:
        case PEVT_USER_MARK:
        case PEVT_ISR_ENTER:
        case PEVT_ISR_EXIT:
            break;

        default:
            log_print("[K/%s] %-14s evt=0x%04X 0x%08lX 0x%08lX (%s)", lvl, "Unknown", r->event_id,
                      (unsigned long) r->arg0, (unsigned long) r->arg1, ctx);
            break;
    }
}

/* =============================== FLUSH TASK =============================== */

/* Drain batch size — how many records to pull per iteration */
#define KLOG_FLUSH_BATCH 8

/* Flush period in milliseconds */
#define KLOG_FLUSH_PERIOD_MS 100

/* ULog drain chunk size */
#define ULOG_FLUSH_CHUNK 128

void log_flush_task(void *param)
{
    (void) param;

    klog_record_t batch[KLOG_FLUSH_BATCH];

    while (1)
    {
        /* 1. Drain KLog — binary kernel records, decode to human-readable */
        uint32_t n = klog_drain(batch, KLOG_FLUSH_BATCH);

        for (uint32_t i = 0; i < n; i++)
        {
            klog_format_record(&batch[i]);
        }

        /* 2. Drain ULog — pre-formatted strings, write directly to UART */
        {
            uint8_t  ulog_chunk[ULOG_FLUSH_CHUNK];
            uint32_t ulog_bytes;

            while ((ulog_bytes = ulog_drain(ulog_chunk, sizeof(ulog_chunk))) > 0)
            {
                extern int _write(int file, char *ptr, int len);
                _write(1, (char *) ulog_chunk, (int) ulog_bytes);
            }
        }

        rtos_delay_ms(KLOG_FLUSH_PERIOD_MS);
    }
}
