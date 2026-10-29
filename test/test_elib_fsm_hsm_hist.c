/* test_elib_fsm_hsm_hist.c - Hierarchical State Machine with History
 * Unit Tests (v2)
 *
 * Ported from v1 plus v2 additions:
 *  - init-time hierarchy validation matrix (same rules as the const
 *    hsm table): cycles, over-deep chains, duplicate state values, the
 *    reserved INVALID value, dangling parent/initial references and
 *    non-child initials are all rejected with a pristine ctx
 *  - dispatch/goto same-leaf regressions: a handler goto resolving back
 *    to the SAME leaf (self transition, or ancestor target whose
 *    history chain restores it) must stop bubbling (v1 missed both)
 *  - reset() short-circuit: wiping history while already sitting on
 *    the resolved leaf fires NO callbacks
 *  - goto rejection leaves the machine and the history untouched
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_fsm.h"

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
static int a_handler_count;      /* ST_A bubble-watch (same-leaf tests) */
static elib_fsm_hsm_hist_ctx_t *handler_ctx;   /* for gotos inside handlers */
static elib_fsm_hsm_hist_ctx_t *cross_goto_ctx; /* for cross-branch handler goto */

static void reset_handler_state(void) {
    handler_count = 0;
    last_handler_id = -99;
    handler_return_value = false;
    a_handler_count = 0;
    handler_ctx = NULL;
    cross_goto_ctx = NULL;
}

static bool on_handler(void *event_data, void *user_data) {
    (void)user_data;
    handler_count++;
    last_handler_id = *(int *)event_data;
    return handler_return_value;
}

static bool handler_a(void *event_data, void *user_data) {
    (void)user_data;
    a_handler_count++;
    last_handler_id = *(int *)event_data;
    return false;   /* never claims: used only to detect illegal bubbling */
}

/* goto(self) inside a handler, returns false: bubbling must stop via
 * the transition counter alone (v1 would have kept bubbling here) */
static bool on_handler_goto_self(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;
    if (handler_ctx != NULL) {
        elib_fsm_hsm_hist_goto(handler_ctx, ST_A1);
    }
    return false;
}

/* goto(ancestor) whose history chain restores the same leaf */
static bool on_handler_goto_ancestor_sameleaf(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;
    if (handler_ctx != NULL) {
        elib_fsm_hsm_hist_goto(handler_ctx, ST_A);   /* restores A1 */
    }
    return false;
}

/* goto across branches inside a handler (B1 -> B2) */
static bool on_handler_goto_b2(void *event_data, void *user_data) {
    (void)event_data;
    (void)user_data;
    if (cross_goto_ctx != NULL) {
        elib_fsm_hsm_hist_goto(cross_goto_ctx, ST_B2);
    }
    return true;
}

/* State descriptors (non-const: last_active is framework-managed) */
static elib_fsm_hsm_hist_state_desc_t test_states[] = {
    /*  state      parent    initial  last_active  entry        exit         run    handler  */
    {   ST_ROOT,   INVALID,  ST_A,    INVALID,     on_entry,    on_exit,     NULL,  NULL     },
    {   ST_A,      ST_ROOT,  ST_A1,   INVALID,     on_entry,    on_exit,     NULL,  NULL     },
    {   ST_B,      ST_ROOT,  ST_B1,   INVALID,     on_entry,    on_exit,     NULL,  NULL     },
    {   ST_A1,     ST_A,     INVALID, INVALID,     on_entry,    on_exit,     on_run, NULL    },
    {   ST_A2,     ST_A,     INVALID, INVALID,     on_entry,    on_exit,     on_run, NULL    },
    {   ST_B1,     ST_B,     INVALID, INVALID,     on_entry,    on_exit,     on_run, NULL    },
    {   ST_B2,     ST_B,     INVALID, INVALID,     on_entry,    on_exit,     on_run, NULL    },
};
#define TEST_STATE_COUNT (sizeof(test_states) / sizeof(test_states[0]))

