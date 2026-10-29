/* test_elib_fsm_hsm.c - Hierarchical State Machine Unit Tests (v2)
 *
 * Ported from v1 plus v2 additions:
 *  - init-time hierarchy validation matrix: parent cycles, over-deep
 *    chains, duplicate state values, the reserved INVALID value,
 *    dangling parent/initial references, non-child initial -- all
 *    rejected with a pristine ctx and zero fired callbacks
 *  - dispatch/goto same-leaf regressions: a handler goto that resolves
 *    back to the SAME leaf (self transition, or ancestor target whose
 *    history/initial chain lands back) must stop bubbling (v1 missed
 *    both cases because it compared ctx->current)
 *  - forest goto: crossing between independent top-level trees (whole
 *    configuration teardown + rebuild)
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_fsm.h"

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
static elib_fsm_hsm_ctx_t *handler_ctx;      /* for gotos inside handlers */

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

static bool on_handler_goto_s2(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;
    if (handler_ctx != NULL) {
        elib_fsm_hsm_goto(handler_ctx, ST_S2);
    }
    return true;
}

/* v2 same-leaf regression handlers */
static bool on_handler_goto_self(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;
    if (handler_ctx != NULL) {
        elib_fsm_hsm_goto(handler_ctx, ST_S1);   /* self, returns false: the
                                                      stop must come from the
                                                      transition counter */
    }
    return false;
}

static bool on_handler_goto_ancestor_sameleaf(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;
    if (handler_ctx != NULL) {
        elib_fsm_hsm_goto(handler_ctx, ST_A);    /* A's initial chain lands
                                                      back on S1 */
    }
    return false;
}

/* Forest fixture states -- defined before the handler that jumps to it */
#define ST_F1        30
#define ST_F2        31
#define ST_F1_LEAF   32
#define ST_F2_LEAF   33

/* v2 forest handler */
static elib_fsm_hsm_ctx_t *forest_goto_ctx;
static bool on_handler_goto_other_tree(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;
    if (forest_goto_ctx != NULL) {
        elib_fsm_hsm_goto(forest_goto_ctx, ST_F2_LEAF);
    }
    return true;
}

