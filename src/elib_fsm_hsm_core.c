/* elib_fsm_hsm_core.c - Hierarchical State Machine Core Implementation
 *
 * External transitions on the strict-LCA domain (self transitions,
 * ancestor targets and top-level targets all externally re-enter the
 * target). The table is structurally validated once at init: dangling
 * parent/initial references, leaf-with-initial, non-child initial,
 * duplicate or reserved state values, hierarchy cycles and over-deep
 * tables are all rejected there.
 *
 * Every traversal is iterative and depth-bounded by
 * ELIB_FSM_HSM_MAX_DEPTH: post-validation a valid chain always fits,
 * and the guards make even a runtime-corrupted table unable to loop
 * forever. A goto inside a dispatch handler is detected via the
 * transition counter in the context -- NOT via current's instability,
 * which historically missed same-leaf transitions. */
#include "elib_fsm.h"
#include <string.h>

/* --- helpers ---------------------------------------------------- */

/* Find state descriptor by state value */
static const elib_fsm_hsm_state_desc_t *hsm_find_state(
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

/* Structural validation of the whole table.
 *
 * Depth rule: a node's parent chain may use at most
 * ELIB_FSM_HSM_MAX_DEPTH-1 parent links before reaching the top
 * (root depth 0; the longest active path holds MAX_DEPTH nodes,
 * which is exactly what the internal path buffers guarantee). A chain
 * that does not reach the top inside that bound is either a cycle or
 * too deep -- both rejected with INVALID_PARAM. */
static elib_fsm_err_t hsm_validate_table(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count) {
    size_t i, j;

    for (i = 0; i < state_count; i++) {
        const elib_fsm_hsm_state_desc_t *desc = &states[i];

        if (desc->state == ELIB_FSM_STATE_INVALID) {
            return ELIB_FSM_ERR_INVALID_PARAM;
        }
        for (j = i + 1; j < state_count; j++) {
            if (states[j].state == desc->state) {
                return ELIB_FSM_ERR_INVALID_PARAM;   /* duplicate value */
            }
        }

        /* parent chain: must exist, must reach top within bound */
        {
            elib_fsm_state_t p = desc->parent;
            int links = 0;
            while (p != ELIB_FSM_STATE_INVALID) {
                const elib_fsm_hsm_state_desc_t *pd = hsm_find_state(
                    states, state_count, p);
                if (pd == NULL) {
                    return ELIB_FSM_ERR_STATE_NOT_FOUND;   /* dangling parent */
                }
                links++;
                if (links > ELIB_FSM_HSM_MAX_DEPTH - 1) {
                    return ELIB_FSM_ERR_INVALID_PARAM;   /* cycle or too deep */
                }
                p = pd->parent;
            }
        }

        /* initial: leaves empty; composites point at a DIRECT child */
        if (desc->initial != ELIB_FSM_STATE_INVALID) {
            const elib_fsm_hsm_state_desc_t *cd = hsm_find_state(
                states, state_count, desc->initial);
            if (cd == NULL) {
                return ELIB_FSM_ERR_STATE_NOT_FOUND;   /* dangling initial */
            }
            if (cd->parent != desc->state) {
                return ELIB_FSM_ERR_INVALID_PARAM;   /* not a direct child */
            }
        }
    }

    return ELIB_FSM_OK;
}

/* Resolve a composite's leaf along the initial chain (iterative).
 * Post-validation the chain cannot cycle and always terminates at a
 * leaf inside the depth bound; a failure return is pure defense. */
static elib_fsm_state_t hsm_find_leaf(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state) {
    elib_fsm_state_t s = state;
    int steps = 0;

    while (steps < ELIB_FSM_HSM_MAX_DEPTH) {
        const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
            states, state_count, s);
        if (desc == NULL) {
            return ELIB_FSM_STATE_INVALID;
        }
        if (desc->initial == ELIB_FSM_STATE_INVALID) {
            return s;                       /* leaf reached */
        }
        s = desc->initial;
        steps++;
    }
    return ELIB_FSM_STATE_INVALID;
}

/* Entry callbacks from ancestor DOWN to descendant (excluding
 * ancestor, including descendant). */
