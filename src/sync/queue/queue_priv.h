#ifndef QUEUE_PRIV_H
#define QUEUE_PRIV_H

#include "kernel_priv.h"
#include "queue.h"
#include "task.h"

/* Queue control block is now defined publicly in queue.h to enable
 * static allocation (rtos_queue_create_static). This header exists for
 * kernel-internal helpers that may be added later. */

#endif /* QUEUE_PRIV_H */