/* Same-leaf regression fixtures (handlers on leaf + parent) */
static elib_fsm_hsm_hist_state_desc_t selfleaf_states[] = {
    { ST_ROOT, INVALID, ST_A, INVALID, on_entry, on_exit, NULL,   NULL  },
    { ST_A,    ST_ROOT, ST_A1, INVALID, on_entry, on_exit, NULL,   handler_a  },
    { ST_B,    ST_ROOT, ST_B1, INVALID, on_entry, on_exit, NULL,   NULL  },
    { ST_A1,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, on_handler_goto_self },
    { ST_A2,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, NULL  },
};

static elib_fsm_hsm_hist_state_desc_t ancestorleaf_states[] = {
    { ST_ROOT, INVALID, ST_A, INVALID, on_entry, on_exit, NULL,   NULL  },
    { ST_A,    ST_ROOT, ST_A1, INVALID, on_entry, on_exit, NULL,   handler_a  },
    { ST_B,    ST_ROOT, ST_B1, INVALID, on_entry, on_exit, NULL,   NULL  },
    { ST_A1,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, on_handler_goto_ancestor_sameleaf },
    { ST_A2,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, NULL  },
};

/* Cross-branch goto fixture (handler on B1 jumps to B2) */
static elib_fsm_hsm_hist_state_desc_t crossgoto_states[] = {
    { ST_ROOT, INVALID, ST_A, INVALID, on_entry, on_exit, NULL,   NULL  },
    { ST_A,    ST_ROOT, ST_A1, INVALID, on_entry, on_exit, NULL,   NULL  },
    { ST_B,    ST_ROOT, ST_B1, INVALID, on_entry, on_exit, NULL,   NULL  },
    { ST_A1,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, NULL },
    { ST_A2,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, NULL },
    { ST_B1,   ST_B,    INVALID, INVALID, on_entry, on_exit, on_run, on_handler_goto_b2 },
    { ST_B2,   ST_B,    INVALID, INVALID, on_entry, on_exit, on_run, NULL },
};

/* Bubbling fixture: handler on B2, none elsewhere */
static elib_fsm_hsm_hist_state_desc_t bubbling_states[] = {
    { ST_ROOT, INVALID, ST_A, INVALID, on_entry, on_exit, NULL,   NULL        },
    { ST_A,    ST_ROOT, ST_A1, INVALID, on_entry, on_exit, NULL,   NULL        },
    { ST_B,    ST_ROOT, ST_B1, INVALID, on_entry, on_exit, NULL,   NULL        },
    { ST_A1,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, NULL },
    { ST_A2,   ST_A,    INVALID, INVALID, on_entry, on_exit, on_run, NULL },
    { ST_B1,   ST_B,    INVALID, INVALID, on_entry, on_exit, on_run, NULL },
    { ST_B2,   ST_B,    INVALID, INVALID, on_entry, on_exit, on_run, on_handler  },
};

/* Forest fixture: two independent top-level states, for reset-target tests */
#define F_ROOT1 10
#define F_ROOT2 20
#define F_C1    11
#define F_C2    21

static elib_fsm_hsm_hist_state_desc_t forest_states[] = {
    /*  state     parent    initial  last_active  entry      exit       run     handler */
    {   F_ROOT1,  INVALID,  F_C1,    INVALID,     on_entry,  on_exit,   NULL,   NULL    },
    {   F_ROOT2,  INVALID,  F_C2,    INVALID,     on_entry,  on_exit,   NULL,   NULL    },
    {   F_C1,     F_ROOT1,  INVALID, INVALID,     on_entry,  on_exit,   on_run, NULL    },
    {   F_C2,     F_ROOT2,  INVALID, INVALID,     on_entry,  on_exit,   on_run, NULL    },
};
#define FOREST_STATE_COUNT (sizeof(forest_states) / sizeof(forest_states[0]))

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
    assert(test_states[0].last_active == ST_A);    /* ROOT.initial = A */
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

    /* Should end up at A1 (ROOT->A->A1) */
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

/* --- v2 validation matrix --- */

