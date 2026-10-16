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
static int event_count;
static void *last_event_data;
static elib_fsm_state_t last_entry_state;
static elib_fsm_state_t last_exit_state;

/* Transition (critical state) callback tracking */
static int trans_count;
static void *last_trans_event;
static elib_fsm_state_t last_trans_from;
static elib_fsm_state_t last_trans_to;
static uint32_t last_trans_remaining;

/* Call order log: 'E'=event, 'R'=run */
static char call_log[32];
static size_t call_log_len;

static void log_call(char c) {
    if (call_log_len < sizeof(call_log) - 1) {
        call_log[call_log_len++] = c;
        call_log[call_log_len] = '\0';
    }
}

static void reset_callback_state(void) {
    entry_count = 0;
    exit_count = 0;
    run_count = 0;
    event_count = 0;
    last_event_data = NULL;
    last_entry_state = -99;
    last_exit_state = -99;
    call_log_len = 0;
    call_log[0] = '\0';
    trans_count = 0;
    last_trans_event = NULL;
    last_trans_from = -99;
    last_trans_to = -99;
    last_trans_remaining = 0;
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
    log_call('R');
}

static void on_event(void *event_data, void *user_data) {
    (void)user_data;
    event_count++;
    last_event_data = event_data;
    log_call('E');
}

/* Transition (critical state) event callback */
static void on_transition_event(void *event_data, elib_fsm_state_t from,
                                elib_fsm_state_t to, uint32_t remaining_ms,
                                void *user_data) {
    (void)user_data;
    trans_count++;
    last_trans_event = event_data;
    last_trans_from = from;
    last_trans_to = to;
    last_trans_remaining = remaining_ms;
}

/* State descriptors */
static const elib_fsm_cb_state_desc_t test_states[] = {
    { STATE_IDLE,   on_entry, on_exit, on_run, on_event },
    { STATE_ACTIVE, on_entry, on_exit, on_run, on_event },
    { STATE_ERROR,  on_entry, on_exit, NULL,   NULL     },  /* no run/event for error state */
};
#define TEST_STATE_COUNT (sizeof(test_states) / sizeof(test_states[0]))

static elib_fsm_cb_ctx_t test_ctx;

static void reset_test(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();
    elib_fsm_cb_init(&test_ctx, test_states, TEST_STATE_COUNT, STATE_IDLE, NULL,
                     NULL);
}

/* --- Init tests --- */

static void test_init_valid(void) {
    printf("Test: init with valid parameters... ");
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_cb_init(&test_ctx, test_states, TEST_STATE_COUNT,
                                           STATE_IDLE, NULL, NULL);
    assert(err == ELIB_FSM_OK);
    assert(test_ctx.bit_flags.initialized == 1);
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
                                           STATE_IDLE, NULL, NULL);
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
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == ELIB_FSM_STATE_INVALID);
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
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == ELIB_FSM_STATE_INVALID);
    assert(entry_count == 0);

    /* Expires on third tick: remaining=10 <= period=10 */
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == STATE_ACTIVE);
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

    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == STATE_ACTIVE);
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
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == ELIB_FSM_STATE_INVALID);
    assert(entry_count == 0);

    /* remaining=15, period=10: 15>10, not expired, remaining=5 */
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == ELIB_FSM_STATE_INVALID);
    assert(entry_count == 0);

    /* remaining=5, period=10: 5<=10, expired */
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == STATE_ACTIVE);
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
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == STATE_ERROR);
    assert(entry_count == 1);
    assert(last_entry_state == STATE_ERROR);

    printf("PASSED\n");
}

/* --- Poll calls run tests --- */

