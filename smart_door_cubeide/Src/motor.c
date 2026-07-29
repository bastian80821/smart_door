/**
 * motor.c
 * -----------------------------------------------------------------------------
 * High-level door movement and timeout handling.
 * -----------------------------------------------------------------------------
 */
#include "motor.h"
#include "hal_wrapper.h"

static door_state_t s_door_state = DOOR_CLOSED;
static sys_direction_t s_open_direction = DIR_NONE;

static uint32_t s_movement_started_ms = 0;
static bool s_error = false;

void motor_init(void)
{
    /*
     * The physical door is assumed to be closed at power-up.
     */
    hal_motor_set(MOTOR_STOP);

    s_door_state = DOOR_CLOSED;
    s_open_direction = DIR_NONE;
    s_movement_started_ms = 0;
    s_error = false;
}

void motor_task(void)
{
    uint32_t now = hal_millis();

    /*
     * Advance the existing HAL-owned stepper sequence.
     */
    hal_motor_task();

    if (s_error) {
        return;
    }

    if ((s_door_state == DOOR_OPENING ||
         s_door_state == DOOR_CLOSING) &&
        (now - s_movement_started_ms) >=
            MOTOR_MOVEMENT_TIMEOUT_MS) {

        hal_motor_set(MOTOR_STOP);
        s_error = true;
        return;
    }

    if (!hal_motor_done()) {
        return;
    }

    if (s_door_state == DOOR_OPENING) {
        s_door_state = DOOR_OPEN;
    }
    else if (s_door_state == DOOR_CLOSING) {
        s_door_state = DOOR_CLOSED;
        s_open_direction = DIR_NONE;
    }
}

bool motor_open(sys_direction_t direction)
{
    if (s_error ||
        s_door_state != DOOR_CLOSED ||
        direction == DIR_NONE) {
        return false;
    }

    s_open_direction = direction;
    s_door_state = DOOR_OPENING;
    s_movement_started_ms = hal_millis();

    hal_motor_set(
        direction == DIR_ENTRY
        ? MOTOR_OPEN_ENTRY
        : MOTOR_OPEN_EXIT
    );

    return true;
}

bool motor_close(void)
{
    if (s_error ||
        s_door_state != DOOR_OPEN) {
        return false;
    }

    s_door_state = DOOR_CLOSING;
    s_movement_started_ms = hal_millis();

    hal_motor_set(MOTOR_CLOSE);

    return true;
}

bool motor_reopen(sys_direction_t direction)
{
    if (direction == DIR_NONE) {
        direction = s_open_direction;
    }

    if (direction == DIR_NONE) {
        return false;
    }

    s_open_direction = direction;
    s_door_state = DOOR_OPENING;
    s_movement_started_ms = hal_millis();

    hal_motor_set(
        direction == DIR_ENTRY
        ? MOTOR_OPEN_ENTRY
        : MOTOR_OPEN_EXIT
    );

    return true;
}

bool motor_force_close(void)
{
    /*
     * Used only after a manual reset and physical inspection.
     */
    s_error = false;
    s_door_state = DOOR_CLOSING;
    s_movement_started_ms = hal_millis();

    hal_motor_set(MOTOR_CLOSE);

    return true;
}

void motor_stop(void)
{
    hal_motor_set(MOTOR_STOP);
}

door_state_t motor_get_state(void)
{
    return s_door_state;
}

bool motor_is_moving(void)
{
    return s_door_state == DOOR_OPENING ||
           s_door_state == DOOR_CLOSING;
}

bool motor_has_error(void)
{
    return s_error;
}

void motor_clear_error(void)
{
    s_error = false;
}