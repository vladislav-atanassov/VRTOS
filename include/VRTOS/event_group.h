#ifndef EVENT_GROUP_H
#define EVENT_GROUP_H

#include "rtos_types.h"

/**
 * @file event_group.h
 * @brief Event Group (Event Flags) API
 *
 * Provides multi-event synchronization where tasks can wait for any or all
 * of a set of bits. Multiple tasks may wait on the same event group with
 * different conditions; set_bits wakes all whose conditions are satisfied.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/* Forward declaration for TCB */
struct rtos_task_control_block;

/* Event group wait timeout values */
#define RTOS_EG_MAX_WAIT ((rtos_tick_t) 0xFFFFFFFFU)
#define RTOS_EG_NO_WAIT  ((rtos_tick_t) 0U)

/**
 * @brief Event group status codes
 */
typedef enum
{
    RTOS_EG_OK          = RTOS_SUCCESS,
    RTOS_EG_ERR_INVALID = RTOS_ERROR_INVALID_PARAM,
    RTOS_EG_ERR_TIMEOUT = RTOS_ERROR_TIMEOUT
} rtos_eg_status_t;

/**
 * @brief Event group structure
 */
typedef struct rtos_event_group
{
    uint32_t                        bits;         /**< Current event bits */
    struct rtos_task_control_block *waiting_list; /**< Head of waiting task list (priority-ordered) */
} rtos_event_group_t;

/**
 * @brief Initialize an event group
 * @param eg Pointer to event group structure
 * @return RTOS_EG_OK on success
 */
rtos_eg_status_t rtos_event_group_init(rtos_event_group_t *eg);

/**
 * @brief Wait for bits in an event group
 * @param eg Pointer to event group
 * @param bits_to_wait Bitmask of bits to wait for
 * @param wait_all true = wait for ALL bits, false = wait for ANY bit
 * @param clear_on_exit true = clear waited bits when condition is met
 * @param bits_out If non-NULL, receives the event group bits at time of wake
 * @param timeout_ticks Timeout in ticks (0 = no wait, RTOS_EG_MAX_WAIT = forever)
 * @return RTOS_EG_OK if condition met, RTOS_EG_ERR_TIMEOUT if timed out
 */
rtos_eg_status_t rtos_event_group_wait_bits(rtos_event_group_t *eg, uint32_t bits_to_wait, bool wait_all,
                                            bool clear_on_exit, uint32_t *bits_out, rtos_tick_t timeout_ticks);

/**
 * @brief Set bits in an event group (task context)
 *
 * Sets the specified bits and wakes all waiting tasks whose conditions
 * are now satisfied. Bits requested for clear-on-exit are cleared after
 * all waiters have been checked (deferred clear).
 *
 * @param eg Pointer to event group
 * @param bits_to_set Bitmask of bits to set
 * @return RTOS_EG_OK on success
 */
rtos_eg_status_t rtos_event_group_set_bits(rtos_event_group_t *eg, uint32_t bits_to_set);

/**
 * @brief Set bits in an event group (ISR-safe)
 * @param eg Pointer to event group
 * @param bits_to_set Bitmask of bits to set
 * @return RTOS_EG_OK on success
 */
rtos_eg_status_t rtos_event_group_set_bits_from_isr(rtos_event_group_t *eg, uint32_t bits_to_set);

/**
 * @brief Clear bits in an event group
 * @param eg Pointer to event group
 * @param bits_to_clear Bitmask of bits to clear
 * @return RTOS_EG_OK on success
 */
rtos_eg_status_t rtos_event_group_clear_bits(rtos_event_group_t *eg, uint32_t bits_to_clear);

/**
 * @brief Get current event group bits (non-blocking)
 * @param eg Pointer to event group
 * @return Current bits value (0 if eg is NULL)
 */
uint32_t rtos_event_group_get_bits(rtos_event_group_t *eg);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_GROUP_H */
