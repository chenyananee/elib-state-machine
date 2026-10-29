/* elib_fsm.h - Embedded state machines, single umbrella header.
 *
 * Four independent flavours, compile only what you use:
 *   elib_fsm        switch/case state tracker (delayed goto, no callbacks)
 *   elib_fsm_cb     callback table (entry/exit/run/event, delayed goto,
 *                   critical-state transition_event)
 *   elib_fsm_hsm    hierarchical machine (parent/initial table, event
 *                   bubbling, strict-LCA external transitions)
 *   elib_fsm_hsm_hist   hsm + deep history (last_active chains)
 *
 * Rules of the game:
 *  - zero dynamic memory: every context is user allocated and self
 *    contained; no globals anywhere in the library
 *  - the state table is validated ONCE at init: cycles, over-deep
 *    hierarchies, duplicate state values, references into the void and
 *    reserved values are ALL rejected there -- the library never
 *    carries an illegal configuration into runtime
 *  - none of the traversals recurse; every loop is depth-bounded, so
 *    even a runtime-corrupted table (history variant allows user
 *    writes) cannot hang the library
 *  - state values are user-defined ints; ELIB_FSM_STATE_INVALID (-1)
 *    is reserved by convention and rejected at init wherever a table
 *    exists to check (fsm has no table: user owns that responsibility)
 *
 * Version: 2.0.0
 */
#ifndef ELIB_FSM_H
#define ELIB_FSM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ELIB_FSM_VERSION "2.0.0"

/* ---- maximum hierarchy depth (hsm / hsm_hist only) ----
 *
 * Bounds every internal path buffer on the C stack and doubles as the
 * init-time hierarchy depth limit: a table deeper than this is
 * rejected with ELIB_FSM_ERR_INVALID_PARAM (v1 silently truncated --
 * never again). Override with a compiler flag (-D) if you must NOT
 * edit this file; library sources and user code must share the same
 * definition. */
#ifndef ELIB_FSM_HSM_MAX_DEPTH
#define ELIB_FSM_HSM_MAX_DEPTH 16
#endif

/* ---- state and event types ----
 *
 * User defines enum values; the framework treats them as int.
 * ELIB_FSM_STATE_INVALID (-1) is RESERVED and rejected by init in all
 * table-driven flavours; it is also what the "no state" observers
 * return (current() during a pending delayed transition, poll() to
 * signal "still waiting", and every API on an uninitialized ctx). */
typedef int elib_fsm_state_t;
typedef int elib_fsm_event_t;               /* user-defined event ids */
#define ELIB_FSM_STATE_INVALID (-1)

/* ---- error codes (shared by all four flavours) ---- */

typedef enum {
    ELIB_FSM_OK = 0,
    ELIB_FSM_ERR_INVALID_PARAM,   /* bad args, or the state table failed
                                       structural validation at init */
    ELIB_FSM_ERR_NOT_INITIALIZED, /* API called before a valid init */
    ELIB_FSM_ERR_STATE_NOT_FOUND  /* a referenced state value is not in
                                       the table (init seed, goto target) */
} elib_fsm_err_t;

/* ================================================================
 * Shared callback types (fsm_cb / hsm / hsm_hist)
 * ================================================================ */

typedef void (*elib_fsm_cb_entry_fn)(elib_fsm_state_t state, void *user_data);
typedef void (*elib_fsm_cb_exit_fn)(elib_fsm_state_t state, void *user_data);
typedef void (*elib_fsm_cb_run_fn)(void *user_data);
typedef void (*elib_fsm_cb_event_fn)(void *event_data, void *user_data);

/* Critical-state event callback (fsm_cb only).
 *
 * Called when an event is delivered (via poll or dispatch) while a
 * delayed transition is pending: the machine has already exited the
 * source state and has no active state.
 *   from         - source state that was exited when the delay started
 *   to           - pending target state (delayed_target)
 *   remaining_ms - milliseconds left before the transition fires, taken
 *                  BEFORE the current tick's period is applied
 * Nullable: when NULL, such events are dropped. */
typedef void (*elib_fsm_cb_transition_event_fn)(void *event_data,
                                                elib_fsm_state_t from,
                                                elib_fsm_state_t to,
                                                uint32_t remaining_ms,
                                                void *user_data);

/* hsm / hsm_hist event handler: return true if handled (stop
 * bubbling), false to bubble up to the parent. */
