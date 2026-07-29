/**
 * auth.h
 * -----------------------------------------------------------------------------
 * Access control: reads cards from the PN532, decides grant/deny against the
 * flash-backed ID list, and handles the admin override fob.
 *
 * Display policy: auth NEVER touches the LCD. It produces events; the management
 * module owns the screen and renders them. This keeps a single display owner.
 *
 * Admin model: tapping the admin fob opens an admin window that stays open while
 * there is activity and closes after a period of inactivity (see auth_touch).
 * While it is open, tapping an unknown card enrols it and the config menu is
 * reachable (mgmt gates on auth_is_admin()).
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
    AUTH_EVT_ADMIN,      /* admin fob tapped: admin mode armed        */
    AUTH_EVT_GRANTED,    /* enrolled card: access granted             */
    AUTH_EVT_DENIED,     /* unknown card: access denied               */
    AUTH_EVT_ENROLLED,   /* a new card was just enrolled              */
    AUTH_EVT_ENROLL_FAIL /* enrolment failed (store full / error)     */
} auth_event_t;

void auth_init(void);
void auth_task(void);

/* Pop the pending event (returns AUTH_EVT_NONE and clears it if none new). */
auth_event_t auth_poll_event(void);

/* True while the admin window is open. mgmt gates its config menu on this. */
bool auth_is_admin(void);

/* Refresh the admin window's inactivity timer -- called by mgmt on any keypress
 * so navigating the menu keeps admin mode alive. Does nothing if not in admin. */
void auth_touch(void);

/* One-shot: true for a single read after a valid entry tap (for the door). */
bool auth_access_granted(void);

/**
 * Enable normal-card authentication only while an LDR passage request exists.
 * The existing admin fob and admin enrolment behaviour are preserved.
 */
void auth_set_passage_request(bool active);

/**
 * Temporarily suspend all NFC processing while the door is moving, the denied
 * feedback is running, or the system is locked.
 */
void auth_set_enabled(bool enabled);

/** True if the PN532 initialised successfully. */
bool auth_reader_ready(void);

/** One-shot denied result for the door/passage controller. */
bool auth_access_denied(void);

#endif /* AUTH_H */
