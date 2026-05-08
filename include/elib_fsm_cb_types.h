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

/* State descriptor - callbacks per state */
typedef struct {
    elib_fsm_state_t state;           /* State value */
    elib_fsm_cb_entry_fn entry;       /* Entry callback (nullable) */
    elib_fsm_cb_exit_fn exit;         /* Exit callback (nullable) */
    elib_fsm_cb_run_fn run;           /* Run/loop callback (nullable) */
} elib_fsm_cb_state_desc_t;

/* Callback state machine context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;                             /* Current state */
    elib_fsm_state_t delayed_target;                      /* Delayed target, ELIB_FSM_STATE_INVALID = none */
    uint32_t delayed_remaining;                           /* Remaining ms for delayed transition */
    const elib_fsm_cb_state_desc_t *states;               /* State descriptor table */
    size_t state_count;                                   /* Number of state descriptors */
    void *user_data;                                      /* User data passed to all callbacks */
    int initialized;                                      /* Initialization flag */
} elib_fsm_cb_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_CB_TYPES_H */