typedef bool (*elib_fsm_hsm_handler_fn)(void *event_data, void *user_data);

/* ================================================================
 * Flavour 1: elib_fsm -- switch/case state machine
 * ================================================================ */

/* Context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;           /* Current state */
    elib_fsm_state_t previous;          /* Last valid state before transition */
    elib_fsm_state_t initial;           /* Initial state */
    elib_fsm_state_t delayed_target;    /* Delayed target, ELIB_FSM_STATE_INVALID = none */
    uint32_t delayed_remaining;         /* Remaining ms for delayed transition */
    struct {
        uint8_t initialized : 1;
    } bit_flags;
} elib_fsm_ctx_t;

/**
 * @brief Initialize switch/case state machine
 * @param ctx User-allocated context pointer
 * @param initial Initial state value
 * @return elib_fsm_err_t error code
 *
 * Note: this flavour has no state table, so `initial` (and every goto
 * target) is taken verbatim. Keep it away from -1: that value belongs
 * to ELIB_FSM_STATE_INVALID.
 */
elib_fsm_err_t elib_fsm_init(elib_fsm_ctx_t *ctx, elib_fsm_state_t initial);

/**
 * @brief Deinitialize state machine
 * @param ctx Context pointer
 */
void elib_fsm_deinit(elib_fsm_ctx_t *ctx);

/**
 * @brief Jump to target state, immediate or delayed
 * @param ctx Context pointer
 * @param target Target state value
 * @param delay_ms Delay in milliseconds, 0 = immediate
 * @return elib_fsm_err_t error code
 *
 * A delayed goto exits "now" (current turns invalid) and fires the
 * arrival when poll()'s accumulated period covers delay_ms. Issuing a
 * new goto while one is pending replaces target and resets the delay.
 * To abort a pending transition without knowing the target, goto the
 * remembered previous() state with delay 0.
 */
elib_fsm_err_t elib_fsm_goto(elib_fsm_ctx_t *ctx,
                              elib_fsm_state_t target,
                              uint32_t delay_ms);

/**
 * @brief Advance one tick and get the (possibly still pending) state
 * @param ctx Context pointer
 * @param period_ms Duration of this tick, 0 = no time passes (delayed
 *                  transitions never advance, but the call is legal)
 * @return Current state; ELIB_FSM_STATE_INVALID while a delayed
 *         transition is pending (or uninitialized ctx)
 */
elib_fsm_state_t elib_fsm_poll(elib_fsm_ctx_t *ctx, uint32_t period_ms);

/**
 * @brief Get current state; INVALID during a pending delayed transition
 */
elib_fsm_state_t elib_fsm_current(const elib_fsm_ctx_t *ctx);

/**
 * @brief Get last valid state (never INVALID during a delay)
 */
elib_fsm_state_t elib_fsm_previous(const elib_fsm_ctx_t *ctx);

/* ================================================================
 * Flavour 2: elib_fsm_cb -- callback state machine
 * ================================================================ */

/* State descriptor - callbacks per state (const-friendly, put in Flash) */
typedef struct {
    elib_fsm_state_t state;           /* State value */
    elib_fsm_cb_entry_fn entry;       /* Entry callback (nullable) */
    elib_fsm_cb_exit_fn exit;         /* Exit callback (nullable) */
    elib_fsm_cb_run_fn run;           /* Run/loop callback (nullable) */
    elib_fsm_cb_event_fn event;       /* Poll event callback (nullable) */
} elib_fsm_cb_state_desc_t;

/* Context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;                         /* Current state */
    elib_fsm_state_t previous;                        /* Last valid state before transition */
    elib_fsm_state_t delayed_target;                  /* ELIB_FSM_STATE_INVALID = none */
    uint32_t delayed_remaining;                       /* Remaining ms for delayed transition */
    const elib_fsm_cb_state_desc_t *states;           /* State descriptor table */
    size_t state_count;                               /* Number of state descriptors */
    void *user_data;                                  /* User data passed to all callbacks */
    elib_fsm_cb_transition_event_fn transition_event; /* Critical-state callback (nullable) */
    struct {
        uint8_t initialized : 1;
    } bit_flags;
} elib_fsm_cb_ctx_t;

