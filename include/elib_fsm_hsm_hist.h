/* elib_fsm_hsm_hist.h - Hierarchical State Machine with History Main Header */
#ifndef ELIB_FSM_HSM_HIST_H
#define ELIB_FSM_HSM_HIST_H

#include "elib_fsm_err.h"
#include "elib_fsm_hsm_hist_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize hierarchical state machine with history
 * @param ctx User-allocated context pointer
 * @param states State descriptor table (non-const, last_active is modified at runtime)
 * @param state_count Number of state descriptors
 * @param initial Initial state value
 * @param user_data User data passed to all callbacks (nullable)
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_hsm_hist_init(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_hsm_hist_state_desc_t *states,
                                       size_t state_count,
                                       elib_fsm_state_t initial,
                                       void *user_data);

/**
 * @brief Deinitialize hierarchical state machine with history
 * @param ctx Context pointer
 */
void elib_fsm_hsm_hist_deinit(elib_fsm_hsm_hist_ctx_t *ctx);

/**
 * @brief Transition to target state (external transition, strict-LCA domain;
 *        resolves the target leaf via last_active history, else initial)
 *        A transition to an ancestor or to the current state is external: the
 *        target is exited and re-entered, then history/initial descends to a leaf.
 * @param ctx Context pointer
 * @param target Target state value
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_hsm_hist_goto(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_state_t target);

/**
 * @brief Reset: clear history (all last_active = initial) and re-enter `target`
 *        `target` is descended via fresh initial chains (history just cleared);
 *        ancestors above the transition domain get their last_active updated.
 *        If the machine is already at the resolved leaf, no exit/entry fires.
 * @param ctx Context pointer
 * @param target State to reset into (pass the machine's initial/top-level state
 *               for the classic "back to initial" behavior)
 * @return ELIB_FSM_ERR_INVALID_PARAM if ctx is NULL; ELIB_FSM_ERR_NOT_INITIALIZED
 *         if not initialized; ELIB_FSM_ERR_STATE_NOT_FOUND if target is not in
 *         the descriptor table
 */
elib_fsm_err_t elib_fsm_hsm_hist_reset(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_state_t target);

/**
 * @brief Advance one tick: call leaf state's run callback, return current leaf state
 * @param ctx Context pointer
 * @return Current leaf state, or ELIB_FSM_STATE_INVALID if error
 */
elib_fsm_state_t elib_fsm_hsm_hist_poll(elib_fsm_hsm_hist_ctx_t *ctx);

/**
 * @brief Dispatch event: bubble up active path, return true if handled
 *        A null event_data means "no event" and returns false without calling
 *        any handler.
 * @param ctx Context pointer
 * @param event_data Event data pointer (user-defined type, passed to handler)
 * @return true if event was handled by any state, false otherwise
 */
bool elib_fsm_hsm_hist_dispatch(elib_fsm_hsm_hist_ctx_t *ctx,
                                 void *event_data);

/**
 * @brief Get current leaf state
 * @param ctx Context pointer
 * @return Current leaf state, or ELIB_FSM_STATE_INVALID if error
 */
elib_fsm_state_t elib_fsm_hsm_hist_current(const elib_fsm_hsm_hist_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_HSM_HIST_H */
