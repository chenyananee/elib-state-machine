/* elib_fsm_types.h - State Machine Type Definitions */
#ifndef ELIB_FSM_TYPES_H
#define ELIB_FSM_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include "elib_fsm_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* State and event types - user defines enum values, framework treats as int */
typedef int elib_fsm_state_t;
typedef int elib_fsm_event_t;

/* Special values */
#define ELIB_FSM_STATE_INVALID  (-1)

/* Switch/case context (user-allocated) */
typedef struct {
    elib_fsm_state_t current;           /* Current state */
    elib_fsm_state_t initial;           /* Initial state */
    elib_fsm_state_t delayed_target;    /* Delayed target, ELIB_FSM_STATE_INVALID = none */
    uint32_t delayed_remaining;         /* Remaining ms for delayed transition */
    int initialized;                    /* Initialization flag */
} elib_fsm_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_TYPES_H */
