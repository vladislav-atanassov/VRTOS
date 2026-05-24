#ifndef LOG_FLUSH_TASK_H
#define LOG_FLUSH_TASK_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief RTOS task function that periodically drains the KLog and ULog
 *        ring buffers and emits the output to UART via log_flush_drain().
 *
 * Created automatically by rtos_init() at the idle priority when either
 * RTOS_KLOG_ENABLED or RTOS_ULOG_ENABLED is non-zero.  Callers must call
 * log_init() (which rtos_init() does) before this task runs.
 *
 * @param param Unused
 */
void log_flush_task(void *param);

#ifdef __cplusplus
}
#endif

#endif /* LOG_FLUSH_TASK_H */
