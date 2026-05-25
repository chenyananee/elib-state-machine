/* elib_fsm_core.c - Switch/Case State Machine Core Implementation */
#include "elib_fsm_core.h"
#include <string.h>

/* Initialize switch/case state machine */
elib_fsm_err_t elib_fsm_init(elib_fsm_ctx_t *ctx,
                              elib_fsm_state_t initial) {
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

/* Deinitialize state machine */
void elib_fsm_deinit(elib_fsm_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    ctx->bit_flags.initialized = 0;
}

/* Jump to target state, immediate or delayed */
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
        ctx->delayed_target = target;
        ctx->delayed_remaining = delay_ms;
    }

    return ELIB_FSM_OK;
}

/* Advance one tick and return current state */
elib_fsm_state_t elib_fsm_poll(elib_fsm_ctx_t *ctx, uint32_t period_ms) {
    if (ctx == NULL || !ctx->bit_flags.initialized) {
        return ELIB_FSM_STATE_INVALID;
    }

    if (ctx->delayed_target == ELIB_FSM_STATE_INVALID) {
        return ctx->current;
    }

    if (ctx->delayed_remaining <= period_ms) {
        ctx->previous = ctx->current;
        ctx->current = ctx->delayed_target;
        ctx->delayed_target = ELIB_FSM_STATE_INVALID;
        ctx->delayed_remaining = 0;
        return ctx->current;
    }

    ctx->delayed_remaining -= period_ms;
    return ELIB_FSM_STATE_INVALID;
}

/* Get current state */
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