/**
 * @brief Initialize callback state machine
 * @param ctx User-allocated context pointer
 * @param states State descriptor table
 * @param state_count Number of state descriptors
 * @param initial Initial state value (must be in the table)
 * @param user_data User data passed to all callbacks (nullable)
 * @param transition_event Critical-state event callback (nullable)
 * @return ELIB_FSM_OK on success; ELIB_FSM_ERR_INVALID_PARAM if
 *         ctx/states are NULL, state_count is 0, a state value is
 *         duplicated or equals ELIB_FSM_STATE_INVALID;
 *         ELIB_FSM_ERR_STATE_NOT_FOUND if initial is not in the table.
 *         On rejection ctx is left untouched (all-zero).
 *
 * The initial state's entry callback fires INSIDE init, after the
 * initialized flag is set: calling goto from within an entry callback
 * is legal and takes effect immediately.
 */
elib_fsm_err_t elib_fsm_cb_init(elib_fsm_cb_ctx_t *ctx,
                                 const elib_fsm_cb_state_desc_t *states,
                                 size_t state_count,
                                 elib_fsm_state_t initial,
                                 void *user_data,
                                 elib_fsm_cb_transition_event_fn transition_event);

/**
 * @brief Deinitialize callback state machine
 * @param ctx Context pointer
 */
void elib_fsm_cb_deinit(elib_fsm_cb_ctx_t *ctx);

/**
 * @brief Jump to target state, immediate or delayed
 * @param ctx Context pointer
 * @param target Target state value (must be in the table)
 * @param delay_ms Delay in milliseconds, 0 = immediate
 * @return elib_fsm_err_t error code
 *
 * Immediate: exit(current) -> entry(target). Delayed: exit(current)
 * fires immediately, entry(target) fires when the delay expires; a
 * delayed target replaces a pending one without a second exit. An
 * immediate goto during a pending delay cancels the delay and enters
 * the target directly (the source was already exited, no new exit
 * fires).
 */
elib_fsm_err_t elib_fsm_cb_goto(elib_fsm_cb_ctx_t *ctx,
                                 elib_fsm_state_t target,
                                 uint32_t delay_ms);

/**
 * @brief Advance one tick: deliver event, call run, return state
 * @param ctx Context pointer
 * @param period_ms Duration of this tick, 0 = no time passes (delayed
 *                  transitions never advance, but critical-state
 *                  events are still delivered)
 * @param event_data This tick's event, NULL = "no event" (event
 *                   callbacks skipped, run still fires)
 * @return Current state; ELIB_FSM_STATE_INVALID while a delayed
 *         transition is pending (or uninitialized ctx)
 *
 * Normal tick: event(current) then run(current). Expiring tick:
 * entry(target) first, then event/run of the NEW state.
 */
elib_fsm_state_t elib_fsm_cb_poll(elib_fsm_cb_ctx_t *ctx, uint32_t period_ms,
                                   void *event_data);

/**
 * @brief Deliver an event on demand (no run callback, repeatable)
 * @param ctx Context pointer
 * @param event_data Event pointer, NULL = dropped (returns OK)
 * @return elib_fsm_err_t error code
 *
 * Normal state: event(current). Pending delayed transition:
 * transition_event(from, to, remaining) instead, dropped when that
 * callback is NULL.
 */
elib_fsm_err_t elib_fsm_cb_dispatch(elib_fsm_cb_ctx_t *ctx, void *event_data);

/**
 * @brief Get current state; INVALID during a pending delay
 */
elib_fsm_state_t elib_fsm_cb_current(const elib_fsm_cb_ctx_t *ctx);

/**
 * @brief Get last valid state (never INVALID during a delay)
 */
elib_fsm_state_t elib_fsm_cb_previous(const elib_fsm_cb_ctx_t *ctx);

/* ================================================================
 * Flavour 3: elib_fsm_hsm -- hierarchical state machine
 * ================================================================ */

/* State descriptor - callbacks and hierarchy per state (const-friendly).
 *
 * Struct rules, ALL validated at init (violations are rejected, never
 * survived):
 *  - state values unique, none equal to ELIB_FSM_STATE_INVALID
 *  - parent: ELIB_FSM_STATE_INVALID = top-level; otherwise must exist
 *    in the table
 *  - initial: leaf states must have INVALID; composites must point at
 *    a DIRECT child of their own
 *  - every parent chain reaches the top within
 *    ELIB_FSM_HSM_MAX_DEPTH links (this rejects cycles and over-deep
 *    tables alike) */