/* State descriptors */
static const elib_fsm_hsm_state_desc_t test_states[] = {
    { ST_ROOT, INVALID, ST_A, on_entry, on_exit, NULL, NULL },
    { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, NULL, NULL },
    { ST_B,    ST_ROOT, ST_S3,         on_entry, on_exit, NULL, NULL },
    { ST_S1,   ST_A,  INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S2,   ST_A,  INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S3,   ST_B,  INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S4,   ST_B,  INVALID, on_entry, on_exit, on_run, NULL },
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

static bool handler_s1(void *event_data, void *user_data) {
    (void)user_data;
    s1_handler_count++;
    last_handler_id = *(int *)event_data;
    return s1_handler_result;
}

static bool handler_a(void *event_data, void *user_data) {
    (void)user_data;
    a_handler_count++;
    last_handler_id = *(int *)event_data;
    return a_handler_result;
}

static bool handler_root(void *event_data, void *user_data) {
    (void)user_data;
    root_handler_count++;
    last_handler_id = *(int *)event_data;
    return root_handler_result;
}

/* State descriptors with handlers for dispatch tests */
static const elib_fsm_hsm_state_desc_t dispatch_states[] = {
    { ST_ROOT, INVALID, ST_A, on_entry, on_exit, on_run, handler_root },
    { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, on_run, handler_a },
    { ST_B,    ST_ROOT, ST_S3,         on_entry, on_exit, on_run, NULL },
    { ST_S1,   ST_A,  INVALID, on_entry, on_exit, on_run, handler_s1 },
    { ST_S2,   ST_A,  INVALID, on_entry, on_exit, on_run, on_handler },
    { ST_S3,   ST_B,  INVALID, on_entry, on_exit, on_run, NULL },
    { ST_S4,   ST_B,  INVALID, on_entry, on_exit, on_run, NULL },
};

/* Same-leaf fixtures */
static const elib_fsm_hsm_state_desc_t selfleaf_states[] = {
    { ST_ROOT, INVALID, ST_A, on_entry, on_exit, NULL, NULL },
    { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, NULL, handler_a },
    { ST_S1,   ST_A,  INVALID, on_entry, on_exit, on_run, on_handler_goto_self },
    { ST_S2,   ST_A,  INVALID, on_entry, on_exit, on_run, NULL },
};

static const elib_fsm_hsm_state_desc_t ancestorleaf_states[] = {
    { ST_ROOT, INVALID, ST_A, on_entry, on_exit, NULL, NULL },
    { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, NULL, handler_a },
    { ST_S1,   ST_A,  INVALID, on_entry, on_exit, on_run,
      on_handler_goto_ancestor_sameleaf },
    { ST_S2,   ST_A,  INVALID, on_entry, on_exit, on_run, NULL },
};

/* Forest fixture: two independent top-level trees (value macros are
 * defined above, next to the handler that jumps between the tops) */

static const elib_fsm_hsm_state_desc_t forest_states[] = {
    { ST_F1,      INVALID,  ST_F1_LEAF, on_entry, on_exit, NULL,
      on_handler_goto_other_tree },
    { ST_F2,      INVALID,  ST_F2_LEAF, on_entry, on_exit, NULL, NULL },
    { ST_F1_LEAF, ST_F1,    INVALID,    on_entry, on_exit, on_run, NULL },
    { ST_F2_LEAF, ST_F2,    INVALID,    on_entry, on_exit, on_run, NULL },
};

static elib_fsm_hsm_ctx_t test_ctx;

static void reset_test(void) {
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();
    reset_handler_state();
    reset_dispatch_tracking();
}

/* --- Init tests --- */

static void test_init_valid(void) {
    printf("Test: init with valid parameters... ");
    reset_test();

    elib_fsm_err_t err = elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT,
                                              ST_ROOT, NULL);
    assert(err == ELIB_FSM_OK);
    assert(test_ctx.bit_flags.initialized == 1);
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

/* --- v2 validation matrix --- */

static void test_validate_parent_cycle_rejected(void) {
    printf("Test: init rejects a parent cycle... ");
    static const elib_fsm_hsm_state_desc_t cycle_states[] = {
        { 50, 51,    INVALID, on_entry, on_exit, NULL, NULL },
        { 51, 50,    INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, cycle_states, 2, 50, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);   /* validation must precede any callback */

    printf("PASSED\n");
}

static void test_validate_overdeep_rejected(void) {
    printf("Test: init rejects a chain deeper than MAX_DEPTH... ");
    /* 17-node chain 100..116: node 116 sits at depth 16 > 15 (a chain
     * of at most MAX_DEPTH-1=15 parent links is allowed) */
    static const elib_fsm_hsm_state_desc_t deep_states[] = {
        { 100, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 101, 100, INVALID, on_entry, on_exit, NULL, NULL },
        { 102, 101, INVALID, on_entry, on_exit, NULL, NULL },
        { 103, 102, INVALID, on_entry, on_exit, NULL, NULL },
        { 104, 103, INVALID, on_entry, on_exit, NULL, NULL },
        { 105, 104, INVALID, on_entry, on_exit, NULL, NULL },
        { 106, 105, INVALID, on_entry, on_exit, NULL, NULL },
        { 107, 106, INVALID, on_entry, on_exit, NULL, NULL },
        { 108, 107, INVALID, on_entry, on_exit, NULL, NULL },
        { 109, 108, INVALID, on_entry, on_exit, NULL, NULL },
        { 110, 109, INVALID, on_entry, on_exit, NULL, NULL },
        { 111, 110, INVALID, on_entry, on_exit, NULL, NULL },
        { 112, 111, INVALID, on_entry, on_exit, NULL, NULL },
        { 113, 112, INVALID, on_entry, on_exit, NULL, NULL },
        { 114, 113, INVALID, on_entry, on_exit, NULL, NULL },
        { 115, 114, INVALID, on_entry, on_exit, NULL, NULL },
        { 116, 115, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, deep_states, 17, 100, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_chain_at_limit_accepted(void) {
    printf("Test: init accepts a chain exactly at the depth limit... ");
    /* 16-node chain 120..135: the deepest node 135 sits at depth 15 --
     * the longest active path (16 nodes) fits the bound exactly. The
     * initial field chains 120 -> ... -> 135 so init descends to 135. */
    static const elib_fsm_hsm_state_desc_t edge_states[] = {
        { 120, INVALID, 121, on_entry, on_exit, NULL, NULL },
        { 121, 120, 122, on_entry, on_exit, NULL, NULL },
        { 122, 121, 123, on_entry, on_exit, NULL, NULL },
        { 123, 122, 124, on_entry, on_exit, NULL, NULL },
        { 124, 123, 125, on_entry, on_exit, NULL, NULL },
        { 125, 124, 126, on_entry, on_exit, NULL, NULL },
        { 126, 125, 127, on_entry, on_exit, NULL, NULL },
        { 127, 126, 128, on_entry, on_exit, NULL, NULL },
        { 128, 127, 129, on_entry, on_exit, NULL, NULL },
        { 129, 128, 130, on_entry, on_exit, NULL, NULL },
        { 130, 129, 131, on_entry, on_exit, NULL, NULL },
        { 131, 130, 132, on_entry, on_exit, NULL, NULL },
        { 132, 131, 133, on_entry, on_exit, NULL, NULL },
        { 133, 132, 134, on_entry, on_exit, NULL, NULL },
        { 134, 133, 135, on_entry, on_exit, NULL, NULL },
        { 135, 134, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, edge_states, 16, 120, NULL)
           == ELIB_FSM_OK);
    assert(ctx.current == 135);
    assert(entry_count == 16);

    printf("PASSED\n");
}

static void test_validate_duplicate_rejected(void) {
    printf("Test: init rejects duplicate state values... ");
    static const elib_fsm_hsm_state_desc_t dup_states[] = {
        { 40, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 41, 40, INVALID, on_entry, on_exit, NULL, NULL },
        { 40, 41, INVALID, on_entry, on_exit, NULL, NULL },   /* duplicate */
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, dup_states, 3, 40, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_invalid_value_rejected(void) {
    printf("Test: init rejects ELIB_FSM_STATE_INVALID as a state value... ");
    static const elib_fsm_hsm_state_desc_t bad_states[] = {
        { 40, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { INVALID, 40, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, bad_states, 2, 40, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_dangling_parent_rejected(void) {
    printf("Test: init rejects a parent pointing outside the table... ");
    static const elib_fsm_hsm_state_desc_t dang_states[] = {
        { 40, 77, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, dang_states, 1, 40, NULL)
           == ELIB_FSM_ERR_STATE_NOT_FOUND);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_dangling_initial_rejected(void) {
    printf("Test: init rejects an initial pointing outside the table... ");
    static const elib_fsm_hsm_state_desc_t dang_states[] = {
        { 40, INVALID, 77, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, dang_states, 1, 40, NULL)
           == ELIB_FSM_ERR_STATE_NOT_FOUND);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_non_child_initial_rejected(void) {
    printf("Test: init rejects an initial that is not a direct child... ");
    static const elib_fsm_hsm_state_desc_t nc_states[] = {
        { 40, INVALID, 41, on_entry, on_exit, NULL, NULL },
        { 41, INVALID, INVALID, on_entry, on_exit, NULL, NULL },  /* 41's
                                                       parent != 40 */
    };
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_init(&ctx, nc_states, 2, 40, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

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
    int evt1 = 1;
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
    printf("Test: goto ancestor state (S1 -> A) is an external transition... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);
    reset_callback_state();

    /* Strict-LCA: domain is A's parent (ROOT), so A itself is exited and
     * re-entered; the target then descends its initial chain back to S1. */
    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, ST_A);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S1);
    assert(exit_count == 2);
    assert(last_exit_state == ST_A);
    assert(entry_count == 2);
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

static void test_goto_uninitialized(void) {
    printf("Test: goto on uninitialized ctx... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    assert(elib_fsm_hsm_goto(&ctx, ST_S1) == ELIB_FSM_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

static void test_goto_state_not_found(void) {
    printf("Test: goto non-existent state... ");
    reset_test();
    elib_fsm_hsm_init(&test_ctx, test_states, TEST_STATE_COUNT, ST_ROOT, NULL);

    elib_fsm_err_t err = elib_fsm_hsm_goto(&test_ctx, 99);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    /* rejection must not have moved the machine */
    assert(elib_fsm_hsm_current(&test_ctx) == ST_S1);
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

/* v2: crossing between independent top-level trees tears down one
 * whole configuration and builds the other from its top. */
static void test_goto_forest_cross_top(void) {
    printf("Test: goto across independent top-level trees (forest)... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_handler_state();

    forest_goto_ctx = &ctx;
    assert(elib_fsm_hsm_init(&ctx, forest_states, 4, ST_F1, NULL)
           == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&ctx) == ST_F1_LEAF);
    reset_callback_state();

    int evt = 1;
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);

    /* no common ancestor: whole F1 config exits (leaf, then top),
     * whole F2 config enters (top, then leaf) */
    assert(elib_fsm_hsm_current(&ctx) == ST_F2_LEAF);
    assert(exit_count == 2);
    assert(last_exit_state == ST_F1);
    assert(entry_count == 2);
    assert(last_entry_state == ST_F2_LEAF);

    /* and back the other way, now driven by goto directly */
    reset_callback_state();
    assert(elib_fsm_hsm_goto(&ctx, ST_F1_LEAF) == ELIB_FSM_OK);
    assert(elib_fsm_hsm_current(&ctx) == ST_F1_LEAF);
    assert(exit_count == 2);
    assert(entry_count == 2);

    forest_goto_ctx = NULL;
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
        { ST_ROOT, INVALID, ST_A, on_entry, on_exit, NULL, NULL },
        { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, NULL, NULL },
        { ST_S1,   ST_A,  INVALID, on_entry, on_exit, NULL, NULL },
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

    int evt = 42;
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

    int evt = 42;
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

    int evt = 42;
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

    int evt = 42;
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

    int evt = 42;
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(root_handler_count == 1);

    printf("PASSED\n");
}

static void test_dispatch_null_ctx(void) {
    printf("Test: dispatch with null ctx... ");
    int evt = 1;
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
        { ST_ROOT, INVALID, ST_A, on_entry, on_exit, on_run, handler_root },
        { ST_A,    ST_ROOT, ST_S1,         on_entry, on_exit, on_run, handler_a },
        { ST_B,    ST_ROOT, ST_S3,         on_entry, on_exit, on_run, NULL },
        { ST_S1,   ST_A,  INVALID, on_entry, on_exit, on_run, on_handler_goto_s2 },
        { ST_S2,   ST_A,  INVALID, on_entry, on_exit, on_run, on_handler },
        { ST_S3,   ST_B,  INVALID, on_entry, on_exit, on_run, NULL },
        { ST_S4,   ST_B,  INVALID, on_entry, on_exit, on_run, NULL },
    };

    s1_handler_result = false;
    a_handler_result = false;
    root_handler_result = false;

    elib_fsm_hsm_init(&ctx, goto_states,
                       sizeof(goto_states) / sizeof(goto_states[0]),
                       ST_ROOT, NULL);

    assert(elib_fsm_hsm_current(&ctx) == ST_S1);

    int evt = 1;
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(elib_fsm_hsm_current(&ctx) == ST_S2);
    assert(a_handler_count == 0);
    assert(root_handler_count == 0);

    printf("PASSED\n");
}

/* v2 regression 1: a self goto inside a handler changes NOTHING the
 * v1 detector could see -- current stays the same leaf -- yet
 * bubbling must stop (the exited state's handlers must not run). */
static void test_dispatch_goto_same_leaf_stops_bubble(void) {
    printf("Test: handler goto(self) stops bubbling on same leaf... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    handler_ctx = &ctx;

    elib_fsm_hsm_init(&ctx, selfleaf_states, 4, ST_ROOT, NULL);
    assert(elib_fsm_hsm_current(&ctx) == ST_S1);
    reset_callback_state();
    reset_dispatch_tracking();

    int evt = 1;
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);                    /* transition counts as handled */
    assert(elib_fsm_hsm_current(&ctx) == ST_S1);/* but the leaf is unchanged */
    assert(s1_handler_count == 1);              /* self handler ran once */
    assert(a_handler_count == 0);               /* parent handler NOT called */
    assert(root_handler_count == 0);

    /* the external self transition itself fired exit+entry of S1 */
    assert(exit_count == 1);
    assert(last_exit_state == ST_S1);
    assert(entry_count == 1);
    assert(last_entry_state == ST_S1);

    handler_ctx = NULL;
    printf("PASSED\n");
}

/* v2 regression 2: goto(ancestor) whose initial chain resolves back to
 * the exact same leaf also bubbling-stops with an unchanged current. */
static void test_dispatch_goto_ancestor_same_leaf_stops_bubble(void) {
    printf("Test: handler goto(ancestor) landing on same leaf stops bubbling... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    handler_ctx = &ctx;

    elib_fsm_hsm_init(&ctx, ancestorleaf_states, 4, ST_ROOT, NULL);
    assert(elib_fsm_hsm_current(&ctx) == ST_S1);
    reset_callback_state();
    reset_dispatch_tracking();

    int evt = 1;
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(elib_fsm_hsm_current(&ctx) == ST_S1);
    assert(s1_handler_count == 1);
    assert(a_handler_count == 0);       /* v1 would have bubbled here */
    assert(root_handler_count == 0);

    /* external transition across the ancestor: exit S1+A, enter A+S1 */
    assert(exit_count == 2);
    assert(last_exit_state == ST_A);
    assert(entry_count == 2);
    assert(last_entry_state == ST_S1);

    handler_ctx = NULL;
    printf("PASSED\n");
}

static void test_dispatch_event_fields(void) {
    printf("Test: dispatch event data is passed correctly... ");
    elib_fsm_hsm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_dispatch_tracking();
    s1_handler_result = true;

    elib_fsm_hsm_init(&ctx, dispatch_states,
                       sizeof(dispatch_states) / sizeof(dispatch_states[0]),
                       ST_ROOT, NULL);

    int evt = 42;
    bool handled = elib_fsm_hsm_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(last_handler_id == 42);

    printf("PASSED\n");
}

static void test_dispatch_null_event(void) {
    printf("Test: dispatch with null event_data... ");
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

    printf("--- table validation ---\n");
    test_validate_parent_cycle_rejected();
    test_validate_overdeep_rejected();
    test_validate_chain_at_limit_accepted();
    test_validate_duplicate_rejected();
    test_validate_invalid_value_rejected();
    test_validate_dangling_parent_rejected();
    test_validate_dangling_initial_rejected();
    test_validate_non_child_initial_rejected();

    test_deinit_null();
    test_uninitialized_context();
    test_current_after_init();
    test_current_null_ctx();
    test_current_uninitialized();

    printf("--- goto ---\n");
    test_goto_sibling_same_parent();
    test_goto_cross_branch();
    test_goto_to_ancestor();
    test_goto_to_descendant();
    test_goto_self_transition();
    test_goto_null_ctx();
    test_goto_uninitialized();
    test_goto_state_not_found();
    test_goto_root_self_transition();
    test_goto_forest_cross_top();

    printf("--- poll ---\n");
    test_poll_calls_run();
    test_poll_null_ctx();
    test_poll_skips_null_run();

    printf("--- dispatch ---\n");
    test_dispatch_handled_by_leaf();
    test_dispatch_bubble_to_parent();
    test_dispatch_bubble_to_root();
    test_dispatch_unhandled();
    test_dispatch_null_handler_bubbles();
    test_dispatch_null_ctx();
    test_dispatch_goto_inside_handler();
    test_dispatch_goto_same_leaf_stops_bubble();
    test_dispatch_goto_ancestor_same_leaf_stops_bubble();
    test_dispatch_event_fields();
    test_dispatch_null_event();

    printf("\n=== All tests passed ===\n");
    return 0;
}
