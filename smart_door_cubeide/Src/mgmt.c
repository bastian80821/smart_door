/**
 * mgmt.c
 * -----------------------------------------------------------------------------
 * A shallow, admin-gated menu driven by the 4x4 keypad and shown on the 2-row
 * LCD. Layout follows the design journal: the top row shows information, the
 * bottom row shows the prompt / current input.
 *
 * Keypad (per the journal, Figure 4): A = up, B = down, C = back, D = ok,
 *   * = delete a digit, # = home (jump back to idle from anywhere).
 *
 * mgmt owns the LCD. It renders the menu, the access-control events auth
 * reports, and the system messages the passage controller pushes, so there is a
 * single display owner. The idle screen tracks the live admin state and redraws
 * itself the moment the admin window opens or expires, with no keypress needed.
 * -----------------------------------------------------------------------------
 */
#include "mgmt.h"
#include "hal_wrapper.h"
#include "config.h"
#include "auth.h"
#include "clock.h"
#include "idstore.h"
#include <stdio.h>
#include <string.h>

/* ---- Admin gate seam ------------------------------------------------------
 * The menu is only reachable while the admin fob window is open. */
static bool is_admin(void) {
    return auth_is_admin();
}

/* ---- Screens and menu items ---------------------------------------------- */
typedef enum {
    ST_IDLE, ST_MENU, ST_EVENT_TIME, ST_EVENT_DUR, ST_CLOCK_SET,
    ST_SHOW, ST_THEME, ST_NOTICE
} mstate_t;

typedef enum { M_EVENT, M_CLOCK, M_SHOW, M_THEME, M_CLEAR, M_COUNT } menu_item_t;

