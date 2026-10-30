[简体中文](README.md) | **English**

# elib-state-machine

An embedded state machine framework with four independent modes and a single public header.

An elibs family member: zero dynamic memory, no global variables, C99, single umbrella-header public API, unified error codes, user-held ctx, and illegal configurations rejected at init.

## Features

- **Switch/Case state machine** (`elib_fsm`): a lightweight state tracker supporting immediate and delayed transitions
- **Callback state machine** (`elib_fsm_cb`): entry/exit/run/event callback scheduling, on-demand event dispatch, delayed transitions, and event hand-off during the critical window
- **Hierarchical state machine** (`elib_fsm_hsm`): parent/child states, event bubbling, and external transition (strict LCA domain) semantics
- **Hierarchical state machine with history** (`elib_fsm_hsm_hist`): deep history, `goto` automatically restores the last active substate, and `reset` returns to the initial state in one call
- **Full structure validation at init**: the state table is validated once in `init` — cycles, excessive depth, duplicate values, the reserved value `-1`, dangling references and non-immediate initial states are all rejected; an illegal configuration never reaches runtime
- **All traversal iterative with a hard bound**: even a history table corrupted at runtime cannot make the library spin forever (an anomaly returns an error immediately, it never hangs)
- Zero dynamic memory allocation, user-allocated context, no build-system dependency

## Building

No build system (family convention). Just compile the source file for the mode you use together with the public header; the four modes do not depend on each other:

```bash
# Switch/Case state machine
gcc -std=c99 -c elib_fsm_core.c -I include

# Callback state machine
gcc -std=c99 -c elib_fsm_cb_core.c -I include

# Hierarchical state machine
gcc -std=c99 -c elib_fsm_hsm_core.c -I include

# Hierarchical state machine with history
gcc -std=c99 -c elib_fsm_hsm_hist_core.c -I include
```

**Custom maximum hierarchy depth**: override it with a compile-time macro (**do not modify the library files**). The library code and the caller of the hsm and hist modes must use **the same** definition; a mismatch is undefined behaviour:

```bash
gcc -std=c99 -DELIB_FSM_HSM_MAX_DEPTH=32 -c elib_fsm_hsm_core.c -I include
gcc -std=c99 -DELIB_FSM_HSM_MAX_DEPTH=32 -c elib_fsm_hsm_hist_core.c -I include
```

When undefined, the default `16` is used.

## Quick Start

### Switch/Case State Machine

Define the states and drive them from the main loop with `elib_fsm_poll()`:

```c
#include "elib_fsm.h"

enum { STATE_IDLE, STATE_ACTIVE, STATE_ERROR };

elib_fsm_ctx_t ctx;
elib_fsm_init(&ctx, STATE_IDLE);

while (1) {
    switch (elib_fsm_poll(&ctx, 10)) {  /* 10ms tick */
        case STATE_IDLE:
            if (button_pressed)
                elib_fsm_goto(&ctx, STATE_ACTIVE, 50);  /* 50ms debounce */
            break;
        case STATE_ACTIVE:
            handle_active();
            break;
        default:
            /* waiting out a delay (poll returned -1) */
            break;
    }
}
```

### Callback State Machine

Define state descriptors with callbacks (a const table may live in flash):

```c
#include "elib_fsm.h"

static void on_entry(elib_fsm_state_t state, void *user_data) { /* ... */ }
static void on_exit(elib_fsm_state_t state, void *user_data) { /* ... */ }
static void on_run(void *user_data) { /* ... */ }
static void on_event(void *event_data, void *user_data) { /* ... */ }

static const elib_fsm_cb_state_desc_t states[] = {
    { STATE_IDLE,   on_entry, on_exit, on_run, on_event },
    { STATE_ACTIVE, on_entry, on_exit, on_run, on_event },
};

elib_fsm_cb_ctx_t ctx;
/* the last argument is the critical-state event callback; NULL drops critical-window events */
elib_fsm_cb_init(&ctx, states, 2, STATE_IDLE, NULL, NULL);

elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 0);   /* exit(IDLE) -> entry(ACTIVE) */
elib_fsm_cb_poll(&ctx, 10, &msg);          /* calls event(msg) first, then run */

/* deliver an event on demand (does not trigger run; callable several times per tick) */
elib_fsm_cb_dispatch(&ctx, &key_event);
```