static void test_validate_parent_cycle_rejected(void) {
    printf("Test: init rejects a parent cycle... ");
    static elib_fsm_hsm_hist_state_desc_t cycle_states[] = {
        { 50, 51,    INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 51, 50,    INVALID, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, cycle_states, 2, 50, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);   /* validation must precede any callback */

    printf("PASSED\n");
}

static void test_validate_overdeep_rejected(void) {
    printf("Test: init rejects a chain deeper than MAX_DEPTH... ");
    /* 17-node chain 100..116: node 116 needs 16 parent links > 15 */
    static elib_fsm_hsm_hist_state_desc_t deep_states[] = {
        { 100, INVALID, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 101, 100, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 102, 101, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 103, 102, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 104, 103, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 105, 104, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 106, 105, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 107, 106, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 108, 107, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 109, 108, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 110, 109, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 111, 110, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 112, 111, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 113, 112, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 114, 113, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 115, 114, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 116, 115, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, deep_states, 17, 100, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_chain_at_limit_accepted(void) {
    printf("Test: init accepts a chain exactly at the depth limit... ");
    /* 16-node chain 140..155: the deepest node 155 needs 15 parent
     * links -- the bound (MAX_DEPTH-1) is met exactly. The initial
     * field chains 140 -> ... -> 155 so init descends to 155. */
    static elib_fsm_hsm_hist_state_desc_t edge_states[] = {
        { 140, INVALID, 141, INVALID, on_entry, on_exit, NULL, NULL },
        { 141, 140, 142, INVALID, on_entry, on_exit, NULL, NULL },
        { 142, 141, 143, INVALID, on_entry, on_exit, NULL, NULL },
        { 143, 142, 144, INVALID, on_entry, on_exit, NULL, NULL },
        { 144, 143, 145, INVALID, on_entry, on_exit, NULL, NULL },
        { 145, 144, 146, INVALID, on_entry, on_exit, NULL, NULL },
        { 146, 145, 147, INVALID, on_entry, on_exit, NULL, NULL },
        { 147, 146, 148, INVALID, on_entry, on_exit, NULL, NULL },
        { 148, 147, 149, INVALID, on_entry, on_exit, NULL, NULL },
        { 149, 148, 150, INVALID, on_entry, on_exit, NULL, NULL },
        { 150, 149, 151, INVALID, on_entry, on_exit, NULL, NULL },
        { 151, 150, 152, INVALID, on_entry, on_exit, NULL, NULL },
        { 152, 151, 153, INVALID, on_entry, on_exit, NULL, NULL },
        { 153, 152, 154, INVALID, on_entry, on_exit, NULL, NULL },
        { 154, 153, 155, INVALID, on_entry, on_exit, NULL, NULL },
        { 155, 154, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, edge_states, 16, 140, NULL)
           == ELIB_FSM_OK);
    assert(ctx.current == 155);
    assert(entry_count == 16);

    printf("PASSED\n");
}

static void test_validate_duplicate_rejected(void) {
    printf("Test: init rejects duplicate state values... ");
    static elib_fsm_hsm_hist_state_desc_t dup_states[] = {
        { 40, INVALID, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 41, 40, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { 40, 41, INVALID, INVALID, on_entry, on_exit, NULL, NULL },   /* duplicate */
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, dup_states, 3, 40, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_invalid_value_rejected(void) {
    printf("Test: init rejects ELIB_FSM_STATE_INVALID as a state value... ");
    static elib_fsm_hsm_hist_state_desc_t bad_states[] = {
        { 40, INVALID, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
        { INVALID, 40, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, bad_states, 2, 40, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_dangling_parent_rejected(void) {
    printf("Test: init rejects a parent pointing outside the table... ");
    static elib_fsm_hsm_hist_state_desc_t dang_states[] = {
        { 40, 77, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, dang_states, 1, 40, NULL)
           == ELIB_FSM_ERR_STATE_NOT_FOUND);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_dangling_initial_rejected(void) {
    printf("Test: init rejects an initial pointing outside the table... ");
    static elib_fsm_hsm_hist_state_desc_t dang_states[] = {
        { 40, INVALID, 77, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, dang_states, 1, 40, NULL)
           == ELIB_FSM_ERR_STATE_NOT_FOUND);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

    printf("PASSED\n");
}

static void test_validate_non_child_initial_rejected(void) {
    printf("Test: init rejects an initial that is not a direct child... ");
    static elib_fsm_hsm_hist_state_desc_t nc_states[] = {
        { 40, INVALID, 41, INVALID, on_entry, on_exit, NULL, NULL },
        { 41, INVALID, INVALID, INVALID, on_entry, on_exit, NULL, NULL },
    };
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();

    assert(elib_fsm_hsm_hist_init(&ctx, nc_states, 2, 40, NULL)
           == ELIB_FSM_ERR_INVALID_PARAM);
    assert(ctx.bit_flags.initialized == 0);
    assert(entry_count == 0);

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

    int evt = 1;
    assert(elib_fsm_hsm_hist_dispatch(&test_ctx, &evt) == false);

    printf("PASSED\n");
}

/* --- Goto tests --- */

static void test_goto_self_transition(void) {
    printf("Test: goto self (A1 -> A1) is an external self-transition... ");
    reset_test();
    reset_callback_state();

    /* Already at A1, goto A1: exits and re-enters A1 */
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(&test_ctx, ST_A1);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);
    assert(exit_count == 1);
    assert(last_exit_state == ST_A1);
    assert(entry_count == 1);
    assert(last_entry_state == ST_A1);

    printf("PASSED\n");
}

static void test_goto_to_ancestor(void) {
    printf("Test: goto ancestor state (A1 -> A) is an external transition... ");
    reset_test();
    reset_callback_state();

    /* Strict-LCA: domain is A's parent (ROOT); A and its child are exited
     * and re-entered, then A's history (A1) restores the leaf. The leaf
     * is resolved BEFORE any callback fires. */
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(&test_ctx, ST_A);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);
    assert(exit_count == 2);
    assert(last_exit_state == ST_A);
    assert(entry_count == 2);
    assert(last_entry_state == ST_A1);

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

    /* Goto A again - should restore to A2 (not A1) */
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
    printf("Test: goto non-existent state leaves machine and history alone... ");
    reset_test();
    elib_fsm_err_t err = elib_fsm_hsm_hist_goto(&test_ctx, 99);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    /* rejection must not move the machine nor write any history */
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);
    assert(test_states[0].last_active == ST_A);
    assert(test_states[1].last_active == ST_A1);
    printf("PASSED\n");
}

static void test_goto_deep_history(void) {
    printf("Test: goto deep history restores full path... ");
    reset_test();
    reset_callback_state();

    /* Path: ROOT->A->A1 */
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);

    /* goto A2: A.last_active = A2 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);

    /* goto B1: ROOT.last_active = B, B.last_active = B1 */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B1);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B1);
    assert(test_states[0].last_active == ST_B);
    assert(test_states[2].last_active == ST_B1);

    /* goto A: ROOT.last_active = A, A.last_active = A2 -> restore A2 */
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

    /* A1 -> A2 -> B1 -> B2 -> A (should restore to A2) */
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
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx, ST_ROOT);
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
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx, ST_ROOT);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);

    printf("PASSED\n");
}

/* v2: reset while ALREADY sitting on the resolved leaf is a pure
 * history wipe + re-record: no exit, no entry, current unchanged. */
static void test_reset_at_resolved_leaf_short_circuits(void) {
    printf("Test: reset(target) already at resolved leaf fires nothing... ");
    reset_test();

    /* Drive into B2 and build a dirty history along the way */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_A2);
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B2);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B2);
    assert(test_states[0].last_active == ST_B);    /* ROOT -> B */
    assert(test_states[1].last_active == ST_A2);   /* A    -> A2 (stale) */
    assert(test_states[2].last_active == ST_B2);   /* B    -> B2 */

    reset_callback_state();

    /* Reset targeting the current leaf itself */
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx, ST_B2);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B2);
    assert(exit_count == 0);    /* short-circuit: nothing fired */
    assert(entry_count == 0);
    assert(run_count == 0);

    /* history WAS wiped and re-recorded along the B2 chain only */
    assert(test_states[0].last_active == ST_B);    /* re-recorded */
    assert(test_states[2].last_active == ST_B2);   /* re-recorded */
    assert(test_states[1].last_active == ST_A1);   /* A wiped back to initial */

    printf("PASSED\n");
}

