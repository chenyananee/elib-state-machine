# HSM (Hierarchical State Machine) Design

## Overview

Add a Hierarchical State Machine (HSM) variant to elib-state-machine, fully decoupled from the existing `elib_fsm` and `elib_fsm_cb` implementations. The HSM supports state hierarchy with event bubbling, following the project's established patterns: zero dynamic allocation, user-provided resources, and consistent API style.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Parent state role | Executable state | Classic HSM semantics: entry/run/exit fire for parent states |
| Event bubbling | Callback return value driven | `true` = handled (stop), `false` = unhandled (bubble up) |
| Delayed transitions | Not supported | Keeps HSM core lean; users implement timers in callbacks |
| Hierarchy definition | `parent` field in state descriptor | Simple, consistent with existing flat descriptor style |
| Initial substate | `initial` field in state descriptor | Standard HSM: entering a composite state descends to `initial` |
| poll vs dispatch | Separate operations | `poll` drives tick (leaf `run`), `dispatch` drives events (bubbling) |
| poll run scope | Leaf state only | Parent `run` invoked only via event bubbling through `dispatch` |
| Callback model | Separate `run` + `handler` | Clear separation of tick vs event; consistent with `elib_fsm_cb` callback signatures |

## Data Structures

### State Descriptor

```c
typedef bool (*elib_fsm_hsm_handler_fn)(elib_fsm_event_t event, void *user_data);

typedef struct {
    elib_fsm_state_t state;                      /* State value */
    elib_fsm_state_t parent;                     /* Parent state, ELIB_FSM_STATE_INVALID = top-level */
    elib_fsm_state_t initial;                    /* Default substate, ELIB_FSM_STATE_INVALID = leaf */
    elib_fsm_cb_entry_fn entry;                  /* Entry callback (nullable) */
    elib_fsm_cb_exit_fn exit;                    /* Exit callback (nullable) */
    elib_fsm_cb_run_fn run;                      /* Poll tick callback (nullable) */
    elib_fsm_hsm_handler_fn handler;             /* Event handler callback (nullable) */
} elib_fsm_hsm_state_desc_t;
```

- `parent == ELIB_FSM_STATE_INVALID` indicates a top-level state
- `initial == ELIB_FSM_STATE_INVALID` indicates a leaf state (no substates)
- Callback types `elib_fsm_cb_entry_fn`, `elib_fsm_cb_exit_fn`, `elib_fsm_cb_run_fn` are reused from `elib_fsm_cb_types.h`
- `handler` returns `bool`: `true` = event handled (stop propagation), `false` = unhandled (bubble up)

### Context

```c
typedef struct {
    elib_fsm_state_t current;                    /* Current leaf state */
    const elib_fsm_hsm_state_desc_t *states;     /* State descriptor table */
    size_t state_count;                          /* Number of state descriptors */
    void *user_data;                             /* User data passed to all callbacks */
    int initialized;                             /* Initialization flag */
} elib_fsm_hsm_ctx_t;
```

- `current` always points to the deepest active (leaf) state
- The full active path can be reconstructed by walking `parent` from `current` upward
- No delayed transition fields (`delayed_target`, `delayed_remaining`)

## API

```c
/* Initialize HSM */
elib_fsm_err_t elib_fsm_hsm_init(elib_fsm_hsm_ctx_t *ctx,
                                  const elib_fsm_hsm_state_desc_t *states,
                                  size_t state_count,
                                  elib_fsm_state_t initial,
                                  void *user_data);

/* Deinitialize HSM */
void elib_fsm_hsm_deinit(elib_fsm_hsm_ctx_t *ctx);

/* Transition to target state (LCA semantics, no delay) */
elib_fsm_err_t elib_fsm_hsm_goto(elib_fsm_hsm_ctx_t *ctx,
                                  elib_fsm_state_t target);

/* Advance one tick: call leaf state's run callback, return current leaf state */
elib_fsm_state_t elib_fsm_hsm_poll(elib_fsm_hsm_ctx_t *ctx);

/* Dispatch event: bubble up active path, return true if handled */
bool elib_fsm_hsm_dispatch(elib_fsm_hsm_ctx_t *ctx,
                            elib_fsm_event_t event);

/* Get current leaf state */
elib_fsm_state_t elib_fsm_hsm_current(const elib_fsm_hsm_ctx_t *ctx);
```

### init

- Validates parameters (ctx, states, state_count non-null/non-zero)
- Validates `initial` state exists in descriptor table
- If `initial` is composite (has `initial` field), validates each step in the `initial` chain exists and follows it to a leaf state
- Sets `current` to the final leaf state
- Calls `entry` for each state on the path from top-level ancestor down to leaf
- Returns `ELIB_FSM_ERR_STATE_NOT_FOUND` if any state in the initial chain is missing from the descriptor table
- Returns `ELIB_FSM_OK` on success

### deinit

- Sets `initialized = 0`

### goto

- Validates ctx and initialization
- Validates target state exists in descriptor table
- Computes LCA of source (current leaf) and target
- Exits from current leaf up to LCA (exclusive), calling `exit` for each
- Enters from LCA down to target (exclusive), calling `entry` for each
- If target is composite (has `initial`), descends `initial` chain to leaf, calling `entry` for each
- Updates `current` to the final leaf state
- Returns `ELIB_FSM_OK` on success

### poll

- Validates ctx and initialization
- Calls `run` callback of current leaf state (if non-null)
- Returns current leaf state

### dispatch

- Validates ctx and initialization
- Starting from current leaf state, calls `handler` callback
- If `handler` returns `true` or is `NULL`: bubble stops, return `true`/`false` respectively
- If `handler` returns `false`: move to parent state, repeat
- If top-level reached without handler returning `true`: return `false`
- If `goto` is called inside a handler, bubbling stops immediately and the transition is executed before `dispatch` returns. Subsequent handlers in the old active path are not called.

