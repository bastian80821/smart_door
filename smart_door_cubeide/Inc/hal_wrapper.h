/**
 * hal_wrapper.h
 * -----------------------------------------------------------------------------
 * A thin layer between our modules and the STM32 HAL / coaST board hardware.
 *
 * BOARD: Nucleo-F303RE on the UNSW coaST carrier.
 *
 * THE RULE FOR THE WHOLE TEAM:
 *   Feature code (auth, door, alarm, management) NEVER touches GPIO/ADC/timer
 *   registers or HAL_* calls directly. It calls the functions below instead.
 *   All hardware detail lives behind this header, in hal_wrapper.c.
 *
 * WHY:
 *   1. Everyone codes against these signatures today, even before wiring, by
 *      using stubbed implementations.
 *   2. If a pin/peripheral detail changes, only hal_wrapper.c changes -- never
 *      the four feature modules. This buffers the .ioc from the rest of us.
 *   3. Clean, reusable functions are explicitly rewarded in code-quality marks.
 *
 * The actual pins are fixed by the coaST board (see PIN TABLE in the README).
 * Feature authors never need to know which pin is which; they use these calls.
 * -----------------------------------------------------------------------------
 */
#ifndef HAL_WRAPPER_H
#define HAL_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>

/* ==== Card reader (PN532 add-on, auth module) ============================== */
/* NOTE: the PN532 is NOT a coaST component -- it is our add-on. It is the only
 * device whose pins we choose (a free I2C/SPI/UART on the Nucleo headers).
 * Confirm the bus in the .ioc; this interface is bus-agnostic on purpose.      */

typedef struct {
    uint8_t bytes[7];   /* MIFARE Classic UID = 4 bytes; 7-byte UIDs also exist */
    uint8_t length;     /* 4 or 7 */
} card_uid_t;

bool hal_reader_init(void);
bool hal_reader_poll(card_uid_t *uid);   /* non-blocking; call each loop */
bool hal_reader_read_sector(const card_uid_t *uid, uint8_t block,
                            const uint8_t key[6], uint8_t block_out[16]);
/** Authenticate to `block` with key A and write 16 bytes (enrolment). Refuses
 *  sector-trailer blocks and sector 0. Card must be selected (polled) first. */
bool hal_reader_write_sector(const card_uid_t *uid, uint8_t block,
                             const uint8_t key[6], const uint8_t data[16]);


/* ==== Light sensors (LDRs, door module) ==================================== */
/* Two analog LDRs on ADC pins: PC4 (R24) and PB1 (R32).
 * TEAM TODO: physically determine which is OUTER (before door) vs INNER
 * (after door) and keep that mapping fixed in hal_wrapper.c -- all direction
 * and tailgating logic depends on it. */

typedef enum { LDR_OUTER, LDR_INNER } ldr_id_t;

/** Raw 12-bit ADC reading (0..4095). Higher = brighter. The door module
 *  compares against a calibrated threshold to decide covered vs clear. */
uint16_t hal_ldr_read(ldr_id_t sensor);

/* ==== Stepper motor / door (door module) =================================== */
/* Stepper with 4 coils: PC5/PA12/PC7/PB2 (A/B/C/D). Direction = the order the
 * coil sequence is stepped. The HAL owns the stepping; the door module just
 * asks for a high-level action and does not care that it is a stepper. */

typedef enum { MOTOR_STOP, MOTOR_OPEN_ENTRY, MOTOR_OPEN_EXIT, MOTOR_CLOSE } motor_cmd_t;

/** Request a motion. Non-blocking: the HAL steps the coils over time (timer
 *  driven) so this does not stall the main loop. */
void hal_motor_set(motor_cmd_t cmd);

/** True once the most recent motion command has finished (door fully open/shut).
 *  The door state machine polls this to know when a transition is complete. */
bool hal_motor_done(void);

/* ==== LED bar (alarm/feedback module) ====================================== */
/* IMPORTANT: the bar is driven by a SHIFT REGISTER, not direct GPIO.
 * Three control pins: RCLK/latch PB7, SRCLK/clock PC0, SER/data PB15.
 * hal_led_bar_set() shifts the mask out; feature code just supplies the mask. */

#define LED_BAR_SEGMENTS 8   /* set to the board's actual segment count */

/** Set the whole bar from a bitmask (bit i -> segment i). Animations are made
 *  by calling this repeatedly over time. */
void hal_led_bar_set(uint32_t mask);

/* Optional: 4 DIRECT LEDs (PB10/PB4/PB5/PB3), independent of the shift-reg bar.
 * Handy for simple status without disturbing the bar animation. id = 0..3.     */
void hal_direct_led(uint8_t id, bool on);

/* ==== Buzzer (alarm/feedback module) ======================================= */
/* PWM buzzer on PC2. Frequency control is what makes the themed single-tone
 * contours possible. The buzzer is MONOPHONIC -- one frequency at a time.      */

/** Play a tone at freq_hz. freq_hz = 0 silences it. */
void hal_buzzer_tone(uint16_t freq_hz);

/* ==== Keypad 4x4 (management module) ======================================= */
/* Matrix: rows PB11-PB14, cols PA8-PA11. Debounced inside the wrapper. */

/** Returns the pressed key ('0'-'9','A'-'D','*','#') or '\0' if none. */
char hal_keypad_scan(void);

/* ==== LCD 16x2 HD44780, 4-bit mode (management module) ===================== */
/* Data D4-D7 on PC8-PC11; control E=PD2, RW=PC6, RS=PA15.
 * hal_lcd_* hide the HD44780 instruction set (see datasheet 4-bit init).       */

void hal_lcd_init(void);
void hal_lcd_clear(void);
/** Write text to a row (0 = top, 1 = bottom), truncated at 16 chars.
 *  Convention: top row = info/prompt, bottom row = input. */
void hal_lcd_write(uint8_t row, const char *text);
/** Load a custom 5x8 glyph (slots 0..7) for themed LCD touches. */
void hal_lcd_define_char(uint8_t slot, const uint8_t rows[8]);

/* ==== Timebase (shared) ==================================================== */

/** Milliseconds since boot. Use for the 5 s door timer, debounce and animation
 *  timing instead of HAL_Delay, so nothing blocks the main loop (responsiveness
 *  marks). */
uint32_t hal_millis(void);

#endif /* HAL_WRAPPER_H */