### Hierarchical State Machine

Define descriptors with hierarchy, and events bubble along the active path:

```c
#include "elib_fsm.h"

enum { ST_OFF, ST_ON };
enum { EVT_CMD_POWER, EVT_CMD_TIMEOUT };

static bool off_handler(void *event_data, void *user_data) {
    elib_fsm_hsm_ctx_t *fsm = (elib_fsm_hsm_ctx_t *)user_data;
    if (*(int *)event_data == EVT_CMD_POWER) {
        elib_fsm_hsm_goto(fsm, ST_ON);
        return true;
    }
    return false;
}

static bool on_handler(void *event_data, void *user_data) {
    elib_fsm_hsm_ctx_t *fsm = (elib_fsm_hsm_ctx_t *)user_data;
    if (*(int *)event_data == EVT_CMD_TIMEOUT) {
        elib_fsm_hsm_goto(fsm, ST_OFF);
        return true;
    }
    return false;
}

static const elib_fsm_hsm_state_desc_t states[] = {
    /*  state    parent                 initial                entry  exit  run   handler      */
    {   ST_OFF,  ELIB_FSM_STATE_INVALID, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, off_handler },
    {   ST_ON,   ELIB_FSM_STATE_INVALID, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, on_handler  },
};

elib_fsm_hsm_ctx_t fsm;
elib_fsm_hsm_init(&fsm, states, 2, ST_OFF, &fsm);

int evt = EVT_CMD_POWER;  elib_fsm_hsm_dispatch(&fsm, &evt);  /* OFF -> ON */
evt = EVT_CMD_TIMEOUT;    elib_fsm_hsm_dispatch(&fsm, &evt);  /* ON  -> OFF */
```

### Hierarchical State Machine with History

The same hierarchy as the standard HSM, but `goto` automatically restores the substate active when it was last left (deep history):

```c
#include "elib_fsm.h"

enum { ST_ROOT, ST_A, ST_B, ST_A1, ST_A2, ST_B1, ST_B2 };

/* note: no const -- the framework writes last_active at runtime, so this table must live in RAM */
static elib_fsm_hsm_hist_state_desc_t states[] = {
    /*  state      parent    initial  last_active  entry     exit      run    handler  */
    {   ST_ROOT,   INVALID,  ST_A,    INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_A,      ST_ROOT,  ST_A1,   INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_B,      ST_ROOT,  ST_B1,   INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_A1,     ST_A,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_A2,     ST_A,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_B1,     ST_B,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_B2,     ST_B,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
};

elib_fsm_hsm_hist_ctx_t fsm;
elib_fsm_hsm_hist_init(&fsm, states, 7, ST_ROOT, NULL);

/* ROOT -> A -> A1 (initial chain) */
elib_fsm_hsm_hist_goto(&fsm, ST_A2);     /* A.last_active = A2 */
elib_fsm_hsm_hist_goto(&fsm, ST_B);      /* B has no history, follow initial -> B1 */
elib_fsm_hsm_hist_goto(&fsm, ST_A);      /* restores to A2! (A.last_active = A2) */
elib_fsm_hsm_hist_reset(&fsm, ST_ROOT);  /* clears all history, back to the clean initial leaf A1 */
```

## Init Structure Validation Matrix

The three table-driven modes (`fsm_cb` / `hsm` / `hsm_hist`) validate the whole state table in `init`. **On failure the ctx is left untouched and no callback is called**; the error code is returned directly:

