/**
 * config.c
 * -----------------------------------------------------------------------------
 * Backing storage + accessors for the shared system configuration (config.h).
 * Also the real implementation of config_event_active_at(), including the
 * wrap-past-midnight case. Times are minutes since midnight (0..1439).
 *
 * When flash persistence is added later, config_set_* are the single place to
 * hook the write-back -- feature code does not change.
 * -----------------------------------------------------------------------------
 */
#include "config.h"

static system_config_t s_config = {
    .event_enabled      = false,
    .event_start_min    = 0,
    .event_duration_min = 0,
    .active_theme       = THEME_DEFAULT,
};

const system_config_t *config_get(void) { return &s_config; }

void config_set_event(bool enabled, uint16_t start_min, uint16_t duration_min) {
    s_config.event_enabled      = enabled;
    s_config.event_start_min    = start_min;
    s_config.event_duration_min = duration_min;
}

void config_set_theme(theme_t theme) { s_config.active_theme = theme; }

bool config_event_active_at(uint16_t now_min) {
    if (!s_config.event_enabled)          return false;
    if (s_config.event_duration_min == 0) return false;

    uint16_t start = (uint16_t)(s_config.event_start_min % 1440u);
    uint16_t now   = (uint16_t)(now_min % 1440u);
    uint32_t end   = (uint32_t)start + s_config.event_duration_min; /* may exceed 1440 */

    if (end >= 1440u) {
        /* duration long enough to wrap; treat >= a full day as always-on */
        if (end - 1440u >= start) return true;
        uint16_t end_wrapped = (uint16_t)(end - 1440u);   /* window crosses midnight */
        return (now >= start) || (now < end_wrapped);
    }
    return (now >= start) && (now < (uint16_t)end);        /* simple same-day window */
}