static void test_poll_calls_run(void) {
    printf("Test: poll calls current state's run callback... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_poll(&test_ctx, 10, NULL);
    assert(run_count == 1);

    elib_fsm_cb_poll(&test_ctx, 10, NULL);
    assert(run_count == 2);

    printf("PASSED\n");
}

static void test_poll_skips_run_during_delay(void) {
    printf("Test: poll skips event and run during delayed transition... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 50);
    assert(exit_count == 1);

    /* poll should skip event and run during delay */
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == ELIB_FSM_STATE_INVALID);
    assert(event_count == 0);
    assert(run_count == 0);

    /* After delay expires, poll should call entry, event, run */
    int evt = 1;
    elib_fsm_cb_poll(&test_ctx, 40, &evt);
    assert(entry_count == 1);
    assert(event_count == 1);
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
    elib_fsm_cb_poll(&test_ctx, 10, NULL);
    assert(run_count == 0);

    printf("PASSED\n");
}

/* --- Poll event callback tests --- */

static void test_event_called_each_tick(void) {
    printf("Test: poll calls event callback each tick... ");
    reset_test();
    reset_callback_state();

    int evt = 42;
    int evt2 = 43;
    elib_fsm_cb_poll(&test_ctx, 10, &evt);
    assert(event_count == 1);
    assert(last_event_data == &evt);

    elib_fsm_cb_poll(&test_ctx, 10, &evt2);
    assert(event_count == 2);
    assert(last_event_data == &evt2);

    printf("PASSED\n");
}

static void test_event_called_before_run(void) {
    printf("Test: event fires before run each tick... ");
    reset_test();
    reset_callback_state();

    int evt = 1;
    elib_fsm_cb_poll(&test_ctx, 10, &evt);
    assert(event_count == 1);
    assert(run_count == 1);
    assert(strcmp(call_log, "ER") == 0);

    printf("PASSED\n");
}

static void test_event_during_delay(void) {
    printf("Test: event skipped during delayed transition... ");
    reset_test();
    reset_callback_state();

    /* Delayed: exit(IDLE) fires, current stays IDLE during wait */
    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 50);
    assert(exit_count == 1);

    /* During delay: event and run both skipped */
    elib_fsm_cb_poll(&test_ctx, 20, NULL);
    assert(event_count == 0);
    assert(run_count == 0);
    assert(call_log_len == 0);

    /* Expiring tick: entry(ACTIVE), then event(ACTIVE), then run(ACTIVE) */
    int evt = 1;
    elib_fsm_cb_poll(&test_ctx, 40, &evt);
    assert(event_count == 1);
    assert(entry_count == 1);
    assert(run_count == 1);
    assert(strcmp(call_log, "ER") == 0);

    printf("PASSED\n");
}

static void test_event_null_skipped(void) {
    printf("Test: null event callback skipped... ");
    reset_test();
    reset_callback_state();

    /* STATE_ERROR has NULL event and NULL run */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 0);
    assert(entry_count == 1);

    event_count = 0;
    run_count = 0;
    elib_fsm_cb_poll(&test_ctx, 10, NULL);
    assert(event_count == 0);
    assert(run_count == 0);

    printf("PASSED\n");
}

/* --- Poll null/edge tests --- */

static void test_poll_null_ctx(void) {
    printf("Test: poll with null ctx... ");
    assert(elib_fsm_cb_poll(NULL, 10, NULL) == ELIB_FSM_STATE_INVALID);
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

    elib_fsm_cb_poll(&test_ctx, 50, NULL);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_IDLE);

    /* Delay expires, current=ACTIVE, previous=IDLE */
    elib_fsm_cb_poll(&test_ctx, 50, NULL);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ACTIVE);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_IDLE);

    /* Immediate: previous becomes ACTIVE */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 0);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_ERROR);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_previous_during_delay(void) {
    printf("Test: previous is last valid state during delayed transition... ");
    reset_test();
    reset_callback_state();

    /* IDLE -> ACTIVE (immediate): previous = IDLE */
    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 0);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_IDLE);

    /* ACTIVE -> ERROR (delayed): exit(ACTIVE) fires immediately, current is
     * INVALID but previous must be the last valid state (ACTIVE) */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 100);
    assert(exit_count == 2);
    assert(last_exit_state == STATE_ACTIVE);
    assert(elib_fsm_cb_current(&test_ctx) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_ACTIVE);

    /* Still pending: previous unchanged */
    assert(elib_fsm_cb_poll(&test_ctx, 50, NULL) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_ACTIVE);

    /* Expires: current = ERROR, previous = ACTIVE */
    assert(elib_fsm_cb_poll(&test_ctx, 50, NULL) == STATE_ERROR);
    assert(elib_fsm_cb_previous(&test_ctx) == STATE_ACTIVE);

    printf("PASSED\n");
}

