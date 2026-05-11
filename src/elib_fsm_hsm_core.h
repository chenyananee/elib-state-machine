/* elib_fsm_hsm_core.h - Hierarchical State Machine Internal Header */
#ifndef ELIB_FSM_HSM_CORE_H
#define ELIB_FSM_HSM_CORE_H

#include "../include/elib_fsm_hsm.h"

/* Maximum hierarchy depth for stack-allocated path arrays */
#define ELIB_FSM_HSM_MAX_DEPTH 16

/* Internal: find state descriptor by state value */
const elib_fsm_hsm_state_desc_t *elib_fsm_hsm_find_state(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state);

/* Internal: find leaf state by following initial chain */
elib_fsm_state_t elib_fsm_hsm_find_leaf(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state);

/* Internal: compute lowest common ancestor of two states */
elib_fsm_state_t elib_fsm_hsm_compute_lca(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t source,
    elib_fsm_state_t target);

/* Internal: call entry callbacks from ancestor down to descendant (excluding ancestor, including descendant) */
void elib_fsm_hsm_enter_path(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t ancestor,
    elib_fsm_state_t descendant,
    void *user_data);

/* Internal: call exit callbacks from descendant up to ancestor (excluding ancestor, including descendant) */
void elib_fsm_hsm_exit_path(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t descendant,
    elib_fsm_state_t ancestor,
    void *user_data);

#endif /* ELIB_FSM_HSM_CORE_H */
