/**
 * motor.h
 * -----------------------------------------------------------------------------
 * High-level non-blocking stepper-door controller.
 *
 * The existing hal_motor_set() and hal_motor_done() interfaces are reused.
 * -----------------------------------------------------------------------------
 */
#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include "system_state.h"

typedef enum {
    DOOR_CLOSED = 0,
    DOOR_OPENING,
    DOOR_OPEN,
    DOOR_CLOSING
} door_state_t;

#define MOTOR_MOVEMENT_TIMEOUT_MS 10000u

void motor_init(void);
void motor_task(void);

bool motor_open(sys_direction_t direction);
bool motor_close(void);

/** Reverse a closing door back towards its required open position. */
bool motor_reopen(sys_direction_t direction);

/** Close from any known position during a manual reset. */
bool motor_force_close(void);

void motor_stop(void);

/** Hold a movement part-way because of an obstruction, and resume it once the
 *  door is clear. All movement timeout is suspended while held. */
void motor_hold(void);
void motor_resume(void);

door_state_t motor_get_state(void);

bool motor_is_moving(void);
bool motor_has_error(void);

/** Clear the software movement-timeout error after manual inspection. */
void motor_clear_error(void);

#endif /* MOTOR_H */
