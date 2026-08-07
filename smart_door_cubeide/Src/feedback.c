/**
 * feedback.c
 * -----------------------------------------------------------------------------
 * LED-bar and buzzer patterns for the access-control system.
 *
 * THEMES
 *   Manager can select a feedback profile (Default, Star Wars, James Bond)
 *   from the admin menu. The profile changes how access granted, access denied
 *   and the two travel directions are signalled.
 *
 *   The intrusion alarm and the system-error indication are deliberately not
 *   themed.
 *
 *   Buzzer is monophonic, so a melody is a short contour of single
 *   tones rather than real music.
 *
 *   Every themed pattern is a list of frames. A frame is an LED-bar mask, a
 *   buzzer frequency and a duration, so adding or retuning a theme is a matter
 *   of editing a table rather than writing new animation code.
 *
 * No long HAL_Delay() calls are used. Every sequence is advanced using
 * hal_millis().
 * -----------------------------------------------------------------------------
 */
#include "feedback.h"
#include "hal_wrapper.h"
#include "config.h"
#include <stddef.h>

#define LED_ALL_MASK             0xFFFFu
#define LED_BOTTOM_MASK          0x0001u

#define ALARM_PHASE_MS             250u
#define ERROR_PHASE_MS             500u

#define ALARM_HIGH_HZ             1900u
#define ALARM_LOW_HZ              1300u

#define TONE_SILENT                  0u

/* One step of a themed pattern: what the bar shows, what the buzzer plays and for how long */
typedef struct {
    uint16_t led_mask;
    uint16_t frequency_hz;
    uint16_t duration_ms;
} frame_t;

/* ---- Default profile ------------------------------------------------------
  */

static const frame_t DEFAULT_ENTRY[] = {
    { 0x0003u, 2200u,  60u }, { 0x000Fu, 2200u,  60u },
    { 0x003Fu, 2200u,  30u }, { 0x00FFu, TONE_SILENT, 30u },
    { 0x03FFu, TONE_SILENT, 30u }, { 0x0FFFu, TONE_SILENT, 30u },
    { 0x3FFFu, TONE_SILENT, 30u }, { 0xFFFFu, TONE_SILENT, 60u },
};

static const frame_t DEFAULT_EXIT[] = {
    { 0xC000u, 2200u,  60u }, { 0xF000u, 2200u,  60u },
    { 0xFC00u, 2200u,  30u }, { 0xFF00u, TONE_SILENT, 30u },
    { 0xFFC0u, TONE_SILENT, 30u }, { 0xFFF0u, TONE_SILENT, 30u },
    { 0xFFFCu, TONE_SILENT, 30u }, { 0xFFFFu, TONE_SILENT, 60u },
};

static const frame_t DEFAULT_DENIED[] = {
    { LED_ALL_MASK, 450u, 160u }, { 0u, TONE_SILENT, 160u },
    { LED_ALL_MASK, 450u, 160u }, { 0u, TONE_SILENT, 160u },
    { LED_ALL_MASK, 450u, 160u }, { 0u, TONE_SILENT, 160u },
};

/* ---- Star Wars profile ----------------------------------------------------
 */

static const frame_t SW_ENTRY[] = {
    { 0x0180u, 1200u,  70u }, { 0x03C0u, 1600u,  70u },
    { 0x07E0u, 2000u,  70u }, { 0x0FF0u, 2400u,  70u },
    { 0x1FF8u, 2800u,  70u }, { 0x3FFCu, 3200u,  70u },
    { 0x7FFEu, 3600u,  70u }, { 0xFFFFu, TONE_SILENT, 120u },
};

static const frame_t SW_EXIT[] = {
    { 0xFFFFu, 3600u,  70u }, { 0x7FFEu, 3200u,  70u },
    { 0x3FFCu, 2800u,  70u }, { 0x1FF8u, 2400u,  70u },
    { 0x0FF0u, 2000u,  70u }, { 0x07E0u, 1600u,  70u },
    { 0x03C0u, 1200u,  70u }, { 0x0180u, TONE_SILENT, 120u },
};

