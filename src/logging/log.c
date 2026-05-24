#include "log.h"

#include "config.h"

#if RTOS_KLOG_ENABLED
extern void klog_backend_init(void);
extern void klog_drain_and_emit(void);
#endif

#if RTOS_ULOG_ENABLED
extern void ulog_backend_init(void);
extern void ulog_drain_and_emit(void);
#endif

void log_init(void)
{
#if RTOS_KLOG_ENABLED
    klog_backend_init();
#endif
#if RTOS_ULOG_ENABLED
    ulog_backend_init();
#endif
}

void log_flush_drain(void)
{
#if RTOS_KLOG_ENABLED
    klog_drain_and_emit();
#endif
#if RTOS_ULOG_ENABLED
    ulog_drain_and_emit();
#endif
}