static void test_reset_to_chosen_state(void) {
    printf("Test: reset(target) stays in the chosen subtree (forest)... ");
    memset(&test_ctx, 0, sizeof(test_ctx));
    reset_callback_state();

    /* Two independent roots; initialize into ROOT2's subtree */
    elib_fsm_hsm_hist_init(&test_ctx, forest_states, FOREST_STATE_COUNT, F_ROOT2, NULL);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == F_C2);

    /* Reset into ROOT2 (the chosen state), NOT the first table entry (ROOT1) */
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx, F_ROOT2);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == F_C2);

    printf("PASSED\n");
}

static void test_reset_to_leaf_updates_ancestors(void) {
    printf("Test: reset(leaf) updates ancestor last_active... ");
    reset_test();
    reset_callback_state();

    /* Move history into the B branch */
    elib_fsm_hsm_hist_goto(&test_ctx, ST_B2);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_B2);
    assert(test_states[0].last_active == ST_B);

    /* Reset into leaf A2: A.last_active must become A2, not A1 */
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx, ST_A2);
    assert(err == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A2);
    assert(test_states[1].last_active == ST_A2);   /* A.last_active = A2 */
    assert(test_states[0].last_active == ST_A);    /* ROOT.last_active = A */

    printf("PASSED\n");
}

