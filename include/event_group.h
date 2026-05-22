#ifndef EVENT_GROUP_H
#define EVENT_GROUP_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct rtos_task_control_block;

#define RTOS_EG_MAX_WAIT ((rtos_tick_t) 0xFFFFFFFFU)
#define RTOS_EG_NO_WAIT  ((rtos_tick_t) 0U)

typedef enum
{
    RTOS_EG_OK          = RTOS_SUCCESS,
    RTOS_EG_ERR_INVALID = RTOS_ERROR_INVALID_PARAM,
    RTOS_EG_ERR_TIMEOUT = RTOS_ERROR_TIMEOUT
} rtos_eg_status_t;

typedef struct rtos_event_group
{
    uint32_t                        bits;
    struct rtos_task_control_block *waiting_list; /* priority-ordered */
} rtos_event_group_t;

/**
 * @brief Initialize an event group, clearing all bits.
 * @param eg Pointer to the event group (must not be NULL).
 * @return RTOS_EG_OK on success, error code otherwise.
 */
rtos_eg_status_t rtos_event_group_init(rtos_event_group_t *eg);

/**
 * @brief Wait for one or more bits to be set. Multiple tasks may wait on the same group
 *        with different conditions; set_bits wakes all tasks whose conditions are satisfied.
 * @param eg            Pointer to the event group.
 * @param bits_to_wait  Bitmask of bits to wait for.
 * @param wait_all      true = wait until ALL bits are set, false = wait for ANY bit.
 * @param clear_on_exit Clear the waited bits when the condition is met.
 * @param bits_out      Receives the group bits at time of wake (can be NULL).
 * @param timeout_ticks Maximum ticks to wait. RTOS_EG_NO_WAIT returns immediately.
 * @return RTOS_EG_OK on success, RTOS_EG_ERR_TIMEOUT on timeout.
 */
rtos_eg_status_t rtos_event_group_wait_bits(rtos_event_group_t *eg, uint32_t bits_to_wait, bool wait_all,
                                            bool clear_on_exit, uint32_t *bits_out, rtos_tick_t timeout_ticks);

/**
 * @brief Set bits in the event group, waking all tasks whose wait conditions are now satisfied.
 * Bits requested for clear_on_exit are cleared after all waiters have been checked (deferred clear).
 * @param eg          Pointer to the event group.
 * @param bits_to_set Bitmask of bits to set.
 * @return RTOS_EG_OK on success, error code otherwise.
 */
rtos_eg_status_t rtos_event_group_set_bits(rtos_event_group_t *eg, uint32_t bits_to_set);

/**
 * @brief ISR-safe variant of rtos_event_group_set_bits().
 * @param eg          Pointer to the event group.
 * @param bits_to_set Bitmask of bits to set.
 * @return RTOS_EG_OK on success, error code otherwise.
 */
rtos_eg_status_t rtos_event_group_set_bits_from_isr(rtos_event_group_t *eg, uint32_t bits_to_set);

/**
 * @brief Clear bits in the event group.
 * @param eg            Pointer to the event group.
 * @param bits_to_clear Bitmask of bits to clear.
 * @return RTOS_EG_OK on success, error code otherwise.
 */
rtos_eg_status_t rtos_event_group_clear_bits(rtos_event_group_t *eg, uint32_t bits_to_clear);

/**
 * @brief Return the current bit state of the event group.
 * @param eg Pointer to the event group.
 * @return Current bitmask value.
 */
uint32_t rtos_event_group_get_bits(rtos_event_group_t *eg);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_GROUP_H */
