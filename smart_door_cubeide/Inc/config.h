/**
 * config.h
 * -----------------------------------------------------------------------------
 * The shared configuration the building manager sets, and the shared theme
 * selection. Written by the management module, read by the door and the
 * alarm/feedback modules.
 *
 *
 * time representation:
 *   Times of day are stored as minutes since midnight,  which avoids
 *   hour/minute pairs for the logic. UI converts to/from HH:MM only for display/entry.
 * -----------------------------------------------------------------------------
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/** The feedback theme profiles.
 */

typedef enum {
    THEME_DEFAULT,
    THEME_STARWARS,
    THEME_JAMESBOND
} theme_t;

/**
 * The live system configuration. One instance exists (owned by the management
 * module), other modules read it through the accessors below.
 */
typedef struct {
    /* --- Scheduled unlock (event) --- */
    bool     event_enabled;
    uint16_t event_start_min;
    uint16_t event_duration_min;

    /* --- Feedback theme --- */
    theme_t  active_theme;      /* Applied across LEDs, buzzer and LCD.          */
} system_config_t;

/* --- Accessors ------------------------------------------------------------- */
/* Implemented in config.c. Modules use these rather than a raw global so that,
 * for example, writes can later be persisted to flash in one place.            */

/** Get a read-only pointer to the current config. */
const system_config_t *config_get(void);

/** Set the scheduled-unlock parameters (called by the management UI). */
void config_set_event(bool enabled, uint16_t start_min, uint16_t duration_min);

/** Set the active theme (called by the management UI). */
void config_set_theme(theme_t theme);

/*  Handles windows that wrap past midnight. The door module calls this. */
bool config_event_active_at(uint16_t now_min);

#endif /* CONFIG_H */
