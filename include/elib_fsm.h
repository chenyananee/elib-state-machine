/* elib_fsm.h - Switch/Case State Machine Main Header */
#ifndef ELIB_FSM_H
#define ELIB_FSM_H

#include "elib_fsm_err.h"
#include "elib_fsm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize switch/case state machine
 * @param ctx User-allocated context pointer
 * @param initial Initial state value
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_init(elib_fsm_ctx_t *ctx,
                              elib_fsm_state_t initial);

/**
 * @brief Deinitialize state machine
 * @param ctx Context pointer
 */
void elib_fsm_deinit(elib_fsm_ctx_t *ctx);

/**
 * @brief Jump to target state, immediate or delayed
 * @param ctx Context pointer
 * @param target Target state value
 * @param delay_ms Delay in milliseconds, 0 = immediate
 * @return elib_fsm_err_t error code
 */
elib_fsm_err_t elib_fsm_goto(elib_fsm_ctx_t *ctx,
                              elib_fsm_state_t target,
                              uint32_t delay_ms);

/**
 * @brief Advance one tick period and get current state
 * @param ctx Context pointer
 * @param period_ms Tick period in milliseconds
 * @return Current state, or ELIB_FSM_STATE_INVALID if delay pending / error
 */
elib_fsm_state_t elib_fsm_poll(elib_fsm_ctx_t *ctx, uint32_t period_ms);

/**
 * @brief Get current state
 * @param ctx Context pointer
 * @return Current state, or ELIB_FSM_STATE_INVALID if ctx is NULL/uninitialized
 */
elib_fsm_state_t elib_fsm_current(const elib_fsm_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_H */
