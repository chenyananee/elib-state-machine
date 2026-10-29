/* test_elib_fsm.c - Switch/Case State Machine Unit Tests (v2)
 *
 * Ported from v1 plus v2 additions: the period_ms = 0 poll semantics
 * ("no time passes, delay never advances, call is legal") are now
 * contract, not accident.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_fsm.h"

/* Test states */
#define STATE_IDLE    0
#define STATE_ACTIVE  1
#define STATE_ERROR   2

static elib_fsm_ctx_t test_ctx;

static void reset_test(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    elib_fsm_init(&test_ctx, STATE_IDLE);
}

/* --- Init tests --- */

static void test_init_valid(void) {
    printf("Test: init with valid parameters... ");
    memset(&test_ctx, 0, sizeof(test_ctx));

    elib_fsm_err_t err = elib_fsm_init(&test_ctx, STATE_IDLE);
    assert(err == ELIB_FSM_OK);
    assert(test_ctx.bit_flags.initialized == 1);
    assert(test_ctx.current == STATE_IDLE);
    assert(test_ctx.initial == STATE_IDLE);
    assert(test_ctx.delayed_target == ELIB_FSM_STATE_INVALID);
    assert(test_ctx.delayed_remaining == 0);

    printf("PASSED\n");
}

static void test_init_null_ctx(void) {
    printf("Test: init with null ctx... ");
    elib_fsm_err_t err = elib_fsm_init(NULL, STATE_IDLE);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

/* --- Deinit tests --- */

static void test_deinit_null(void) {
    printf("Test: deinit with null ctx... ");
    elib_fsm_deinit(NULL);
    printf("PASSED\n");
}

static void test_uninitialized_context(void) {
    printf("Test: uninitialized context rejects operations... ");
    reset_test();
    elib_fsm_deinit(&test_ctx);

    assert(elib_fsm_goto(&test_ctx, STATE_ACTIVE, 0) == ELIB_FSM_ERR_NOT_INITIALIZED);
    assert(elib_fsm_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);

    printf("PASSED\n");
}

/* --- Immediate goto tests --- */

static void test_goto_immediate(void) {
    printf("Test: immediate goto (delay=0)... ");
    reset_test();

    assert(elib_fsm_poll(&test_ctx, 10) == STATE_IDLE);

    elib_fsm_err_t err = elib_fsm_goto(&test_ctx, STATE_ACTIVE, 0);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ACTIVE);

    err = elib_fsm_goto(&test_ctx, STATE_ERROR, 0);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ERROR);

    printf("PASSED\n");
}

