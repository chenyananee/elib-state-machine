/* elib_fsm_cb_core.c - Callback State Machine Core Implementation */
#include "elib_fsm_cb_core.h"
#include <string.h>

/* Find state descriptor by state value */
const elib_fsm_cb_state_desc_t *elib_fsm_cb_find_state(
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

/* Initialize callback state machine */
elib_fsm_err_t elib_fsm_cb_init(elib_fsm_cb_ctx_t *ctx,
                                 const elib_fsm_cb_state_desc_t *states,
                                 size_t state_count,
                                 elib_fsm_state_t initial,
                                 void *user_data) {
    if (ctx == NULL || states == NULL || state_count == 0) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }

    memset(ctx, 0, sizeof(elib_fsm_cb_ctx_t));

    ctx->states = states;
    ctx->state_count = state_count;
    ctx->current = initial;
    ctx->delayed_target = ELIB_FSM_STATE_INVALID;
    ctx->user_data = user_data;
    ctx->initialized = 1;

    /* Call entry callback of initial state */
    const elib_fsm_cb_state_desc_t *desc = elib_fsm_cb_find_state(
        states, state_count, initial);
    if (desc != NULL && desc->entry != NULL) {
        desc->entry(initial, user_data);
    }

    return ELIB_FSM_OK;
}

/* Deinitialize callback state machine */
void elib_fsm_cb_deinit(elib_fsm_cb_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->initialized = 0;
}

/* Jump to target state, immediate or delayed */
elib_fsm_err_t elib_fsm_cb_goto(elib_fsm_cb_ctx_t *ctx,
                                 elib_fsm_state_t target,
                                 uint32_t delay_ms) {
    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }

    if (delay_ms == 0) {
        /* Immediate: exit current (if not in delayed transition), then entry */
        if (ctx->delayed_target == ELIB_FSM_STATE_INVALID) {
            /* Not in delay — exit current state */
            const elib_fsm_cb_state_desc_t *src_desc = elib_fsm_cb_find_state(
                ctx->states, ctx->state_count, ctx->current);
            if (src_desc != NULL && src_desc->exit != NULL) {
                src_desc->exit(ctx->current, ctx->user_data);
            }
        }
        /* Cancel any pending delay */
        ctx->delayed_target = ELIB_FSM_STATE_INVALID;
        ctx->delayed_remaining = 0;
        /* Set current and call entry */
        ctx->current = target;
        const elib_fsm_cb_state_desc_t *dst_desc = elib_fsm_cb_find_state(
            ctx->states, ctx->state_count, target);
        if (dst_desc != NULL && dst_desc->entry != NULL) {
            dst_desc->entry(target, ctx->user_data);
        }
    } else {
        /* Delayed: exit now (if not already in delay), entry at expiration */
        if (ctx->delayed_target == ELIB_FSM_STATE_INVALID) {
            /* Not already in delay — exit current state */
            const elib_fsm_cb_state_desc_t *src_desc = elib_fsm_cb_find_state(
                ctx->states, ctx->state_count, ctx->current);
            if (src_desc != NULL && src_desc->exit != NULL) {
                src_desc->exit(ctx->current, ctx->user_data);
            }
        }
        ctx->delayed_target = target;
        ctx->delayed_remaining = delay_ms;
    }

    return ELIB_FSM_OK;
}

/* Advance one tick, call run callback, and return current state */
elib_fsm_state_t elib_fsm_cb_poll(elib_fsm_cb_ctx_t *ctx, uint32_t period_ms) {
    if (ctx == NULL || !ctx->initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    if (ctx->delayed_target != ELIB_FSM_STATE_INVALID) {
        if (ctx->delayed_remaining <= period_ms) {
            /* Expired — transition to delayed target */
            ctx->current = ctx->delayed_target;
            ctx->delayed_target = ELIB_FSM_STATE_INVALID;
            ctx->delayed_remaining = 0;

            /* Call entry callback of new state */
            const elib_fsm_cb_state_desc_t *desc = elib_fsm_cb_find_state(
                ctx->states, ctx->state_count, ctx->current);
            if (desc != NULL && desc->entry != NULL) {
                desc->entry(ctx->current, ctx->user_data);
            }
        } else {
            ctx->delayed_remaining -= period_ms;
            return ELIB_FSM_STATE_INVALID;
        }
    }

    /* Call run callback of current state */
    const elib_fsm_cb_state_desc_t *desc = elib_fsm_cb_find_state(
        ctx->states, ctx->state_count, ctx->current);
    if (desc != NULL && desc->run != NULL) {
        desc->run(ctx->user_data);
    }

    return ctx->current;
}

/* Get current state */
elib_fsm_state_t elib_fsm_cb_current(const elib_fsm_cb_ctx_t *ctx) {
    if (ctx == NULL || !ctx->initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    if (ctx->delayed_target != ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}
