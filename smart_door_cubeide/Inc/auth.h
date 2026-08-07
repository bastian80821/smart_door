/**
 * auth.h
 * -----------------------------------------------------------------------------
 * Reads cards from the PN532, decides grant/deny against the
 * flash-backed ID list plus secret, and handles the admin key.
 *
 * Auth never touches the LCD, but produces events. The management module owns the screen and renders them.
 *
 * Presenting the admin key opens an admin window that stays open
 * while there is activity and closes after a period of inactivity. While it is
 * open, tapping an unknown card enrols it and the config menu is reachable. The
 * same tap also enables override, so alarms are cleared and the door opens.
 *
 * Non-blocking: call auth_task() once per main loop.
 * -----------------------------------------------------------------------------
 */
#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>

/* What happened on the most recent card tap. mgmt reads and renders these. */
typedef enum {
    AUTH_EVT_NONE = 0,
    AUTH_EVT_ADMIN,      /* admin key presented: admin mode armed      */
    AUTH_EVT_GRANTED,    /* enrolled card: access granted              */
    AUTH_EVT_DENIED,     /* unknown card or wrong secret: denied       */
    AUTH_EVT_ENROLLED,   /* a new card was just enrolled               */
    AUTH_EVT_ENROLL_FAIL /* enrolment failed (store full / write error)*/
} auth_event_t;

void auth_init(void);
void auth_task(void);

/* Pop the pending event (AUTH_EVT_NONE if nothing new). */
auth_event_t auth_poll_event(void);

/* True while the admin window is open. mgmt gates its config menu on this. */
bool auth_is_admin(void);

/* Refresh the admin window's inactivity timer. mgmt calls this on any keypress
 * so navigating the menu keeps admin mode alive. */
void auth_touch(void);

/* True once after the admin key was presented, so the door can
 * perform an override opening. */
bool auth_take_override_request(void);

/* One-shot results consumed by the passage controller via nfc.c. */
bool auth_access_granted(void);
bool auth_access_denied(void);

/* --- Passage-controller hooks --------------------------------------------- */

/* Ordinary cards are only evaluated while a passage request is active, so a
 * card presented at random never drives the door. */
void auth_set_passage_request(bool active);

/* Mute reader while door is moving or the system is locked. (avoid unresponsiveness) */
void auth_set_enabled(bool enabled);

/* True if the PN532 initialised successfully at startup. */
bool auth_reader_ready(void);

#endif /* AUTH_H */
