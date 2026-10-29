/* elib_fsm_core.c - Switch/Case State Machine Core Implementation
 *
 * Minimal state tracker: no table, no callbacks, immediate or delayed
 * goto. The user's main loop drives poll() and switches on the state.
 * A pending delayed transition makes current() report INVALID while
 * previous() keeps serving the last valid state. */
#include "elib_fsm.h"
#include <string.h>

elib_fsm_err_t elib_fsm_init(elib_fsm_ctx_t *ctx, elib_fsm_state_t initial) {
    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }

    memset(ctx, 0, sizeof(elib_fsm_ctx_t));

    ctx->initial = initial;
    ctx->current = initial;
    ctx->previous = initial;
    ctx->delayed_target = ELIB_FSM_STATE_INVALID;
    ctx->bit_flags.initialized = 1;

    return ELIB_FSM_OK;
}

void elib_fsm_deinit(elib_fsm_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->bit_flags.initialized = 0;
}

elib_fsm_err_t elib_fsm_goto(elib_fsm_ctx_t *ctx,
                              elib_fsm_state_t target,
                              uint32_t delay_ms) {
    if (ctx == NULL) {
        return ELIB_FSM_ERR_INVALID_PARAM;
    }
    if (!ctx->bit_flags.initialized) {
        return ELIB_FSM_ERR_NOT_INITIALIZED;
    }

    if (delay_ms == 0) {
        ctx->previous = ctx->current;
        ctx->current = target;
        ctx->delayed_target = ELIB_FSM_STATE_INVALID;
        ctx->delayed_remaining = 0;
    } else {
        ctx->previous = ctx->current;
        ctx->delayed_target = target;
        ctx->delayed_remaining = delay_ms;
    }

    return ELIB_FSM_OK;
}

elib_fsm_state_t elib_fsm_poll(elib_fsm_ctx_t *ctx, uint32_t period_ms) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    if (ctx->delayed_target == ELIB_FSM_STATE_INVALID) {
        return ctx->current;
    }

    /* period 0 = no time passes: never fires, never rewinds */
    if (ctx->delayed_remaining <= period_ms) {
        ctx->previous = ctx->current;
        ctx->current = ctx->delayed_target;
        ctx->delayed_target = ELIB_FSM_STATE_INVALID;
        ctx->delayed_remaining = 0;
        return ctx->current;
    }

    if (period_ms > 0u) {
        ctx->delayed_remaining -= period_ms;
    }
    return ELIB_FSM_STATE_INVALID;
}

elib_fsm_state_t elib_fsm_current(const elib_fsm_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    if (ctx->delayed_target != ELIB_FSM_STATE_INVALID) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->current;
}

elib_fsm_state_t elib_fsm_previous(const elib_fsm_ctx_t *ctx) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }
    return ctx->previous;
}
