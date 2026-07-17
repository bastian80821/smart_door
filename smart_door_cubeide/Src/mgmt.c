/**
 * mgmt.c
 * Created on: 03 July 2026
 * Author: bastianmarschner
 * -----------------------------------------------------------------------------
 * A shallow, admin-gated menu driven by the 4x4 keypad and shown on the 2-row
 * LCD. Layout follows the design journal: the top row shows information, the
 * bottom row shows the prompt / current input.
 *
 * Keypad (per the journal, Figure 4): A = up, B = down, C = back, D = ok,
 *   * = delete a digit, # = home (jump back to idle from anywhere).
 *
 * mgmt owns the LCD: it renders both the menu and the access-control events that
 * auth reports (granted / denied / enrolled), so there is a single display owner.
 * The idle screen tracks the live admin state and redraws itself the moment the
 * admin window opens or expires, with no keypress required.
 * -----------------------------------------------------------------------------
 */
#include "mgmt.h"
#include "hal_wrapper.h"
#include "config.h"
#include "auth.h"
#include <stdio.h>

/* ---- Admin gate seam ------------------------------------------------------
 * The menu is only reachable while the admin fob window is open. */
static bool is_admin(void) {
    return auth_is_admin();
}

/* ---- Screens and menu items ---------------------------------------------- */
typedef enum {
    ST_IDLE, ST_MENU, ST_EVENT_TIME, ST_EVENT_DUR, ST_SHOW, ST_THEME, ST_NOTICE
} mstate_t;

typedef enum { M_EVENT, M_SHOW, M_THEME, M_COUNT } menu_item_t;

static const char *const MENU_LABELS[M_COUNT] = {
    "Set event",
    "Show config",
    "Set theme",
};

static const char *const THEME_NAMES[] = {
    "Default", "Star Wars", "James Bond",
};

/* ---- State --------------------------------------------------------------- */
static mstate_t    state = ST_IDLE;
static menu_item_t menu_pos = M_EVENT;
static char        entry[8];        /* digit buffer for the current field */
static uint8_t     entry_len = 0;
static theme_t     theme_pick = THEME_DEFAULT;
static uint16_t    pending_start = 0;   /* start time captured before duration */

static mstate_t    notice_next = ST_IDLE;
static uint32_t    notice_until = 0;

/* Last admin state we drew on the idle screen, so we can refresh idle the
 * instant the admin window opens or expires -- even with no keypress. */
static bool        idle_admin_shown = false;

/* ---- Rendering ----------------------------------------------------------- */
static void show_idle(void) {
    idle_admin_shown = auth_is_admin();
    if (auth_is_admin()) {
        hal_lcd_write(0, "Admin mode");
        hal_lcd_write(1, "D=menu Tap=enrol");
    } else {
        hal_lcd_write(0, "Smart Door");
        hal_lcd_write(1, "Tap admin key");
    }
}

static void show_menu(void) {
    char top[17];
    snprintf(top, sizeof top, ">%s", MENU_LABELS[menu_pos]);
    hal_lcd_write(0, top);
    hal_lcd_write(1, "A/B move   D=ok");
}

static void show_event_time(void) {
    char h0 = entry_len > 0 ? entry[0] : '_';
    char h1 = entry_len > 1 ? entry[1] : '_';
    char m0 = entry_len > 2 ? entry[2] : '_';
    char m1 = entry_len > 3 ? entry[3] : '_';
    char bot[17];
    snprintf(bot, sizeof bot, "%c%c:%c%c   D=ok", h0, h1, m0, m1);
    hal_lcd_write(0, "Event start");
    hal_lcd_write(1, bot);
}

static void show_event_dur(void) {
    char digits[4] = "___";
    for (uint8_t i = 0; i < entry_len && i < 3; i++) digits[i] = entry[i];
    char bot[17];
    snprintf(bot, sizeof bot, "%s min    D=ok", digits);
    hal_lcd_write(0, "Event duration");
    hal_lcd_write(1, bot);
}

static void show_config(void) {
    const system_config_t *c = config_get();
    if (!c->event_enabled) {
        hal_lcd_write(0, "No event set");
        hal_lcd_write(1, "C = back");
        return;
    }
    char top[17], bot[17];
    snprintf(top, sizeof top, "Start %02u:%02u",
             (unsigned)(c->event_start_min / 60), (unsigned)(c->event_start_min % 60));
    snprintf(bot, sizeof bot, "Dur %u min", (unsigned)c->event_duration_min);
    hal_lcd_write(0, top);
    hal_lcd_write(1, bot);
}

static void show_theme(void) {
    char top[17];
    snprintf(top, sizeof top, "Theme: %s", THEME_NAMES[theme_pick]);
    hal_lcd_write(0, top);
    hal_lcd_write(1, "A/B pick   D=ok");
}

static void render(void) {
    switch (state) {
        case ST_IDLE:       show_idle();       break;
        case ST_MENU:       show_menu();       break;
        case ST_EVENT_TIME: show_event_time(); break;
        case ST_EVENT_DUR:  show_event_dur();  break;
        case ST_SHOW:       show_config();     break;
        case ST_THEME:      show_theme();      break;
        case ST_NOTICE:     break;             /* message already on screen */
    }
}

