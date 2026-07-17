/**
 * auth.c
 * -----------------------------------------------------------------------------
 * Created on: 17 July 2026
 * Author: bastianmarschner
 *
 * Card access control (see auth.h). Reads the PN532, checks the flash ID list
 * and the admin fob, and reports the outcome as an event. It does not touch the
 * LCD -- mgmt renders the events and owns the display.
 * -----------------------------------------------------------------------------
 */
#include "auth.h"
#include "hal_wrapper.h"
#include "idstore.h"
#include <string.h>

/* The supplied blue fob is the administrative override key. */
static const uint8_t ADMIN_UID[4] = { 0x4C, 0xE4, 0x2D, 0x07 };

#define ADMIN_IDLE_MS  10000u   /* admin window closes after this much inactivity */
#define TAP_COOLDOWN_MS  800u   /* ignore repeat reads of a card held on the reader */

static uint32_t     admin_until = 0;         /* admin window expiry (hal_millis) */
static uint32_t     last_tap = 0;            /* debounce timestamp */
static auth_event_t pending = AUTH_EVT_NONE; /* event awaiting mgmt */
static bool         granted_flag = false;

static bool uid_is_admin(const card_uid_t *u) {
    return u->length == 4 && memcmp(u->bytes, ADMIN_UID, 4) == 0;
}

static bool admin_window_open(void) {
    return (int32_t)(admin_until - hal_millis()) > 0;
}

void auth_init(void) {
    hal_reader_init();
    admin_until = 0;
    last_tap = 0;
    pending = AUTH_EVT_NONE;
    granted_flag = false;
}

void auth_task(void) {
    card_uid_t uid;
    if (!hal_reader_poll(&uid)) return;          /* no card this tick */

    uint32_t now = hal_millis();
    if ((now - last_tap) < TAP_COOLDOWN_MS) return;   /* debounce */
    last_tap = now;

    if (uid_is_admin(&uid)) {
        admin_until = now + ADMIN_IDLE_MS;       /* arm / refresh admin mode */
        pending = AUTH_EVT_ADMIN;
        return;
    }

    if (admin_window_open()) {                   /* enrol this card */
        if (idstore_add(uid.bytes, uid.length)) pending = AUTH_EVT_ENROLLED;
        else                                     pending = AUTH_EVT_ENROLL_FAIL;
        admin_until = now + ADMIN_IDLE_MS;       /* stay in admin, refresh window */
        return;
    }

    if (idstore_contains(uid.bytes, uid.length)) {
        granted_flag = true;
        pending = AUTH_EVT_GRANTED;
    } else {
        pending = AUTH_EVT_DENIED;
    }
}

auth_event_t auth_poll_event(void) {
    auth_event_t e = pending;
    pending = AUTH_EVT_NONE;
    return e;
}

bool auth_is_admin(void) {
    return admin_window_open();
}

void auth_touch(void) {
    if (admin_window_open()) admin_until = hal_millis() + ADMIN_IDLE_MS;
}

bool auth_access_granted(void) {
    bool g = granted_flag;
    granted_flag = false;
    return g;
}
