/**
 * passage.c
 * -----------------------------------------------------------------------------
 * Door-passage state machine
 *
 * triggers from idle only, so a crossing in progress never starts a new cycle:
 *   Outside sensor covered, inside clear  -> entry request, requires a card.
 *   Inside sensor covered, outside clear  -> exit request, no card required
 *
 *
 * safe open:
 *   Before the motor moves, the sensor on the side the door swings toward must
 *   be clear
 *
 *   The door stays open for a fixed window and then closes once both sensors
 *   read clear.
 *
 * Tailgate invariant: During any open window exactly one authorised inward movement is expected,
 *   so an unexpected break of the inside sensor means someone entered without
 *   authorisation, which raises the alarm. 5 consecutive rejections of a card does too.
 *
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
#include "clock.h"
#include "config.h"
#include <stddef.h>

#define NFC_SCAN_TIMEOUT_MS      10000u  /* how long we wait for a card         */
#define WAIT_CLEAR_TIMEOUT_MS    15000u  /* destination side blocked for too long */
#define OPEN_WINDOW_MS            5000u  /* five second passage window  */
#define MAX_FAILED_ATTEMPTS           5u

static uint32_t s_state_started_ms = 0;
static uint32_t s_open_since_ms = 0;

static uint8_t  s_inside_breaks = 0;      /* inside-sensor breaks this window   */
static uint8_t  s_inside_allowance = 0;   /* how many are legitimate            */

static bool     s_reset_closing = false;

/* Publish whether a configured unlock window is active right now. The clock
 * must have been set by the manager. Until then schedules stay inert. */
static void update_scheduled_unlock(void)
{
    sys_set_scheduled_unlock(
        clock_is_set() && config_event_active_at(clock_now_min())
    );
}

/* The sensor on the side the door swings toward. Entry opens inward. */
static ldr_id_t destination_sensor(sys_direction_t direction)
{
    return direction == DIR_ENTRY ? LDR_2 : LDR_1;
}

/* ---- State entries -------------------------------------------------------- */

static void enter_idle(void)
{
    nfc_set_locked(false);
    nfc_cancel_request();

    sys_set_state(SYS_IDLE);
    sys_set_direction(DIR_NONE);
    sys_set_alarm_reason(ALARM_REASON_NONE);
    sys_set_tailgate_flag(false);

    s_state_started_ms = hal_millis();
    s_inside_breaks = 0;
    s_reset_closing = false;

    ldr_clear_events();

    feedback_set_mode(FEEDBACK_IDLE);
    lcd_feedback_show_idle();
}

/* Entry only: ask for a card before door moves */
static void enter_authenticating(void)
{
    sys_set_direction(DIR_ENTRY);
    sys_set_state(SYS_AUTHENTICATING);

    s_state_started_ms = hal_millis();

    nfc_set_locked(false);
    nfc_start_request();

    feedback_set_mode(FEEDBACK_WAITING_FOR_NFC);
    lcd_feedback_show_waiting(DIR_ENTRY);
}

/* Hold until the destination side is clear, then move */
static void enter_wait_clear(sys_direction_t direction)
{
    sys_set_direction(direction);
    sys_set_state(SYS_WAIT_CLEAR);

    s_state_started_ms = hal_millis();

    nfc_cancel_request();
    nfc_set_locked(true);

    feedback_set_mode(
        direction == DIR_ENTRY ? FEEDBACK_GRANTED_ENTRY : FEEDBACK_GRANTED_EXIT
    );
    lcd_feedback_show_granted(direction);
}

static void enter_system_error(void)
{
    if (sys_get_state() == SYS_ALARM) {
        return;                       /* never downgrade an intrusion alarm */
    }

    nfc_cancel_request();
    nfc_set_locked(true);

    /* Ordinary cards stay ignored, but the reader keeps listening so the
     * admin key can still override. The motor is idle in this state,
     * so polling doesn't make the system unresponsive */
    auth_set_enabled(true);

    sys_set_state(SYS_ERROR);
    s_reset_closing = false;

    feedback_set_mode(FEEDBACK_SYSTEM_ERROR);
    lcd_feedback_show_system_error();
}

static void enter_opening(void)
{
    if (!motor_open(sys_get_direction())) {
        enter_system_error();
        return;
    }

    sys_reset_failed_attempts();
    ldr_clear_events();

    sys_set_state(SYS_OPENING);
    s_state_started_ms = hal_millis();
}

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

static void enter_alarm(void)
{
    nfc_cancel_request();
    nfc_set_locked(true);

    /* Admin key must be audible while the system is locked down. */
    auth_set_enabled(true);

    sys_set_direction(DIR_NONE);
    sys_set_alarm_reason(ALARM_REASON_UNAUTHORIZED_ACCESS);
    sys_set_state(SYS_ALARM);

    feedback_set_mode(FEEDBACK_UNAUTHORIZED_ALARM);
    lcd_feedback_show_unauthorized();
}

/* ---- Init ----------------------------------------------------------------- */

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

    if (!ldr_ready || !nfc_is_ready()) {
        enter_system_error();
        return;
    }

    enter_idle();
}

