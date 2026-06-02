/*
 * Host-build override of the board-specific rtos_config.h.
 *
 * The kernel's include/config.h #include "rtos_config.h" before applying
 * its #ifndef defaults, so this stub gets resolved first. Empty body =
 * accept every default from include/config.h. Add overrides here if a host
 * test needs a non-default RTOS_MAX_TASKS etc.
 */
#ifndef HOST_RTOS_CONFIG_H
#define HOST_RTOS_CONFIG_H

/* Host builds do not have UART or a real scheduler — disable both loggers
 * so kernel.c does not reference klog_init/ulog_init/log_flush_task. */
#ifndef RTOS_KLOG_ENABLED
#define RTOS_KLOG_ENABLED 0
#endif
#ifndef RTOS_ULOG_ENABLED
#define RTOS_ULOG_ENABLED 0
#endif

#endif /* HOST_RTOS_CONFIG_H */