static void test_previous_null_ctx(void) {
    printf("Test: previous with null ctx... ");
    assert(elib_fsm_cb_previous(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

/* --- State validation tests --- */

static void test_init_invalid_initial(void) {
    printf("Test: init with initial state not in table... ");
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_cb_init(&test_ctx, test_states, TEST_STATE_COUNT,
                                           99, NULL, NULL);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    assert(test_ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_goto_invalid_target(void) {
    printf("Test: goto to state not in table is rejected... ");
    reset_test();
    reset_callback_state();

    /* Immediate: rejected, no callbacks, state unchanged */
    elib_fsm_err_t err = elib_fsm_cb_goto(&test_ctx, 77, 0);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_IDLE);
    assert(exit_count == 0);
    assert(entry_count == 0);

    /* Delayed: rejected, no exit, no pending delay */
    err = elib_fsm_cb_goto(&test_ctx, 77, 100);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    assert(exit_count == 0);
    assert(test_ctx.delayed_target == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_current(&test_ctx) == STATE_IDLE);

    printf("PASSED\n");
}

/* --- Dispatch tests --- */

static void test_dispatch_calls_event(void) {
    printf("Test: dispatch calls current state's event callback... ");
    reset_test();
    reset_callback_state();

    int evt = 42;
    elib_fsm_err_t err = elib_fsm_cb_dispatch(&test_ctx, &evt);
    assert(err == ELIB_FSM_OK);
    assert(event_count == 1);
    assert(last_event_data == &evt);

    printf("PASSED\n");
}

static void test_dispatch_null_event_data(void) {
    printf("Test: dispatch with null event_data is dropped... ");
    reset_test();
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_cb_dispatch(&test_ctx, NULL);
    assert(err == ELIB_FSM_OK);
    assert(event_count == 0);

    printf("PASSED\n");
}

static void test_dispatch_does_not_call_run(void) {
    printf("Test: dispatch does not call run callback... ");
    reset_test();
    reset_callback_state();

    int evt = 1;
    elib_fsm_cb_dispatch(&test_ctx, &evt);
    assert(event_count == 1);
    assert(run_count == 0);

    printf("PASSED\n");
}

static void test_dispatch_skips_during_delay(void) {
    printf("Test: dispatch skips event during delayed transition... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 50);
    assert(exit_count == 1);

    /* Dispatch during delay should be skipped */
    elib_fsm_err_t err = elib_fsm_cb_dispatch(&test_ctx, NULL);
    assert(err == ELIB_FSM_OK);
    assert(event_count == 0);

    printf("PASSED\n");
}

static void test_dispatch_null_ctx(void) {
    printf("Test: dispatch with null ctx... ");
    elib_fsm_err_t err = elib_fsm_cb_dispatch(NULL, NULL);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_dispatch_uninitialized(void) {
    printf("Test: dispatch with uninitialized ctx... ");
    reset_test();
    elib_fsm_cb_deinit(&test_ctx);

    elib_fsm_err_t err = elib_fsm_cb_dispatch(&test_ctx, NULL);
    assert(err == ELIB_FSM_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

static void test_dispatch_null_event_callback(void) {
    printf("Test: dispatch skips null event callback... ");
    reset_test();
    reset_callback_state();

    /* STATE_ERROR has NULL event */
    elib_fsm_cb_goto(&test_ctx, STATE_ERROR, 0);
    assert(entry_count == 1);

    event_count = 0;
    elib_fsm_err_t err = elib_fsm_cb_dispatch(&test_ctx, NULL);
    assert(err == ELIB_FSM_OK);
    assert(event_count == 0);

    printf("PASSED\n");
}

static void test_dispatch_multiple_times(void) {
    printf("Test: dispatch can be called multiple times per tick... ");
    reset_test();
    reset_callback_state();

    int evt1 = 1, evt2 = 2;
    elib_fsm_cb_dispatch(&test_ctx, &evt1);
    elib_fsm_cb_dispatch(&test_ctx, NULL);   /* null event_data is dropped */
    elib_fsm_cb_dispatch(&test_ctx, &evt2);

    assert(event_count == 2);
    assert(last_event_data == &evt2);

    printf("PASSED\n");
}

/* --- Transition (critical state) event callback tests --- */

static void test_transition_event_poll_during_delay(void) {
    printf("Test: poll delivers event to transition callback while delayed... ");
    elib_fsm_cb_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_fsm_cb_init(&ctx, test_states, TEST_STATE_COUNT, STATE_IDLE, NULL,
                     on_transition_event);
    reset_callback_state();

    int evt = 7;
    elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 50);
    assert(exit_count == 1);
    assert(trans_count == 0);

    assert(elib_fsm_cb_poll(&ctx, 20, &evt) == ELIB_FSM_STATE_INVALID);
    assert(trans_count == 1);
    assert(last_trans_event == &evt);
    assert(last_trans_from == STATE_IDLE);
    assert(last_trans_to == STATE_ACTIVE);
    assert(last_trans_remaining == 50);
    assert(event_count == 0);   /* current state's event callback not used */
    assert(run_count == 0);     /* run not called during delay */

    printf("PASSED\n");
}

static void test_transition_event_remaining_decrements(void) {
    printf("Test: transition callback sees remaining decrease across ticks... ");
    elib_fsm_cb_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_fsm_cb_init(&ctx, test_states, TEST_STATE_COUNT, STATE_IDLE, NULL,
                     on_transition_event);
    reset_callback_state();

    int evt = 1;
    elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 50);
    elib_fsm_cb_poll(&ctx, 20, &evt);
    assert(last_trans_remaining == 50);
    elib_fsm_cb_poll(&ctx, 20, &evt);
    assert(last_trans_remaining == 30);
    assert(trans_count == 2);

    printf("PASSED\n");
}

static void test_transition_event_null_event_data_skipped(void) {
    printf("Test: null event_data skips transition callback while delayed... ");
    elib_fsm_cb_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_fsm_cb_init(&ctx, test_states, TEST_STATE_COUNT, STATE_IDLE, NULL,
                     on_transition_event);
    reset_callback_state();

    int evt = 1;
    elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 100);

    /* null event_data: neither poll nor dispatch reaches the callback */
    assert(elib_fsm_cb_poll(&ctx, 20, NULL) == ELIB_FSM_STATE_INVALID);
    assert(trans_count == 0);
    assert(elib_fsm_cb_dispatch(&ctx, NULL) == ELIB_FSM_OK);
    assert(trans_count == 0);

    /* non-null event_data: callback fires */
    assert(elib_fsm_cb_poll(&ctx, 20, &evt) == ELIB_FSM_STATE_INVALID);
    assert(trans_count == 1);
    assert(last_trans_event == &evt);

    printf("PASSED\n");
}

static void test_transition_event_dispatch_during_delay(void) {
    printf("Test: dispatch delivers event to transition callback while delayed... ");
    elib_fsm_cb_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_fsm_cb_init(&ctx, test_states, TEST_STATE_COUNT, STATE_IDLE, NULL,
                     on_transition_event);
    reset_callback_state();

    int evt = 9;
    elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 50);
    elib_fsm_err_t err = elib_fsm_cb_dispatch(&ctx, &evt);
    assert(err == ELIB_FSM_OK);
    assert(trans_count == 1);
    assert(last_trans_event == &evt);
    assert(last_trans_from == STATE_IDLE);
    assert(last_trans_to == STATE_ACTIVE);
    assert(last_trans_remaining == 50);
    assert(event_count == 0);   /* current state's event callback not used */

    printf("PASSED\n");
}

static void test_transition_event_null_drops(void) {
    printf("Test: null transition callback drops events during delay... ");
    reset_test();               /* initialized with NULL transition callback */
    reset_callback_state();

    int evt = 5;
    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 50);
    assert(elib_fsm_cb_poll(&test_ctx, 20, &evt) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_cb_dispatch(&test_ctx, &evt) == ELIB_FSM_OK);
    assert(trans_count == 0);
    assert(event_count == 0);
    assert(run_count == 0);

    printf("PASSED\n");
}

static void test_transition_event_not_called_on_expiry(void) {
    printf("Test: transition callback not called on expiring tick... ");
    elib_fsm_cb_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_fsm_cb_init(&ctx, test_states, TEST_STATE_COUNT, STATE_IDLE, NULL,
                     on_transition_event);
    reset_callback_state();

    int evt = 3;
    elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 10);
    assert(elib_fsm_cb_poll(&ctx, 10, &evt) == STATE_ACTIVE);
    assert(trans_count == 0);   /* arrival uses the target's normal event */
    assert(entry_count == 1);
    assert(event_count == 1);
    assert(last_event_data == &evt);
    assert(run_count == 1);

    printf("PASSED\n");
}

/* --- Null event_data tests --- */

static void test_poll_null_event_skips_event(void) {
    printf("Test: poll with null event_data skips event but still runs... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_poll(&test_ctx, 10, NULL);
    assert(event_count == 0);
    assert(run_count == 1);
    assert(call_log_len == 1 && call_log[0] == 'R');

    printf("PASSED\n");
}

static void test_expiry_null_event_skips_event(void) {
    printf("Test: expiring tick with null event_data skips event... ");
    reset_test();
    reset_callback_state();

    elib_fsm_cb_goto(&test_ctx, STATE_ACTIVE, 10);
    assert(elib_fsm_cb_poll(&test_ctx, 10, NULL) == STATE_ACTIVE);
    assert(entry_count == 1);
    assert(event_count == 0);
    assert(run_count == 1);

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
    test_event_called_each_tick();
    test_event_called_before_run();
    test_event_during_delay();
    test_event_null_skipped();
    test_transition_event_poll_during_delay();
    test_transition_event_remaining_decrements();
    test_transition_event_dispatch_during_delay();
    test_transition_event_null_drops();
    test_transition_event_not_called_on_expiry();
    test_transition_event_null_event_data_skipped();
    test_poll_null_event_skips_event();
    test_expiry_null_event_skips_event();
    test_poll_null_ctx();
    test_delayed_immediate_during_delay();
    test_previous();
    test_previous_during_delay();
    test_previous_null_ctx();
    test_init_invalid_initial();
    test_goto_invalid_target();
    test_dispatch_calls_event();
    test_dispatch_null_event_data();
    test_dispatch_does_not_call_run();
    test_dispatch_skips_during_delay();
    test_dispatch_null_ctx();
    test_dispatch_uninitialized();
    test_dispatch_null_event_callback();
    test_dispatch_multiple_times();

    printf("\n=== All tests passed ===\n");
    return 0;
}