/* ---- Main task ------------------------------------------------------------ */

/* Clear an alarm or fault with admin key */
static void admin_override_reset(void)
{
    sys_reset_failed_attempts();
    sys_set_tailgate_flag(false);
    sys_set_alarm_reason(ALARM_REASON_NONE);
    motor_clear_error();

    s_reset_closing = false;

    if (motor_get_state() == DOOR_CLOSED) {
        enter_idle();
        return;
    }

    if (!motor_is_moving()) {
        (void)motor_force_close();
    }

    feedback_set_mode(FEEDBACK_IDLE);        /* alarm stops immediately */
    nfc_set_locked(false);
    nfc_cancel_request();

    sys_set_state(SYS_CLOSING);
    s_state_started_ms = hal_millis();
}

void passage_task(void)
{
    uint32_t now = hal_millis();
    sys_state_t state = sys_get_state();

    update_scheduled_unlock();

    if (mgmt_take_admin_reset_request()) {
        if (state == SYS_ALARM || state == SYS_ERROR) {
            admin_override_reset();
            return;
        }
    }
    else if (mgmt_take_reset_request()) {
        (void)passage_manual_reset();
    }

    /* An override is only meaningful when the door is at rest. Presenting the
     * administrative key while a cycle is already running would otherwise latch
     * a request that fires the moment the door finishes, reopening it for no
     * reason. Discard it instead. The request is kept in the alarm and fault states, where clearing the
     * lockout and then opening is exactly the intended override behaviour. */
    switch (state) {
    case SYS_WAIT_CLEAR:
    case SYS_OPENING:
    case SYS_OPEN:
    case SYS_CLOSING:
    case SYS_SCHEDULED_UNLOCK:
        (void)auth_take_override_request();
        break;
    default:
        break;
    }

    /* Hardware faults lock the passage system, but never mask an alarm. */
    if (state != SYS_ALARM && state != SYS_ERROR &&
        (ldr_has_error() || motor_has_error() || !nfc_is_ready())) {
        enter_system_error();
        return;
    }

    switch (state) {

    /* ---------------------------------------------------------------- IDLE */
    case SYS_IDLE: {
        /* Read both events first: the accessors clear them when read. */
        bool outside_broke = ldr_became_covered(LDR_1);
        bool inside_broke  = ldr_became_covered(LDR_2);

        /* The admin key is an override: it opens the door on its own,
         * without a sensor request, while also showing the admin menu. */
        if (auth_take_override_request()) {
            enter_wait_clear(DIR_ENTRY);
            break;
        }

        /* A configured unlock window holds the door open for its duration. */
        if (sys_get_scheduled_unlock()) {
            enter_wait_clear(DIR_ENTRY);
            break;
        }

        /* Admin mode owns the interface; do not start a passage underneath it. */
        if (auth_is_admin()) {
            break;
        }

        /* Both sides blocked at once is ambiguous -- wait for the doorway to
         * settle rather than guessing a direction. */
        if (outside_broke && inside_broke) {
            break;
        }

        if (outside_broke && !ldr_is_covered(LDR_2)) {
            enter_authenticating();            /* entry: card required */
        }
        else if (inside_broke && !ldr_is_covered(LDR_1)) {
            enter_wait_clear(DIR_EXIT);        /* exit: no card required */
        }
        break;
    }

    /* ------------------------------------------------------ AUTHENTICATING */
    case SYS_AUTHENTICATING: {
        nfc_result_t result;

        if ((now - s_state_started_ms) >= NFC_SCAN_TIMEOUT_MS) {
            enter_idle();                      /* nobody presented a card */
            break;
        }

        result = nfc_poll_result();

        if (result == NFC_RESULT_AUTHORIZED) {
            enter_wait_clear(DIR_ENTRY);
        }
        else if (result == NFC_RESULT_DENIED) {
            if (sys_increment_failed_attempts() >= MAX_FAILED_ATTEMPTS) {
                enter_alarm();
            } else {
                enter_access_denied();
            }
        }
        else if (result == NFC_RESULT_ERROR) {
            enter_system_error();
        }
        break;
    }

    /* --------------------------------------------------------- WAIT_CLEAR */
    case SYS_WAIT_CLEAR:
        /* Do not move until the side we swing toward is clear. */
        if (!ldr_is_covered(destination_sensor(sys_get_direction()))) {
            enter_opening();
        }
        else if ((now - s_state_started_ms) >= WAIT_CLEAR_TIMEOUT_MS) {
            enter_idle();                      /* gave up, door never moved */
        }
        break;

    /* ------------------------------------------------------------ OPENING */
    case SYS_OPENING: {
        /* The safe-open rule applies while the door is travelling, not only
         * before it starts: if somebody steps into the side the door is
         * swinging toward, hold the movement until they are clear rather than
         * pushing the door into them. */
        if (ldr_is_covered(destination_sensor(sys_get_direction()))) {
            motor_hold();
            break;
        }

        motor_resume();                 /* no op unless door was holding */

        if (motor_get_state() == DOOR_OPEN) {
            /* During a scheduled window, door stays open for the whole event
             */
            if (sys_get_scheduled_unlock()) {
                sys_set_state(SYS_SCHEDULED_UNLOCK);
                ldr_clear_events();
                lcd_feedback_show_scheduled();
                break;
            }

            sys_set_state(SYS_OPEN);

            s_open_since_ms = now;
            s_inside_breaks = 0;

            /* One inside break is expected from an authorised entrant; an
             * exiting person is already outside the door's inner sensor. */
            s_inside_allowance =
                (sys_get_direction() == DIR_ENTRY) ? 1u : 0u;

            ldr_clear_events();                /* ignore anything during travel */
            lcd_feedback_show_open(sys_get_direction());
        }
        break;
    }

    /* --------------------------------------------------------------- OPEN */
    case SYS_OPEN: {
        /* Tailgate invariant: count inside-sensor breaks against the
         * allowance for this direction. */
        if (ldr_became_covered(LDR_2)) {
            s_inside_breaks++;

            if (s_inside_breaks > s_inside_allowance) {
                sys_set_tailgate_flag(true);
                enter_alarm();
                break;
            }
        }
        (void)ldr_became_covered(LDR_1);       /* consume; not part of the rule */

        /* Close once the window has elapsed AND the doorway is empty. If it is
         * still occupied, keep waiting and never close on a person. */
        if ((now - s_open_since_ms) >= OPEN_WINDOW_MS && ldr_all_clear()) {
            if (motor_close()) {
                sys_set_state(SYS_CLOSING);
                s_state_started_ms = now;
            } else {
                enter_system_error();
            }
        }
        break;
    }

    /* ------------------------------------------------------------ CLOSING */
    case SYS_CLOSING:
        /* Interruptible: an obstruction re-opens the door */
        if (ldr_any_covered()) {
            if (motor_reopen(sys_get_direction())) {
                sys_set_state(SYS_OPENING);
            } else {
                enter_system_error();
            }
            break;
        }

        if (motor_get_state() == DOOR_CLOSED) {
            enter_idle();
        }
        break;

    /* -------------------------------------------------- SCHEDULED_UNLOCK */
    case SYS_SCHEDULED_UNLOCK:
        /* Held open for the whole event. No card is required, alarms suspended.
         * Close once the window ends and the doorway is empty. */

        if (!sys_get_scheduled_unlock() && ldr_all_clear()) {
            if (motor_close()) {
                sys_set_state(SYS_CLOSING);
                s_state_started_ms = now;
            } else {
                enter_system_error();
            }
        }
        break;

    /* ------------------------------------------------------ ACCESS_DENIED */
    case SYS_ACCESS_DENIED:
        if (feedback_is_complete()) {
            enter_idle();
        }
        break;

    /* -------------------------------------------------------------- ALARM */
    case SYS_ALARM:
        /* A reset requested while the door was still open has to bring it home
         * before the lockout can end, so the closing movement is completed
         * here. Without this the alarm would latch forever, because tailgating
         * is always detected while the door is open. */
        if (s_reset_closing) {
            if (ldr_any_covered()) {
                /* Someone is back in the doorway: reopen and abandon the
                 * reset rather than closing on them. */
                (void)motor_reopen(sys_get_direction());
                s_reset_closing = false;
            }
            else if (motor_get_state() == DOOR_CLOSED) {
                sys_reset_failed_attempts();
                sys_set_tailgate_flag(false);
                sys_set_alarm_reason(ALARM_REASON_NONE);
                s_reset_closing = false;
                enter_idle();
            }
        }
        break;

    /* -------------------------------------------------------------- ERROR */
    case SYS_ERROR:
        if (motor_get_state() == DOOR_CLOSING && ldr_any_covered()) {
            (void)motor_reopen(sys_get_direction());
            s_reset_closing = false;
        }

        if (s_reset_closing && motor_get_state() == DOOR_CLOSED) {
            enter_idle();
        }
        break;

    default:
        break;
    }
}

/* ---- Manual reset--------------- */

bool passage_manual_reset(void)
{
    sys_state_t state = sys_get_state();

    if (state != SYS_ALARM && state != SYS_ERROR) {
        return false;
    }

    /* Never reset or drive the door while the doorway is occupied. */
    if (!ldr_all_clear()) {
        return false;
    }

    if (state == SYS_ALARM) {
        if (motor_get_state() != DOOR_CLOSED) {
            /* Close the door, alarm clears once it is shut. */
            if (!motor_is_moving() && !motor_force_close()) {
                return false;
            }
            s_reset_closing = true;
            return true;
        }

        sys_reset_failed_attempts();
        sys_set_tailgate_flag(false);
        sys_set_alarm_reason(ALARM_REASON_NONE);
        enter_idle();
        return true;
    }

    if (!nfc_is_ready() && !nfc_reinitialize()) {
        return false;
    }
    if (ldr_has_error()) {
        return false;
    }

    motor_clear_error();

    if (motor_get_state() == DOOR_CLOSED) {
        enter_idle();
        return true;
    }

    if (!motor_is_moving()) {
        if (!motor_force_close()) {
            return false;
        }
        s_reset_closing = true;
        return true;
    }

    return false;
}
