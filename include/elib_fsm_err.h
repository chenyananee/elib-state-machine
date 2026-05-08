/* elib_fsm_err.h - State Machine Error Codes */
#ifndef ELIB_FSM_ERR_H
#define ELIB_FSM_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ELIB_FSM_OK = 0,
    ELIB_FSM_ERR_INVALID_PARAM,        /* Invalid parameter */
    ELIB_FSM_ERR_NOT_INITIALIZED,      /* Not initialized */
    ELIB_FSM_ERR_TRANSITION_NOT_FOUND, /* No matching transition (cb variant) */
    ELIB_FSM_DELAYED_TRANSITION,       /* Delayed transition fired this tick */
} elib_fsm_err_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_FSM_ERR_H */