static void test_reset_state_not_found(void) {
    printf("Test: reset with non-existent target... ");
    reset_test();
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(&test_ctx, 99);
    assert(err == ELIB_FSM_ERR_STATE_NOT_FOUND);
    /* rejection must not move the machine */
    assert(elib_fsm_hsm_hist_current(&test_ctx) == ST_A1);
    printf("PASSED\n");
}

static void test_reset_null_ctx(void) {
    printf("Test: reset with null ctx... ");
    elib_fsm_err_t err = elib_fsm_hsm_hist_reset(NULL, ST_ROOT);
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
    printf("Test: dispatch calls current leaf handler... ");
    reset_test();
    test_states[ST_A1].handler = on_handler;

    int evt = 7;
    handler_return_value = true;
    bool handled = elib_fsm_hsm_hist_dispatch(&test_ctx, &evt);
    assert(handled == true);
    assert(handler_count == 1);
    assert(last_handler_id == 7);

    test_states[ST_A1].handler = NULL;   /* cleanup */
    printf("PASSED\n");
}

static void test_dispatch_bubbles_up(void) {
    printf("Test: dispatch with no handlers on the path returns false... ");
    reset_test();
    int evt = 1;
    bool handled = elib_fsm_hsm_hist_dispatch(&test_ctx, &evt);
    assert(handled == false);
    printf("PASSED\n");
}

static void test_dispatch_bubble_to_leaf_handler(void) {
    printf("Test: dispatch reaches the only handler on the path... ");
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_handler_state();
    handler_return_value = true;

    assert(elib_fsm_hsm_hist_init(&ctx, bubbling_states,
                                   sizeof(bubbling_states) /
                                   sizeof(bubbling_states[0]),
                                   ST_ROOT, NULL) == ELIB_FSM_OK);
    /* descend into B: goto B2 */
    assert(elib_fsm_hsm_hist_goto(&ctx, ST_B2) == ELIB_FSM_OK);
    reset_callback_state();

    int evt = 5;
    assert(elib_fsm_hsm_hist_dispatch(&ctx, &evt) == true);
    assert(handler_count == 1);
    assert(last_handler_id == 5);

    printf("PASSED\n");
}

/* v2 regression 1: a self goto inside a handler changes NOTHING the
 * v1 detector could see -- the resolved leaf stays A1 -- yet bubbling
 * must stop and the parent handler must NOT run. */
static void test_dispatch_goto_same_leaf_stops_bubble(void) {
    printf("Test: handler goto(self) stops bubbling on same leaf... ");
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_handler_state();
    handler_ctx = &ctx;

    assert(elib_fsm_hsm_hist_init(&ctx, selfleaf_states,
                                   sizeof(selfleaf_states) /
                                   sizeof(selfleaf_states[0]),
                                   ST_ROOT, NULL) == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&ctx) == ST_A1);
    reset_callback_state();

    int evt = 1;
    bool handled = elib_fsm_hsm_hist_dispatch(&ctx, &evt);
    assert(handled == true);                     /* transition counts as handled */
    assert(elib_fsm_hsm_hist_current(&ctx) == ST_A1); /* leaf unchanged */
    assert(handler_count == 0);                  /* only the leaf handler ran */
    assert(a_handler_count == 0);                /* parent NOT called */

    /* the external self transition itself fired exit+entry of A1 */
    assert(exit_count == 1);
    assert(last_exit_state == ST_A1);
    assert(entry_count == 1);
    assert(last_entry_state == ST_A1);

    handler_ctx = NULL;
    printf("PASSED\n");
}

