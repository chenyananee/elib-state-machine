/* elib_fsm_hsm.h - Hierarchical State Machine Main Header */
#ifndef ELIB_FSM_HSM_H
#define ELIB_FSM_HSM_H

#include "elib_fsm_err.h"
#include "elib_fsm_hsm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize hierarchical state machine
 * @param ctx User-allocated context pointer
 * @param states State descriptor table
 * @param state_count Number of state descriptors
 * @param initial Initial state value (may be composite, descends to leaf via initial chain)
 * @param user_data User data passed to all callbacks (nullable)
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_hsm_init(elib_fsm_hsm_ctx_t *ctx,
                                  const elib_fsm_hsm_state_desc_t *states,
                                  size_t state_count,
                                  elib_fsm_state_t initial,
                                  void *user_data);

/**
 * @brief Deinitialize hierarchical state machine
 * @param ctx Context pointer
 */
void elib_fsm_hsm_deinit(elib_fsm_hsm_ctx_t *ctx);

/**
 * @brief Transition to target state (LCA semantics, no delay)
 * @param ctx Context pointer
 * @param target Target state value
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_hsm_goto(elib_fsm_hsm_ctx_t *ctx,
                                  elib_fsm_state_t target);

/**
 * @brief Advance one tick: call leaf state's run callback, return current leaf state
 * @param ctx Context pointer
 * @return Current leaf state, or ELIB_FSM_STATE_INVALID if error
 */
elib_fsm_state_t elib_fsm_hsm_poll(elib_fsm_hsm_ctx_t *ctx);

/**
 * @brief Dispatch event: bubble up active path, return true if handled
 * @param ctx Context pointer
 * @param event Event structure pointer
 * @return true if event was handled by any state, false otherwise
 */
bool elib_fsm_hsm_dispatch(elib_fsm_hsm_ctx_t *ctx,
                            const elib_fsm_hsm_event_t *event);

/**
 * @brief Get current leaf state
 * @param ctx Context pointer
 * @return Current leaf state, or ELIB_FSM_STATE_INVALID if error
 */
elib_fsm_state_t elib_fsm_hsm_current(const elib_fsm_hsm_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_HSM_H */
