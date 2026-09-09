/* test_elib_fsm_hsm_hist.c - Hierarchical State Machine with History Unit Tests */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/elib_fsm_hsm_hist.h"

/* Test states - 3-level hierarchy
 *
 *          ROOT (initial: A)
 *         /    \
 *        A      B
 *       / \    / \
 *      A1  A2  B1  B2
 */
#define ST_ROOT 0
#define ST_A    1
#define ST_B    2
#define ST_A1   3
#define ST_A2   4
#define ST_B1   5
#define ST_B2   6

#define INVALID ELIB_FSM_STATE_INVALID

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

/* Handler tracking */
static int handler_count;
static elib_fsm_event_t last_handler_id;
static bool handler_return_value;
static elib_fsm_hsm_hist_ctx_t *handler_ctx;

static void reset_handler_state(void) {
    handler_count = 0;
    last_handler_id = -99;
    handler_return_value = false;
    handler_ctx = NULL;
}

static bool on_handler(void *event_data, void *user_data) {
    (void)user_data;
    handler_count++;
    last_handler_id = *(int *)event_data;
    return handler_return_value;
}

static bool on_handler_goto_b2(void *event_data, void *user_data) {
    (void)event_data;
    if (handler_ctx != NULL) {
        elib_fsm_hsm_hist_goto(handler_ctx, ST_B2);
    }
    return true;
}

/* State descriptors (non-const!) */
static elib_fsm_hsm_hist_state_desc_t test_states[] = {
    /*  state      parent    initial  last_active  entry        exit         run    handler  */
    {   ST_ROOT,   INVALID,  ST_A,    INVALID,     NULL,        NULL,        NULL,  NULL     },
    {   ST_A,      ST_ROOT,  ST_A1,   INVALID,     on_entry,    on_exit,    NULL,  NULL     },
    {   ST_B,      ST_ROOT,  ST_B1,   INVALID,     on_entry,    on_exit,    NULL,  NULL     },
    {   ST_A1,     ST_A,     INVALID, INVALID,     on_entry,    on_exit,    on_run, NULL    },
    {   ST_A2,     ST_A,     INVALID, INVALID,     on_entry,    on_exit,    on_run, NULL    },
    {   ST_B1,     ST_B,     INVALID, INVALID,     on_entry,    on_exit,    on_run, NULL    },
    {   ST_B2,     ST_B,     INVALID, INVALID,     on_entry,    on_exit,    on_run, NULL    },
};
#define TEST_STATE_COUNT (sizeof(test_states) / sizeof(test_states[0]))

static elib_fsm_hsm_hist_ctx_t test_ctx;

static void reset_test(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();
    reset_handler_state();
    elib_fsm_hsm_hist_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
}

/* --- Init tests --- */

static void test_init_sets_last_active(void) {
    printf("Test: init sets last_active = initial for all composite states... ");
    reset_test();

    /* After init, all last_active should be set to their initial values */
    assert(test_states[0].last_active == ST_A);   /* ROOT.initial = A */
    assert(test_states[1].last_active == ST_A1);   /* A.initial = A1 */
    assert(test_states[2].last_active == ST_B1);   /* B.initial = B1 */
    assert(test_states[3].last_active == INVALID); /* A1 is leaf */
    assert(test_states[4].last_active == INVALID); /* A2 is leaf */
    assert(test_states[5].last_active == INVALID); /* B1 is leaf */
    assert(test_states[6].last_active == INVALID); /* B2 is leaf */

    printf("PASSED\n");
}

static void test_init_follows_initial_chain(void) {
    printf("Test: init follows initial chain to leaf... ");
    reset_test();

    /* Should end up at A1 (ROOT→A→A1) */
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);

    printf("PASSED\n");
}

static void test_init_calls_entry_callbacks(void) {
    printf("Test: init calls entry callbacks on path... ");
    reset_test();
    reset_callback_state();
    elib_fsm_hsm_hist_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);

    /* entry(ROOT), entry(A), entry(A1) */
    assert(entry_count == 3);
    assert(last_entry_state == ST_A1);

    printf("PASSED\n");
}

