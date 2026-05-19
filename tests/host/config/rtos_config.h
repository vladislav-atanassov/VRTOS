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

/*
 * The kernel's profiling.h checks RTOS_PROFILING_SYSTEM_ENABLED. Force it off
 * for host builds so we don't drag in DWT cycle-counter dependencies.
 */
#ifndef RTOS_PROFILING_SYSTEM_ENABLED
#define RTOS_PROFILING_SYSTEM_ENABLED 0
#endif

#endif /* HOST_RTOS_CONFIG_H */
