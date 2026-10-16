/* elib_fsm_hsm_hist_core.c - Hierarchical State Machine with History Core Implementation */
#include "elib_fsm_hsm_hist_core.h"
#include <string.h>

/* --- Internal helpers --- */

/* Find state descriptor by state value */
elib_fsm_hsm_hist_state_desc_t *elib_fsm_hsm_hist_find_state(
    elib_fsm_hsm_hist_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state) {
    for (size_t i = 0; i < state_count; i++) {
        if (states[i].state == state) {
            return &states[i];
        }
    }
    return NULL;
}

/* Find leaf by following last_active chain (with initial fallback) */
elib_fsm_state_t elib_fsm_hsm_hist_find_leaf(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t state) {
    elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
        ctx->states, ctx->state_count, state);
    if (desc == NULL) {
        return ELIB_FSM_STATE_INVALID;
    }

    /* Leaf: no last_active and no initial */
    if (desc->last_active == ELIB_FSM_STATE_INVALID &&
        desc->initial == ELIB_FSM_STATE_INVALID) {
        return state;
    }

    /* Prefer last_active, fallback to initial */
    elib_fsm_state_t child = (desc->last_active != ELIB_FSM_STATE_INVALID)
                             ? desc->last_active
                             : desc->initial;
    return elib_fsm_hsm_hist_find_leaf(ctx, child);
}

/* Record, for every composite on the path to `leaf`, which immediate child
 * leads to it. This matches init's convention (last_active = immediate child)
 * and keeps find_leaf's last_active-first descent correct. */
void elib_fsm_hsm_hist_set_path(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t target,
    elib_fsm_state_t leaf) {
    (void)target; /* `leaf` alone identifies the path */
    elib_fsm_state_t child = leaf;
    while (child != ELIB_FSM_STATE_INVALID) {
        elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, child);
        if (desc == NULL) {
            break;
        }
        elib_fsm_state_t parent = desc->parent;
        if (parent == ELIB_FSM_STATE_INVALID) {
            break;
        }
        elib_fsm_hsm_hist_state_desc_t *pd = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, parent);
        /* Only composite states (have initial) track history */
        if (pd != NULL && pd->initial != ELIB_FSM_STATE_INVALID) {
            pd->last_active = child;
        }
        child = parent;
    }
}

/* Is `anc` a strict ancestor of `state` (lies on state's parent chain)? */
static bool elib_fsm_hsm_hist_is_strict_ancestor(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t anc,
    elib_fsm_state_t state) {
    elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
        ctx->states, ctx->state_count, state);
    elib_fsm_state_t p = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    while (p != ELIB_FSM_STATE_INVALID) {
        if (p == anc) {
            return true;
        }
        desc = elib_fsm_hsm_hist_find_state(ctx->states, ctx->state_count, p);
        p = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }
    return false;
}

/* Compute the transition domain: lowest strict ancestor of source that is also
 * a strict ancestor of target. Same semantics as elib_fsm_hsm_compute_domain;
 * INVALID means "no common ancestor -> exit/enter the whole configuration". */
static elib_fsm_state_t elib_fsm_hsm_hist_compute_domain(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t source,
    elib_fsm_state_t target) {
    elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
        ctx->states, ctx->state_count, source);
    elib_fsm_state_t anc = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    while (anc != ELIB_FSM_STATE_INVALID) {
        if (elib_fsm_hsm_hist_is_strict_ancestor(ctx, anc, target)) {
            return anc;
        }
        desc = elib_fsm_hsm_hist_find_state(ctx->states, ctx->state_count, anc);
        anc = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }
    return ELIB_FSM_STATE_INVALID;
}

/* Enter path from ancestor down to descendant (exclusive of ancestor, inclusive of descendant) */
static void elib_fsm_hsm_hist_enter_path(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t ancestor,
    elib_fsm_state_t descendant,
    void *user_data) {
    elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
    int depth = 0;
    elib_fsm_state_t s = descendant;
    while (s != ancestor && s != ELIB_FSM_STATE_INVALID && depth < ELIB_FSM_HSM_MAX_DEPTH) {
        path[depth++] = s;
        elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, s);
        s = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }
    for (int i = depth - 1; i >= 0; i--) {
        elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, path[i]);
        if (desc != NULL && desc->entry != NULL) {
            desc->entry(path[i], user_data);
        }
    }
}

/* Exit path from descendant up to ancestor (exclusive of ancestor, inclusive of descendant) */
static void elib_fsm_hsm_hist_exit_path(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t descendant,
    elib_fsm_state_t ancestor,
    void *user_data) {
    elib_fsm_state_t s = descendant;
    while (s != ancestor && s != ELIB_FSM_STATE_INVALID) {
        elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, s);
        if (desc != NULL && desc->exit != NULL) {
            desc->exit(s, user_data);
        }
        s = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }
}

/* Initialize entry callbacks for a path */
static void elib_fsm_hsm_hist_init_entries(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t leaf) {
    elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
    int depth = 0;
    elib_fsm_state_t s = leaf;
    while (s != ELIB_FSM_STATE_INVALID && depth < ELIB_FSM_HSM_MAX_DEPTH) {
        path[depth++] = s;
        elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, s);
        s = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
    }
    for (int i = depth - 1; i >= 0; i--) {
        elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, path[i]);
        if (desc != NULL && desc->entry != NULL) {
            desc->entry(path[i], ctx->user_data);
        }
    }
}

/* --- Public API --- */

