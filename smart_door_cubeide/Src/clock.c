/**
 * clock.c
 * -----------------------------------------------------------------------------
 * Software time-of-day clock (see clock.h).
 *
 * We record the wall-clock time at the moment it is set, together with the
 * millisecond tick at that instant. The current time is then the set time plus
 * the elapsed minutes, taken modulo a day. Using the tick difference means the
 * 32-bit millisecond counter wrapping (~49 days) does not corrupt the result.
 * -----------------------------------------------------------------------------
 */
#include "clock.h"
#include "hal_wrapper.h"

#define MINUTES_PER_DAY  1440u

static uint16_t s_set_minute = 0;      /* time of day when it was set */
static uint32_t s_set_tick_ms = 0;     /* hal_millis() at that moment */
static bool     s_is_set = false;

void clock_init(void) {
    s_set_minute = 0;
    s_set_tick_ms = hal_millis();
    s_is_set = false;
}

void clock_set(uint8_t hours, uint8_t minutes) {
    if (hours > 23u || minutes > 59u) {
        return;                         /* caller validates; ignore nonsense */
    }
    s_set_minute  = (uint16_t)(hours * 60u + minutes);
    s_set_tick_ms = hal_millis();
    s_is_set = true;
}

uint16_t clock_now_min(void) {
    if (!s_is_set) {
        return 0;
    }
    uint32_t elapsed_ms = hal_millis() - s_set_tick_ms;   /* wrap-safe */
    uint32_t elapsed_min = elapsed_ms / 60000u;
    return (uint16_t)((s_set_minute + elapsed_min) % MINUTES_PER_DAY);
}

bool clock_is_set(void) {
    return s_is_set;
}
