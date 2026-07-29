/**
 * feedback.c
 * -----------------------------------------------------------------------------
 * LED-bar and buzzer patterns for the access-control system.
 *
 * No long HAL_Delay() calls are used. Every sequence is advanced using
 * hal_millis().
 * -----------------------------------------------------------------------------
 */
#include "feedback.h"
#include "hal_wrapper.h"

#define LED_ALL_MASK             0xFFFFu
#define LED_BOTTOM_MASK          0x0001u

#define SWEEP_STEP_MS               50u
#define GRANTED_BEEP_MS            150u
#define DENIED_PHASE_MS            160u
#define ALARM_PHASE_MS             250u
#define ERROR_PHASE_MS             500u

#define GRANTED_BEEP_HZ           2200u
#define DENIED_BEEP_HZ             450u
#define ALARM_HIGH_HZ             1900u
#define ALARM_LOW_HZ              1300u

static feedback_mode_t s_mode = FEEDBACK_IDLE;

static uint32_t s_started_ms = 0;
static uint32_t s_last_step_ms = 0;

static uint8_t s_step = 0;
static bool s_phase_on = false;
static bool s_complete = true;

/** Update the LED bar and buzzer together. */
static void set_outputs(uint16_t led_mask, uint16_t frequency_hz)
{
    hal_led_bar_set(led_mask);
    hal_buzzer_tone(frequency_hz);
}

void feedback_init(void)
{
    s_mode = FEEDBACK_IDLE;
    s_started_ms = hal_millis();
    s_last_step_ms = s_started_ms;
    s_step = 0;
    s_phase_on = true;
    s_complete = true;

    /*
     * Idle requirement:
     * D5-D20 continuously on and buzzer off.
     */
    set_outputs(LED_ALL_MASK, 0);
}

void feedback_set_mode(feedback_mode_t mode)
{
    if (mode == s_mode) {
        return;
    }

    s_mode = mode;
    s_started_ms = hal_millis();
    s_last_step_ms = s_started_ms;
    s_step = 0;
    s_phase_on = true;
    s_complete = false;

    switch (s_mode) {
    case FEEDBACK_IDLE:
    case FEEDBACK_WAITING_FOR_NFC:
        set_outputs(LED_ALL_MASK, 0);
        s_complete = true;
        break;

    case FEEDBACK_GRANTED_ENTRY:
        /*
         * Start the upward sweep at D5.
         */
        set_outputs(LED_BOTTOM_MASK, GRANTED_BEEP_HZ);
        break;

    case FEEDBACK_GRANTED_EXIT:
        /*
         * Start the downward sweep at D20.
         */
        set_outputs((uint16_t)(1u << 15), GRANTED_BEEP_HZ);
        break;

    case FEEDBACK_ACCESS_DENIED:
        /*
         * First of three flashes and low-frequency beeps.
         */
        set_outputs(LED_ALL_MASK, DENIED_BEEP_HZ);
        break;

    case FEEDBACK_UNAUTHORIZED_ALARM:
        set_outputs(LED_ALL_MASK, ALARM_HIGH_HZ);
        break;

    case FEEDBACK_SYSTEM_ERROR:
        set_outputs(LED_BOTTOM_MASK, 0);
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

        /*
         * Only one short beep is required.
         */
        if ((now - s_started_ms) >= GRANTED_BEEP_MS) {
            hal_buzzer_tone(0);
        }

        if (!s_complete &&
            (now - s_last_step_ms) >= SWEEP_STEP_MS) {

            uint16_t mask;

            s_last_step_ms = now;

            if (s_step < 15u) {
                s_step++;
            }

            if (s_mode == FEEDBACK_GRANTED_ENTRY) {
                /*
                 * Growing mask from D5 towards D20.
                 */
                mask = s_step >= 15u
                     ? LED_ALL_MASK
                     : (uint16_t)((1u << (s_step + 1u)) - 1u);
            } else {
                /*
                 * Growing mask from D20 towards D5.
                 */
                mask = (uint16_t)(
                    LED_ALL_MASK << (15u - s_step)
                );
            }

            hal_led_bar_set(mask);

            if (s_step >= 15u) {
                s_complete = true;
            }
        }

        break;

    /* --------------------------------------------------------------------- */
    case FEEDBACK_ACCESS_DENIED:

        /*
         * Six phases:
         *
         * ON, OFF, ON, OFF, ON, OFF
         *
         * This produces three LED flashes and three buzzer beeps.
         */
        if (!s_complete &&
            (now - s_last_step_ms) >= DENIED_PHASE_MS) {

            s_last_step_ms = now;
            s_step++;
            s_phase_on = !s_phase_on;

            if (s_step >= 6u) {
                set_outputs(0, 0);
                s_complete = true;
            }
            else if (s_phase_on) {
                set_outputs(LED_ALL_MASK, DENIED_BEEP_HZ);
            }
            else {
                set_outputs(0, 0);
            }
        }

        break;

    /* --------------------------------------------------------------------- */
    case FEEDBACK_UNAUTHORIZED_ALARM:

        /*
         * The LEDs flash continuously.
         *
         * The buzzer remains active and alternates between two alarm
         * frequencies instead of switching off.
         */
        if ((now - s_last_step_ms) >= ALARM_PHASE_MS) {
            s_last_step_ms = now;
            s_phase_on = !s_phase_on;

            set_outputs(
                s_phase_on ? LED_ALL_MASK : 0,
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
                s_phase_on ? LED_BOTTOM_MASK : 0,
                0
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