| Violation | Error code |
|------|--------|
| Duplicate state value (lookup becomes ambiguous) | `ELIB_FSM_ERR_INVALID_PARAM` |
| State value == `ELIB_FSM_STATE_INVALID` (-1 is reserved and would corrupt the "no state" signal) | `ELIB_FSM_ERR_INVALID_PARAM` |
| `parent` / `initial` pointing at a state outside the table (dangling reference) | `ELIB_FSM_ERR_STATE_NOT_FOUND` |
| The parent chain forms a cycle, or the chain depth exceeds `ELIB_FSM_HSM_MAX_DEPTH` | `ELIB_FSM_ERR_INVALID_PARAM` |
| A leaf state has `initial` set; a composite state's `initial` is not one of its immediate children | `ELIB_FSM_ERR_INVALID_PARAM` |
| `initial` (the seed state) is not in the table | `ELIB_FSM_ERR_STATE_NOT_FOUND` |

What the constraints mean:

- **Depth invariant**: the parent chain of a state may take at most `ELIB_FSM_HSM_MAX_DEPTH - 1` parent hops to reach the top level. A cycle that never terminates and a chain that exceeds the bound are **both rejected** here — the v1 era of "cycles are UB and excessive depth is silently truncated" is over
- **initial must be an immediate child**: together with the depth bound, this makes initial chains inherently acyclic and guarantees they reach a leaf
- Validation is two iterative passes at O(n·depth), with zero dynamic memory and no recursion
- `elib_fsm` (switch/case mode) has no state table and nothing to validate: the user owns the state values, but stay away from `-1` (the reserved value) all the same

Runtime protection (defence in depth): the `hsm_hist` table is not const, so the user can corrupt it at any moment. Every path traversal (find/descend/exit/enter/domain) enforces the depth bound and the immediate-child check; an illegal chain returns an error and is refused, never an infinite loop.

## Core Semantics

### Transition Domain (Strict LCA External Transition)

`goto` in `hsm` / `hsm_hist` is an **external transition**, and the transition domain = the nearest common **strict** ancestor of the source leaf and the target state:

- Jump to a sibling: only the currently active subtree path is exited/entered
- Jump to an ancestor or to itself: the environment **above** that level is preserved and the domain is the ancestor's parent — that is, the ancestor itself is exited and then entered again (strict semantics)
- Jump to a top-level state / the top level of another tree in the forest: there is no common ancestor, so everything exits and then re-enters
- When the target is a composite state, descend to the leaf along its `initial` chain (hsm); `hsm_hist` instead consults the `last_active` chain first to restore the historical leaf

`hsm_hist` also writes the chain back after a transition: once `goto` succeeds, `last_active` is written back along the whole parent chain of the new leaf (up to the top level), so that any later descent from an outer composite state also reaches this new branch.

### Delayed Transition Timing (fsm / fsm_cb)

`goto(target, delay_ms > 0)`:

1. **exit happens immediately**: the call exits the source state (`fsm_cb` fires the `exit` callback right away; `current` becomes "no state")
2. **entry happens at expiry**: on the `poll()` tick whose accumulated period covers `delay_ms`, `entry(target)` runs first, then the new state handles this tick's event and `run`

While suspended (the critical window):

- `current()` / `poll()` return `ELIB_FSM_STATE_INVALID`; `previous()` still returns the source state
- Events go to the `transition_event` callback registered at init (fsm_cb) and are dropped when none is registered; the `remaining_ms` argument is the value **before** this tick's period is deducted
- **`poll(period_ms = 0)` is a legal call**: no time elapses and the delay countdown does not advance at all (no number of calls triggers it prematurely), yet critical-state events **are still delivered** — using a `period=0` poll as a pure event pump is a documented contract
- Calling `goto` again while suspended replaces the pending target (no duplicate exit); `goto(target, 0)` lands immediately (the source has already exited, so **no second exit is fired**)
- Cancelling a pending delayed transition (whoever the original target was): `goto(previous(), 0)` returns straight to the source state — the library offers no cancel API, and this idiom is the cancellation