typedef struct {
    elib_fsm_state_t state;                      /* State value */
    elib_fsm_state_t parent;                     /* Parent state, INVALID = top-level */
    elib_fsm_state_t initial;                    /* Default substate, INVALID = leaf */
    elib_fsm_cb_entry_fn entry;                  /* Entry callback (nullable) */
    elib_fsm_cb_exit_fn exit;                    /* Exit callback (nullable) */
    elib_fsm_cb_run_fn run;                      /* Poll tick callback (nullable);
                                                      leaf states only: poll calls
                                                      the run of the active LEAF,
                                                      composites are not run */
    elib_fsm_hsm_handler_fn handler;             /* Event handler (nullable) */
} elib_fsm_hsm_state_desc_t;

/* Context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;                    /* Current leaf state */
    uint32_t transition_seq;                     /* Bumped by every goto; lets
                                                      dispatch detect a goto hidden
                                                      inside a handler even when the
                                                      resolved leaf is unchanged */
    const elib_fsm_hsm_state_desc_t *states;     /* State descriptor table */
    size_t state_count;                          /* Number of state descriptors */
    void *user_data;                             /* User data passed to all callbacks */
    struct {
        uint8_t initialized : 1;
    } bit_flags;
} elib_fsm_hsm_ctx_t;

/**
 * @brief Initialize hierarchical state machine
 * @param ctx User-allocated context pointer
 * @param states State descriptor table
 * @param state_count Number of state descriptors
 * @param initial Initial state value (may be composite; descends to a
 *                leaf along the initial chain)
 * @param user_data User data passed to all callbacks (nullable)
 * @return ELIB_FSM_OK; ELIB_FSM_ERR_INVALID_PARAM if ctx/states are
 *         NULL, state_count is 0, or the table fails validation
 *         (duplicate/-1 state values, dangling parent/initial
 *         references, cycles, depth > ELIB_FSM_HSM_MAX_DEPTH, leaf
 *         with an initial, composite with a non-child initial);
 *         ELIB_FSM_ERR_STATE_NOT_FOUND if initial is not in the table.
 *         On rejection ctx is left untouched.
 *
 * Entry callbacks fire for the whole path top-down INSIDE init.
 */
elib_fsm_err_t elib_fsm_hsm_init(elib_fsm_hsm_ctx_t *ctx,
                                  const elib_fsm_hsm_state_desc_t *states,
                                  size_t state_count,
                                  elib_fsm_state_t initial,
                                  void *user_data);

/**
 * @brief Deinitialize hierarchical state machine
 * @param ctx Context pointer
 */
void elib_fsm_hsm_deinit(elib_fsm_hsm_ctx_t *ctx);

/**
 * @brief Transition to target state (external, strict-LCA domain)
 * @param ctx Context pointer
 * @param target Target state value (must be in the table)
 * @return elib_fsm_err_t error code
 *
 * Exit path runs source -> just above the domain (lowest strict common
 * ancestor), then entry runs down to target, descending any initial
 * chain to a leaf. A transition to self, to an ancestor, or to a
 * top-level state is external: the target itself is exited and
 * re-entered. A top-level target has no common ancestor: the whole
 * configuration is torn down and rebuilt from the top.
 */
elib_fsm_err_t elib_fsm_hsm_goto(elib_fsm_hsm_ctx_t *ctx,
                                  elib_fsm_state_t target);

/**
 * @brief Advance one tick: run the active LEAF, return it
 */
elib_fsm_state_t elib_fsm_hsm_poll(elib_fsm_hsm_ctx_t *ctx);

/**
 * @brief Dispatch an event along the active path, bubbling upward
 * @param ctx Context pointer
 * @param event_data Event pointer, NULL = no event (returns false)
 * @return true if a handler claimed it, or if a handler performed a
 *         goto (bubbling stops on transition); false otherwise.
 *
 * A goto inside a handler stops bubbling -- even when that goto
 * resolves back to the very same leaf, the transition counter makes
 * the stop unambiguous.
 */
bool elib_fsm_hsm_dispatch(elib_fsm_hsm_ctx_t *ctx, void *event_data);

/**
 * @brief Get current leaf state
 */
elib_fsm_state_t elib_fsm_hsm_current(const elib_fsm_hsm_ctx_t *ctx);

/* ================================================================
 * Flavour 4: elib_fsm_hsm_hist -- hierarchical machine with history
 * ================================================================ */

