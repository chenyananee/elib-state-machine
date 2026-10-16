/* elib_fsm_cb_types.h - Callback State Machine Type Definitions */
#ifndef ELIB_FSM_CB_TYPES_H
#define ELIB_FSM_CB_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include "elib_fsm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callback function types */
typedef void (*elib_fsm_cb_entry_fn)(elib_fsm_state_t state, void *user_data);
typedef void (*elib_fsm_cb_exit_fn)(elib_fsm_state_t state, void *user_data);
typedef void (*elib_fsm_cb_run_fn)(void *user_data);
typedef void (*elib_fsm_cb_event_fn)(void *event_data, void *user_data);

/* Transition (critical-state) event callback.
 *
 * Called when an event is delivered while a delayed transition is pending
 * (the machine has already exited the source state and has no active state).
 *   from         - source state that was exited when the delay started
 *   to           - pending target state (delayed_target)
 *   remaining_ms - milliseconds left before the transition fires, taken before
 *                  the current tick's period is applied
 * Nullable: when NULL, such events are dropped. */
typedef void (*elib_fsm_cb_transition_event_fn)(void *event_data,
                                                 elib_fsm_state_t from,
                                                 elib_fsm_state_t to,
                                                 uint32_t remaining_ms,
                                                 void *user_data);

/* State descriptor - callbacks per state */
typedef struct {
    elib_fsm_state_t state;           /* State value */
    elib_fsm_cb_entry_fn entry;       /* Entry callback (nullable) */
    elib_fsm_cb_exit_fn exit;         /* Exit callback (nullable) */
    elib_fsm_cb_run_fn run;           /* Run/loop callback (nullable) */
    elib_fsm_cb_event_fn event;       /* Poll event callback (nullable) */
} elib_fsm_cb_state_desc_t;

/* Callback state machine context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;                             /* Current state */
    elib_fsm_state_t previous;                            /* Last valid state before transition */
    elib_fsm_state_t delayed_target;                      /* Delayed target, ELIB_FSM_STATE_INVALID = none */
    uint32_t delayed_remaining;                           /* Remaining ms for delayed transition */
    const elib_fsm_cb_state_desc_t *states;               /* State descriptor table */
    size_t state_count;                                   /* Number of state descriptors */
    void *user_data;                                      /* User data passed to all callbacks */
    elib_fsm_cb_transition_event_fn transition_event;     /* Critical-state event callback (nullable) */
    struct {
        uint8_t initialized : 1;
    } bit_flags;
} elib_fsm_cb_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_CB_TYPES_H */
