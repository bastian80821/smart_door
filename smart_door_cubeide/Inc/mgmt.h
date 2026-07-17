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

void mgmt_init(void);   /* draw the idle screen once at startup */
void mgmt_task(void);   /* advance the interface; call every loop */

#endif /* MGMT_H */
