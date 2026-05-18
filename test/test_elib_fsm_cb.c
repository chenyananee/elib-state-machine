/* test_elib_fsm_cb.c - Callback State Machine Unit Tests */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/elib_fsm_cb.h"

/* Test states */
#define STATE_IDLE    0
#define STATE_ACTIVE  1
#define STATE_ERROR   2

/* Callback tracking */
static int entry_count;
static int exit_count;
static int run_count;
static elib_fsm_state_t last_entry_state;
static elib_fsm_state_t last_exit_state;

static void reset_callback_state(void) {
    entry_count = 0;
    exit_count = 0;
    run_count = 0;
    last_entry_state = -99;
    last_exit_state = -99;
}

/* Test callbacks */
static void on_entry(elib_fsm_state_t state, void *user_data) {
    (void)user_data;
    entry_count++;
    last_entry_state = state;
}

static void on_exit(elib_fsm_state_t state, void *user_data) {
    (void)user_data;
    exit_count++;
    last_exit_state = state;
}

static void on_run(void *user_data) {
    (void)user_data;
    run_count++;
}

/* State descriptors */
static const elib_fsm_cb_state_desc_t test_states[] = {
    { STATE_IDLE,   on_entry, on_exit, on_run },
    { STATE_ACTIVE, on_entry, on_exit, on_run },
    { STATE_ERROR,  on_entry, on_exit, NULL    },  /* no run for error state */
};
#define TEST_STATE_COUNT (sizeof(test_states) / sizeof(test_states[0]))

static elib_fsm_cb_ctx_t test_ctx;

static void reset_test(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();
    elib_fsm_cb_init(&test_ctx, test_states, TEST_STATE_COUNT, STATE_IDLE, NULL);
}

/* --- Init tests --- */

static void test_init_valid(void) {
    printf("Test: init with valid parameters... ");
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_cb_init(&test_ctx, test_states, TEST_STATE_COUNT,
                                           STATE_IDLE, NULL);
    assert(err == ELIB_FSM_OK);
    assert(test_ctx.initialized == 1);
    assert(test_ctx.current == STATE_IDLE);
    assert(test_ctx.delayed_target == ELIB_FSM_STATE_INVALID);
    assert(test_ctx.delayed_remaining == 0);
    assert(entry_count == 1);
    assert(last_entry_state == STATE_IDLE);

    printf("PASSED\n");
}

static void test_init_null_ctx(void) {
    printf("Test: init with null ctx... ");
    elib_fsm_err_t err = elib_fsm_cb_init(NULL, test_states, TEST_STATE_COUNT,
                                           STATE_IDLE, NULL);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

/* --- Deinit tests --- */

static void test_deinit_null(void) {
    printf("Test: deinit with null ctx... ");
    elib_fsm_cb_deinit(NULL);
    printf("PASSED\n");
}

static void test_uninitialized_context(void) {
    printf("Test: uninitialized context rejects operations... ");
    reset_test();
    elib_fsm_cb_deinit(&test_ctx);

    assert(elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 0) == ELIB_FSM_ERR_NOT_INITIALIZED);
    assert(elib_fsm_cb_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_current(&test_ctx) == ELIB_FSM_STATE_INVALID);

    printf("PASSED\n");
}

/* --- Immediate goto tests --- */

static void test_goto_immediate(void) {
    printf("Test: immediate goto (delay=0)... ");
    reset_test();
    reset_callback_state();

    assert(elib_fsm_cb_current(&test_ctx) == STATE_IDLE);

    elib_fsm_err_t err = elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 0);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ACTIVE);

    /* Verify callback sequence: exit(IDLE) -> entry(ACTIVE) */
    assert(exit_count == 1);
    assert(last_exit_state == STATE_IDLE);
    assert(entry_count == 1);
    assert(last_entry_state == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_goto_null_ctx(void) {
    printf("Test: goto with null ctx... ");
    elib_fsm_err_t err = elib_fsm_cb_goto(NULL, STATE_ACTIVE, 0);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_goto_immediate_cancels_delayed(void) {
    printf("Test: immediate goto cancels pending delay... ");
    reset_test();
    reset_callback_state();

    /* Set delayed transition: exit(IDLE) fires */
    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 100);
    assert(exit_count == 1);
    assert(last_exit_state == STATE_IDLE);

    /* Immediate goto to ERROR: no exit (already exited), entry(ERROR) fires */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 0);
    assert(exit_count == 1);  /* no additional exit */
    assert(entry_count == 1);
    assert(last_entry_state == STATE_ERROR);
    assert(test_ctx.delayed_target == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ERROR);

    printf("PASSED\n");
}

/* --- Delayed transition tests --- */

static void test_delayed_exit_fires_immediately(void) {
    printf("Test: delayed goto fires exit immediately... ");
    reset_test();
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 50);
    assert(err == ELIB_FSM_OK);

    /* exit(IDLE) should have fired immediately */
    assert(exit_count == 1);
    assert(last_exit_state == STATE_IDLE);

    /* entry(ACTIVE) should NOT have fired yet */
    assert(entry_count == 0);

    /* No current state during delay (exit fired, entry not yet) */
    assert(elib_fsm_cb_current(&test_ctx) == ELIB_FSM_STATE_INVALID);

    printf("PASSED\n");
}

