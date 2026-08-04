/**
 * lcd_feedback.h
 * -----------------------------------------------------------------------------
 * Access-control LCD messages.
 *
 * This module does not write to the LCD hardware directly. It sends display
 * requests to mgmt.c so the management module remains the single LCD owner.
 * -----------------------------------------------------------------------------
 */
#ifndef LCD_FEEDBACK_H
#define LCD_FEEDBACK_H

#include "system_state.h"

void lcd_feedback_init(void);
void lcd_feedback_task(void);

void lcd_feedback_show_idle(void);
void lcd_feedback_show_waiting(sys_direction_t direction);
void lcd_feedback_show_granted(sys_direction_t direction);
void lcd_feedback_show_denied(void);
void lcd_feedback_show_unauthorized(void);
void lcd_feedback_show_system_error(void);
void lcd_feedback_show_open(sys_direction_t direction);
void lcd_feedback_show_open(sys_direction_t direction);
void lcd_feedback_show_scheduled(void);

#endif /* LCD_FEEDBACK_H */