static void test_goto_null_ctx(void) {
    printf("Test: goto with null ctx... ");
    elib_fsm_err_t err = elib_fsm_goto(NULL, STATE_ACTIVE, 0);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_goto_cancels_delayed(void) {
    printf("Test: immediate goto cancels pending delay... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 100);
    assert(elib_fsm_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);

    elib_fsm_goto(&test_ctx, STATE_ERROR, 0);
    assert(test_ctx.delayed_target == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ERROR);

    printf("PASSED\n");
}

/* --- Delayed transition tests --- */

static void test_delayed_expires(void) {
    printf("Test: delayed goto expires correctly... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 30);

    /* Not yet expired */
    assert(elib_fsm_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);

    /* Expires on third tick: remaining=10 <= period=10 */
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ACTIVE);

    /* No longer delayed */
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_delayed_exact_period(void) {
    printf("Test: delayed goto with delay == period... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 10);
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_delayed_not_aligned(void) {
    printf("Test: delayed goto with non-aligned period... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 25);

    /* remaining=25, period=10: 25>10, not expired, remaining=15 */
    assert(elib_fsm_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    /* remaining=15, period=10: 15>10, not expired, remaining=5 */
    assert(elib_fsm_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    /* remaining=5, period=10: 5<=10, expired */
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_delayed_replaces_previous(void) {
    printf("Test: new delayed goto replaces previous... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 100);
    elib_fsm_goto(&test_ctx, STATE_ERROR, 20);

    /* New delay is 20ms for STATE_ERROR, old one is gone */
    assert(elib_fsm_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ERROR);

    printf("PASSED\n");
}

/* v2: poll(0) is a legal "zero time passed" tick -- the delay must
 * never advance and never fire, no matter how many times called. */
static void test_poll_zero_period(void) {
    printf("Test: poll(0) passes zero time, delay frozen... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 10);

    for (int i = 0; i < 100; i++) {
        assert(elib_fsm_poll(&test_ctx, 0) == ELIB_FSM_STATE_INVALID);
        assert(test_ctx.delayed_remaining == 10);   /* never touched */
    }
    assert(elib_fsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);

    /* a real tick still lands normally afterwards */
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ACTIVE);

    /* zero-time polls on an idle (non-delayed) machine are harmless */
    assert(elib_fsm_poll(&test_ctx, 0) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_delay_huge_period_fires(void) {
    printf("Test: period larger than remaining fires immediately... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 5);
    assert(elib_fsm_poll(&test_ctx, 1000) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_poll_null_ctx(void) {
    printf("Test: poll with null ctx... ");
    assert(elib_fsm_poll(NULL, 10) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

/* --- Current state tests --- */

static void test_current(void) {
    printf("Test: current returns current state... ");
    reset_test();

    assert(elib_fsm_current(&test_ctx) == STATE_IDLE);

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 0);
    assert(elib_fsm_current(&test_ctx) == STATE_ACTIVE);

    elib_fsm_goto(&test_ctx, STATE_ERROR, 0);
    assert(elib_fsm_current(&test_ctx) == STATE_ERROR);

    printf("PASSED\n");
}

static void test_current_null_ctx(void) {
    printf("Test: current with null ctx... ");
    assert(elib_fsm_current(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

static void test_current_uninitialized(void) {
    printf("Test: current with uninitialized ctx... ");
    reset_test();
    elib_fsm_deinit(&test_ctx);
    assert(elib_fsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

static void test_current_during_delay(void) {
    printf("Test: current returns INVALID during delay... ");
    reset_test();

    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 100);
    assert(elib_fsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);

    elib_fsm_poll(&test_ctx, 50);
    assert(elib_fsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);

    elib_fsm_poll(&test_ctx, 50);
    assert(elib_fsm_current(&test_ctx) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_previous(void) {
    printf("Test: previous returns last valid state across transitions... ");
    reset_test();

    assert(elib_fsm_previous(&test_ctx) == STATE_IDLE);

    /* Delayed: previous stays IDLE */
    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 100);
    assert(elib_fsm_previous(&test_ctx) == STATE_IDLE);

    elib_fsm_poll(&test_ctx, 50);
    assert(elib_fsm_previous(&test_ctx) == STATE_IDLE);

    /* Delay expires, current=ACTIVE, previous=IDLE */
    elib_fsm_poll(&test_ctx, 50);
    assert(elib_fsm_current(&test_ctx) == STATE_ACTIVE);
    assert(elib_fsm_previous(&test_ctx) == STATE_IDLE);

    /* Immediate: previous becomes ACTIVE */
    elib_fsm_goto(&test_ctx, STATE_ERROR, 0);
    assert(elib_fsm_current(&test_ctx) == STATE_ERROR);
    assert(elib_fsm_previous(&test_ctx) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_previous_during_delay(void) {
    printf("Test: previous is last valid state during delayed transition... ");
    reset_test();

    /* IDLE -> ACTIVE (immediate): previous = IDLE */
    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 0);
    assert(elib_fsm_previous(&test_ctx) == STATE_IDLE);

    /* ACTIVE -> ERROR (delayed): machine is mid-transition, current is
     * INVALID but previous must be the last valid state (ACTIVE) */
    elib_fsm_goto(&test_ctx, STATE_ERROR, 100);
    assert(elib_fsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_previous(&test_ctx) == STATE_ACTIVE);

    /* Still pending: previous unchanged */
    assert(elib_fsm_poll(&test_ctx, 50) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_previous(&test_ctx) == STATE_ACTIVE);

    /* Expires: current = ERROR, previous = ACTIVE */
    assert(elib_fsm_poll(&test_ctx, 50) == STATE_ERROR);
    assert(elib_fsm_previous(&test_ctx) == STATE_ACTIVE);

    printf("PASSED\n");
}

/* v2 documented abort idiom: no cancel API -- goto(previous, 0). */
static void test_abort_pending_delay_via_previous(void) {
    printf("Test: pending delay aborted via goto(previous, 0)... ");
    reset_test();

    /* ACTIVE -> ERROR delayed (ACTIVE was reached immediately first) */
    elib_fsm_goto(&test_ctx, STATE_ACTIVE, 0);
    elib_fsm_goto(&test_ctx, STATE_ERROR, 100);
    assert(elib_fsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_previous(&test_ctx) == STATE_ACTIVE);

    /* abort: re-enter the remembered previous state immediately */
    assert(elib_fsm_goto(&test_ctx, elib_fsm_previous(&test_ctx), 0)
           == ELIB_FSM_OK);
    assert(test_ctx.delayed_target == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_current(&test_ctx) == STATE_ACTIVE);
    /* no phantom fire later */
    assert(elib_fsm_poll(&test_ctx, 10) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_previous_null_ctx(void) {
    printf("Test: previous with null ctx... ");
    assert(elib_fsm_previous(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

int main(void) {
    printf("=== elib-state-machine (switch/case) tests ===\n\n");

    test_init_valid();
    test_init_null_ctx();
    test_deinit_null();
    test_uninitialized_context();
    test_goto_immediate();
    test_goto_null_ctx();
    test_goto_cancels_delayed();
    test_delayed_expires();
    test_delayed_exact_period();
    test_delayed_not_aligned();
    test_delayed_replaces_previous();
    test_poll_zero_period();
    test_delay_huge_period_fires();
    test_poll_null_ctx();
    test_current();
    test_current_null_ctx();
    test_current_uninitialized();
    test_current_during_delay();
    test_previous();
    test_previous_during_delay();
    test_abort_pending_delay_via_previous();
    test_previous_null_ctx();

    printf("\n=== All tests passed ===\n");
    return 0;
}
