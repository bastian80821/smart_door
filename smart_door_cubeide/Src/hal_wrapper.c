/**
 * hal_wrapper.c  -- STUB IMPLEMENTATION
 * -----------------------------------------------------------------------------
 * Placeholder bodies so the whole project LINKS and runs from the first clone,
 * before any pin is wired or the .ioc is even finished. Every function returns a
 * calm, safe default: no card present, sensors read CLEAR, motor reports DONE,
 * no keypress, LEDs/buzzer/LCD do nothing. The system therefore sits quietly in
 * SYS_IDLE instead of hanging or false-alarming.
 *
 * Replace these bodies one at a time with real HAL code as hardware comes up.
 * This is the ONLY .c file allowed to include the HAL / touch registers.
 * -----------------------------------------------------------------------------
 */
#include "hal_wrapper.h"
#include "main.h"   /* pulls the STM32 HAL + CubeMX-generated handles */

/* ---- Card reader (PN532) ---- */
bool hal_reader_init(void) { return true; }
bool hal_reader_poll(card_uid_t *uid) { (void)uid; return false; }          /* no card */
bool hal_reader_read_sector(uint8_t sector, const uint8_t key[6], uint8_t block_out[16]) {
    (void)sector; (void)key; (void)block_out; return false;
}

/* ---- Light sensors ----
 * Higher = brighter. A clear (uncovered) LDR sees light -> high reading, so we
 * return a high value: the safe-open guard sees "nobody there" and does not block. */
uint16_t hal_ldr_read(ldr_id_t sensor) { (void)sensor; return 4000; }       /* CLEAR */

/* ---- Stepper / door ----
 * done == true means the state machine never waits on a motion that will never
 * finish, so transitions complete instantly in stub phase instead of stalling. */
void hal_motor_set(motor_cmd_t cmd) { (void)cmd; }
bool hal_motor_done(void) { return true; }

/* ---- LED bar + direct LEDs ---- */
void hal_led_bar_set(uint32_t mask) { (void)mask; }
void hal_direct_led(uint8_t id, bool on) { (void)id; (void)on; }

/* ---- Buzzer ---- */
void hal_buzzer_tone(uint16_t freq_hz) { (void)freq_hz; }

/* ---- Keypad ---- */
char hal_keypad_scan(void) { return '\0'; }                                  /* no key */

/* ---- LCD ---- */
void hal_lcd_init(void) {}
void hal_lcd_clear(void) {}
void hal_lcd_write(uint8_t row, const char *text) { (void)row; (void)text; }
void hal_lcd_define_char(uint8_t slot, const uint8_t rows[8]) { (void)slot; (void)rows; }

/* ---- Timebase ----
 * Real even in stub phase: HAL_GetTick() works from HAL_Init() onward, no pins
 * needed. Debounce/animation timing is therefore usable immediately. */
uint32_t hal_millis(void) { return HAL_GetTick(); }
