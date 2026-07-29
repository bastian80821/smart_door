/**
 * passage.c
 * -----------------------------------------------------------------------------
 * Door-passage state machine using the team's existing SYS_* and DIR_* values.
 *
 * Entry:
 *   LDR_1 covered
 *   -> NFC authorised
 *   -> open entry direction
 *   -> LDR_2 covered
 *   -> LDR_2 clear
 *   -> close
 *
 * Exit:
 *   LDR_2 covered
 *   -> NFC authorised
 *   -> open exit direction
 *   -> LDR_1 covered
 *   -> LDR_1 clear
 *   -> close
 * -----------------------------------------------------------------------------
 */

#include "passage.h"

#include "auth.h"
#include "feedback.h"
#include "lcd_feedback.h"
#include "ldr.h"
#include "mgmt.h"
#include "motor.h"
#include "nfc.h"
#include "system_state.h"
#include <stddef.h>

#define NFC_SCAN_TIMEOUT_MS            10000u
#define PASSAGE_TRIGGER_TIMEOUT_MS     15000u
#define PASSAGE_CLEAR_TIMEOUT_MS       15000u
#define MAX_FAILED_ATTEMPTS                5u

static uint32_t s_state_started_ms = 0;

static bool s_expected_sensor_seen = false;
static bool s_reset_closing = false;

/** Return the sensor opposite the passage-request sensor. */
static ldr_id_t expected_passage_sensor(void)
{
    return sys_get_direction() == DIR_ENTRY
         ? LDR_2
         : LDR_1;
}

/** Return the controller to its normal resting state. */
static void enter_idle(void)
{
    nfc_set_locked(false);
    nfc_cancel_request();

    sys_set_state(SYS_IDLE);
    sys_set_direction(DIR_NONE);
    sys_set_alarm_reason(ALARM_REASON_NONE);

    s_state_started_ms = hal_millis();
    s_expected_sensor_seen = false;
    s_reset_closing = false;

    ldr_clear_events();

    feedback_set_mode(FEEDBACK_IDLE);
    lcd_feedback_show_idle();
}

/** Start the card-scan timeout after the first sensor is covered. */
static void enter_authenticating(sys_direction_t direction)
{
    sys_set_direction(direction);
    sys_set_state(SYS_AUTHENTICATING);

    s_state_started_ms = hal_millis();
    s_expected_sensor_seen = false;

    nfc_set_locked(false);
    nfc_start_request();

    feedback_set_mode(FEEDBACK_WAITING_FOR_NFC);
    lcd_feedback_show_waiting(direction);
}

/** Open the door after a valid card. */
static void enter_granted(sys_direction_t direction)
{
    bool started;

    /*
     * A successful access resets consecutive failed attempts.
     */
    sys_reset_failed_attempts();

    nfc_cancel_request();
    nfc_set_locked(true);

    ldr_clear_events();

    feedback_set_mode(
        direction == DIR_ENTRY
        ? FEEDBACK_GRANTED_ENTRY
        : FEEDBACK_GRANTED_EXIT
    );

    lcd_feedback_show_granted(direction);

    started = motor_open(direction);

    if (!started) {
        sys_set_state(SYS_ERROR);
        feedback_set_mode(FEEDBACK_SYSTEM_ERROR);
        lcd_feedback_show_system_error();
        return;
    }

    sys_set_state(SYS_OPENING);
    s_state_started_ms = hal_millis();
}

/** Run the finite denied feedback pattern. */
static void enter_access_denied(void)
{
    nfc_cancel_request();
    nfc_set_locked(true);

    sys_set_direction(DIR_NONE);
    sys_set_state(SYS_ACCESS_DENIED);

    s_state_started_ms = hal_millis();

    feedback_set_mode(FEEDBACK_ACCESS_DENIED);
    lcd_feedback_show_denied();
}

