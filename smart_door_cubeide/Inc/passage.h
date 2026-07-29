/**
 * passage.h
 * -----------------------------------------------------------------------------
 * Coordinates the existing authentication, system state, LDR, motor, LCD and
 * feedback modules.
 * -----------------------------------------------------------------------------
 */
#ifndef PASSAGE_H
#define PASSAGE_H

#include <stdbool.h>

void passage_init(void);
void passage_task(void);

/**
 * Clear SYS_ALARM or SYS_ERROR after manual inspection.
 *
 * The same function is triggered by pressing '#' while the alarm/error message
 * is displayed.
 */
bool passage_manual_reset(void);

#endif /* PASSAGE_H */
