/**
 * mgmt.h
 * -----------------------------------------------------------------------------
 * Management / admin interface: the LCD + keypad menu the building manager uses
 * to configure the scheduled unlock (start time + duration) and the feedback
 * theme. Everything it changes is written through config.h; the door and alarm
 * modules read those settings from there.
 *
 * Non-blocking: call mgmt_task() once per iteration of the main loop. It never
 * waits on input, so the door/alarm/auth tasks keep running alongside it.
 * -----------------------------------------------------------------------------
 */
#ifndef MGMT_H
#define MGMT_H

#include <stdbool.h>

void mgmt_init(void);   /* draw the idle screen once at startup */
void mgmt_task(void);   /* advance the interface; call every loop */

/**
 * Change the normal non-admin idle message.
 * The existing admin screen still has priority while admin mode is active.
 */
void mgmt_set_idle_message(const char *top, const char *bottom);

/**
 * Temporarily give a system message priority over the menu and auth notices.
 */
void mgmt_set_system_message(const char *top, const char *bottom);

/** Remove the temporary message and return to the normal idle display. */
void mgmt_clear_system_message(void);


void mgmt_set_critical_message(const char *top, const char *bottom);

/**
 * Manual reset request.
 * While a system message is active, pressing '#' creates a one-shot reset
 * request for the passage controller.
 */
bool mgmt_take_reset_request(void);

#endif /* MGMT_H */
