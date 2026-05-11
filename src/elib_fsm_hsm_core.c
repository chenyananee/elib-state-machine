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

/* Compute lowest common ancestor of source and target */
elib_fsm_state_t elib_fsm_hsm_compute_lca(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t source,
    elib_fsm_state_t target) {
    /* Walk from target upward; first ancestor that is also an ancestor of source is the LCA */
    elib_fsm_state_t t = target;
    while (t != ELIB_FSM_STATE_INVALID) {
        elib_fsm_state_t s = source;
        while (s != ELIB_FSM_STATE_INVALID) {
            if (s == t) return t;
            const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(states, state_count, s);
            s = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
        }
        const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(states, state_count, t);
        t = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }
    return ELIB_FSM_STATE_INVALID;
}

/* Call entry callbacks from ancestor down to descendant (excluding ancestor, including descendant) */
void elib_fsm_hsm_enter_path(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t ancestor,
    elib_fsm_state_t descendant,
    void *user_data) {
    elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
    int depth = 0;
    elib_fsm_state_t s = descendant;
    while (s != ancestor && s != ELIB_FSM_STATE_INVALID && depth < ELIB_FSM_HSM_MAX_DEPTH) {
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
}

/* Call exit callbacks from descendant up to ancestor (excluding ancestor, including descendant) */
void elib_fsm_hsm_exit_path(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t descendant,
    elib_fsm_state_t ancestor,
    void *user_data) {
    elib_fsm_state_t s = descendant;
    while (s != ancestor && s != ELIB_FSM_STATE_INVALID) {
        const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(states, state_count, s);
        if (desc != NULL && desc->exit != NULL) {
            desc->exit(s, user_data);
        }
        s = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }
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

/* Transition to target state (LCA semantics) */
elib_fsm_err_t elib_fsm_hsm_goto(elib_fsm_hsm_ctx_t *ctx,
                                  elib_fsm_state_t target) {
    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }

    /* Validate target state exists */
    if (elib_fsm_hsm_find_state(ctx->states, ctx->state_count, target) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    elib_fsm_state_t source = ctx->current;

    /* Compute LCA */
    elib_fsm_state_t lca = elib_fsm_hsm_compute_lca(ctx->states, ctx->state_count, source, target);

    /* Exit from source up to LCA (exclusive) */
    elib_fsm_hsm_exit_path(ctx->states, ctx->state_count, source, lca, ctx->user_data);

    /* Enter from LCA down to target (exclusive of LCA, inclusive of target) */
    elib_fsm_hsm_enter_path(ctx->states, ctx->state_count, lca, target, ctx->user_data);

    /* If target is composite, follow initial chain */
    elib_fsm_state_t leaf = elib_fsm_hsm_find_leaf(ctx->states, ctx->state_count, target);
    if (leaf != target && leaf != ELIB_FSM_STATE_INVALID) {
        elib_fsm_hsm_enter_path(ctx->states, ctx->state_count, target, leaf, ctx->user_data);
    }

    ctx->current = (leaf != ELIB_FSM_STATE_INVALID) ? leaf : target;

    return ELIB_FSM_OK;
}

/* Advance one tick: call leaf state's run callback, return current leaf state */
elib_fsm_state_t elib_fsm_hsm_poll(elib_fsm_hsm_ctx_t *ctx) {
    if (ctx == NULL || !ctx->initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(
        ctx->states, ctx->state_count, ctx->current);
    if (desc != NULL && desc->run != NULL) {
        desc->run(ctx->user_data);
    }

    return ctx->current;
}

/* Dispatch event: bubble up active path, return true if handled */
bool elib_fsm_hsm_dispatch(elib_fsm_hsm_ctx_t *ctx,
                            elib_fsm_event_t event) {
    if (ctx == NULL || !ctx->initialized) {
        return false;
    }

    elib_fsm_state_t state = ctx->current;
    while (state != ELIB_FSM_STATE_INVALID) {
        const elib_fsm_hsm_state_desc_t *desc = elib_fsm_hsm_find_state(
            ctx->states, ctx->state_count, state);
        if (desc == NULL) {
            return false;
        }

        elib_fsm_state_t before = ctx->current;
        bool handled = (desc->handler != NULL) && desc->handler(event, ctx->user_data);

        /* If goto was called inside handler, stop bubbling */
        if (ctx->current != before) {
            return true;
        }

        if (handled) {
            return true;
        }

        state = desc->parent;
    }

    return false;
}
