/**
 * system_state.h
 * -----------------------------------------------------------------------------
 * The single source of truth for "what the system is doing right now".
 *
 * Every module (auth, door, alarm, management UI) reads and writes the system
 * state through this header. If two modules ever disagree about the current
 * state, this is the file to look at first.
 *
 * WHY THIS EXISTS:
 *   The door, the alarm and the feedback LEDs all need to react to the same
 *   events (e.g. "someone is being let in"). Rather than each module inventing
 *   its own idea of state, they all share this one enum. That is what lets the
 *   four modules be written in parallel and still agree at integration time.
 *
 * OWNERSHIP:
 *   The door/sensor module is the primary driver of the motion states
 *   (OPENING/OPEN/CLOSING). Auth drives AUTHENTICATING. Management drives
 *   CONFIG and SCHEDULED_UNLOCK. Alarm only ever READS the state (plus the
 *   tailgate flag below); it never sets it.
 * -----------------------------------------------------------------------------
 */
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * The system's top-level state machine. Ordered roughly in the sequence a
 * normal entry passes through, so the enum reads like the flow itself.
 */
typedef enum {
    SYS_IDLE,             /* Door shut, nothing happening. Default resting state. */
    SYS_AUTHENTICATING,   /* A card has been presented; auth is checking it.      */
    SYS_WAIT_CLEAR,       /* Access decided; waiting for the destination-side     */
                          /* sensor to clear before the door may move (safety).   */
    SYS_OPENING,          /* Motor is driving the door open.                      */
    SYS_OPEN,             /* Door fully open; 5 s passage window is running.       */
    SYS_CLOSING,          /* Motor is driving the door shut.                      */
    SYS_ALARM,            /* Security event (e.g. tailgate). Door holds open,     */
                          /* lights + buzzer active. See tailgate flag below.     */
    SYS_CONFIG,           /* Manager is in the admin menu (behind authorisation). */
    SYS_SCHEDULED_UNLOCK,  /* Door held unlocked for a configured event window.    */
    
    SYS_ACCESS_DENIED,
    SYS_ERROR
} sys_state_t;

/**
 * Direction of travel through the door. Committed at the moment of the trigger
 * (a tap arms ENTRY, an inside-sensor break with no tap arms EXIT) and used to
 * decide which way the motor swings and which sensor the safe-open guard watches.
 */
typedef enum {
    DIR_NONE,   /* No movement currently armed. */
    DIR_ENTRY,  /* Inward: outside -> inside.   */
    DIR_EXIT    /* Outward: inside -> outside.  */
} sys_direction_t;

/**
 * The reason SYS_ALARM is active.
 * This allows the existing generic SYS_ALARM state to support both the team's
 * existing tailgate alarm and the new five-failed-attempt alarm.
 */
typedef enum {
    ALARM_REASON_NONE = 0,
    ALARM_REASON_TAILGATE,
    ALARM_REASON_UNAUTHORIZED_ACCESS
} sys_alarm_reason_t;

/* --- Shared state accessors ------------------------------------------------ */
/* Implemented once in system_state.c. All modules use these rather than a raw
 * global, so that state changes can later be logged or guarded in one place.   */

sys_state_t     sys_get_state(void);
void            sys_set_state(sys_state_t s);

sys_direction_t sys_get_direction(void);
void            sys_set_direction(sys_direction_t d);

/* --- Cross-module event flags ---------------------------------------------- */
/* These are the explicit "seams" between modules. Keeping them here (rather
 * than as private globals) is what lets door and alarm be developed separately.
 */

/** Raised by the door/sensor module the instant an unauthenticated inward
 *  crossing is detected. The alarm module polls this and reacts. Cleared by
 *  the alarm module once the event is handled. */
bool sys_get_tailgate_flag(void);
void sys_set_tailgate_flag(bool v);

/** Raised by the management module while a scheduled unlock window is active.
 *  The door module reads it to decide whether to stay open without a card. */
bool sys_get_scheduled_unlock(void);
void sys_set_scheduled_unlock(bool v);

/* --- Alarm reason --------------------------------------------------------- */
sys_alarm_reason_t sys_get_alarm_reason(void);
void sys_set_alarm_reason(sys_alarm_reason_t reason);

/* --- Consecutive failed access attempts ---------------------------------- */
uint8_t sys_get_failed_attempts(void);

/** Increase the counter by one and return its new value. */
uint8_t sys_increment_failed_attempts(void);

/** Reset the counter after a successful access or manual alarm reset. */
void sys_reset_failed_attempts(void);

#endif /* SYSTEM_STATE_H */