static const char *const MENU_LABELS[M_COUNT] = {
    "Set event",
    "Set clock",
    "Show config",
    "Set theme",
    "Clear cards",
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

/* ---- Messages pushed in by the passage controller ------------------------ */
static bool system_message_active = false;
static bool system_message_dirty = false;
/* Critical messages (alarm, system error) always own the display. Routine door
 * progress does not, so an admin using the menu is not interrupted by it. */
static bool system_message_critical = false;
static bool reset_requested = false;
/* An override by the administrative key is stronger than the '#' reset: it is
 * not conditional on the doorway sensors, because an authorised person is
 * physically present and can see the door. */
static bool admin_reset_requested = false;

static bool custom_idle_enabled = false;

static char system_top[17] = "";
static char system_bottom[17] = "";

static char custom_idle_top[17] = "";
static char custom_idle_bottom[17] = "";

/* ---- Rendering ----------------------------------------------------------- */
static void show_idle(void) {
    idle_admin_shown = auth_is_admin();

    if (auth_is_admin()) {
        hal_lcd_write(0, "Admin mode");
        hal_lcd_write(1, "D=menu Tap=enrol");
    } else if (custom_idle_enabled) {
        hal_lcd_write(0, custom_idle_top);
        hal_lcd_write(1, custom_idle_bottom);
    } else {
        hal_lcd_write(0, "Smart Door");
        hal_lcd_write(1, "Tap admin key");
    }
}

/* Copy and safely limit one LCD row to 16 characters. */
static void copy_lcd_row(char destination[17], const char *source)
{
    memset(destination, 0, 17);

    if (source != NULL) {
        strncpy(destination, source, 16);
    }
}

static void show_menu(void) {
    char top[17];
    snprintf(top, sizeof top, ">%s", MENU_LABELS[menu_pos]);
    hal_lcd_write(0, top);
    hal_lcd_write(1, "A/B move   D=ok");
}

/* Shared HH:MM entry display, used for both the event start and the clock. */
static void show_hhmm(const char *title) {
    char h0 = entry_len > 0 ? entry[0] : '_';
    char h1 = entry_len > 1 ? entry[1] : '_';
    char m0 = entry_len > 2 ? entry[2] : '_';
    char m1 = entry_len > 3 ? entry[3] : '_';
    char bot[17];
    snprintf(bot, sizeof bot, "%c%c:%c%c *=del D=ok", h0, h1, m0, m1);
    hal_lcd_write(0, title);
    hal_lcd_write(1, bot);
}

static void show_event_time(void) { show_hhmm("Event start"); }
static void show_clock_set(void)  { show_hhmm("Set clock now"); }

static void show_event_dur(void) {
    char digits[4] = "___";
    for (uint8_t i = 0; i < entry_len && i < 3; i++) digits[i] = entry[i];
    char bot[17];
    snprintf(bot, sizeof bot, "%s m *=del D=ok", digits);
    hal_lcd_write(0, "Event duration");
    hal_lcd_write(1, bot);
}

static void show_config(void) {
    const system_config_t *c = config_get();
    char top[17], bot[17];

    if (!c->event_enabled) {
        snprintf(top, sizeof top, "No event set");
    } else {
        snprintf(top, sizeof top, "%02u:%02u +%um",
                 (unsigned)(c->event_start_min / 60),
                 (unsigned)(c->event_start_min % 60),
                 (unsigned)c->event_duration_min);
    }

    /* Second row carries the live clock and the selected theme, so one screen
     * answers "what is configured" and "what time does the door think it is". */
    if (clock_is_set()) {
        snprintf(bot, sizeof bot, "%02u:%02u %s",
                 (unsigned)(clock_now_min() / 60),
                 (unsigned)(clock_now_min() % 60),
                 THEME_NAMES[c->active_theme]);
    } else {
        snprintf(bot, sizeof bot, "--:-- %s", THEME_NAMES[c->active_theme]);
    }

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
        case ST_CLOCK_SET:  show_clock_set();  break;
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

static void go(mstate_t s) {
    state = s;

    /* A door message parked while the menu was open becomes visible again the
     * moment we return to idle. */
    if (s == ST_IDLE && system_message_active) {
        system_message_dirty = true;
    }

    render();
}

/* Parse the four-digit buffer as HH:MM. Returns false if out of range. */
static bool parse_hhmm(uint8_t *hh_out, uint8_t *mm_out) {
    uint8_t hh = (uint8_t)((entry[0] - '0') * 10 + (entry[1] - '0'));
    uint8_t mm = (uint8_t)((entry[2] - '0') * 10 + (entry[3] - '0'));

    if (hh >= 24u || mm >= 60u) {
        return false;
    }

    *hh_out = hh;
    *mm_out = mm;
    return true;
}

/* ---- Public interface ---------------------------------------------------- */
void mgmt_init(void) {
    state = ST_IDLE;
    render();
}

void mgmt_set_idle_message(const char *top, const char *bottom)
{
    copy_lcd_row(custom_idle_top, top);
    copy_lcd_row(custom_idle_bottom, bottom);

    custom_idle_enabled = true;

    if (!system_message_active && state == ST_IDLE) {
        show_idle();
    }
}

void mgmt_set_system_message(const char *top, const char *bottom)
{
    copy_lcd_row(system_top, top);
    copy_lcd_row(system_bottom, bottom);

    system_message_active = true;
    system_message_dirty = true;
    system_message_critical = false;
}

void mgmt_set_critical_message(const char *top, const char *bottom)
{
    copy_lcd_row(system_top, top);
    copy_lcd_row(system_bottom, bottom);

    system_message_active = true;
    system_message_dirty = true;
    system_message_critical = true;

    /* An alarm or fault pre-empts whatever the manager was doing. */
    state = ST_IDLE;
}

void mgmt_clear_system_message(void)
{
    system_message_active = false;
    system_message_dirty = false;
    system_message_critical = false;

    state = ST_IDLE;
    show_idle();
}

bool mgmt_take_reset_request(void)
{
    bool result = reset_requested;

    reset_requested = false;

    return result;
}

bool mgmt_take_admin_reset_request(void)
{
    bool result = admin_reset_requested;

    admin_reset_requested = false;

    return result;
}

void mgmt_task(void) {
    /* Auth events are read first. The admin key must be acknowledged even when
     * a door message is on screen, otherwise presenting it while the door is
     * opening would be silently swallowed. */
    auth_event_t evt = auth_poll_event();

    if (evt == AUTH_EVT_ADMIN) {
        /* The administrative key is a full override. As well as opening the
         * door it clears an alarm or fault outright -- an authorised manager
         * arriving at the door is exactly who should be able to end one. The
         * reset is requested from the passage controller rather than merely
         * hiding the message, so the buzzer and lights actually stop and the
         * system unlocks instead of alarming behind a friendlier screen. */
        admin_reset_requested = true;

        system_message_active = false;
        system_message_dirty = false;
        system_message_critical = false;

        go(ST_IDLE);                    /* renders the admin resting screen */
        return;
    }

    /* "In the menu" means the manager is looking at something other than the
     * resting screen and should not have it pulled away. */
    bool in_menu = (state != ST_IDLE && state != ST_NOTICE);

    /* Routine door progress yields both to an open menu and to admin mode.
     * Alarms and faults are critical and always take the display. */
    bool show_message = system_message_active &&
                        !auth_is_admin() &&
                        (system_message_critical || !in_menu);

    if (show_message) {
        /* Access results during a door cycle belong to the passage controller,
         * which is already displaying them. */
        evt = AUTH_EVT_NONE;

        if (system_message_dirty) {
            hal_lcd_write(0, system_top);
            hal_lcd_write(1, system_bottom);

            system_message_dirty = false;
        }

        char key = hal_keypad_scan();

        /* '#' is the manual reset while an alarm or fault is displayed. */
        if (key == '#') {
            reset_requested = true;
            return;
        }

        /* An authorised admin may open the menu over a routine message, but
         * never over an alarm, which has to be dealt with first. */
        if (key == 'D' && !system_message_critical && auth_is_admin()) {
            auth_touch();
            menu_pos = M_EVENT;
            go(ST_MENU);
            return;
        }

        if (key != '\0') {
            auth_touch();          /* any keypress counts as admin activity */
        }

        return;
    }

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
        idle_admin_shown = auth_is_admin();

        /* Admin just expired: a door message parked behind it becomes due. */
        if (!idle_admin_shown && system_message_active) {
            system_message_dirty = true;
            return;
        }

        show_idle();
    }

    /* Surface any access-control event from auth (granted/denied/enrolled). */
    switch (evt) {
        case AUTH_EVT_ADMIN:
            break;                      /* already handled above */
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
                case M_CLOCK: go(ST_CLOCK_SET);  break;
                case M_SHOW:  go(ST_SHOW);       break;
                case M_THEME: theme_pick = config_get()->active_theme; go(ST_THEME); break;

                case M_CLEAR:
                    /* Wipes every enrolled card. Acts immediately rather than
                     * asking for confirmation: on a two-row display an extra
                     * prompt costs more than it protects, and re-enrolling is
                     * a single tap. */
                    if (idstore_clear_all()) {
                        notice("Cards cleared", ST_MENU);
                    } else {
                        notice("Clear failed", ST_MENU);
                    }
                    break;

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
            uint8_t hh, mm;
            if (parse_hhmm(&hh, &mm)) {
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
            for (uint8_t i = 0; i < entry_len; i++) mins = (uint16_t)(mins * 10 + (entry[i] - '0'));
            if (mins >= 1u && mins <= 1439u) {
                config_set_event(true, pending_start, mins);
                notice("Event saved", ST_MENU);
            } else {
                notice("Range 1-1439", ST_EVENT_DUR);
            }
        }
        break;

    /* Set the wall-clock time the scheduler compares against. */
    case ST_CLOCK_SET:
        if      (k >= '0' && k <= '9' && entry_len < 4) { entry[entry_len++] = k; render(); }
        else if (k == '*' && entry_len > 0)             { entry_len--; render(); }
        else if (k == 'C')                              { go(ST_MENU); }
        else if (k == 'D' && entry_len == 4) {
            uint8_t hh, mm;
            if (parse_hhmm(&hh, &mm)) {
                clock_set(hh, mm);
                notice("Clock set", ST_MENU);
            } else {
                notice("Invalid time", ST_CLOCK_SET);
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
