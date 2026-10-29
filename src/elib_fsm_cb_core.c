/* elib_fsm_cb_core.c - Callback State Machine Core Implementation
 *
 * Flat state table (const-friendly), entry/exit/run/event callbacks,
 * immediate or delayed goto with a critical-state event relay. The
 * table is structurally validated ONCE at init: duplicate state
 * values, the reserved INVALID value, references into the void -- all
 * rejected, nothing illegal survives into runtime. */
#include "elib_fsm.h"
#include <string.h>

/* Find state descriptor by state value */
static const elib_fsm_cb_state_desc_t *cb_find_state(
    const elib_fsm_cb_state_desc_t *states,
    size_t state_count,
    elib_fsm_state_t state) {
    for (size_t i = 0; i < state_count; i++) {
        if (states[i].state == state) {
            return &states[i];
        }
    }
    return NULL;
}

/* Full table validation. Returns ELIB_FSM_OK or a rejection code.
 * - a state value equal to ELIB_FSM_STATE_INVALID poisons the "no
 *   state" signalling of current()/poll(): rejected;
 * - duplicate values make find_stateambiguous: rejected. */
static elib_fsm_err_t cb_validate_table(const elib_fsm_cb_state_desc_t *states,
                                        size_t state_count) {
    for (size_t i = 0; i < state_count; i++) {
        if (states[i].state == ELIB_FSM_STATE_INVALID) {
            return ELIB_FSM_ERR_INVALID_PARAM;
        }
        for (size_t j = i + 1; j < state_count; j++) {
            if (states[i].state == states[j].state) {
                return ELIB_FSM_ERR_INVALID_PARAM;
            }
        }
    }
    return ELIB_FSM_OK;
}

static void cb_call_entry(const elib_fsm_cb_ctx_t *ctx,
                          const elib_fsm_cb_state_desc_t *desc,
                          elib_fsm_state_t state) {
    if (desc != NULL && desc->entry != NULL) {
        desc->entry(state, ctx->user_data);
    }
}

static void cb_call_exit(const elib_fsm_cb_ctx_t *ctx, elib_fsm_state_t state) {
    const elib_fsm_cb_state_desc_t *desc =
        cb_find_state(ctx->states, ctx->state_count, state);
    if (desc != NULL && desc->exit != NULL) {
        desc->exit(state, ctx->user_data);
    }
}

elib_fsm_err_t elib_fsm_cb_init(elib_fsm_cb_ctx_t *ctx,
                                 const elib_fsm_cb_state_desc_t *states,
                                 size_t state_count,
                                 elib_fsm_state_t initial,
                                 void *user_data,
                                 elib_fsm_cb_transition_event_fn transition_event) {
    elib_fsm_err_t err;

    if (ctx == NULL || states == NULL || state_count == 0) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }

    err = cb_validate_table(states, state_count);
    if (err != ELIB_FSM_OK) {
        return err;
    }

    if (cb_find_state(states, state_count, initial) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    memset(ctx, 0, sizeof(elib_fsm_cb_ctx_t));

    ctx->states = states;
    ctx->state_count = state_count;
    ctx->current = initial;
    ctx->previous = initial;
    ctx->delayed_target = ELIB_FSM_STATE_INVALID;
    ctx->user_data = user_data;
    ctx->transition_event = transition_event;
    ctx->bit_flags.initialized = 1;

    /* Entry of the initial state fires INSIDE init. The initialized
     * flag is already up, so an entry callback may legally goto. */
    cb_call_entry(ctx, cb_find_state(states, state_count, initial), initial);

    return ELIB_FSM_OK;
}

void elib_fsm_cb_deinit(elib_fsm_cb_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->bit_flags.initialized = 0;
}

