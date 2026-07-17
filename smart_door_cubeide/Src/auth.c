/*
 * auth.c
 *
 *  Created on: 17 July 2026
 *      Author: bastianmarschner
 *       Card access control (see auth.h). Ties the PN532 reader to the flash ID list
 *       and the admin fob, and drives short LCD feedback on each tap.
 */


#include "auth.h"
#include "hal_wrapper.h"
#include "idstore.h"
#include <string.h>

/* The supplied blue fob is the administrative override key. */
static const uint8_t ADMIN_UID[4] = { 0x4C, 0xE4, 0x2D, 0x07 };

#define ADMIN_WINDOW_MS  5000u   /* how long admin mode stays armed after a fob tap */
#define TAP_COOLDOWN_MS   800u   /* ignore repeat reads of a card still on the reader */

static uint32_t admin_until = 0;     /* admin window expiry (hal_millis timebase) */
static uint32_t last_tap = 0;        /* debounce: time of last accepted tap */
static bool     granted_flag = false;

static bool uid_is_admin(const card_uid_t *u) {
    return u->length == 4 && memcmp(u->bytes, ADMIN_UID, 4) == 0;
}

static bool admin_window_open(void) {
    return (int32_t)(admin_until - hal_millis()) > 0;
}

/* Brief on-screen feedback, then a short hold so the message is readable. */
static void flash_msg(const char *top, const char *bot) {
    hal_lcd_write(0, top);
    hal_lcd_write(1, bot);
}

void auth_init(void) {
    hal_reader_init();
    admin_until = 0;
    last_tap = 0;
    granted_flag = false;
}

void auth_task(void) {
    card_uid_t uid;
    if (!hal_reader_poll(&uid)) return;          /* no card this tick */

    /* Debounce: one physical tap can be read many times while held near. */
    uint32_t now = hal_millis();
    if ((now - last_tap) < TAP_COOLDOWN_MS) return;
    last_tap = now;

    if (uid_is_admin(&uid)) {
        admin_until = now + ADMIN_WINDOW_MS;     /* arm admin mode */
        flash_msg("Admin OK", "Card=enrol D=menu");
        return;
    }

    if (admin_window_open()) {                   /* fob was just tapped: enrol this card */
        if (idstore_add(uid.bytes, uid.length)) flash_msg("Card enrolled", "");
        else                                     flash_msg("Enrol failed", "");
        admin_until = 0;                         /* consume the window */
        return;
    }

    if (idstore_contains(uid.bytes, uid.length)) {
        granted_flag = true;                     /* door module will act on this */
        flash_msg("Access granted", "");
    } else {
        flash_msg("Access denied", "");
    }
}

bool auth_is_admin(void) {
    return admin_window_open();
}

bool auth_access_granted(void) {
    bool g = granted_flag;
    granted_flag = false;
    return g;
}