static void test_delayed_entry_fires_at_expiration(void) {
    printf("Test: delayed goto fires entry at expiration... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 30);
    assert(exit_count == 1);

    /* Not yet expired */
    assert(elib_fsm_cb_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(entry_count == 0);

    /* Expires on third tick: remaining=10 <= period=10 */
    assert(elib_fsm_cb_poll(&test_ctx, 10) == STATE_ACTIVE);
    assert(entry_count == 1);
    assert(last_entry_state == STATE_ACTIVE);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_delayed_exact_period(void) {
    printf("Test: delayed goto with delay == period... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 10);
    assert(exit_count == 1);

    assert(elib_fsm_cb_poll(&test_ctx, 10) == STATE_ACTIVE);
    assert(entry_count == 1);

    printf("PASSED\n");
}

static void test_delayed_not_aligned(void) {
    printf("Test: delayed goto with non-aligned period... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 25);
    assert(exit_count == 1);

    /* remaining=25, period=10: 25>10, not expired, remaining=15 */
    assert(elib_fsm_cb_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(entry_count == 0);

    /* remaining=15, period=10: 15>10, not expired, remaining=5 */
    assert(elib_fsm_cb_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(entry_count == 0);

    /* remaining=5, period=10: 5<=10, expired */
    assert(elib_fsm_cb_poll(&test_ctx, 10) == STATE_ACTIVE);
    assert(entry_count == 1);

    printf("PASSED\n");
}

static void test_delayed_replaces_previous(void) {
    printf("Test: new delayed goto replaces previous (no re-exit)... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 100);
    assert(exit_count == 1);
    assert(last_exit_state == STATE_IDLE);

    /* Replace with new delayed target — no exit should fire */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 20);
    assert(exit_count == 1);  /* no additional exit */

    /* New delay is 20ms for STATE_ERROR */
    assert(elib_fsm_cb_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_poll(&test_ctx, 10) == STATE_ERROR);
    assert(entry_count == 1);
    assert(last_entry_state == STATE_ERROR);

    printf("PASSED\n");
}

/* --- Poll calls run tests --- */

static void test_poll_calls_run(void) {
    printf("Test: poll calls current state's run callback... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_poll(&test_ctx, 10);
    assert(run_count == 1);

    elib_fsm_cb_poll(&test_ctx, 10);
    assert(run_count == 2);

    printf("PASSED\n");
}

static void test_poll_skips_run_during_delay(void) {
    printf("Test: poll skips run during delayed transition... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 50);
    assert(exit_count == 1);

    /* poll should skip run during delay */
    assert(elib_fsm_cb_poll(&test_ctx, 10) == ELIB_FSM_STATE_INVALID);
    assert(run_count == 0);

    /* After delay expires, poll should call run */
    elib_fsm_cb_poll(&test_ctx, 40);
    assert(entry_count == 1);
    assert(run_count == 1);

    printf("PASSED\n");
}

static void test_poll_skips_null_run(void) {
    printf("Test: poll skips null run callback... ");
    reset_test();
    reset_callback_state();

    /* Transition to error state (which has NULL run) */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 0);
    assert(entry_count == 1);

    run_count = 0;
    elib_fsm_cb_poll(&test_ctx, 10);
    assert(run_count == 0);

    printf("PASSED\n");
}

/* --- Poll null/edge tests --- */

static void test_poll_null_ctx(void) {
    printf("Test: poll with null ctx... ");
    assert(elib_fsm_cb_poll(NULL, 10) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

static void test_delayed_immediate_during_delay(void) {
    printf("Test: immediate goto during delay (no re-exit)... ");
    reset_test();
    reset_callback_state();

    /* Delayed: exit(IDLE) fires */
    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 50);
    assert(exit_count == 1);
    assert(last_exit_state == STATE_IDLE);
    assert(entry_count == 0);

    /* Immediate goto during delay: no exit, entry(ERROR) fires */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 0);
    assert(exit_count == 1);  /* no additional exit */
    assert(entry_count == 1);
    assert(last_entry_state == STATE_ERROR);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ERROR);

    printf("PASSED\n");
}

/* --- Previous state tests --- */

static void test_previous(void) {
    printf("Test: previous returns last valid state across transitions... ");
    reset_test();
    reset_callback_state();

    assert(elib_fsm_cb_previous(&test_ctx) == STATE_IDLE);

    /* Delayed: previous stays IDLE */
    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 100);
    assert(exit_count == 1);
    assert(elib_fsm_cb_current(&test_ctx) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_IDLE);

    elib_fsm_cb_poll(&test_ctx, 50);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_IDLE);

    /* Delay expires, current=ACTIVE, previous=IDLE */
    elib_fsm_cb_poll(&test_ctx, 50);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ACTIVE);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_IDLE);

    /* Immediate: previous becomes ACTIVE */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 0);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ERROR);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_previous_null_ctx(void) {
    printf("Test: previous with null ctx... ");
    assert(elib_fsm_cb_previous(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

int main(void) {
    printf("=== elib-state-machine (callback) tests ===\n\n");

    test_init_valid();
    test_init_null_ctx();
    test_deinit_null();
    test_uninitialized_context();
    test_goto_immediate();
    test_goto_null_ctx();
    test_goto_immediate_cancels_delayed();
    test_delayed_exit_fires_immediately();
    test_delayed_entry_fires_at_expiration();
    test_delayed_exact_period();
    test_delayed_not_aligned();
    test_delayed_replaces_previous();
    test_poll_calls_run();
    test_poll_skips_run_during_delay();
    test_poll_skips_null_run();
    test_poll_null_ctx();
    test_delayed_immediate_during_delay();
    test_previous();
    test_previous_null_ctx();

    printf("\n=== All tests passed ===\n");
    return 0;
}
