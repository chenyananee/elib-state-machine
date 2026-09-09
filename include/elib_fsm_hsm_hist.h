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
 * @brief Transition to target state (uses last_active to find leaf)
 * @param ctx Context pointer
 * @param target Target state value
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_hsm_hist_goto(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_state_t target);

/**
 * @brief Reset state machine: restore all last_active to initial, re-enter initial path
 * @param ctx Context pointer
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_hsm_hist_reset(elib_fsm_hsm_hist_ctx_t *ctx);

/**
 * @brief Advance one tick: call leaf state's run callback, return current leaf state
 * @param ctx Context pointer
 * @return Current leaf state, or ELIB_FSM_STATE_INVALID if error
 */
elib_fsm_state_t elib_fsm_hsm_hist_poll(elib_fsm_hsm_hist_ctx_t *ctx);

/**
 * @brief Dispatch event: bubble up active path, return true if handled
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
