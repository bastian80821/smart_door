/**
 * clock.h
 * -----------------------------------------------------------------------------
 * Time-of-day clock for the scheduled-unlock feature.
 *
 * The coaST carrier has no battery-backed VBAT, so a hardware RTC would lose
 * the time on every power cycle exactly as this does. We therefore derive the
 * time of day from the millisecond tick and let the manager set it from the
 * admin menu, which keeps the .ioc untouched and makes the set step explicit.
 *
 * Time is minutes since midnight (0..1439), matching config.h.
 * -----------------------------------------------------------------------------
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stdbool.h>

void     clock_init(void);
/** Set the current time of day (from the admin menu). */
void     clock_set(uint8_t hours, uint8_t minutes);
/** Current time of day in minutes since midnight, wrapping at midnight. */
uint16_t clock_now_min(void);
/** True once the manager has set the time (before that, schedules are inert). */
bool     clock_is_set(void);

#endif /* CLOCK_H */
