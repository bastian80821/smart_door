/**
 * ldr.c
 * -----------------------------------------------------------------------------
 * Threshold, hysteresis and debounce processing for the two passage LDRs.
 *
 * Low ADC value -> sensor covered -> door blocked
 * -----------------------------------------------------------------------------
 */
#include "ldr.h"
#include <string.h>

typedef struct {
    uint16_t raw;

    uint16_t covered_threshold;
    uint16_t clear_threshold;

    bool stable_covered;
    bool candidate_covered;

    uint32_t candidate_since_ms;
    uint32_t last_change_ms;

    bool covered_event;
    bool clear_event;
} ldr_runtime_t;

static ldr_runtime_t s_ldr[2];

static uint16_t s_sample_period_ms =
    LDR_DEFAULT_SAMPLE_PERIOD_MS;

static uint16_t s_debounce_ms =
    LDR_DEFAULT_DEBOUNCE_MS;

static uint32_t s_last_sample_ms = 0;

static bool s_error = false;

/**
 * Apply hysteresis to one sensor.
 *
 * When clear:
 *   raw <= covered threshold means potentially covered.
 *
 * When covered:
 *   raw must rise to the clear threshold before becoming clear again.
 */
static bool calculate_candidate(const ldr_runtime_t *sensor)
{
    if (sensor->stable_covered) {
        return sensor->raw < sensor->clear_threshold;
    }

    return sensor->raw <= sensor->covered_threshold;
}

static void update_sensor(ldr_id_t id, uint32_t now)
{
    ldr_runtime_t *sensor = &s_ldr[id];
    bool candidate;

    sensor->raw = hal_ldr_read(id);

    if (!hal_ldr_last_read_ok(id)) {
        s_error = true;
        return;
    }

    candidate = calculate_candidate(sensor);

    /*
     * The possible state changed, restart debounce timer
     */
    if (candidate != sensor->candidate_covered) {
        sensor->candidate_covered = candidate;
        sensor->candidate_since_ms = now;
        return;
    }

    /*
     * Accept the change only after it remains stable for the full debounce
     * period.
     */
    if (candidate != sensor->stable_covered &&
        (now - sensor->candidate_since_ms) >= s_debounce_ms) {

        sensor->stable_covered = candidate;
        sensor->last_change_ms = now;

        if (candidate) {
            sensor->covered_event = true;
        } else {
            sensor->clear_event = true;
        }
    }
}

bool ldr_init(const ldr_config_t *config)
{
    static const ldr_config_t defaults = {
        .ldr1_covered_threshold = LDR_DEFAULT_COVERED_THRESHOLD,
        .ldr1_clear_threshold = LDR_DEFAULT_CLEAR_THRESHOLD,

        .ldr2_covered_threshold = LDR_DEFAULT_COVERED_THRESHOLD,
        .ldr2_clear_threshold = LDR_DEFAULT_CLEAR_THRESHOLD,

        .sample_period_ms = LDR_DEFAULT_SAMPLE_PERIOD_MS,
        .debounce_ms = LDR_DEFAULT_DEBOUNCE_MS
    };

    const ldr_config_t *selected =
        config != NULL ? config : &defaults;

    memset(s_ldr, 0, sizeof s_ldr);

    s_ldr[LDR_1].covered_threshold =
        selected->ldr1_covered_threshold;

    s_ldr[LDR_1].clear_threshold =
        selected->ldr1_clear_threshold;

    s_ldr[LDR_2].covered_threshold =
        selected->ldr2_covered_threshold;

    s_ldr[LDR_2].clear_threshold =
        selected->ldr2_clear_threshold;

    s_sample_period_ms =
        selected->sample_period_ms == 0u
        ? 1u
        : selected->sample_period_ms;

    s_debounce_ms = selected->debounce_ms;

    s_last_sample_ms = 0;
    s_error = !hal_ldr_init();

    /*
     * Take initial readings without producing passage events.
     */
    if (!s_error) {
        uint32_t now = hal_millis();

        for (uint8_t i = 0; i < 2u; i++) {
            ldr_id_t id = (ldr_id_t)i;

            s_ldr[id].raw = hal_ldr_read(id);

            if (!hal_ldr_last_read_ok(id)) {
                s_error = true;
                break;
            }

            s_ldr[id].stable_covered =
                s_ldr[id].raw <= s_ldr[id].covered_threshold;

            s_ldr[id].candidate_covered =
                s_ldr[id].stable_covered;

            s_ldr[id].candidate_since_ms = now;
            s_ldr[id].last_change_ms = now;
        }
    }

    return !s_error;
}

void ldr_task(void)
{
    uint32_t now = hal_millis();

    if (s_last_sample_ms != 0u &&
        (now - s_last_sample_ms) < s_sample_period_ms) {
        return;
    }

    s_last_sample_ms = now;

    /*
     * The error represents the latest sampling cycle rather than permanently
     * latching one temporary ADC failure.
     */
    s_error = false;

    update_sensor(LDR_1, now);
    update_sensor(LDR_2, now);
}

uint16_t ldr_read_raw(ldr_id_t sensor)
{
    return s_ldr[sensor].raw;
}

bool ldr_is_covered(ldr_id_t sensor)
{
    return s_ldr[sensor].stable_covered;
}

bool ldr_any_covered(void)
{
    return ldr_is_covered(LDR_1) ||
           ldr_is_covered(LDR_2);
}

bool ldr_all_clear(void)
{
    return !ldr_any_covered();
}

bool ldr_became_covered(ldr_id_t sensor)
{
    bool event = s_ldr[sensor].covered_event;

    s_ldr[sensor].covered_event = false;

    return event;
}

bool ldr_became_clear(ldr_id_t sensor)
{
    bool event = s_ldr[sensor].clear_event;

    s_ldr[sensor].clear_event = false;

    return event;
}

sys_direction_t ldr_take_request_direction(void)
{
    bool ldr1_event = s_ldr[LDR_1].covered_event;
    bool ldr2_event = s_ldr[LDR_2].covered_event;

    sys_direction_t direction = DIR_NONE;

    if (ldr1_event && !ldr2_event) {
        direction = DIR_ENTRY;
    }
    else if (ldr2_event && !ldr1_event) {
        direction = DIR_EXIT;
    }
    else if (ldr1_event && ldr2_event) {
        /*
         * Both events may be waiting when this function runs. Compare the
         * accepted debounce timestamps to determine which happened first.
         */
        if (s_ldr[LDR_1].last_change_ms <
            s_ldr[LDR_2].last_change_ms) {

            direction = DIR_ENTRY;
        }
        else if (s_ldr[LDR_2].last_change_ms <
                 s_ldr[LDR_1].last_change_ms) {

            direction = DIR_EXIT;
        }
    }

    /*
     * Consume both request events so one cover cannot start two sequences.
     */
    s_ldr[LDR_1].covered_event = false;
    s_ldr[LDR_2].covered_event = false;

    return direction;
}

void ldr_clear_events(void)
{
    s_ldr[LDR_1].covered_event = false;
    s_ldr[LDR_1].clear_event = false;

    s_ldr[LDR_2].covered_event = false;
    s_ldr[LDR_2].clear_event = false;
}

bool ldr_has_error(void)
{
    return s_error;
}

void ldr_set_thresholds(
    ldr_id_t sensor,
    uint16_t covered_threshold,
    uint16_t clear_threshold)
{
    /*
     * A gap between the thresholds provides hysteresis.
     */
    if (covered_threshold >= clear_threshold) {
        return;
    }

    s_ldr[sensor].covered_threshold = covered_threshold;
    s_ldr[sensor].clear_threshold = clear_threshold;
}
