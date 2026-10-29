/* elib_fsm_hsm_hist_core.c - Hierarchical State Machine with History
 *
 * The hsm engine with deep history: every composite on the active
 * path remembers its last active branch (last_active chains), so a
 * goto back into a subtree resumes where it was left. The descriptor
 * table is deliberately NON-const -- last_active is framework-managed
 * runtime state living inside the table (per project decision), which
 * is why this flavour's table must live in RAM and why the run-time
 * guards are thicker here than anywhere else in the family.
 *
 * Init validation is identical to elib_fsm_hsm. reset() wipes all
 * history, re-records the target chain explicitly (including
 * ancestors above the transition domain, which are not exited and
 * re-entered), and lands on the clean initial leaf. */
#include "elib_fsm.h"
#include <string.h>

/* --- helpers ------------------------------------------------------ */

static elib_fsm_hsm_hist_state_desc_t *hist_find_state(
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

/* read-only twin of hist_find_state (used by validation) */
static const elib_fsm_hsm_hist_state_desc_t *hist_find_state_c(
    const elib_fsm_hsm_hist_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state) {
    for (size_t i = 0; i < state_count; i++) {
        if (states[i].state == state) {
            return &states[i];
        }
    }
    return NULL;
}

/* Structural validation: same rules as the const hsm table. */
static elib_fsm_err_t hist_validate_table(
    const elib_fsm_hsm_hist_state_desc_t *states,
    size_t state_count) {
    size_t i, j;

    for (i = 0; i < state_count; i++) {
        const elib_fsm_hsm_hist_state_desc_t *desc = &states[i];

        if (desc->state == ELIB_FSM_STATE_INVALID) {
            return ELIB_FSM_ERR_INVALID_PARAM;
        }
        for (j = i + 1; j < state_count; j++) {
            if (states[j].state == desc->state) {
                return ELIB_FSM_ERR_INVALID_PARAM;
            }
        }

        {
            elib_fsm_state_t p = desc->parent;
            int links = 0;
            while (p != ELIB_FSM_STATE_INVALID) {
                const elib_fsm_hsm_hist_state_desc_t *pd = hist_find_state_c(
                    states, state_count, p);
                if (pd == NULL) {
                    return ELIB_FSM_ERR_STATE_NOT_FOUND;
                }
                links++;
                if (links > ELIB_FSM_HSM_MAX_DEPTH - 1) {
                    return ELIB_FSM_ERR_INVALID_PARAM; /* cycle or too deep */
                }
                p = pd->parent;
            }
        }

        if (desc->initial != ELIB_FSM_STATE_INVALID) {
            const elib_fsm_hsm_hist_state_desc_t *cd = hist_find_state_c(
                states, state_count, desc->initial);
            if (cd == NULL) {
                return ELIB_FSM_ERR_STATE_NOT_FOUND;
            }
            if (cd->parent != desc->state) {
                return ELIB_FSM_ERR_INVALID_PARAM; /* not a direct child */
            }
        }
    }

    return ELIB_FSM_OK;
}

/* Resolve the effective leaf: last_active-first (deep history),
 * falling back to the initial chain. Iterative and depth-bounded --
 * never recursive (a corrupted runtime table must not smash the
 * stack). */
static elib_fsm_state_t hist_find_leaf(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_state_t state) {
    elib_fsm_state_t s = state;
    int steps = 0;

    while (steps < ELIB_FSM_HSM_MAX_DEPTH) {
        elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
            ctx->states, ctx->state_count, s);
        if (desc == NULL) {
            return ELIB_FSM_STATE_INVALID;
        }
        if (desc->last_active == ELIB_FSM_STATE_INVALID &&
            desc->initial == ELIB_FSM_STATE_INVALID) {
            return s;                              /* leaf */
        }
        if (desc->last_active != ELIB_FSM_STATE_INVALID) {
            /* history guard: last_active MUST be a direct child
             * (framework-managed, so a violation means the user poked
             * the table at runtime); fall back would be a lie -- the
             * recorded branch is the recorded branch. Defensive:
             * treat as unresolvable rather than follow a bogus link. */
            elib_fsm_hsm_hist_state_desc_t *cd = hist_find_state(
                ctx->states, ctx->state_count, desc->last_active);
            if (cd == NULL || cd->parent != desc->state) {
                return ELIB_FSM_STATE_INVALID;
            }
            s = desc->last_active;
        } else {
            s = desc->initial;
        }
        steps++;
    }
    return ELIB_FSM_STATE_INVALID;   /* defensive: bound exceeded */
}

/* Record history: for every composite along the leaf's ancestry,
 * remember the branch that leads to this leaf. Walks the whole way to
 * the top, so ancestors OUTSIDE the transition domain (never exited
 * or re-entered) also end up pointing at the new branch -- required
 * for deep-history descent from any future goto. */