### current

- Validates ctx and initialization
- Returns current leaf state, or `ELIB_FSM_STATE_INVALID` on error

## Transition Semantics (LCA Algorithm)

### LCA Computation

1. Collect ancestor set of source state (walk `parent` from source up to root)
2. Walk `parent` from target upward; first ancestor found in source's ancestor set is the LCA

### Transition Execution

1. **Exit phase**: From current leaf, call `exit` upward until reaching (but not including) the LCA
2. **Enter phase**: From LCA, call `entry` downward until reaching (but not including) the target
3. **Initial descent**: If target is composite (has `initial`), follow `initial` chain calling `entry` for each state until reaching a leaf

### Edge Cases

- **Self-transition** (`goto` to current state): External self-transition — exit then re-enter (standard HSM behavior)
- **Transition to ancestor**: Exit up to just below the ancestor; ancestor is not re-entered
- **Transition to descendant**: Enter path down; current state is not exited (it becomes part of the active path)

### Example

```
       ROOT
      /    \
     A      B
    / \    / \
   S1  S2  S3  S4
```

`goto(S1 → S3)`, LCA = ROOT:
- Exit: `exit(S1)`, `exit(A)`
- Enter: `entry(B)`, `entry(S3)`

`goto(S1 → S2)`, LCA = A:
- Exit: `exit(S1)`
- Enter: `entry(S2)`

`goto(ROOT → S3)` (entering composite from outside):
- Enter: `entry(ROOT)`, `entry(B)`, `entry(S3)`

## Event Bubbling

`dispatch(ctx, event)` execution:

1. Start at current leaf state
2. Call `handler(event, user_data)`
3. If returns `true` → stop, `dispatch` returns `true`
4. If returns `false` → move to parent, go to step 2
5. If `handler` is `NULL` → treat as unhandled, go to step 4
6. If top-level reached without `true` → `dispatch` returns `false`

**Constraints**:
- No entry/exit callbacks fire during bubbling
- If `goto` is called inside a handler, bubbling stops immediately

## Error Codes

Add to `elib_fsm_err.h`:

```c
ELIB_FSM_ERR_STATE_NOT_FOUND,    /* Target state not found in descriptor table */
```

## File Structure

| File | Purpose |
|------|---------|
| `include/elib_fsm_hsm_types.h` | HSM type definitions |
| `include/elib_fsm_hsm.h` | HSM public API |
| `src/elib_fsm_hsm_core.h` | HSM internal header |
| `src/elib_fsm_hsm_core.c` | HSM core implementation |
| `test/test_elib_fsm_hsm.c` | HSM unit tests |

### Dependencies

- `elib_fsm_hsm_types.h` depends on `elib_fsm_types.h` (reuses `elib_fsm_state_t`, `elib_fsm_event_t`) and `elib_fsm_cb_types.h` (reuses `elib_fsm_cb_entry_fn`, `elib_fsm_cb_exit_fn`, `elib_fsm_cb_run_fn`)
- `elib_fsm_hsm.h` depends on `elib_fsm_err.h` + `elib_fsm_hsm_types.h`
- No dependency on `elib_fsm_core.c` or `elib_fsm_cb_core.c` (fully decoupled)

## Usage Example

```c
#include "elib_fsm_hsm.h"

enum {
    ST_ROOT, ST_STOPPED, ST_RUNNING,
    ST_PAUSED, ST_PLAYING,
    EVT_START, EVT_STOP, EVT_PAUSE, EVT_RESUME, EVT_TICK
};

static bool stopped_handler(elib_fsm_event_t evt, void *ud) {
    if (evt == EVT_START) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_RUNNING); return true; }
    return false;
}

static bool running_handler(elib_fsm_event_t evt, void *ud) {
    if (evt == EVT_STOP) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_STOPPED); return true; }
    return false;
}

static bool playing_handler(elib_fsm_event_t evt, void *ud) {
    if (evt == EVT_PAUSE) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_PAUSED); return true; }
    return false;
}

static bool paused_handler(elib_fsm_event_t evt, void *ud) {
    if (evt == EVT_RESUME) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_PLAYING); return true; }
    return false;
}

static const elib_fsm_hsm_state_desc_t states[] = {
    { ST_ROOT,    ELIB_FSM_STATE_INVALID, ST_STOPPED, NULL, NULL, NULL, NULL       },
    { ST_STOPPED, ST_ROOT,   ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, stopped_handler },
    { ST_RUNNING, ST_ROOT,   ST_PLAYING,  NULL, NULL, NULL, running_handler },
    { ST_PAUSED,  ST_RUNNING, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, paused_handler },
    { ST_PLAYING, ST_RUNNING, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, playing_handler },
};

elib_fsm_hsm_ctx_t fsm;
elib_fsm_hsm_init(&fsm, states, 5, ST_ROOT, NULL);

/* Entry path: ROOT -> STOPPED (via initial) */
elib_fsm_hsm_dispatch(&fsm, EVT_START);  /* STOPPED handler: goto RUNNING */
/* Transition: exit(STOPPED), enter(RUNNING), enter(PLAYING via initial) */
elib_fsm_hsm_dispatch(&fsm, EVT_PAUSE);  /* PLAYING handler: goto PAUSED */
/* Transition: exit(PLAYING), enter(PAUSED) */
elib_fsm_hsm_dispatch(&fsm, EVT_STOP);   /* PAUSED handler: unhandled -> bubbles to RUNNING */
/* RUNNING handler handles STOP: goto STOPPED */
/* Transition: exit(PAUSED), exit(RUNNING), enter(STOPPED) */
```
