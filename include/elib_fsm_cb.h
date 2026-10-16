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
 * @param transition_event Callback for events arriving during a pending delayed
 *                         transition (critical state); nullable = drop
 * @return ELIB_FSM_OK on success; ELIB_FSM_ERR_INVALID_PARAM if ctx/states is
 *         NULL or state_count is 0; ELIB_FSM_ERR_STATE_NOT_FOUND if initial is
 *         not present in the descriptor table (ctx is left untouched)
 */
elib_fsm_err_t elib_fsm_cb_init(elib_fsm_cb_ctx_t *ctx,
                                 const elib_fsm_cb_state_desc_t *states,
                                 size_t state_count,
                                 elib_fsm_state_t initial,
                                 void *user_data,
                                 elib_fsm_cb_transition_event_fn transition_event);

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
 * @return ELIB_FSM_OK on success; ELIB_FSM_ERR_INVALID_PARAM if ctx is NULL;
 *         ELIB_FSM_ERR_NOT_INITIALIZED if not initialized;
 *         ELIB_FSM_ERR_STATE_NOT_FOUND if target is not in the descriptor table
 *         (no exit/entry fires, state is unchanged)
 */
elib_fsm_err_t elib_fsm_cb_goto(elib_fsm_cb_ctx_t *ctx,
                                 elib_fsm_state_t target,
                                 uint32_t delay_ms);

/**
 * @brief Advance one tick.
 *        - no pending delay: call current state's event callback then run callback
 *        - delay pending, not expired: route event_data to transition_event
 *          (if set), skip event/run, return ELIB_FSM_STATE_INVALID
 *        - delay expires this tick: call entry, then event, then run
 *        A null event_data means "no event": no event callback (state event or
 *        transition_event) is invoked; run still fires on a normal tick.
 * @param ctx Context pointer
 * @param period_ms Tick period in ms; must be > 0 to advance a pending delayed
 *                  transition (0 = no time elapses, so the countdown will not
 *                  progress)
 * @param event_data Opaque event data (nullable; NULL invokes no event callback)
 * @return Current state, or ELIB_FSM_STATE_INVALID during delay
 */
elib_fsm_state_t elib_fsm_cb_poll(elib_fsm_cb_ctx_t *ctx, uint32_t period_ms,
                                  void *event_data);

/**
 * @brief Dispatch an event to the current state's event callback.
 *        During a pending delayed transition the event is routed to
 *        transition_event when set, otherwise it is dropped.
 *        A null event_data means "no event" and invokes no event callback.
 * @param ctx Context pointer
 * @param event_data Opaque event data (nullable; NULL invokes no event callback)
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
 * @brief Get last valid state (the state being left)
 *        Never returns ELIB_FSM_STATE_INVALID during a pending delay: while
 *        current() reports INVALID, previous() still returns the source state.
 * @param ctx Context pointer
 * @return Last valid state, or ELIB_FSM_STATE_INVALID if ctx is NULL/uninitialized
 */
elib_fsm_state_t elib_fsm_cb_previous(const elib_fsm_cb_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_CB_H */
