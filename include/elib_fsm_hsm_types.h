/* elib_fsm_hsm_types.h - Hierarchical State Machine Type Definitions */
#ifndef ELIB_FSM_HSM_TYPES_H
#define ELIB_FSM_HSM_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "elib_fsm_types.h"
#include "elib_fsm_cb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Event handler callback: returns true if handled (stop propagation), false to bubble up */
typedef bool (*elib_fsm_hsm_handler_fn)(elib_fsm_event_t event, void *user_data);

/* State descriptor - callbacks and hierarchy per state */
typedef struct {
    elib_fsm_state_t state;                      /* State value */
    elib_fsm_state_t parent;                     /* Parent state, ELIB_FSM_STATE_INVALID = top-level */
    elib_fsm_state_t initial;                    /* Default substate, ELIB_FSM_STATE_INVALID = leaf */
    elib_fsm_cb_entry_fn entry;                  /* Entry callback (nullable) */
    elib_fsm_cb_exit_fn exit;                    /* Exit callback (nullable) */
    elib_fsm_cb_run_fn run;                      /* Poll tick callback (nullable) */
    elib_fsm_hsm_handler_fn handler;             /* Event handler callback (nullable) */
} elib_fsm_hsm_state_desc_t;

/* HSM context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;                    /* Current leaf state */
    const elib_fsm_hsm_state_desc_t *states;     /* State descriptor table */
    size_t state_count;                          /* Number of state descriptors */
    void *user_data;                             /* User data passed to all callbacks */
    int initialized;                             /* Initialization flag */
} elib_fsm_hsm_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_HSM_TYPES_H */