/* G G G Eb -- the march's opening rhythm, stomped in pairs on the bar. */
static const frame_t SW_DENIED[] = {
    { LED_ALL_MASK, 392u, 200u }, { 0u, TONE_SILENT,  90u },
    { LED_ALL_MASK, 392u, 200u }, { 0u, TONE_SILENT,  90u },
    { LED_ALL_MASK, 392u, 200u }, { 0u, TONE_SILENT,  90u },
    { LED_ALL_MASK, 311u, 380u }, { 0u, TONE_SILENT, 120u },
};

/* ---- James Bond profile ---------------------------------------------------
 */

static const frame_t JB_ENTRY[] = {
    { 0x0001u,  800u,  45u }, { 0x0004u,  950u,  45u },
    { 0x0010u, 1100u,  45u }, { 0x0040u, 1250u,  45u },
    { 0x0100u, 1400u,  45u }, { 0x0400u, 1550u,  45u },
    { 0x1000u, 1700u,  45u }, { 0xFFFFu, 1850u, 150u },
};

static const frame_t JB_EXIT[] = {
    { 0xFFFFu, 1850u, 100u }, { 0x7FFFu, 1700u,  45u },
    { 0x1FFFu, 1550u,  45u }, { 0x07FFu, 1400u,  45u },
    { 0x01FFu, 1250u,  45u }, { 0x007Fu, 1100u,  45u },
    { 0x001Fu,  950u,  45u }, { 0x0000u,  800u, 100u },
};

static const frame_t JB_DENIED[] = {
    { 0x00C0u, 659u, 150u }, { 0x0300u, 698u, 150u },
    { 0x00C0u, 659u, 150u }, { 0x0300u, 698u, 150u },
    { 0x00C0u, 659u, 150u }, { 0x0000u, TONE_SILENT, 150u },
};

/* ---- Profile lookup ------------------------------------------------------- */

typedef struct {
    const frame_t *frames;
    uint8_t        count;
} pattern_t;

#define PATTERN(a) { (a), (uint8_t)(sizeof (a) / sizeof (a)[0]) }

/* Indexed by theme, then by which of the three themed events occurred. */
typedef enum { EV_ENTRY = 0, EV_EXIT, EV_DENIED, EV_COUNT } themed_event_t;

static const pattern_t PATTERNS[3][EV_COUNT] = {
    /* THEME_DEFAULT   */ { PATTERN(DEFAULT_ENTRY), PATTERN(DEFAULT_EXIT), PATTERN(DEFAULT_DENIED) },
    /* THEME_STARWARS  */ { PATTERN(SW_ENTRY),      PATTERN(SW_EXIT),      PATTERN(SW_DENIED)      },
    /* THEME_JAMESBOND */ { PATTERN(JB_ENTRY),      PATTERN(JB_EXIT),      PATTERN(JB_DENIED)      },
};

/* ---- Runtime state -------------------------------------------------------- */

static feedback_mode_t s_mode = FEEDBACK_IDLE;

static uint32_t s_last_step_ms = 0;

static uint8_t s_step = 0;
static bool s_phase_on = false;
static bool s_complete = true;

static const frame_t *s_frames = NULL;   /* active themed pattern, if any */
static uint8_t        s_frame_count = 0;

/** Update the LED bar and buzzer together. */
static void set_outputs(uint16_t led_mask, uint16_t frequency_hz)
{
    hal_led_bar_set(led_mask);
    hal_buzzer_tone(frequency_hz);
}

/** Apply one frame of the running pattern. */
static void show_frame(uint8_t index)
{
    set_outputs(
        s_frames[index].led_mask,
        s_frames[index].frequency_hz
    );
}

/** Select the pattern for a themed event using the configured profile. */
static void start_pattern(themed_event_t event)
{
    theme_t theme = config_get()->active_theme;

    if ((unsigned)theme >= 3u) {
        theme = THEME_DEFAULT;           /* defensive: unknown profile */
    }

    s_frames      = PATTERNS[theme][event].frames;
    s_frame_count = PATTERNS[theme][event].count;

    s_step = 0;
    s_complete = false;

    show_frame(0);
}

