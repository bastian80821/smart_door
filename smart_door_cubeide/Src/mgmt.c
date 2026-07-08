/**
 * mgmt.c
 * -----------------------------------------------------------------------------
 * A shallow, admin-gated menu driven by the 4x4 keypad and shown on the 2-row
 * LCD. Layout follows the design journal: the top row shows information, the
 * bottom row shows the prompt / current input.
 *
 * Keypad (per the journal): A = up, B = down, C = back, D = ok, * = delete.
 *
 * Flow:
 *   IDLE --D--> [admin?] --> MENU --> { set time, set duration, show, theme }
 * Config is only reachable when is_admin() is true, so it sits behind the
 * administrative authorisation the spec requires.
 * -----------------------------------------------------------------------------
 */
#include "mgmt.h"
#include "hal_wrapper.h"
#include "config.h"
#include <stdio.h>

/* ---- Admin gate seam ------------------------------------------------------
 * The menu is only reachable when an admin is authorised. Auth (the PN532) is
 * not wired up yet, so this returns true for bench testing. When the auth
 * module lands, repoint this at the real admin flag -- nothing else changes.
 * Flip it to false to exercise the "Access denied" path. */
static bool is_admin(void) {
    return true;
}

/* ---- Screens and menu items ---------------------------------------------- */
typedef enum {
    ST_IDLE, ST_MENU, ST_TIME, ST_DURATION, ST_SHOW, ST_THEME, ST_NOTICE
} mstate_t;

typedef enum { M_TIME, M_DURATION, M_SHOW, M_THEME, M_COUNT } menu_item_t;

static const char *const MENU_LABELS[M_COUNT] = {
    "Set unlock time",
    "Set duration",
    "Show config",
    "Set theme",
};

static const char *const THEME_NAMES[] = {
    "Default", "Star Wars", "James Bond",
};

/* ---- State --------------------------------------------------------------- */
static mstate_t    state = ST_IDLE;
static menu_item_t menu_pos = M_TIME;
static char        entry[8];        /* digit buffer for time / duration entry */
static uint8_t     entry_len = 0;
static theme_t     theme_pick = THEME_DEFAULT;

/* A NOTICE is a short confirmation ("Saved") that clears itself after ~1 s
 * without needing a keypress -- timed off hal_millis so it never blocks. */
static mstate_t    notice_next = ST_IDLE;
static uint32_t    notice_until = 0;

/* ---- Rendering ----------------------------------------------------------- */
static void show_idle(void) {
    hal_lcd_write(0, "Smart Door");
    hal_lcd_write(1, "D = admin menu");
}

static void show_menu(void) {
    char top[17];
    snprintf(top, sizeof top, ">%s", MENU_LABELS[menu_pos]);
    hal_lcd_write(0, top);
    hal_lcd_write(1, "A/B move   D=ok");
}

static void show_time(void) {
    /* Fill HH:MM left to right as digits arrive; underscores for the rest. */
    char h0 = entry_len > 0 ? entry[0] : '_';
    char h1 = entry_len > 1 ? entry[1] : '_';
    char m0 = entry_len > 2 ? entry[2] : '_';
    char m1 = entry_len > 3 ? entry[3] : '_';
    char bot[17];
    snprintf(bot, sizeof bot, "%c%c:%c%c   D=ok", h0, h1, m0, m1);
    hal_lcd_write(0, "Set unlock time");
    hal_lcd_write(1, bot);
}

static void show_duration(void) {
    char digits[4] = "___";
    for (uint8_t i = 0; i < entry_len && i < 3; i++) digits[i] = entry[i];
    char bot[17];
    snprintf(bot, sizeof bot, "%s min    D=ok", digits);
    hal_lcd_write(0, "Set duration");
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
        case ST_IDLE:     show_idle();     break;
        case ST_MENU:     show_menu();     break;
        case ST_TIME:     show_time();     break;
        case ST_DURATION: show_duration(); break;
        case ST_SHOW:     show_config();   break;
        case ST_THEME:    show_theme();    break;
        case ST_NOTICE:   break;           /* message already on screen */
    }
}