elib_fsm_err_t elib_fsm_cb_goto(elib_fsm_cb_ctx_t *ctx,
                                 elib_fsm_state_t target,
                                 uint32_t delay_ms) {
    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }

    /* Validate target BEFORE anything fires: a rejected goto must not
     * move the machine or touch a single callback. */
    if (cb_find_state(ctx->states, ctx->state_count, target) == NULL) {
        return ELIB_FSM_ERR_STATE_NOT_FOUND;
    }

    if (delay_ms == 0) {
        /* Immediate. During a pending delay the source was already
         * exited, so no new exit fires -- we only cancel the delay. */
        if (ctx->delayed_target == ELIB_FSM_STATE_INVALID) {
            cb_call_exit(ctx, ctx->current);
        }
        ctx->delayed_target = ELIB_FSM_STATE_INVALID;
        ctx->delayed_remaining = 0;
        ctx->previous = ctx->current;
        ctx->current = target;
        cb_call_entry(ctx, cb_find_state(ctx->states, ctx->state_count, target),
                      target);
    } else {
        /* Delayed: exit now, entry at expiration. Replacing an already
         * pending target redirects without a second exit. */
        if (ctx->delayed_target == ELIB_FSM_STATE_INVALID) {
            cb_call_exit(ctx, ctx->current);
        }
        ctx->previous = ctx->current;
        ctx->delayed_target = target;
        ctx->delayed_remaining = delay_ms;
    }

    return ELIB_FSM_OK;
}

elib_fsm_state_t elib_fsm_cb_poll(elib_fsm_cb_ctx_t *ctx, uint32_t period_ms,
                                   void *event_data) {
    const elib_fsm_cb_state_desc_t *desc;

    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    if (ctx->delayed_target != ELIB_FSM_STATE_INVALID) {
        if (ctx->delayed_remaining <= period_ms) {
            /* Expiring tick: land first, then the new state services
             * the event and the run. */
            ctx->previous = ctx->current;
            ctx->current = ctx->delayed_target;
            ctx->delayed_target = ELIB_FSM_STATE_INVALID;
            ctx->delayed_remaining = 0;

            cb_call_entry(ctx, cb_find_state(ctx->states, ctx->state_count,
                                             ctx->current), ctx->current);
        } else {
            /* Still pending (critical state): relay the event to the
             * transition callback (remaining taken BEFORE the tick's
             * period is applied), advance the countdown, skip event
             * and run of the exited source state. */
            if (ctx->transition_event != NULL && event_data != NULL) {
                ctx->transition_event(event_data, ctx->current,
                                      ctx->delayed_target,
                                      ctx->delayed_remaining, ctx->user_data);
            }
            if (period_ms > 0u) {
                ctx->delayed_remaining -= period_ms;
            }
            return ELIB_FSM_STATE_INVALID;
        }
    }

    /* Normal tick, or the tick the delayed transition landed on. */
    desc = cb_find_state(ctx->states, ctx->state_count, ctx->current);
    if (desc != NULL && desc->event != NULL && event_data != NULL) {
        desc->event(event_data, ctx->user_data);
    }
    if (desc != NULL && desc->run != NULL) {
        desc->run(ctx->user_data);
    }

    return ctx->current;
}

elib_fsm_err_t elib_fsm_cb_dispatch(elib_fsm_cb_ctx_t *ctx, void *event_data) {
    const elib_fsm_cb_state_desc_t *desc;

    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }
    if (event_data == NULL) {
        return ELIB_FSM_OK;
    }

    /* Critical state: route to the transition relay (drop when none). */
    if (ctx->delayed_target != ELIB_FSM_STATE_INVALID) {
        if (ctx->transition_event != NULL) {
            ctx->transition_event(event_data, ctx->current,
                                  ctx->delayed_target,
                                  ctx->delayed_remaining, ctx->user_data);
        }
        return ELIB_FSM_OK;
    }

    desc = cb_find_state(ctx->states, ctx->state_count, ctx->current);
    if (desc != NULL && desc->event != NULL) {
        desc->event(event_data, ctx->user_data);
    }

    return ELIB_FSM_OK;
}

elib_fsm_state_t elib_fsm_cb_current(const elib_fsm_cb_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    if (ctx->delayed_target != ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}

elib_fsm_state_t elib_fsm_cb_previous(const elib_fsm_cb_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->previous;
}
