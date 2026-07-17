/**
 * auth.c
 *
 * Created on: 17 July 2026
 * Author: bastianmarschner
 * -----------------------------------------------------------------------------
 * Card access control (see auth.h). Reads the PN532, checks the flash ID list
 * AND a per-card secret stored in MIFARE block 4, and handles the admin fob.
 *
 * Two-factor: a card is granted only if its UID is enrolled in the ID list and
 * block 4 holds the secret derived from that card's UID. A cloned UID fails the
 * second factor because the clone does not carry the correct block-4 secret.
 *
 * Honest limit (per the design journal): MIFARE Classic's Crypto1 is broken, so
 * this raises the bar against casual cloning rather than being truly secure.
 *
 * auth does not touch the LCD -- it reports events; mgmt renders them.
 * -----------------------------------------------------------------------------
 */
#include "auth.h"
#include "hal_wrapper.h"
#include "idstore.h"
#include <string.h>
#include "main.h"

/* The supplied blue fob is the administrative override key. */
static const uint8_t ADMIN_UID[4] = { 0x4C, 0xE4, 0x2D, 0x07 };

/* MIFARE default key A on factory cards, and the block that holds our secret. */
static const uint8_t DEFAULT_KEY[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
#define SECRET_BLOCK  4

/* Project salt mixed into every per-card secret so the stored value can't be
 * reproduced from the UID alone. (Prototype-grade; see the note above.) */
static const uint8_t SALT[16] = {
    'D','E','S','N','2','0','0','0', 0x5A,0xA5,0x3C,0xC3,0x69,0x96,0x0F,0xF0
};

#define ADMIN_IDLE_MS  10000u
#define TAP_COOLDOWN_MS  800u

static uint32_t     admin_until = 0;
static uint32_t     last_tap = 0;
static auth_event_t pending = AUTH_EVT_NONE;
static bool         granted_flag = false;

/* Derive this card's 16-byte secret from its UID + the salt. Simple, fast, and
 * deterministic: every card gets a distinct value that isn't just its UID. */
static void derive_secret(const card_uid_t *uid, uint8_t out[16]) {
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t u = uid->bytes[i % (uid->length ? uid->length : 1)];
        out[i] = (uint8_t)(SALT[i] ^ (u + i * 7u) ^ (uid->length << 3));
    }
}

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
    if (!hal_reader_poll(&uid)) return;          /* single poll -> selects card */

    uint32_t now = hal_millis();
    if ((now - last_tap) < TAP_COOLDOWN_MS) return;
    last_tap = now;

    if (uid_is_admin(&uid)) {
        admin_until = now + ADMIN_IDLE_MS;
        pending = AUTH_EVT_ADMIN;
        return;
    }

    if (admin_window_open()) {                   /* enrol: list + stamp secret */
        uint8_t secret[16];
        derive_secret(&uid, secret);
        bool listed  = idstore_add(uid.bytes, uid.length);
        bool stamped = hal_reader_write_sector(&uid, SECRET_BLOCK, DEFAULT_KEY, secret);
        pending = (listed && stamped) ? AUTH_EVT_ENROLLED : AUTH_EVT_ENROLL_FAIL;
        admin_until = now + ADMIN_IDLE_MS;       /* stay in admin */
        return;
    }

    /* --- Two-factor verify: UID enrolled AND block-4 secret matches. --- */
    if (!idstore_contains(uid.bytes, uid.length)) {
        pending = AUTH_EVT_DENIED;
        return;
    }
    uint8_t want[16], got[16];
    derive_secret(&uid, want);
    if (hal_reader_read_sector(&uid, SECRET_BLOCK, DEFAULT_KEY, got)
        && memcmp(want, got, 16) == 0) {
        granted_flag = true;
        pending = AUTH_EVT_GRANTED;
    } else {
        pending = AUTH_EVT_DENIED;               /* listed but wrong/missing secret */
    }
}

auth_event_t auth_poll_event(void) {
    auth_event_t e = pending;
    pending = AUTH_EVT_NONE;
    return e;
}

bool auth_is_admin(void) { return admin_window_open(); }

void auth_touch(void) {
    if (admin_window_open()) admin_until = hal_millis() + ADMIN_IDLE_MS;
}

bool auth_access_granted(void) {
    bool g = granted_flag;
    granted_flag = false;
    return g;
}