static void notice(const char *msg, mstate_t next) {
    hal_lcd_write(0, msg);
    hal_lcd_write(1, "");
    notice_next = next;
    notice_until = hal_millis() + 1000;
    state = ST_NOTICE;
}

/* ---- Public interface ---------------------------------------------------- */
void mgmt_init(void) {
    state = ST_IDLE;
    render();
}

void mgmt_task(void) {
    if (state == ST_NOTICE) {
        if ((int32_t)(hal_millis() - notice_until) >= 0) {
            state = notice_next;
            render();
        }
        return;
    }

    char k = hal_keypad_scan();
    if (k == '\0') return;              /* nothing new this tick */

    switch (state) {
    case ST_IDLE:
        if (k == 'D') {
            if (is_admin()) { menu_pos = M_TIME; state = ST_MENU; render(); }
            else            { notice("Access denied", ST_IDLE); }
        }
        break;

    case ST_MENU:
        if      (k == 'A') { menu_pos = (menu_pos == M_TIME) ? (menu_item_t)(M_COUNT - 1)
                                                             : (menu_item_t)(menu_pos - 1); render(); }
        else if (k == 'B') { menu_pos = (menu_item_t)((menu_pos + 1) % M_COUNT); render(); }
        else if (k == 'C') { state = ST_IDLE; render(); }
        else if (k == 'D') {
            entry_len = 0;
            switch (menu_pos) {
                case M_TIME:     state = ST_TIME;     break;
                case M_DURATION: state = ST_DURATION; break;
                case M_SHOW:     state = ST_SHOW;     break;
                case M_THEME:    theme_pick = config_get()->active_theme; state = ST_THEME; break;
                default: break;
            }
            render();
        }
        break;

    case ST_TIME:
        if      (k >= '0' && k <= '9' && entry_len < 4) { entry[entry_len++] = k; render(); }
        else if (k == '*' && entry_len > 0)             { entry_len--; render(); }
        else if (k == 'C')                              { state = ST_MENU; render(); }
        else if (k == 'D' && entry_len == 4) {
            uint8_t hh = (entry[0] - '0') * 10 + (entry[1] - '0');
            uint8_t mm = (entry[2] - '0') * 10 + (entry[3] - '0');
            if (hh < 24 && mm < 60) {
                config_set_event(true, hh * 60 + mm, config_get()->event_duration_min);
                notice("Time saved", ST_MENU);
            } else {
                notice("Invalid time", ST_TIME);
            }
        }
        break;

    case ST_DURATION:
        if      (k >= '0' && k <= '9' && entry_len < 3) { entry[entry_len++] = k; render(); }
        else if (k == '*' && entry_len > 0)             { entry_len--; render(); }
        else if (k == 'C')                              { state = ST_MENU; render(); }
        else if (k == 'D' && entry_len > 0) {
            uint16_t mins = 0;
            for (uint8_t i = 0; i < entry_len; i++) mins = mins * 10 + (entry[i] - '0');
            if (mins >= 1 && mins <= 1439) {
                config_set_event(true, config_get()->event_start_min, mins);
                notice("Duration saved", ST_MENU);
            } else {
                notice("Range 1-1439", ST_DURATION);
            }
        }
        break;

    case ST_SHOW:
        if (k == 'C' || k == 'D') { state = ST_MENU; render(); }
        break;

    case ST_THEME:
        if      (k == 'A') { theme_pick = (theme_pick == THEME_DEFAULT) ? THEME_JAMESBOND
                                                                        : (theme_t)(theme_pick - 1); render(); }
        else if (k == 'B') { theme_pick = (theme_pick == THEME_JAMESBOND) ? THEME_DEFAULT
                                                                          : (theme_t)(theme_pick + 1); render(); }
        else if (k == 'C') { state = ST_MENU; render(); }
        else if (k == 'D') { config_set_theme(theme_pick); notice("Theme saved", ST_MENU); }
        break;

    default: break;
    }
}
