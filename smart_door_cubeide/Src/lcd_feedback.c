/**
 * lcd_feedback.c
 * -----------------------------------------------------------------------------
 * LCD messages for the passage and alarm system.
 *
 * mgmt.c remains the only direct LCD owner.
 * -----------------------------------------------------------------------------
 */
#include "lcd_feedback.h"
#include "mgmt.h"

void lcd_feedback_init(void)
{
    /*
     * Replace only the normal idle text.
     *
     * The existing admin-mode display remains unchanged.
     */
    mgmt_set_idle_message("System Idle", "");
}

void lcd_feedback_task(void)
{
    /*
     * No hardware update is required here.
     *
     * mgmt_task() performs the actual LCD writes.
     */
}

void lcd_feedback_show_idle(void)
{
    mgmt_clear_system_message();
}

void lcd_feedback_show_waiting(sys_direction_t direction)
{
    mgmt_set_system_message(
        "Scan NFC Card",
        direction == DIR_ENTRY ? "Entering" : "Exiting"
    );
}

void lcd_feedback_show_granted(sys_direction_t direction)
{
    mgmt_set_system_message(
        "Access Granted",
        direction == DIR_ENTRY ? "Entering" : "Exiting"
    );
}

void lcd_feedback_show_denied(void)
{
    mgmt_set_system_message("Access Denied", "");
}

void lcd_feedback_show_unauthorized(void)
{
    /*
     * The complete message is split across the 16x2 display.
     */
    mgmt_set_system_message("Unauthorized", "Access");
}

void lcd_feedback_show_system_error(void)
{
    mgmt_set_system_message("System Error", "# = reset");
}