/* State descriptor with history tracking.
 *
 * NOTE: this table is NOT const -- the framework writes `last_active`
 * at runtime, so it must live in RAM. Keep the struct rules of
 * elib_fsm_hsm_state_desc_t; the same validation runs at init.
 * `last_active` is framework-managed: init seeds it to `initial`, and
 * every goto/reset refreshes the chain (deep history). */
typedef struct {
    elib_fsm_state_t state;                      /* State value */
    elib_fsm_state_t parent;                     /* Parent state, INVALID = top-level */
    elib_fsm_state_t initial;                    /* Default substate, INVALID = leaf */
    elib_fsm_state_t last_active;                /* Runtime history (framework-managed) */
    elib_fsm_cb_entry_fn entry;                  /* Entry callback (nullable) */
    elib_fsm_cb_exit_fn exit;                    /* Exit callback (nullable) */
    elib_fsm_cb_run_fn run;                      /* Poll tick callback (nullable;
                                                      leaf only, as hsm) */
    elib_fsm_hsm_handler_fn handler;             /* Event handler (nullable) */
} elib_fsm_hsm_hist_state_desc_t;

/* Context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;                    /* Current leaf state */
    uint32_t transition_seq;                     /* As hsm: dispatch-goto detection */
    elib_fsm_hsm_hist_state_desc_t *states;      /* State descriptor table (non-const) */
    size_t state_count;                          /* Number of state descriptors */
    void *user_data;                             /* User data passed to all callbacks */
    struct {
        uint8_t initialized : 1;
    } bit_flags;
} elib_fsm_hsm_hist_ctx_t;

/**
 * @brief Initialize hierarchical state machine with history
 * @param ctx User-allocated context pointer
 * @param states State descriptor table (NON-const, last_active is
 *               framework-managed)
 * @param state_count Number of state descriptors
 * @param initial Initial state value
 * @param user_data User data passed to all callbacks (nullable)
 * @return Error codes exactly as elib_fsm_hsm_init; additionally all
 *         last_active fields are seeded to their states' initial.
 */
elib_fsm_err_t elib_fsm_hsm_hist_init(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_hsm_hist_state_desc_t *states,
                                       size_t state_count,
                                       elib_fsm_state_t initial,
                                       void *user_data);

/**
 * @brief Deinitialize hierarchical state machine with history
 * @param ctx Context pointer
 */
void elib_fsm_hsm_hist_deinit(elib_fsm_hsm_hist_ctx_t *ctx);

/**
 * @brief Transition to target, restoring its history leaf if any
 * @param ctx Context pointer
 * @param target Target state value (must be in the table)
 * @return elib_fsm_err_t error code
 *
 * Domain semantics identical to elib_fsm_hsm_goto. The leaf is
 * resolved last_active-first (deep history), falling back to the
 * initial chain for never-visited composites. The whole chain up to
 * the top is re-recorded, so outer composites also remember the branch
 * that now leads to the new leaf.
 */
elib_fsm_err_t elib_fsm_hsm_hist_goto(elib_fsm_hsm_hist_ctx_t *ctx,
                                       elib_fsm_state_t target);

/**
 * @brief Clear ALL history, then enter target on clean initial chains
 * @param ctx Context pointer
 * @param target Re-entry state (must be in the table); pass the top /
 *               machine initial for the classic "back to square one"
 * @return elib_fsm_err_t error code
 *
 * History is wiped first, the target's clean leaf resolved, and the
 * chain recorded -- INCLUDING ancestors above the transition domain,
 * which are not exited/re-entered but must still point at the new
 * branch. No exit/entry fires when the resolved leaf is already the
 * current one.
 */
elib_fsm_err_t elib_fsm_hsm_hist_reset(elib_fsm_hsm_hist_ctx_t *ctx,
                                        elib_fsm_state_t target);

/**
 * @brief Advance one tick: run the active leaf, return it
 */
elib_fsm_state_t elib_fsm_hsm_hist_poll(elib_fsm_hsm_hist_ctx_t *ctx);

/**
 * @brief Dispatch an event along the active path (as hsm)
 */
bool elib_fsm_hsm_hist_dispatch(elib_fsm_hsm_hist_ctx_t *ctx, void *event_data);

/**
 * @brief Get current leaf state
 */
elib_fsm_state_t elib_fsm_hsm_hist_current(const elib_fsm_hsm_hist_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_H */