/* v2 regression 2: goto(ancestor) whose HISTORY chain restores the
 * exact same leaf -> current unchanged, bubbling must still stop. */
static void test_dispatch_goto_ancestor_same_leaf_stops_bubble(void) {
    printf("Test: handler goto(ancestor) landing on same leaf stops bubbling... ");
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_handler_state();
    handler_ctx = &ctx;

    assert(elib_fsm_hsm_hist_init(&ctx, ancestorleaf_states,
                                   sizeof(ancestorleaf_states) /
                                   sizeof(ancestorleaf_states[0]),
                                   ST_ROOT, NULL) == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&ctx) == ST_A1);
    reset_callback_state();

    int evt = 1;
    bool handled = elib_fsm_hsm_hist_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(elib_fsm_hsm_hist_current(&ctx) == ST_A1); /* history restored A1 */
    assert(handler_count == 0);
    assert(a_handler_count == 0);               /* v1 would have bubbled here */

    /* external transition across A: exit A1 then A, enter A then A1 */
    assert(exit_count == 2);
    assert(last_exit_state == ST_A);
    assert(entry_count == 2);
    assert(last_entry_state == ST_A1);

    handler_ctx = NULL;
    printf("PASSED\n");
}

/* goto inside a handler crossing to another branch also stops bubbling
 * (this one changes the leaf; it was dead code in v1) */
static void test_dispatch_goto_cross_branch(void) {
    printf("Test: handler goto(B2) stops bubbling with a real transition... ");
    elib_fsm_hsm_hist_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    reset_callback_state();
    reset_handler_state();
    cross_goto_ctx = &ctx;

    assert(elib_fsm_hsm_hist_init(&ctx, crossgoto_states,
                                   sizeof(crossgoto_states) /
                                   sizeof(crossgoto_states[0]),
                                   ST_ROOT, NULL) == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&ctx) == ST_A1);
    /* move to B1: its handler carries the goto */
    assert(elib_fsm_hsm_hist_goto(&ctx, ST_B1) == ELIB_FSM_OK);
    assert(elib_fsm_hsm_hist_current(&ctx) == ST_B1);
    reset_callback_state();

    int evt = 2;
    bool handled = elib_fsm_hsm_hist_dispatch(&ctx, &evt);
    assert(handled == true);
    assert(elib_fsm_hsm_hist_current(&ctx) == ST_B2);
    /* sibling transition under same parent B */
    assert(exit_count == 1);
    assert(last_exit_state == ST_B1);
    assert(entry_count == 1);
    assert(last_entry_state == ST_B2);

    cross_goto_ctx = NULL;
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

    printf("--- goto ---\n");
    test_goto_self_transition();
    test_goto_to_ancestor();
    test_goto_updates_last_active();
    test_goto_restores_history();
    test_goto_sibling();
    test_goto_null_ctx();
    test_goto_state_not_found();
    test_goto_deep_history();
    test_goto_after_multiple_transitions();

    printf("--- reset ---\n");
    test_reset_restores_last_active();
    test_reset_transitions_to_initial();
    test_reset_at_resolved_leaf_short_circuits();
    test_reset_to_chosen_state();
    test_reset_to_leaf_updates_ancestors();
    test_reset_state_not_found();
    test_reset_null_ctx();

    printf("--- poll ---\n");
    test_poll_calls_run();
    test_poll_null_ctx();

    printf("--- dispatch ---\n");
    test_dispatch_calls_handler();
    test_dispatch_bubbles_up();
    test_dispatch_bubble_to_leaf_handler();
    test_dispatch_goto_same_leaf_stops_bubble();
    test_dispatch_goto_ancestor_same_leaf_stops_bubble();
    test_dispatch_goto_cross_branch();
    test_dispatch_null_ctx();
    test_dispatch_null_event();

    test_current_returns_leaf();
    test_current_null_ctx();

    printf("\n=== All tests passed ===\n");
    return 0;
}
