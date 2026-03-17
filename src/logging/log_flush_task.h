#ifndef LOG_FLUSH_TASK_H
#define LOG_FLUSH_TASK_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief RTOS task function that periodically drains the KLog ring buffer
 *        and outputs decoded, human-readable records to UART via printf.
 *
 * Should be created at the lowest priority so it only runs when no other
 * task needs the CPU. Call klog_init() before starting this task.
 *
 * @param param Unused
 */
void log_flush_task(void *param);

#ifdef __cplusplus
}
#endif

#endif /* LOG_FLUSH_TASK_H */
