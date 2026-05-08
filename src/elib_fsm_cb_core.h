/* elib_fsm_cb_core.h - Callback State Machine Internal Header */
#ifndef ELIB_FSM_CB_CORE_H
#define ELIB_FSM_CB_CORE_H

#include "../include/elib_fsm_cb.h"

/* Internal: find state descriptor by state value */
const elib_fsm_cb_state_desc_t *elib_fsm_cb_find_state(
    const elib_fsm_cb_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state);

#endif /* ELIB_FSM_CB_CORE_H */
