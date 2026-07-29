/**
 * feedback.h
 * -----------------------------------------------------------------------------
 * Non-blocking LED-bar and buzzer feedback.
 *
 * The LED bar contains 16 LEDs:
 *
 *   bit 0  = D5
 *   bit 15 = D20
 *
 * Call feedback_task() repeatedly from the main loop.
 * -----------------------------------------------------------------------------
 */
#ifndef FEEDBACK_H
#define FEEDBACK_H

#include <stdbool.h>
#include "system_state.h"

typedef enum {
    FEEDBACK_IDLE = 0,
    FEEDBACK_WAITING_FOR_NFC,
    FEEDBACK_GRANTED_ENTRY,
    FEEDBACK_GRANTED_EXIT,
    FEEDBACK_ACCESS_DENIED,
    FEEDBACK_UNAUTHORIZED_ALARM,
    FEEDBACK_SYSTEM_ERROR
} feedback_mode_t;

void feedback_init(void);

/** Start a new LED and buzzer pattern. */
void feedback_set_mode(feedback_mode_t mode);

/** Continue the current pattern without blocking the main loop. */
void feedback_task(void);

/** True when a finite pattern such as Access Denied has finished. */
bool feedback_is_complete(void);

feedback_mode_t feedback_get_mode(void);

#endif /* FEEDBACK_H */