### Convergence of goto Inside dispatch (hsm / hsm_hist)

Calling `goto` from a handler is a first-class use: once a transition happens, **bubbling stops immediately** (`dispatch` returns `true`). The decision is based on the transition counter `transition_seq` in the ctx (incremented on every successful goto) and **does not depend on whether current changed** — so even when goto resolves back to the **same leaf** (a self-transition, or a jump to an ancestor followed by a history restore back to the original leaf), bubbling still converges correctly. This is a v2 behaviour fix over v1: in v1, a goto to the same leaf inside a handler made the engine keep bubbling to ancestors that had already exited.

How the handler's return value relates to goto: if a transition happened it stops (returns `true`); only when no transition happened does the handler's `bool` return value matter.

### Entry Reentrancy Inside init

In the three callback modes, `init` sets `initialized` before firing the entry of the initial state (for hsm/hist this is the whole initial path, top-down) — **calling goto from an entry callback is legal** and takes effect immediately.

## API Reference

### Error Codes (Shared by All Four Modes)

| Error code | Meaning |
|--------|------|
| `ELIB_FSM_OK` | Success |
| `ELIB_FSM_ERR_INVALID_PARAM` | Illegal argument (NULL ctx, empty table, structure validation failure) |
| `ELIB_FSM_ERR_NOT_INITIALIZED` | API called while the ctx is uninitialized (or already deinit'ed) |
| `ELIB_FSM_ERR_STATE_NOT_FOUND` | A state value outside the table was referenced (init seed, goto target, dangling parent/initial) |

### Switch/Case State Machine

| Function | Description |
|------|------|
| `elib_fsm_init(ctx, initial)` | Initialize and set the initial state (this mode has no table and no validation; `initial` is a user-defined value) |
| `elib_fsm_deinit(ctx)` | Deinitialize |
| `elib_fsm_goto(ctx, target, delay_ms)` | Transition to a state; `delay=0` is immediate, `delay>0` delayed; a further goto while suspended replaces the target |
| `elib_fsm_poll(ctx, period_ms)` | Advance one tick and return the state; returns -1 while waiting out a delay; `period_ms=0` is legal (no time elapses, the delay freezes) |
| `elib_fsm_current(ctx)` | Current state; returns -1 while waiting out a delay |
| `elib_fsm_previous(ctx)` | The last valid state (during a delay it returns the delayed source state, not -1) |

### Callback State Machine

| Function | Description |
|------|------|
| `elib_fsm_cb_init(ctx, states, state_count, initial, user_data, transition_event)` | Initialize (with the full validation matrix, see above); the entry of the initial state is fired inside init |
| `elib_fsm_cb_deinit(ctx)` | Deinitialize |
| `elib_fsm_cb_goto(ctx, target, delay_ms)` | `delay=0` immediate (exit→entry); `delay>0` delayed (exit now, entry at expiry); a target outside the table returns `STATE_NOT_FOUND` and fires no callback |
| `elib_fsm_cb_poll(ctx, period_ms, event_data)` | One tick: a normal tick runs event then run; an expiry tick runs entry(target) and then lets the new state handle event/run; a critical tick hands the event to `transition_event` and skips event/run; `event_data=NULL` means "no event" — all event callbacks are skipped and run proceeds as usual |
| `elib_fsm_cb_dispatch(ctx, event_data)` | Deliver an event on demand (does not trigger run; callable several times per tick); during the critical window it is likewise handed to `transition_event`; `event_data=NULL` returns OK directly |
| `elib_fsm_cb_current(ctx)` / `elib_fsm_cb_previous(ctx)` | Same semantics as switch/case |

**Critical-state event callback (`transition_event`)**:

| Argument | Description |
|------|------|
| `event_data` | Event data pointer |
| `from` | The source state that has already been exited |
| `to` | The target state to be entered |
| `remaining_ms` | Milliseconds left until expiry (before this tick's period is deducted) |
| `user_data` | User data |

### Hierarchical State Machine

State descriptor (`elib_fsm_hsm_state_desc_t`):

| Field | Description |
|------|------|
| `state` | State value (unique, ≠ -1) |
| `parent` | Parent state, INVALID = top level (must exist in the table, the chain bound applies, see the validation matrix) |
| `initial` | Default substate; a leaf must use INVALID and a composite must point at an **immediate** child |
| `entry` / `exit` | Enter/exit callbacks (nullable); `run` in hsm is meaningful only for **leaves**: poll calls the run of the current leaf only and a composite's run is ignored (consistent with family precedent, documented rather than rejected) |
| `handler` | Event handling callback (nullable), `bool (*)(void *event_data, void *user_data)`; returning `true` means handled (stop bubbling) |

| Function | Description |
|------|------|
| `elib_fsm_hsm_init(ctx, states, state_count, initial, user_data)` | Initialize (full validation matrix); descend to the leaf along the initial chain, firing every entry on the path top-down inside init |
| `elib_fsm_hsm_deinit(ctx)` | Deinitialize |
| `elib_fsm_hsm_goto(ctx, target)` | External transition (strict LCA domain, see Core Semantics); a target outside the table returns `STATE_NOT_FOUND` and the machine stays put |
| `elib_fsm_hsm_poll(ctx)` | One tick: calls the current leaf's run and returns the leaf state |
| `elib_fsm_hsm_dispatch(ctx, event_data)` | The event bubbles from the leaf to the root along the active path; a goto inside a handler stops it (including a goto to the same leaf, see Core Semantics); `event_data=NULL` returns false |
| `elib_fsm_hsm_current(ctx)` | Current leaf state |

### Hierarchical State Machine with History

The descriptor has one extra field, `last_active` — **deep history managed by the framework at runtime** (written by the framework, read-only for the user). The whole table must therefore be **non-const and in RAM**.

| Function | Description |
|------|------|
| `elib_fsm_hsm_hist_init(ctx, states, state_count, initial, user_data)` | Initialize (same full validation matrix as hsm); every `last_active` is seeded with its own `initial` and the chain descends to a leaf |
| `elib_fsm_hsm_hist_deinit(ctx)` | Deinitialize |
| `elib_fsm_hsm_hist_goto(ctx, target)` | External transition (same domain semantics as hsm); leaf resolution is **history first** (the `last_active` chain), falling back to the initial chain when there is no history; once resolved, the whole chain is written back up to the top level |
| `elib_fsm_hsm_hist_reset(ctx, target)` | First clears **all** history (back to `initial`), then enters the resolved leaf of `target` through a clean chain and writes the whole chain back — including ancestors outside the transition domain that take no part in exit/entry; **when the resolved leaf is already the current leaf no exit/entry is fired** (a pure history reset); a `target` outside the table returns `STATE_NOT_FOUND` |
| `elib_fsm_hsm_hist_poll(ctx)` / `elib_fsm_hsm_hist_dispatch(ctx, event_data)` / `elib_fsm_hsm_hist_current(ctx)` | Same as hsm |

**Choosing hsm vs hsm_hist**:

```
Scenario: ROOT → A → A2 → goto(B) → goto(A)

standard hsm:      goto(A) → follow initial → back to A1 (fixed initial state)
hsm with history:  goto(A) → restore history → back to A2 (last position)
```

- `elib_fsm_hsm`: every entry into a composite starts from the fixed initial state (protocols, simple UIs); the 7-field const table may live in flash
- `elib_fsm_hsm_hist`: when the leave position must be remembered (complex UI pages, multiple contexts); the 8-field non-const table must be in RAM

## Usage Constraints

- **The reserved value `-1`**: `ELIB_FSM_STATE_INVALID` is reserved **by convention**. Table-driven modes have init reject any state value that hits `-1`; `elib_fsm` has no table and relies on user discipline — hitting `-1` corrupts the "no state" signal, so do not do it
- **State table lifetime**: the hsm table may be const in flash (read-only traversal after init); the hsm_hist table must be in RAM and the user **must not rewrite** `last_active` by hand (corrupting it enters the defensive path, where `goto`/`dispatch` return an error instead of hanging)
- **re-entrancy**: calling this ctx's API from inside a callback is by design (see "Entry Reentrancy Inside init" and "Convergence of goto Inside dispatch"); concurrent access to the same ctx from multiple tasks is not supported (no locks, single-threaded context use)
- **Context size**: the ctx is self-contained with no external memory; the hsm path buffer lives on the stack, bounded by `ELIB_FSM_HSM_MAX_DEPTH`

## Semantic Changes from v1 to v2

Note the following when upgrading from v1 (the ctx memory layout is incompatible; this is a breaking refactor):

1. **Public headers unified**: the nine headers (the whole `elib_fsm*.h` family) are merged into the single umbrella header `include/elib_fsm.h`, and include paths move from relative paths to plain `-Iinclude`
2. **Error codes renamed**: `DELAYED_TRANSITION` (dead code with no return point anywhere in the library) is removed; the three states `INVALID_PARAM` / `NOT_INITIALIZED` / `STATE_NOT_FOUND` now have the clear division of labour shown in the table above (in v1, "structure violation" and "out-of-table reference" were mixed into `INVALID_PARAM`)
3. **init validation**: v1 validated the state table not at all (cycles were UB, excessive depth was silently truncated); v2 validates everything, rejects it all and leaves the ctx unpolluted
4. **Overly deep tables**: v1 silently truncated entry (leaking unpaired callbacks); v2 rejects them at init
5. **Same-leaf miss fixed in dispatch**: in v1 a goto back to the same leaf inside a handler bubbled to already-exited ancestors; v2 closes it with the transition counter (see Core Semantics)
6. **poll(0) contract**: in v1, `period_ms=0` was merely an implementation detail that "happened not to advance"; v2 promotes it to a documented contract — no time elapses, the delay freezes, and critical events are still delivered
7. **Cancelling a delayed transition**: no cancel API is added; use the `goto(previous(), 0)` idiom (as in v1; v2 documents it and adds tests)
8. **Build system**: `CMakeLists.txt` and the `.github` CI are removed (family convention: no build system, see the compile commands above)
9. **What hsm/hist do not offer** (unchanged from v1, explicit non-goals): delayed goto, previous, cancel; the UML internal transition/entry-point/guard/choice pseudostates; event queues; orthogonal regions

## Testing

Build and run the four assertion test suites on the host (each has the ported v1 cases plus v2 additions: the validation matrix, the same-leaf bubbling regression, the poll(0) contract, the reset short-circuit, and so on):

```bash
gcc -std=c99 -Wall -Wextra -o test_fsm test/test_elib_fsm.c src/elib_fsm_core.c -I include && ./test_fsm

gcc -std=c99 -Wall -Wextra -o test_fsm_cb test/test_elib_fsm_cb.c src/elib_fsm_cb_core.c -I include && ./test_fsm_cb

gcc -std=c99 -Wall -Wextra -o test_hsm test/test_elib_fsm_hsm.c src/elib_fsm_hsm_core.c -I include && ./test_hsm

gcc -std=c99 -Wall -Wextra -o test_hsm_hist test/test_elib_fsm_hsm_hist.c src/elib_fsm_hsm_hist_core.c -I include && ./test_hsm_hist
```

All tests compile clean with arm-none-eabi-gcc (`-std=c99 -Wall -Wextra -Werror`), and every assertion's expected value was derived by hand.