void feedback_init(void)
{
    s_mode = FEEDBACK_IDLE;
    s_last_step_ms = hal_millis();
    s_step = 0;
    s_phase_on = true;
    s_complete = true;
    s_frames = NULL;
    s_frame_count = 0;

    /*
     * Idle requirement:
     * D5-D20 continuously on and buzzer off.
     */
    set_outputs(LED_ALL_MASK, TONE_SILENT);
}

void feedback_set_mode(feedback_mode_t mode)
{
    if (mode == s_mode) {
        return;
    }

    s_mode = mode;
    s_last_step_ms = hal_millis();
    s_step = 0;
    s_phase_on = true;
    s_complete = false;
    s_frames = NULL;
    s_frame_count = 0;

    switch (s_mode) {
    case FEEDBACK_IDLE:
    case FEEDBACK_WAITING_FOR_NFC:
        set_outputs(LED_ALL_MASK, TONE_SILENT);
        s_complete = true;
        break;

    case FEEDBACK_GRANTED_ENTRY:
        start_pattern(EV_ENTRY);
        break;

    case FEEDBACK_GRANTED_EXIT:
        start_pattern(EV_EXIT);
        break;

    case FEEDBACK_ACCESS_DENIED:
        start_pattern(EV_DENIED);
        break;

    case FEEDBACK_UNAUTHORIZED_ALARM:
        /* Never themed */
        set_outputs(LED_ALL_MASK, ALARM_HIGH_HZ);
        break;

    case FEEDBACK_SYSTEM_ERROR:
        /* Never themed. */
        set_outputs(LED_BOTTOM_MASK, TONE_SILENT);
        break;
    }
}

void feedback_task(void)
{
    uint32_t now = hal_millis();

    switch (s_mode) {
    case FEEDBACK_IDLE:
    case FEEDBACK_WAITING_FOR_NFC:
        break;

    /* --------------------------------------------------------------------- */
    case FEEDBACK_GRANTED_ENTRY:
    case FEEDBACK_GRANTED_EXIT:
    case FEEDBACK_ACCESS_DENIED:

        /*
         * Themed sequences all advance the same way: hold each frame for its
         * own duration, then move on, and fall silent at the end.
         */
        if (s_complete || s_frames == NULL) {
            break;
        }

        if ((now - s_last_step_ms) < s_frames[s_step].duration_ms) {
            break;
        }

        s_last_step_ms = now;
        s_step++;

        if (s_step >= s_frame_count) {
            hal_buzzer_tone(TONE_SILENT);
            s_complete = true;
            break;
        }

        show_frame(s_step);
        break;

    /* --------------------------------------------------------------------- */
    case FEEDBACK_UNAUTHORIZED_ALARM:

        /*
         * The LEDs flash continuously and the buzzer alternates between two
         * alarm frequencies instead of switching off.
         */
        if ((now - s_last_step_ms) >= ALARM_PHASE_MS) {
            s_last_step_ms = now;
            s_phase_on = !s_phase_on;

            set_outputs(
                s_phase_on ? LED_ALL_MASK : 0u,
                s_phase_on ? ALARM_HIGH_HZ : ALARM_LOW_HZ
            );
        }

        break;

    /* --------------------------------------------------------------------- */
    case FEEDBACK_SYSTEM_ERROR:

        /*
         * Flash only D5. Keep the buzzer off.
         */
        if ((now - s_last_step_ms) >= ERROR_PHASE_MS) {
            s_last_step_ms = now;
            s_phase_on = !s_phase_on;

            set_outputs(
                s_phase_on ? LED_BOTTOM_MASK : 0u,
                TONE_SILENT
            );
        }

        break;
    }
}

bool feedback_is_complete(void)
{
    return s_complete;
}

feedback_mode_t feedback_get_mode(void)
{
    return s_mode;
}