/* Initialize hierarchical state machine with history */
elib_fsm_err_t elib_fsm_hsm_hist_init(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_hsm_hist_state_desc_t *states,
                                       size_t state_count,
                                       elib_fsm_state_t initial,
                                       void *user_data) {
    if (ctx == NULL || states == NULL || state_count == 0) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }

    /* Validate initial state exists */
    if (elib_fsm_hsm_hist_find_state(states, state_count, initial) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    memset(ctx, 0, sizeof(elib_fsm_hsm_hist_ctx_t));

    /* Set all last_active = initial */
    for (size_t i = 0; i < state_count; i++) {
        states[i].last_active = states[i].initial;
    }

    ctx->states = states;
    ctx->state_count = state_count;
    ctx->user_data = user_data;

    /* Follow last_active chain to leaf */
    elib_fsm_state_t leaf = elib_fsm_hsm_hist_find_leaf(ctx, initial);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }
    ctx->current = leaf;
    ctx->bit_flags.initialized = 1;

    /* Call entry for each state on the path from top-level ancestor down to leaf */
    elib_fsm_hsm_hist_init_entries(ctx, leaf);

    return ELIB_FSM_OK;
}

/* Deinitialize hierarchical state machine with history */
void elib_fsm_hsm_hist_deinit(elib_fsm_hsm_hist_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->bit_flags.initialized = 0;
}

/* Transition to target state (uses last_active to find leaf) */
elib_fsm_err_t elib_fsm_hsm_hist_goto(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_state_t target) {
    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }

    /* Validate target state exists */
    if (elib_fsm_hsm_hist_find_state(ctx->states, ctx->state_count, target) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    elib_fsm_state_t source = ctx->current;

    /* Resolve the effective target leaf: last_active (history) first, else initial */
    elib_fsm_state_t leaf = elib_fsm_hsm_hist_find_leaf(ctx, target);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    /* External transition: domain = strict LCA of source and the requested
     * target (INVALID => no common ancestor, exit/enter whole configuration).
     * A transition to the current state or to an ancestor is external, so the
     * target is exited and re-entered. */
    elib_fsm_state_t domain = elib_fsm_hsm_hist_compute_domain(ctx, source, target);

    /* Exit from source up to the domain */
    elib_fsm_hsm_hist_exit_path(ctx, source, domain, ctx->user_data);

    /* Enter from the domain down to the requested target */
    elib_fsm_hsm_hist_enter_path(ctx, domain, target, ctx->user_data);

    /* If the target is composite, descend to its effective (history) leaf */
    if (leaf != target) {
        elib_fsm_hsm_hist_enter_path(ctx, target, leaf, ctx->user_data);
    }

    /* Record history along the target path */
    elib_fsm_hsm_hist_set_path(ctx, target, leaf);

    ctx->current = leaf;
    return ELIB_FSM_OK;
}

/* Reset: clear history (all last_active = initial) and re-enter `target` */
elib_fsm_err_t elib_fsm_hsm_hist_reset(elib_fsm_hsm_hist_ctx_t *ctx,
                                        elib_fsm_state_t target) {
    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }

    /* Validate target state exists */
    if (elib_fsm_hsm_hist_find_state(ctx->states, ctx->state_count, target) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    /* Clear history: every state falls back to its initial substate */
    for (size_t i = 0; i < ctx->state_count; i++) {
        ctx->states[i].last_active = ctx->states[i].initial;
    }

    /* Resolve the target's clean leaf via the freshly restored initial chains */
    elib_fsm_state_t leaf = elib_fsm_hsm_hist_find_leaf(ctx, target);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    /* Record history along the target path: ancestors above the transition
     * domain are not exited/re-entered, so their last_active must be pointed at
     * the new branch explicitly. */
    elib_fsm_hsm_hist_set_path(ctx, target, leaf);

    elib_fsm_state_t source = ctx->current;

    /* Already at the resolved leaf: history cleared/recorded, nothing to do */
    if (source == leaf) {
        return ELIB_FSM_OK;
    }

    /* Compute transition domain (strict LCA) */
    elib_fsm_state_t domain = elib_fsm_hsm_hist_compute_domain(ctx, source, leaf);

    /* Exit from source up to the domain */
    elib_fsm_hsm_hist_exit_path(ctx, source, domain, ctx->user_data);

    /* Enter from the domain down to the resolved leaf */
    elib_fsm_hsm_hist_enter_path(ctx, domain, leaf, ctx->user_data);

    ctx->current = leaf;
    return ELIB_FSM_OK;
}

/* Advance one tick: call leaf state's run callback, return current leaf state */
elib_fsm_state_t elib_fsm_hsm_hist_poll(elib_fsm_hsm_hist_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
        ctx->states, ctx->state_count, ctx->current);
    if (desc != NULL && desc->run != NULL) {
        desc->run(ctx->user_data);
    }

    return ctx->current;
}

/* Dispatch event: bubble up active path, return true if handled */
bool elib_fsm_hsm_hist_dispatch(elib_fsm_hsm_hist_ctx_t *ctx,
                                 void *event_data) {
    /* A null event_data means "no event": nothing is dispatched. */
    if (ctx == NULL || !ctx->bit_flags.initialized || event_data == NULL) {
        return false;
    }

    elib_fsm_state_t state = ctx->current;
    while (state != ELIB_FSM_STATE_INVALID) {
        elib_fsm_hsm_hist_state_desc_t *desc = elib_fsm_hsm_hist_find_state(
            ctx->states, ctx->state_count, state);
        if (desc == NULL) {
            return false;
        }

        elib_fsm_state_t before = ctx->current;
        bool handled = (desc->handler != NULL) && desc->handler(event_data, ctx->user_data);

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

/* Get current leaf state */
elib_fsm_state_t elib_fsm_hsm_hist_current(const elib_fsm_hsm_hist_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}