static void hsm_enter_path(const elib_fsm_hsm_state_desc_t *states,
                           size_t state_count,
                           elib_fsm_state_t ancestor,
                           elib_fsm_state_t descendant,
                           void *user_data) {
    elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
    int depth = 0;
    elib_fsm_state_t s = descendant;

    while (s != ancestor && s != ELIB_FSM_STATE_INVALID &&
           depth < ELIB_FSM_HSM_MAX_DEPTH) {
        const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
            states, state_count, s);
        if (desc == NULL) {
            break;
        }
        path[depth++] = s;
        s = desc->parent;
    }
    for (int i = depth - 1; i >= 0; i--) {
        const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
            states, state_count, path[i]);
        if (desc != NULL && desc->entry != NULL) {
            desc->entry(path[i], user_data);
        }
    }
}

/* Exit callbacks from descendant UP to ancestor (excluding ancestor,
 * including descendant). Depth-bounded: symmetric with enter_path, so
 * no conceivable table (even runtime-corrupted history tables) can
 * spin this loop forever. */
static void hsm_exit_path(const elib_fsm_hsm_state_desc_t *states,
                          size_t state_count,
                          elib_fsm_state_t descendant,
                          elib_fsm_state_t ancestor,
                          void *user_data) {
    elib_fsm_state_t s = descendant;
    int depth = 0;

    while (s != ancestor && s != ELIB_FSM_STATE_INVALID &&
           depth < ELIB_FSM_HSM_MAX_DEPTH) {
        const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
            states, state_count, s);
        if (desc == NULL) {
            break;
        }
        if (desc->exit != NULL) {
            desc->exit(s, user_data);
        }
        s = desc->parent;
        depth++;
    }
}

/* Lowest strict ancestor of `source` that is also a strict ancestor
 * of `target` (the transition domain). Because the domain is strict,
 * self/ancestor targets overshoot one level: the target is externally
 * exited and re-entered. INVALID means "no common ancestor": the
 * caller tears down and rebuilds the whole configuration (includes
 * the top-level state itself, whose parent is INVALID). */
static elib_fsm_state_t hsm_compute_domain(
    const elib_fsm_hsm_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t source,
    elib_fsm_state_t target) {
    const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
        states, state_count, source);
    elib_fsm_state_t anc = (desc != NULL) ? desc->parent
                                          : ELIB_FSM_STATE_INVALID;
    int steps = 0;

    while (anc != ELIB_FSM_STATE_INVALID &&
           steps < ELIB_FSM_HSM_MAX_DEPTH) {
        /* is `anc` a strict ancestor of `target`? */
        const elib_fsm_hsm_state_desc_t *td = hsm_find_state(
            states, state_count, target);
        elib_fsm_state_t p = (td != NULL) ? td->parent
                                          : ELIB_FSM_STATE_INVALID;
        int hops = 0;
        bool found = false;

        while (p != ELIB_FSM_STATE_INVALID &&
               hops < ELIB_FSM_HSM_MAX_DEPTH) {
            const elib_fsm_hsm_state_desc_t *pd = hsm_find_state(
                states, state_count, p);
            if (p == anc) {
                found = true;
                break;
            }
            p = (pd != NULL) ? pd->parent : ELIB_FSM_STATE_INVALID;
            hops++;
        }
        if (found) {
            return anc;
        }

        desc = hsm_find_state(states, state_count, anc);
        anc = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
        steps++;
    }
    return ELIB_FSM_STATE_INVALID;
}

/* The whole active path leaf -> top, for init's top-down entry walk. */
static void hsm_init_entries(const elib_fsm_hsm_state_desc_t *states,
                             size_t state_count,
                             elib_fsm_state_t leaf,
                             void *user_data) {
    elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
    int depth = 0;
    elib_fsm_state_t s = leaf;

    /* s becomes NULL state only via table corruption; loop guard
     * bounds the valid chain (leaf depth+1 nodes <= MAX_DEPTH) */
    while (s != ELIB_FSM_STATE_INVALID && depth < ELIB_FSM_HSM_MAX_DEPTH) {
        path[depth++] = s;
        const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
            states, state_count, s);
        if (desc == NULL) {
            break;
        }
        s = desc->parent;
    }
    for (int i = depth - 1; i >= 0; i--) {
        const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
            states, state_count, path[i]);
        if (desc != NULL && desc->entry != NULL) {
            desc->entry(path[i], user_data);
        }
    }
}

