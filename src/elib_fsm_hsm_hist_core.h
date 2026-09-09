/* elib_fsm_hsm_hist_core.h - Hierarchical State Machine with History Internal Header */
#ifndef ELIB_FSM_HSM_HIST_CORE_H
#define ELIB_FSM_HSM_HIST_CORE_H

#include "../include/elib_fsm_hsm_hist.h"
#include "elib_fsm_hsm_core.h"

/* Internal: find state descriptor by state value */
elib_fsm_hsm_hist_state_desc_t *elib_fsm_hsm_hist_find_state(
    elib_fsm_hsm_hist_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state);

/* Internal: find leaf by following last_active chain */
elib_fsm_state_t elib_fsm_hsm_hist_find_leaf(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t state);

/* Internal: set last_active on path from target up to top-level */
void elib_fsm_hsm_hist_set_path(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t target,
    elib_fsm_state_t leaf);

#endif /* ELIB_FSM_HSM_HIST_CORE_H */