/** Lock the system after five consecutive denied cards. */
static void enter_unauthorized_alarm(void)
{
    nfc_cancel_request();
    nfc_set_locked(true);

    /*
     * The fifth denial happens before the door opens, so the motor should
     * already be at its original closed position.
     */
    motor_stop();

    sys_set_direction(DIR_NONE);
    sys_set_alarm_reason(ALARM_REASON_UNAUTHORIZED_ACCESS);
    sys_set_state(SYS_ALARM);

    feedback_set_mode(FEEDBACK_UNAUTHORIZED_ALARM);
    lcd_feedback_show_unauthorized();
}

/** Enter the locked system-error state. */
static void enter_system_error(void)
{
    /*
     * Do not replace the higher-priority unauthorized-access alarm.
     */
    if (sys_get_state() == SYS_ALARM) {
        return;
    }

    nfc_cancel_request();
    nfc_set_locked(true);

    sys_set_state(SYS_ERROR);

    s_reset_closing = false;

    feedback_set_mode(FEEDBACK_SYSTEM_ERROR);
    lcd_feedback_show_system_error();
}

void passage_init(void)
{
    bool ldr_ready;

    motor_init();
    feedback_init();
    lcd_feedback_init();
    nfc_init();

    ldr_ready = ldr_init(NULL);

    sys_set_state(SYS_IDLE);
    sys_set_direction(DIR_NONE);
    sys_set_alarm_reason(ALARM_REASON_NONE);
    sys_reset_failed_attempts();

    /*
     * Enter the normal system-error state when required hardware
     * cannot be initialised.
     */
    if (!ldr_ready || !nfc_is_ready()) {
        enter_system_error();
        return;
    }

    enter_idle();
}