/* --- public API -------------------------------------------------- */

elib_fsm_err_t elib_fsm_hsm_init(elib_fsm_hsm_ctx_t *ctx,
                                  const elib_fsm_hsm_state_desc_t *states,
                                  size_t state_count,
                                  elib_fsm_state_t initial,
                                  void *user_data) {
    elib_fsm_state_t leaf;
    elib_fsm_err_t err;

    if (ctx == NULL || states == NULL || state_count == 0) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }

    err = hsm_validate_table(states, state_count);
    if (err != ELIB_FSM_OK) {
        return err;
    }

    if (hsm_find_state(states, state_count, initial) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    leaf = hsm_find_leaf(states, state_count, initial);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        /* unreachable after validation; kept as defense */
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    memset(ctx, 0, sizeof(elib_fsm_hsm_ctx_t));

    ctx->states = states;
    ctx->state_count = state_count;
    ctx->user_data = user_data;
    ctx->current = leaf;
    ctx->transition_seq = 0;
    ctx->bit_flags.initialized = 1;

    /* entry callbacks top-down along the whole active path, INSIDE
     * init (post-initialized: entry callbacks may legally goto) */
    hsm_init_entries(states, state_count, leaf, user_data);

    return ELIB_FSM_OK;
}

void elib_fsm_hsm_deinit(elib_fsm_hsm_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->bit_flags.initialized = 0;
}

elib_fsm_state_t elib_fsm_hsm_current(const elib_fsm_hsm_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}

elib_fsm_err_t elib_fsm_hsm_goto(elib_fsm_hsm_ctx_t *ctx,
                                  elib_fsm_state_t target) {
    elib_fsm_state_t source, domain, leaf;

    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }
    if (hsm_find_state(ctx->states, ctx->state_count, target) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    /* Resolve everything BEFORE any callback fires: a goto either
     * completes or leaves the machine untouched. */
    leaf = hsm_find_leaf(ctx->states, ctx->state_count, target);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        /* unreachable after init validation; kept as defense */
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    source = ctx->current;
    domain = hsm_compute_domain(ctx->states, ctx->state_count, source, target);

    hsm_exit_path(ctx->states, ctx->state_count, source, domain, ctx->user_data);
    hsm_enter_path(ctx->states, ctx->state_count, domain, target, ctx->user_data);

    /* target composite: descend to its (initial-chain) leaf */
    if (leaf != target) {
        hsm_enter_path(ctx->states, ctx->state_count, target, leaf,
                       ctx->user_data);
    }

    ctx->current = leaf;
    ctx->transition_seq++;   /* dispatch detects gotos via this */

    return ELIB_FSM_OK;
}

elib_fsm_state_t elib_fsm_hsm_poll(elib_fsm_hsm_ctx_t *ctx) {
    const elib_fsm_hsm_state_desc_t *desc;

    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    desc = hsm_find_state(ctx->states, ctx->state_count, ctx->current);
    if (desc != NULL && desc->run != NULL) {
        desc->run(ctx->user_data);
    }

    return ctx->current;
}

bool elib_fsm_hsm_dispatch(elib_fsm_hsm_ctx_t *ctx, void *event_data) {
    elib_fsm_state_t state;

    if (ctx == NULL || !ctx->bit_flags.initialized || event_data == NULL) {
        return false;
    }

    state = ctx->current;
    while (state != ELIB_FSM_STATE_INVALID) {
        const elib_fsm_hsm_state_desc_t *desc = hsm_find_state(
            ctx->states, ctx->state_count, state);
        if (desc == NULL) {
            return false;   /* defensive: table corrupted at runtime */
        }

        uint32_t seq_before = ctx->transition_seq;
        bool handled = (desc->handler != NULL) &&
                       desc->handler(event_data, ctx->user_data);

        /* A goto inside the handler stops bubbling -- detected via
         * the transition counter, so it holds even when the goto
         * resolves back to the SAME leaf (self/ancestor targets). */
        if (ctx->transition_seq != seq_before) {
            return true;
        }
        if (handled) {
            return true;
        }

        state = desc->parent;
    }

    return false;
}
