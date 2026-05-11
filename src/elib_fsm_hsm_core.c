/* elib_fsm_hsm_core.c - Hierarchical State Machine Core Implementation */
#include "elib_fsm_hsm_core.h"
#include <string.h>

/* --- Internal helpers --- */

/* Find state descriptor by state value */
const elib_fsm_hsm_state_desc_t *elib_fsm_hsm_find_state(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state) {
    for (size_t i = 0; i < state_count; i++) {
        if (states[i].state == state) {
            return &states[i];
        }
    }
    return NULL;
}

/* Find leaf state by following initial chain */
elib_fsm_state_t elib_fsm_hsm_find_leaf(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state) {
    const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(states, state_count, state);
    while (desc != NULL && desc->initial != ELIB_FSM_STATE_INVALID) {
        desc = elib_fsm_hsm_find_state(states, state_count, desc->initial);
        if (desc == NULL) return ELIB_FSM_STATE_INVALID;
    }
    return desc != NULL ? desc->state : ELIB_FSM_STATE_INVALID;
}

/* --- Public API --- */

/* Initialize hierarchical state machine */
elib_fsm_err_t elib_fsm_hsm_init(elib_fsm_hsm_ctx_t *ctx,
                                  const elib_fsm_hsm_state_desc_t *states,
                                  size_t state_count,
                                  elib_fsm_state_t initial,
                                  void *user_data) {
    if (ctx == NULL || states == NULL || state_count == 0) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }

    /* Validate initial state exists */
    if (elib_fsm_hsm_find_state(states, state_count, initial) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    memset(ctx, 0, sizeof(elib_fsm_hsm_ctx_t));

    ctx->states = states;
    ctx->state_count = state_count;
    ctx->user_data = user_data;

    /* Follow initial chain to leaf */
    elib_fsm_state_t leaf = elib_fsm_hsm_find_leaf(states, state_count, initial);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }
    ctx->current = leaf;
    ctx->initialized = 1;

    /* Call entry for each state on the path from top-level ancestor down to leaf */
    elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
    int depth = 0;
    elib_fsm_state_t s = leaf;
    while (s != ELIB_FSM_STATE_INVALID && depth < ELIB_FSM_HSM_MAX_DEPTH) {
        path[depth++] = s;
        const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(states, state_count, s);
        s = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }

    for (int i = depth - 1; i >= 0; i--) {
        const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(states, state_count, path[i]);
        if (desc != NULL && desc->entry != NULL) {
            desc->entry(path[i], user_data);
        }
    }

    return ELIB_FSM_OK;
}

/* Deinitialize hierarchical state machine */
void elib_fsm_hsm_deinit(elib_fsm_hsm_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->initialized = 0;
}

/* Get current leaf state */
elib_fsm_state_t elib_fsm_hsm_current(const elib_fsm_hsm_ctx_t *ctx) {
    if (ctx == NULL || !ctx->initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}

/* Stub: goto - implemented in Task 3 */
elib_fsm_err_t elib_fsm_hsm_goto(elib_fsm_hsm_ctx_t *ctx,
                                  elib_fsm_state_t target) {
    (void)ctx; (void)target;
    return ELIB_FSM_ERR_NOT_INITIALIZED;
}

/* Stub: poll - implemented in Task 4 */
elib_fsm_state_t elib_fsm_hsm_poll(elib_fsm_hsm_ctx_t *ctx) {
    if (ctx == NULL || !ctx->initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}

/* Stub: dispatch - implemented in Task 5 */
bool elib_fsm_hsm_dispatch(elib_fsm_hsm_ctx_t *ctx,
                            elib_fsm_event_t event) {
    (void)ctx; (void)event;
    return false;
}