void passage_task(void)
{
    uint32_t now = hal_millis();
    sys_state_t state = sys_get_state();

    /*
     * '#' is captured by mgmt while an alarm or system-error LCD message is
     * active.
     */
    if (mgmt_take_reset_request()) {
        (void)passage_manual_reset();
    }

    /*
     * Hardware and reader errors lock the normal passage system.
     */
    if (state != SYS_ALARM &&
        state != SYS_ERROR &&
        (ldr_has_error() ||
         motor_has_error() ||
         !nfc_is_ready())) {

        enter_system_error();
        return;
    }

    switch (sys_get_state()) {
    /* --------------------------------------------------------------------- */
    case SYS_IDLE: {
        sys_direction_t direction;

        /*
         * Do not begin a normal passage sequence while the existing admin
         * window is active.
         */
        if (auth_is_admin()) {
            break;
        }

        direction = ldr_take_request_direction();

        /*
         * If both sensors are currently blocked, direction is unsafe or
         * ambiguous. Wait until the doorway clears.
         */
        if (direction != DIR_NONE &&
            !ldr_any_covered()) {
            break;
        }

        if (direction != DIR_NONE) {
            enter_authenticating(direction);
        }

        break;
    }

    /* --------------------------------------------------------------------- */
    case SYS_AUTHENTICATING: {
        nfc_result_t result;

        if ((now - s_state_started_ms) >=
            NFC_SCAN_TIMEOUT_MS) {

            /*
             * No card was presented in time.
             */
            enter_idle();
            break;
        }

        result = nfc_poll_result();

        if (result == NFC_RESULT_AUTHORIZED) {
            enter_granted(sys_get_direction());
        }
        else if (result == NFC_RESULT_DENIED) {
            uint8_t failed =
                sys_increment_failed_attempts();

            if (failed >= MAX_FAILED_ATTEMPTS) {
                enter_unauthorized_alarm();
            } else {
                enter_access_denied();
            }
        }
        else if (result == NFC_RESULT_ERROR) {
            enter_system_error();
        }

        break;
    }

    /* --------------------------------------------------------------------- */
    case SYS_OPENING:

        if (motor_get_state() == DOOR_OPEN) {
            sys_set_state(SYS_OPEN);

            s_state_started_ms = now;
            s_expected_sensor_seen = false;

            /*
             * Ignore sensor transitions that happened before the door reached
             * its open position.
             */
            ldr_clear_events();
        }

        break;

    /* --------------------------------------------------------------------- */
    case SYS_OPEN: {
        ldr_id_t expected =
            expected_passage_sensor();

        if (!s_expected_sensor_seen) {
            /*
             * Wait for the opposite sensor.
             */
            if (ldr_became_covered(expected) ||
                ldr_is_covered(expected)) {

                s_expected_sensor_seen = true;
                s_state_started_ms = now;
            }
            else if ((now - s_state_started_ms) >=
                     PASSAGE_TRIGGER_TIMEOUT_MS) {

                /*
                 * Keep the door open and lock into SYS_ERROR.
                 */
                enter_system_error();
            }
        }
        else {
            /*
             * The opposite sensor was covered. Wait until it and the initiating
             * sensor are both clear.
             */
            if (!ldr_is_covered(expected) &&
                ldr_all_clear()) {

                if (!motor_close()) {
                    enter_system_error();
                } else {
                    sys_set_state(SYS_CLOSING);
                    s_state_started_ms = now;
                }
            }
            else if ((now - s_state_started_ms) >=
                     PASSAGE_CLEAR_TIMEOUT_MS) {

                /*
                 * A person or object remained in the doorway for too long.
                 * Keep the door open.
                 */
                enter_system_error();
            }
        }

        break;
    }

    /* --------------------------------------------------------------------- */
    case SYS_CLOSING:

        /*
         * Never continue closing while either sensor is covered.
         */
        if (ldr_any_covered()) {
            (void)motor_reopen(sys_get_direction());
            enter_system_error();
            break;
        }

        if (motor_get_state() == DOOR_CLOSED) {
            enter_idle();
        }

        break;

    /* --------------------------------------------------------------------- */
    case SYS_ACCESS_DENIED:

        if (feedback_is_complete()) {
            enter_idle();
        }

        break;

    /* --------------------------------------------------------------------- */
    case SYS_ALARM:

        /*
         * All normal LDR and NFC access remains disabled.
         *
         * Only passage_manual_reset() may leave this state.
         */
        break;

    /* --------------------------------------------------------------------- */
    case SYS_ERROR:

        /*
         * If an obstruction appears during a manual-reset closing movement,
         * reopen and remain in SYS_ERROR.
         */
        if (motor_get_state() == DOOR_CLOSING &&
            ldr_any_covered()) {

            (void)motor_reopen(sys_get_direction());
            s_reset_closing = false;
        }

        if (s_reset_closing &&
            motor_get_state() == DOOR_CLOSED) {

            enter_idle();
        }

        break;

    /*
     * Existing team states are left available for their original modules.
     */
    case SYS_WAIT_CLEAR:
    case SYS_CONFIG:
    case SYS_SCHEDULED_UNLOCK:
    default:
        break;
    }
}

bool passage_manual_reset(void)
{
    sys_state_t state = sys_get_state();

    if (state != SYS_ALARM &&
        state != SYS_ERROR) {
        return false;
    }

    /*
     * Do not reset or close while someone is blocking the doorway.
     */
    if (!ldr_all_clear()) {
        return false;
    }

    if (state == SYS_ALARM) {
        if (motor_get_state() != DOOR_CLOSED) {
            return false;
        }

        sys_reset_failed_attempts();
        sys_set_alarm_reason(ALARM_REASON_NONE);

        enter_idle();
        return true;
    }

    /*
     * Retry PN532 initialisation if the error originated during startup.
     */
    if (!nfc_is_ready() &&
        !nfc_reinitialize()) {
        return false;
    }

    /*
     * A continuing LDR ADC failure will immediately re-enter SYS_ERROR.
     */
    if (ldr_has_error()) {
        return false;
    }

    motor_clear_error();

    if (motor_get_state() == DOOR_CLOSED) {
        enter_idle();
        return true;
    }

    /*
     * The door may have been intentionally left open after a passage timeout.
     */
    if (!motor_is_moving()) {
        if (!motor_force_close()) {
            return false;
        }

        s_reset_closing = true;
        return true;
    }

    return false;
}