static void test_init_null_ctx(void) {
    printf("Test: init with null ctx... ");
    elib_fsm_err_t err = elib_fsm_hsm_hist_init(NULL, test_states, TEST_STATE_COUNT,
                                                  ST_ROOT, NULL);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

/* --- Deinit tests --- */

static void test_deinit_null(void) {
    printf("Test: deinit with null ctx... ");
    elib_fsm_hsm_hist_deinit(NULL);
    printf("PASSED\n");
}

static void test_uninitialized_context(void) {
    printf("Test: uninitialized context rejects operations... ");
    reset_test();
    elib_fsm_hsm_hist_deinit(&test_ctx);

    assert(elib_fsm_hsm_hist_goto(&test_ctx, ST_A) == ELIB_FSM_ERR_NOT_INITIALIZED);
    assert(elib_fsm_hsm_hist_poll(&test_ctx) == ELIB_FSM_STATE_INVALID);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ELIB_FSM_STATE_INVALID);

    printf("PASSED\n");
}

/* --- Goto tests --- */

static void test_goto_same_state_noop(void) {
    printf("Test: goto same state is no-op... ");
    reset_test();
    reset_callback_state();

    /* Already at A1, goto A1 */
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(&test_ctx, ST_A1);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);
    assert(exit_count == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_goto_updates_last_active(void) {
    printf("Test: goto updates last_active on target path... ");
    reset_test();
    reset_callback_state();

    /* Currently at A1, goto A2 */
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);

    /* A.last_active should be updated to A2 */
    assert(test_states[1].last_active == ST_A2);

    printf("PASSED\n");
}

static void test_goto_restores_history(void) {
    printf("Test: goto restores to last_active leaf... ");
    reset_test();
    reset_callback_state();

    /* At A1, goto A2 to set A.last_active = A2 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    assert(test_states[1].last_active == ST_A2);

    /* Goto B (B.initial = B1) */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B1);

    /* Goto A again — should restore to A2 (not A1) */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);

    printf("PASSED\n");
}

static void test_goto_sibling(void) {
    printf("Test: goto sibling under same parent... ");
    reset_test();
    reset_callback_state();

    /* At A1, goto B1 */
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(&test_ctx, ST_B1);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B1);

    /* exit(A1), exit(A), entry(B), entry(B1) */
    assert(exit_count == 2);
    assert(entry_count == 2);
    assert(last_exit_state == ST_A);
    assert(last_entry_state == ST_B1);

    printf("PASSED\n");
}

static void test_goto_null_ctx(void) {
    printf("Test: goto with null ctx... ");
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(NULL, ST_A);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_goto_state_not_found(void) {
    printf("Test: goto non-existent state... ");
    reset_test();
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(&test_ctx, 99);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    printf("PASSED\n");
}

static void test_goto_deep_history(void) {
    printf("Test: goto deep history restores full path... ");
    reset_test();
    reset_callback_state();

    /* Path: ROOT→A→A1 */
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);

    /* goto A2: A.last_active = A2 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);

    /* goto B1: ROOT.last_active = B, B.last_active = B1 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B1);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B1);
    assert(test_states[0].last_active == ST_B);
    assert(test_states[2].last_active == ST_B1);

    /* goto A: ROOT.last_active = A, A.last_active = A2 → restore A2 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);
    assert(test_states[0].last_active == ST_A);
    assert(test_states[1].last_active == ST_A2);

    printf("PASSED\n");
}

static void test_goto_after_multiple_transitions(void) {
    printf("Test: goto after multiple transitions preserves correct history... ");
    reset_test();
    reset_callback_state();

    /* A1 → A2 → B1 → B2 → A (should restore to A2) */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B1);
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B2);
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A);

    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);
    assert(test_states[1].last_active == ST_A2);
    assert(test_states[2].last_active == ST_B2);

    printf("PASSED\n");
}

/* --- Reset tests --- */

static void test_reset_restores_last_active(void) {
    printf("Test: reset restores all last_active to initial... ");
    reset_test();
    reset_callback_state();

    /* Change history: goto A2, then B2 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B2);

    /* Verify history changed */
    assert(test_states[1].last_active == ST_A2);
    assert(test_states[2].last_active == ST_B2);

    /* Reset */
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx);
    assert(err == ELIB_FSM_OK);

    /* All last_active should be back to initial */
    assert(test_states[0].last_active == ST_A);
    assert(test_states[1].last_active == ST_A1);
    assert(test_states[2].last_active == ST_B1);

    printf("PASSED\n");
}

