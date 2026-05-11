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
    assert(elib_fsm_hsm_dispatch(&test_ctx, 1) == false);
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

    printf("\n=== All tests passed ===\n");
    return 0;
}