static void notice(const char *msg, mstate_t next) {
    hal_lcd_write(0, msg);
    hal_lcd_write(1, "");
    notice_next = next;
    notice_until = hal_millis() + 1000;
    state = ST_NOTICE;
}

static void go(mstate_t s) { state = s; render(); }

/* ---- Public interface ---------------------------------------------------- */
void mgmt_init(void) {
    state = ST_IDLE;
    render();
}

void mgmt_task(void) {
    if (state == ST_NOTICE) {
        if ((int32_t)(hal_millis() - notice_until) >= 0) go(notice_next);
        return;
    }

    /* Global admin inactivity timeout: the moment the admin window expires,
         * drop straight to idle from wherever we are (menu, submenu, mid-entry). */
    if (state != ST_IDLE && !auth_is_admin()) {
        go(ST_IDLE);
        return;
    }

    /* Idle reflects the live admin state: redraw the instant the admin window
     * opens or expires, without waiting for a keypress. */
    if (state == ST_IDLE && auth_is_admin() != idle_admin_shown) {
        show_idle();
    }

    /* Surface any access-control event from auth (granted/denied/enrolled). */
    switch (auth_poll_event()) {
        case AUTH_EVT_ADMIN:        notice("Admin mode", ST_IDLE);     return;
        case AUTH_EVT_GRANTED:      notice("Access granted", ST_IDLE); return;
        case AUTH_EVT_DENIED:       notice("Access denied", ST_IDLE);  return;
        case AUTH_EVT_ENROLLED:     notice("Card enrolled", ST_IDLE);  return;
        case AUTH_EVT_ENROLL_FAIL:  notice("Enrol failed", ST_IDLE);   return;
        case AUTH_EVT_NONE:         break;
    }

    char k = hal_keypad_scan();
    if (k == '\0') return;              /* nothing new this tick */
    auth_touch();                       /* keypress counts as admin activity */

    /* Home key: bail out to idle from anywhere except idle itself. */
    if (k == '#' && state != ST_IDLE) { go(ST_IDLE); return; }

    switch (state) {
    case ST_IDLE:
        if (k == 'D') {
            if (is_admin()) { menu_pos = M_EVENT; go(ST_MENU); }
            else            { notice("Tap admin key", ST_IDLE); }
        }
        break;

    case ST_MENU:
        if      (k == 'A') { menu_pos = (menu_pos == M_EVENT) ? (menu_item_t)(M_COUNT - 1)
                                                              : (menu_item_t)(menu_pos - 1); render(); }
        else if (k == 'B') { menu_pos = (menu_item_t)((menu_pos + 1) % M_COUNT); render(); }
        else if (k == 'C') { go(ST_IDLE); }
        else if (k == 'D') {
            entry_len = 0;
            switch (menu_pos) {
                case M_EVENT: go(ST_EVENT_TIME); break;
                case M_SHOW:  go(ST_SHOW);       break;
                case M_THEME: theme_pick = config_get()->active_theme; go(ST_THEME); break;
                default: break;
            }
        }
        break;

    /* Step 1 of "set event": the start time. */
    case ST_EVENT_TIME:
        if      (k >= '0' && k <= '9' && entry_len < 4) { entry[entry_len++] = k; render(); }
        else if (k == '*' && entry_len > 0)             { entry_len--; render(); }
        else if (k == 'C')                              { go(ST_MENU); }
        else if (k == 'D' && entry_len == 4) {
            uint8_t hh = (entry[0] - '0') * 10 + (entry[1] - '0');
            uint8_t mm = (entry[2] - '0') * 10 + (entry[3] - '0');
            if (hh < 24 && mm < 60) {
                pending_start = (uint16_t)(hh * 60 + mm);
                entry_len = 0;
                go(ST_EVENT_DUR);
            } else {
                notice("Invalid time", ST_EVENT_TIME);
            }
        }
        break;

    /* Step 2 of "set event": the duration. Commits both together. */
    case ST_EVENT_DUR:
        if      (k >= '0' && k <= '9' && entry_len < 3) { entry[entry_len++] = k; render(); }
        else if (k == '*' && entry_len > 0)             { entry_len--; render(); }
        else if (k == 'C')                              { entry_len = 0; go(ST_EVENT_TIME); }
        else if (k == 'D' && entry_len > 0) {
            uint16_t mins = 0;
            for (uint8_t i = 0; i < entry_len; i++) mins = mins * 10 + (entry[i] - '0');
            if (mins >= 1 && mins <= 1439) {
                config_set_event(true, pending_start, mins);
                notice("Event saved", ST_MENU);
            } else {
                notice("Range 1-1439", ST_EVENT_DUR);
            }
        }
        break;

    case ST_SHOW:
        if (k == 'C' || k == 'D') { go(ST_MENU); }
        break;

    case ST_THEME:
        if      (k == 'A') { theme_pick = (theme_pick == THEME_DEFAULT) ? THEME_JAMESBOND
                                                                        : (theme_t)(theme_pick - 1); render(); }
        else if (k == 'B') { theme_pick = (theme_pick == THEME_JAMESBOND) ? THEME_DEFAULT
                                                                          : (theme_t)(theme_pick + 1); render(); }
        else if (k == 'C') { go(ST_MENU); }
        else if (k == 'D') { config_set_theme(theme_pick); notice("Theme saved", ST_MENU); }
        break;

    default: break;
    }
}
