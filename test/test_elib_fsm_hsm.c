/* test_elib_fsm_hsm.c - Hierarchical State Machine Unit Tests */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/elib_fsm_hsm.h"

/* Test states - 3-level hierarchy
 *
 *          ROOT
 *         /    \
 *        A      B
 *       / \    / \
 *      S1  S2  S3  S4
 */
#define ST_ROOT 0
#define ST_A    1
#define ST_B    2
#define ST_S1   3
#define ST_S2   4
#define ST_S3   5
#define ST_S4   6

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
static elib_fsm_hsm_ctx_t *handler_ctx;

static void reset_handler_state(void) {
    handler_count = 0;
    last_handler_id = -99;
    handler_return_value = false;
    handler_ctx = NULL;
}

static bool on_handler(const elib_fsm_hsm_event_t *event, void *user_data) {
    (void)user_data;
    handler_count++;
    last_handler_id = event->id;
    return handler_return_value;
}

static bool on_handler_goto_s2(const elib_fsm_hsm_event_t *event, void *user_data) {
    (void)event;
    if (handler_ctx != NULL) {
        elib_fsm_hsm_goto(handler_ctx, ST_S2);
    }
    return true;
}

/* State descriptors */
static const elib_fsm_hsm_state_desc_t test_states[] = {
    { ST_ROOT, ELIB_FSM_STATE_INVALID, ST_A, on_entry, on_exit, NULL, NULL },
    { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, NULL, NULL },
    { ST_B,    ST_ROOT, ST_S3,         on_entry, on_exit, NULL, NULL },
    { ST_S1,   ST_A,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S2,   ST_A,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S3,   ST_B,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S4,   ST_B,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
};
#define TEST_STATE_COUNT (sizeof(test_states) / sizeof(test_states[0]))

/* Handler implementations for dispatch test */
static int s1_handler_count;
static int a_handler_count;
static int root_handler_count;
static bool s1_handler_result;
static bool a_handler_result;
static bool root_handler_result;

static void reset_dispatch_tracking(void) {
    s1_handler_count = 0;
    a_handler_count = 0;
    root_handler_count = 0;
    s1_handler_result = false;
    a_handler_result = false;
    root_handler_result = false;
}

static bool handler_s1(const elib_fsm_hsm_event_t *event, void *user_data) {
    (void)user_data;
    s1_handler_count++;
    last_handler_id = event->id;
    return s1_handler_result;
}

static bool handler_a(const elib_fsm_hsm_event_t *event, void *user_data) {
    (void)user_data;
    a_handler_count++;
    last_handler_id = event->id;
    return a_handler_result;
}

static bool handler_root(const elib_fsm_hsm_event_t *event, void *user_data) {
    (void)user_data;
    root_handler_count++;
    last_handler_id = event->id;
    return root_handler_result;
}

/* State descriptors with handlers for dispatch tests */
static const elib_fsm_hsm_state_desc_t dispatch_states[] = {
    { ST_ROOT, ELIB_FSM_STATE_INVALID, ST_A, on_entry, on_exit, on_run, handler_root },
    { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, on_run, handler_a },
    { ST_B,    ST_ROOT, ST_S3,         on_entry, on_exit, on_run, NULL },
    { ST_S1,   ST_A,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, handler_s1 },
    { ST_S2,   ST_A,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, on_handler },
    { ST_S3,   ST_B,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S4,   ST_B,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
};

static elib_fsm_hsm_ctx_t test_ctx;

static void reset_test(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();
}

/* --- Init tests --- */

static void test_init_valid(void) {
    printf("Test: init with valid parameters... ");
    reset_test();

    elib_fsm_err_t err = elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT,
                                             ST_ROOT, NULL);
    assert(err == ELIB_FSM_OK);
    assert(test_ctx.initialized == 1);
    assert(test_ctx.current == ST_S1);
    assert(entry_count == 3);
    assert(exit_count == 0);

    printf("PASSED\n");
}

static void test_init_null_ctx(void) {
    printf("Test: init with null ctx... ");
    elib_fsm_err_t err = elib_fsm_hsm_init(NULL, test_states, TEST_STATE_COUNT,
                                             ST_ROOT, NULL);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_init_null_states(void) {
    printf("Test: init with null states... ");
    elib_fsm_hsm_ctx_t ctx;
    elib_fsm_err_t err = elib_fsm_hsm_init(&ctx, NULL, TEST_STATE_COUNT,
                                             ST_ROOT, NULL);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_init_zero_count(void) {
    printf("Test: init with zero state_count... ");
    elib_fsm_hsm_ctx_t ctx;
    elib_fsm_err_t err = elib_fsm_hsm_init(&ctx, test_states, 0,
                                             ST_ROOT, NULL);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_init_state_not_found(void) {
    printf("Test: init with non-existent initial state... ");
    elib_fsm_hsm_ctx_t ctx;
    elib_fsm_err_t err = elib_fsm_hsm_init(&ctx, test_states, TEST_STATE_COUNT,
                                             99, NULL);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    printf("PASSED\n");
}

static void test_init_deep_entry(void) {
    printf("Test: init entry fires for entire path... ");
    reset_test();

    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    assert(entry_count == 3);

    printf("PASSED\n");
}

/* --- Deinit tests --- */

static void test_deinit_null(void) {
    printf("Test: deinit with null ctx... ");
    elib_fsm_hsm_deinit(NULL);
    printf("PASSED\n");
}

static void test_uninitialized_context(void) {
    printf("Test: uninitialized context rejects operations... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    elib_fsm_hsm_deinit(&test_ctx);

    assert(elib_fsm_hsm_goto(&test_ctx, ST_S2) == ELIB_FSM_ERR_NOT_INITIALIZED);
    assert(elib_fsm_hsm_poll(&test_ctx) == ELIB_FSM_STATE_INVALID);
    static const elib_fsm_hsm_event_t evt1 = {1};
    assert(elib_fsm_hsm_dispatch(&test_ctx, &evt1) == false);
    assert(elib_fsm_hsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);

    printf("PASSED\n");
}

/* --- Current tests --- */

static void test_current_after_init(void) {
    printf("Test: current returns leaf after init... ");
    reset_test();

    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S1);

    printf("PASSED\n");
}

static void test_current_null_ctx(void) {
    printf("Test: current with null ctx... ");
    assert(elib_fsm_hsm_current(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

static void test_current_uninitialized(void) {
    printf("Test: current with uninitialized ctx... ");
    reset_test();
    assert(elib_fsm_hsm_current(&test_ctx) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

/* --- Goto tests --- */

static void test_goto_sibling_same_parent(void) {
    printf("Test: goto sibling under same parent (S1 -> S2)... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, ST_S2);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S2);
    assert(exit_count == 1);
    assert(last_exit_state == ST_S1);
    assert(entry_count == 1);
    assert(last_entry_state == ST_S2);

    printf("PASSED\n");
}

static void test_goto_cross_branch(void) {
    printf("Test: goto across branches (S1 -> S3)... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, ST_S3);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S3);
    assert(exit_count == 2);
    assert(entry_count == 2);

    printf("PASSED\n");
}

static void test_goto_to_ancestor(void) {
    printf("Test: goto ancestor state (S1 -> A)... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, ST_A);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S1);
    assert(exit_count == 1);
    assert(last_exit_state == ST_S1);
    assert(entry_count == 1);
    assert(last_entry_state == ST_S1);

    printf("PASSED\n");
}

static void test_goto_to_descendant(void) {
    printf("Test: goto descendant state (A -> S3) via composite target with initial... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, ST_B);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S3);
    assert(exit_count == 2);
    assert(entry_count == 2);

    printf("PASSED\n");
}

static void test_goto_self_transition(void) {
    printf("Test: goto self (S1 -> S1) external self-transition... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, ST_S1);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S1);
    assert(exit_count == 1);
    assert(last_exit_state == ST_S1);
    assert(entry_count == 1);
    assert(last_entry_state == ST_S1);

    printf("PASSED\n");
}

static void test_goto_null_ctx(void) {
    printf("Test: goto with null ctx... ");
    elib_fsm_err_t err = elib_fsm_hsm_goto(NULL, ST_S2);
    assert(err == ELIB_FSM_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_goto_state_not_found(void) {
    printf("Test: goto non-existent state... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, 99);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    printf("PASSED\n");
}

static void test_goto_root_self_transition(void) {
    printf("Test: goto ROOT from inside (S1 -> ROOT)... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, ST_ROOT);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S1);
    assert(exit_count == 3);
    assert(entry_count == 3);

    printf("PASSED\n");
}

/* --- Poll tests --- */

static void test_poll_calls_run(void) {
    printf("Test: poll calls leaf state's run callback... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    elib_fsm_state_t state = elib_fsm_hsm_poll(&test_ctx);
    assert(state == ST_S1);
    assert(run_count == 1);

    elib_fsm_hsm_poll(&test_ctx);
    assert(run_count == 2);

    printf("PASSED\n");
}

static void test_poll_null_ctx(void) {
    printf("Test: poll with null ctx... ");
    assert(elib_fsm_hsm_poll(NULL) == ELIB_FSM_STATE_INVALID);
    printf("PASSED\n");
}

static void test_poll_skips_null_run(void) {
    printf("Test: poll skips null run callback... ");
    reset_test();

    static const elib_fsm_hsm_state_desc_t no_run_states[] = {
        { ST_ROOT, ELIB_FSM_STATE_INVALID, ST_A, on_entry, on_exit, NULL, NULL },
        { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, NULL, NULL },
        { ST_S1,   ST_A,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    elib_fsm_hsm_init(&ctx, no_run_states, 3, ST_ROOT, NULL);
    run_count = 0;

    elib_fsm_state_t state = elib_fsm_hsm_poll(&ctx);
    assert(state == ST_S1);
    assert(run_count == 0);

    printf("PASSED\n");
}

/* --- Dispatch tests --- */

static void test_dispatch_handled_by_leaf(void) {
    printf("Test: dispatch handled by leaf state... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    s1_handler_result = true;

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_ROOT, NULL);

    elib_fsm_hsm_event_t evt = {.id = 42, .sub_id = 0, .cmd = 0, .arg = 0, .data = NULL, .exec = NULL};
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(s1_handler_count == 1);
    assert(last_handler_id == 42);
    assert(a_handler_count == 0);
    assert(root_handler_count == 0);

    printf("PASSED\n");
}

static void test_dispatch_bubble_to_parent(void) {
    printf("Test: dispatch bubbles to parent when leaf returns false... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    s1_handler_result = false;
    a_handler_result = true;

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_ROOT, NULL);

    elib_fsm_hsm_event_t evt = {.id = 42, .sub_id = 0, .cmd = 0, .arg = 0, .data = NULL, .exec = NULL};
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(s1_handler_count == 1);
    assert(a_handler_count == 1);
    assert(root_handler_count == 0);

    printf("PASSED\n");
}

static void test_dispatch_bubble_to_root(void) {
    printf("Test: dispatch bubbles all the way to root... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    s1_handler_result = false;
    a_handler_result = false;
    root_handler_result = true;

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_ROOT, NULL);

    elib_fsm_hsm_event_t evt = {.id = 42, .sub_id = 0, .cmd = 0, .arg = 0, .data = NULL, .exec = NULL};
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(s1_handler_count == 1);
    assert(a_handler_count == 1);
    assert(root_handler_count == 1);

    printf("PASSED\n");
}

static void test_dispatch_unhandled(void) {
    printf("Test: dispatch returns false when no handler handles... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    s1_handler_result = false;
    a_handler_result = false;
    root_handler_result = false;

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_ROOT, NULL);

    elib_fsm_hsm_event_t evt = {.id = 42, .sub_id = 0, .cmd = 0, .arg = 0, .data = NULL, .exec = NULL};
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == false);
    assert(s1_handler_count == 1);
    assert(a_handler_count == 1);
    assert(root_handler_count == 1);

    printf("PASSED\n");
}

static void test_dispatch_null_handler_bubbles(void) {
    printf("Test: dispatch with null handler bubbles up... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_B, NULL);
    root_handler_result = true;

    elib_fsm_hsm_event_t evt = {.id = 42, .sub_id = 0, .cmd = 0, .arg = 0, .data = NULL, .exec = NULL};
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(root_handler_count == 1);

    printf("PASSED\n");
}

static void test_dispatch_null_ctx(void) {
    printf("Test: dispatch with null ctx... ");
    elib_fsm_hsm_event_t evt = {.id = 1, .sub_id = 0, .cmd = 0, .arg = 0, .data = NULL, .exec = NULL};
    assert(elib_fsm_hsm_dispatch(NULL, &evt) == false);
    printf("PASSED\n");
}

static void test_dispatch_goto_inside_handler(void) {
    printf("Test: dispatch with goto inside handler stops bubbling... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    handler_ctx = &ctx;

    static const elib_fsm_hsm_state_desc_t goto_states[] = {
        { ST_ROOT, ELIB_FSM_STATE_INVALID, ST_A, on_entry, on_exit, on_run, handler_root },
        { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, on_run, handler_a },
        { ST_B,    ST_ROOT, ST_S3,         on_entry, on_exit, on_run, NULL },
        { ST_S1,   ST_A,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, on_handler_goto_s2 },
        { ST_S2,   ST_A,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, on_handler },
        { ST_S3,   ST_B,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
        { ST_S4,   ST_B,  ELIB_FSM_STATE_INVALID, on_entry, on_exit, on_run, NULL },
    };

    s1_handler_result = false;
    a_handler_result = false;
    root_handler_result = false;

    elib_fsm_hsm_init(&ctx, goto_states,
                       sizeof(goto_states) / sizeof(goto_states[0]),
                       ST_ROOT, NULL);

    assert(elib_fsm_hsm_current(&ctx) == ST_S1);

    elib_fsm_hsm_event_t evt = {.id = 1, .sub_id = 0, .cmd = 0, .arg = 0, .data = NULL, .exec = NULL};
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(elib_fsm_hsm_current(&ctx) == ST_S2);
    assert(a_handler_count == 0);
    assert(root_handler_count == 0);

    printf("PASSED\n");
}

static void test_dispatch_event_fields(void) {
    printf("Test: dispatch event fields are passed correctly... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    s1_handler_result = true;

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_ROOT, NULL);

    int payload = 42;
    static bool exec_called = false;
    exec_called = false;

    elib_fsm_hsm_event_t evt = {
        .id = 10,
        .sub_id = 20,
        .cmd = 30,
        .arg = 40,
        .data = &payload,
        .exec = NULL,
    };
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(last_handler_id == 10);

    printf("PASSED\n");
}

static void test_dispatch_null_event(void) {
    printf("Test: dispatch with null event returns false... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_ROOT, NULL);

    assert(elib_fsm_hsm_dispatch(&ctx, NULL) == false);

    printf("PASSED\n");
}

int main(void) {
    printf("=== elib-state-machine (hsm) tests ===\n\n");

    test_init_valid();
    test_init_null_ctx();
    test_init_null_states();
    test_init_zero_count();
    test_init_state_not_found();
    test_init_deep_entry();
    test_deinit_null();
    test_uninitialized_context();
    test_current_after_init();
    test_current_null_ctx();
    test_current_uninitialized();

    test_goto_sibling_same_parent();
    test_goto_cross_branch();
    test_goto_to_ancestor();
    test_goto_to_descendant();
    test_goto_self_transition();
    test_goto_null_ctx();
    test_goto_state_not_found();
    test_goto_root_self_transition();

    test_poll_calls_run();
    test_poll_null_ctx();
    test_poll_skips_null_run();

    test_dispatch_handled_by_leaf();
    test_dispatch_bubble_to_parent();
    test_dispatch_bubble_to_root();
    test_dispatch_unhandled();
    test_dispatch_null_handler_bubbles();
    test_dispatch_null_ctx();
    test_dispatch_goto_inside_handler();
    test_dispatch_event_fields();
    test_dispatch_null_event();

    printf("\n=== All tests passed ===\n");
    return 0;
}
