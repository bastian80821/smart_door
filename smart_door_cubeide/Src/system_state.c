/**
 * system_state.c
 * -----------------------------------------------------------------------------
 * Backing storage + accessors for the shared system state (see system_state.h).
 * This is NOT a stub in the throwaway sense: this is the real, final home of the
 * state variables. Feature modules never touch these directly -- only through
 * the accessors below -- so state changes can later be logged or guarded here in
 * one place.
 * -----------------------------------------------------------------------------
 */
#include "system_state.h"

static sys_state_t     s_state     = SYS_IDLE;
static sys_direction_t s_direction = DIR_NONE;
static bool            s_tailgate  = false;
static bool            s_sched     = false;

sys_state_t sys_get_state(void)          { return s_state; }
void        sys_set_state(sys_state_t s) { s_state = s; }

sys_direction_t sys_get_direction(void)              { return s_direction; }
void            sys_set_direction(sys_direction_t d) { s_direction = d; }

bool sys_get_tailgate_flag(void)  { return s_tailgate; }
void sys_set_tailgate_flag(bool v){ s_tailgate = v; }

bool sys_get_scheduled_unlock(void)  { return s_sched; }
void sys_set_scheduled_unlock(bool v){ s_sched = v; }
