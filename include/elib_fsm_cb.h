/* elib_fsm_cb.h - Callback State Machine Main Header */
#ifndef ELIB_FSM_CB_H
#define ELIB_FSM_CB_H

#include "elib_fsm_err.h"
#include "elib_fsm_cb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize callback state machine
 * @param ctx User-allocated context pointer
 * @param states State descriptor table
 * @param state_count Number of state descriptors
 * @param initial Initial state value
 * @param user_data User data passed to all callbacks (nullable)
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_cb_init(elib_fsm_cb_ctx_t *ctx,
                                 const elib_fsm_cb_state_desc_t *states,
                                 size_t state_count,
                                 elib_fsm_state_t initial,
                                 void *user_data);

/**
 * @brief Deinitialize callback state machine
 * @param ctx Context pointer
 */
void elib_fsm_cb_deinit(elib_fsm_cb_ctx_t *ctx);

/**
 * @brief Jump to target state, immediate or delayed
 * @param ctx Context pointer
 * @param target Target state
 * @param delay_ms 0=immediate (exit->entry), >0=delayed (exit now, entry at expiration)
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_cb_goto(elib_fsm_cb_ctx_t *ctx,
                                 elib_fsm_state_t target,
                                 uint32_t delay_ms);

/**
 * @brief Advance one tick; call event and run callbacks when no delay is pending,
 *        skip both during delayed transitions, call entry on delay expiration
 * @param ctx Context pointer
 * @param period_ms Tick period in ms
 * @param event_data Opaque event data passed to the event callback (nullable)
 * @return Current state, or ELIB_FSM_STATE_INVALID during delay
 */
elib_fsm_state_t elib_fsm_cb_poll(elib_fsm_cb_ctx_t *ctx, uint32_t period_ms,
                                  void *event_data);

/**
 * @brief Dispatch an event to the current state's event callback
 * @param ctx Context pointer
 * @param event_data Opaque event data passed to the event callback (nullable)
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_cb_dispatch(elib_fsm_cb_ctx_t *ctx, void *event_data);

/**
 * @brief Get current state
 * @param ctx Context pointer
 * @return Current state, or ELIB_FSM_STATE_INVALID if ctx is NULL/uninitialized/delayed
 */
elib_fsm_state_t elib_fsm_cb_current(const elib_fsm_cb_ctx_t *ctx);

/**
 * @brief Get last valid state (never returns ELIB_FSM_STATE_INVALID during delay)
 * @param ctx Context pointer
 * @return Last valid state, or ELIB_FSM_STATE_INVALID if ctx is NULL/uninitialized
 */
elib_fsm_state_t elib_fsm_cb_previous(const elib_fsm_cb_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_CB_H */