static void hist_set_path(elib_fsm_hsm_hist_ctx_t *ctx, elib_fsm_state_t leaf) {
    elib_fsm_state_t child = leaf;
    int depth = 0;

    while (child != ELIB_FSM_STATE_INVALID &&
           depth < ELIB_FSM_HSM_MAX_DEPTH) {
        elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
            ctx->states, ctx->state_count, child);
        if (desc == NULL) {
            break;
        }
        elib_fsm_state_t parent = desc->parent;
        if (parent == ELIB_FSM_STATE_INVALID) {
            break;
        }
        elib_fsm_hsm_hist_state_desc_t *pd = hist_find_state(
            ctx->states, ctx->state_count, parent);
        if (pd == NULL) {
            break;
        }
        /* composites track history (leaves have nothing to track:
         * they carry no initial, so their last_active stays INVALID) */
        if (pd->initial != ELIB_FSM_STATE_INVALID) {
            pd->last_active = child;
        }
        child = parent;
        depth++;
    }
}

static void hist_enter_path(elib_fsm_hsm_hist_ctx_t *ctx,
                            elib_fsm_state_t ancestor,
                            elib_fsm_state_t descendant,
                            void *user_data) {
    elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
    int depth = 0;
    elib_fsm_state_t s = descendant;

    while (s != ancestor && s != ELIB_FSM_STATE_INVALID &&
           depth < ELIB_FSM_HSM_MAX_DEPTH) {
        elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
            ctx->states, ctx->state_count, s);
        if (desc == NULL) {
            break;
        }
        path[depth++] = s;
        s = desc->parent;
    }
    for (int i = depth - 1; i >= 0; i--) {
        elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
            ctx->states, ctx->state_count, path[i]);
        if (desc != NULL && desc->entry != NULL) {
            desc->entry(path[i], user_data);
        }
    }
}

static void hist_exit_path(elib_fsm_hsm_hist_ctx_t *ctx,
                           elib_fsm_state_t descendant,
                           elib_fsm_state_t ancestor,
                           void *user_data) {
    elib_fsm_state_t s = descendant;
    int depth = 0;

    while (s != ancestor && s != ELIB_FSM_STATE_INVALID &&
           depth < ELIB_FSM_HSM_MAX_DEPTH) {
        elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
            ctx->states, ctx->state_count, s);
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

/* Compute the transition domain (strict LCA), as in the const hsm
 * engine but over the writable table. */
static elib_fsm_state_t hist_compute_domain(
    elib_fsm_hsm_hist_ctx_t *ctx,
    elib_fsm_state_t source,
    elib_fsm_state_t target) {
    elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
        ctx->states, ctx->state_count, source);
    elib_fsm_state_t anc = (desc != NULL) ? desc->parent
                                          : ELIB_FSM_STATE_INVALID;
    int steps = 0;

    while (anc != ELIB_FSM_STATE_INVALID &&
           steps < ELIB_FSM_HSM_MAX_DEPTH) {
        elib_fsm_hsm_hist_state_desc_t *td = hist_find_state(
            ctx->states, ctx->state_count, target);
        elib_fsm_state_t p = (td != NULL) ? td->parent
                                          : ELIB_FSM_STATE_INVALID;
        int hops = 0;
        bool found = false;

        while (p != ELIB_FSM_STATE_INVALID &&
               hops < ELIB_FSM_HSM_MAX_DEPTH) {
            elib_fsm_hsm_hist_state_desc_t *pd = hist_find_state(
                ctx->states, ctx->state_count, p);
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

        desc = hist_find_state(ctx->states, ctx->state_count, anc);
        anc = (desc != NULL) ? desc->parent : ELIB_FSM_STATE_INVALID;
        steps++;
    }
    return ELIB_FSM_STATE_INVALID;
}

/* --- public API ----------------------------------------------------- */

elib_fsm_err_t elib_fsm_hsm_hist_init(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_hsm_hist_state_desc_t *states,
                                       size_t state_count,
                                       elib_fsm_state_t initial,
                                       void *user_data) {
    elib_fsm_state_t leaf;

    if (ctx == NULL || states == NULL || state_count == 0) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    {
        elib_fsm_err_t err = hist_validate_table(states, state_count);
        if (err != ELIB_FSM_OK) {
            return err;
        }
    }
    if (hist_find_state(states, state_count, initial) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    memset(ctx, 0, sizeof(elib_fsm_hsm_hist_ctx_t));

    /* seed history: every composite remembers its initial branch,
     * every leaf has nothing to remember (INVALID) */
    for (size_t i = 0; i < state_count; i++) {
        states[i].last_active = states[i].initial;
    }

    ctx->states = states;
    ctx->state_count = state_count;
    ctx->user_data = user_data;
    ctx->transition_seq = 0;
    ctx->bit_flags.initialized = 1;

    leaf = hist_find_leaf(ctx, initial);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        /* unreachable after validation; kept as defense */
        ctx->bit_flags.initialized = 0;
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }
    ctx->current = leaf;

    /* entry callbacks top-down INSIDE init */
    {
        elib_fsm_state_t path[ELIB_FSM_HSM_MAX_DEPTH];
        int depth = 0;
        elib_fsm_state_t s = leaf;
        while (s != ELIB_FSM_STATE_INVALID && depth < ELIB_FSM_HSM_MAX_DEPTH) {
            elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
                ctx->states, ctx->state_count, s);
            if (desc == NULL) {
                break;
            }
            path[depth++] = s;
            s = desc->parent;
        }
        for (int i = depth - 1; i >= 0; i--) {
            elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
                ctx->states, ctx->state_count, path[i]);
            if (desc != NULL && desc->entry != NULL) {
                desc->entry(path[i], ctx->user_data);
            }
        }
    }

    return ELIB_FSM_OK;
}

