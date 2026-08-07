/**
 * ldr.h
 * -----------------------------------------------------------------------------
 * Debounced two-LDR passage detection.
 *
 * Physical mapping:
 *
 *   LDR_1 = LDR_OUTER
 *   LDR_2 = LDR_INNER
 *
 * -----------------------------------------------------------------------------
 */
#ifndef LDR_H
#define LDR_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_wrapper.h"
#include "system_state.h"

#define LDR_1 LDR_OUTER
#define LDR_2 LDR_INNER

typedef struct {
    uint16_t ldr1_covered_threshold;
    uint16_t ldr1_clear_threshold;

    uint16_t ldr2_covered_threshold;
    uint16_t ldr2_clear_threshold;

    uint16_t sample_period_ms;
    uint16_t debounce_ms;
} ldr_config_t;

/*
 * Calibrated threshhold values (UNSW law library)
 */
#define LDR_DEFAULT_COVERED_THRESHOLD  700u
#define LDR_DEFAULT_CLEAR_THRESHOLD    1100u
#define LDR_DEFAULT_SAMPLE_PERIOD_MS     10u
#define LDR_DEFAULT_DEBOUNCE_MS           50u

bool ldr_init(const ldr_config_t *config);
void ldr_task(void);

uint16_t ldr_read_raw(ldr_id_t sensor);

bool ldr_is_covered(ldr_id_t sensor);
bool ldr_any_covered(void);
bool ldr_all_clear(void);

bool ldr_became_covered(ldr_id_t sensor);
bool ldr_became_clear(ldr_id_t sensor);

/** Determine direction from the first LDR that became covered. */
sys_direction_t ldr_take_request_direction(void);

void ldr_clear_events(void);

bool ldr_has_error(void);

void ldr_set_thresholds(
    ldr_id_t sensor,
    uint16_t covered_threshold,
    uint16_t clear_threshold
);

#endif /* LDR_H */