static void test_reset_transitions_to_initial(void) {
    printf("Test: reset transitions to initial leaf... ");
    reset_test();
    reset_callback_state();

    /* At A1, go to A2 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);

    reset_callback_state();

    /* Reset should go back to A1 */
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);

    printf("PASSED\n");
}

static void test_reset_null_ctx(void) {
    printf("Test: reset with null ctx... ");
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(NULL);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

/* --- Poll tests --- */

static void test_poll_calls_run(void) {
    printf("Test: poll calls leaf state's run callback... ");
    reset_test();
    reset_callback_state();

    elib_fsm_hsm_hist_poll(&test_ctx);
    assert(run_count == 1);

    elib_fsm_hsm_hist_poll(&test_ctx);
    assert(run_count == 2);

    printf("PASSED\n");
}

static void test_poll_null_ctx(void) {
    printf("Test: poll with null ctx... ");
    assert(elib_fsm_hsm_hist_poll(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

/* --- Dispatch tests --- */

static void test_dispatch_calls_handler(void) {
    printf("Test: dispatch calls current state's handler... ");
    reset_test();
    reset_handler_state();

    /* Add handler to A1 */
    test_states[3].handler = on_handler;

    int evt = 1;
    handler_return_value = true;
    bool handled = elib_fsm_hsm_hist_dispatch(&test_ctx, &evt);
    assert(handled == true);
    assert(handler_count == 1);
    assert(last_handler_id == 1);

    /* Cleanup */
    test_states[3].handler = NULL;

    printf("PASSED\n");
}

static void test_dispatch_bubbles_up(void) {
    printf("Test: dispatch bubbles up if not handled... ");
    reset_test();
    reset_handler_state();

    /* No handlers on any state — should return false */
    int evt = 1;
    bool handled = elib_fsm_hsm_hist_dispatch(&test_ctx, &evt);
    assert(handled == false);

    printf("PASSED\n");
}

static void test_dispatch_null_ctx(void) {
    printf("Test: dispatch with null ctx... ");
    int evt = 1;
    bool handled = elib_fsm_hsm_hist_dispatch(NULL, &evt);
    assert(handled == false);
    printf("PASSED\n");
}

static void test_dispatch_null_event(void) {
    printf("Test: dispatch with null event_data... ");
    reset_test();
    bool handled = elib_fsm_hsm_hist_dispatch(&test_ctx, NULL);
    assert(handled == false);
    printf("PASSED\n");
}

/* --- Current tests --- */

static void test_current_returns_leaf(void) {
    printf("Test: current returns leaf state... ");
    reset_test();

    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);

    elib_fsm_hsm_hist_goto(&test_ctx, ST_B2);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B2);

    printf("PASSED\n");
}

static void test_current_null_ctx(void) {
    printf("Test: current with null ctx... ");
    assert(elib_fsm_hsm_hist_current(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

int main(void) {
    printf("=== elib-state-machine (hsm_hist) tests ===\n\n");

    test_init_sets_last_active();
    test_init_follows_initial_chain();
    test_init_calls_entry_callbacks();
    test_init_null_ctx();
    test_deinit_null();
    test_uninitialized_context();
    test_goto_same_state_noop();
    test_goto_updates_last_active();
    test_goto_restores_history();
    test_goto_sibling();
    test_goto_null_ctx();
    test_goto_state_not_found();
    test_goto_deep_history();
    test_goto_after_multiple_transitions();
    test_reset_restores_last_active();
    test_reset_transitions_to_initial();
    test_reset_null_ctx();
    test_poll_calls_run();
    test_poll_null_ctx();
    test_dispatch_calls_handler();
    test_dispatch_bubbles_up();
    test_dispatch_null_ctx();
    test_dispatch_null_event();
    test_current_returns_leaf();
    test_current_null_ctx();

    printf("\n=== All tests passed ===\n");
    return 0;
}
