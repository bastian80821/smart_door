/**
 * auth.c
 * -----------------------------------------------------------------------------
 * Card access control (see auth.h). Reads the PN532, checks the flash ID list
 * and a per-card secret stored in MIFARE block 4, and handles the admin fob.
 *
 * Two-factor: a card is granted only if its UID is enrolled in the ID list and
 * block 4 holds the secret derived from that card's UID.
 *
 * Limitation: MIFARE Classic's Crypto1 is publically broken, and one broken card
 * can compromise all the others
 *
 * Admin fob doubles as an emergency override: tapping the fob enters
 * the management menu and requests an immediate door opening, no matter any sensors.
 *
 * auth does not touch the LCD -- it reports events; mgmt renders them.
 * -----------------------------------------------------------------------------
 */
#include "auth.h"
#include "hal_wrapper.h"
#include "idstore.h"
#include <stdint.h>
#include <string.h>
#include "main.h"

/* Set admin fob uid*/
static const uint8_t ADMIN_UID[4] = { 0x4C, 0xE4, 0x2D, 0x07 };

/* MIFARE default key A on factory cards, and the block that holds the secret. */
static const uint8_t DEFAULT_KEY[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
#define SECRET_BLOCK  4

/* Secret
 */
static const uint8_t SALT[16] = {
    'D','E','S','N','2','0','0','0', 0x5A,0xA5,0x3C,0xC3,0x69,0x96,0x0F,0xF0
};

#define ADMIN_IDLE_MS  10000u   /* admin window closes after this much inactivity */
#define TAP_COOLDOWN_MS  800u   /* ignore repeat reads of a card held on the reader */
#define POLL_INTERVAL_MS 120u   /* reader poll rate */

static uint32_t     admin_until = 0;         /* admin window expiry (hal_millis) */
static uint32_t     last_tap = 0;            /* debounce timestamp */
static uint32_t     last_poll = 0;           /* reader rate limiter */
static auth_event_t pending = AUTH_EVT_NONE; /* event awaiting mgmt */

static bool granted_flag = false;            /* valid card accepted   */
static bool denied_flag = false;             /* card rejected         */
static bool override_request = false;        /* admin key tap   */

static bool passage_request_active = false;  /* a door request is waiting on NFC */
static bool auth_enabled = true;             /* reader muted during door motion  */
static bool reader_ready = false;            /* PN532 initialised successfully   */

/* Derive a card's 16-byte secret from its UID + the secret.
 * Deterministic: every card gets a distinct value that is not just its UID. */
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
    reader_ready = hal_reader_init();

    admin_until = 0;
    last_tap = 0;
    last_poll = 0;
    pending = AUTH_EVT_NONE;

    granted_flag = false;
    denied_flag = false;
    override_request = false;

    passage_request_active = false;
    auth_enabled = true;
}

void auth_task(void) {
    if (!reader_ready) {
        return;
    }

    /* Polling the reader costs tens of milliseconds when no card is present,
     * because the PN532 is given time to answer. Doing that every iteration
     * starves the rest of the loop -- the stepper only advances once per pass,
     * so a continuous poll makes the door movement miss its deadline. Limiting
     * the rate keeps card taps responsive while leaving the loop free. */
    uint32_t poll_now = hal_millis();
    if ((poll_now - last_poll) < POLL_INTERVAL_MS) {
        return;
    }
    last_poll = poll_now;

    card_uid_t uid;
    if (!hal_reader_poll(&uid)) {
        return;                                  /* no card this tick */
    }

    uint32_t now = hal_millis();
    if ((now - last_tap) < TAP_COOLDOWN_MS) {
        return;                                  /* debounce a held card */
    }
    last_tap = now;

    if (uid_is_admin(&uid)) {
        /* Admin fob overrides everything, including door movements and alarms
         */
        admin_until = now + ADMIN_IDLE_MS;
        override_request = true;
        pending = AUTH_EVT_ADMIN;
        return;
    }

    /* Enrolment in admin mode only
     */
    if (admin_window_open()) {                   /* enrol: list + stamp secret */
        uint8_t secret[16];
        derive_secret(&uid, secret);

        bool listed  = idstore_add(uid.bytes, uid.length);
        bool stamped = hal_reader_write_sector(&uid, SECRET_BLOCK, DEFAULT_KEY, secret);

        pending = (listed && stamped) ? AUTH_EVT_ENROLLED : AUTH_EVT_ENROLL_FAIL;
        admin_until = now + ADMIN_IDLE_MS;       /* stay in admin mode */
        return;
    }

    /* None-admin cards only count while the system is listening for one and the
     * door is actually asking. */
    if (!auth_enabled || !passage_request_active) {
        return;
    }

    /* --- Two-factor verify: UID enrolled and block-4 secret matches. --- */
    if (!idstore_contains(uid.bytes, uid.length)) {
        denied_flag = true;
        pending = AUTH_EVT_DENIED;
        return;
    }

    uint8_t want[16], got[16];
    derive_secret(&uid, want);

    if (hal_reader_read_sector(&uid, SECRET_BLOCK, DEFAULT_KEY, got)
        && memcmp(want, got, 16) == 0) {
        denied_flag = false;
        granted_flag = true;
        pending = AUTH_EVT_GRANTED;
    } else {
        denied_flag = true;
        pending = AUTH_EVT_DENIED;               /* listed but wrong secret */
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
    if (admin_window_open()) {
        admin_until = hal_millis() + ADMIN_IDLE_MS;
    }
}

bool auth_take_override_request(void) {
    bool r = override_request;
    override_request = false;
    return r;
}

bool auth_access_granted(void) {
    bool g = granted_flag;
    granted_flag = false;
    return g;
}

bool auth_access_denied(void) {
    bool d = denied_flag;
    denied_flag = false;
    return d;
}

/* --- Passage-controller hooks --------------------------------------------- */

void auth_set_passage_request(bool active) {
    passage_request_active = active;
}

void auth_set_enabled(bool enabled) {
    auth_enabled = enabled;
}

bool auth_reader_ready(void) {
    return reader_ready;
}