void elib_fsm_hsm_hist_deinit(elib_fsm_hsm_hist_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->bit_flags.initialized = 0;
}

elib_fsm_state_t elib_fsm_hsm_hist_current(
    const elib_fsm_hsm_hist_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}

elib_fsm_err_t elib_fsm_hsm_hist_goto(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_state_t target) {
    elib_fsm_state_t domain, leaf;

    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }
    if (hist_find_state(ctx->states, ctx->state_count, target) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    /* resolve the history/initial leaf BEFORE any callback fires */
    leaf = hist_find_leaf(ctx, target);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    domain = hist_compute_domain(ctx, ctx->current, target);

    hist_exit_path(ctx, ctx->current, domain, ctx->user_data);
    hist_enter_path(ctx, domain, target, ctx->user_data);

    if (leaf != target) {
        hist_enter_path(ctx, target, leaf, ctx->user_data);
    }

    /* re-record the chain, all the way to the top */
    hist_set_path(ctx, leaf);

    ctx->current = leaf;
    ctx->transition_seq++;

    return ELIB_FSM_OK;
}

elib_fsm_err_t elib_fsm_hsm_hist_reset(elib_fsm_hsm_hist_ctx_t *ctx,
                                        elib_fsm_state_t target) {
    elib_fsm_state_t source, domain, leaf;

    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }
    if (hist_find_state(ctx->states, ctx->state_count, target) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    /* wipe history first: every state falls back to its initial */
    for (size_t i = 0; i < ctx->state_count; i++) {
        ctx->states[i].last_active = ctx->states[i].initial;
    }

    leaf = hist_find_leaf(ctx, target);
    if (leaf == ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    /* record the clean chain BEFORE the callbacks: ancestors above the
     * transition domain will not be exited/re-entered, so their
     * last_active must already point at the new branch when the
     * callbacks run */
    hist_set_path(ctx, leaf);

    source = ctx->current;
    if (source == leaf) {
        /* already at the resolved leaf: history wiped and re-recorded,
         * nothing to fire */
        return ELIB_FSM_OK;
    }

    domain = hist_compute_domain(ctx, source, leaf);

    hist_exit_path(ctx, source, domain, ctx->user_data);
    hist_enter_path(ctx, domain, leaf, ctx->user_data);

    ctx->current = leaf;

    return ELIB_FSM_OK;
}

elib_fsm_state_t elib_fsm_hsm_hist_poll(elib_fsm_hsm_hist_ctx_t *ctx) {
    elib_fsm_hsm_hist_state_desc_t *desc;

    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    desc = hist_find_state(ctx->states, ctx->state_count, ctx->current);
    if (desc != NULL && desc->run != NULL) {
        desc->run(ctx->user_data);
    }

    return ctx->current;
}

bool elib_fsm_hsm_hist_dispatch(elib_fsm_hsm_hist_ctx_t *ctx,
                                 void *event_data) {
    elib_fsm_state_t state;

    if (ctx == NULL || !ctx->bit_flags.initialized || event_data == NULL) {
        return false;
    }

    state = ctx->current;
    while (state != ELIB_FSM_STATE_INVALID) {
        elib_fsm_hsm_hist_state_desc_t *desc = hist_find_state(
            ctx->states, ctx->state_count, state);
        if (desc == NULL) {
            return false;   /* defensive: table corrupted at runtime */
        }

        uint32_t seq_before = ctx->transition_seq;
        bool handled = (desc->handler != NULL) &&
                       desc->handler(event_data, ctx->user_data);

        /* same-leaf gotos inside handlers also stop bubbling */
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
