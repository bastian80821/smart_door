/**
 * hal_wrapper.c
 * -----------------------------------------------------------------------------
 * Hardware layer behind hal_wrapper.h. This is the only file that touches the
 * HAL / registers directly.
 *
 * Status: the LCD is implemented (HD44780, 4-bit mode). The remaining peripherals
 * are still safe stubs and get filled in as each is brought up on the bench.
 * -----------------------------------------------------------------------------
 */
#include "hal_wrapper.h"
#include "main.h"   /* STM32 HAL + CubeMX-generated handles */

/* ---- Pin map (coaST board) -----------------------------------------------
 * Kept local so feature code never sees a pin number. Mirrors the README table.
 * Data lines D4..D7 are PC8..PC11 (contiguous, same port).                    */
#define LCD_RS_PORT   GPIOA
#define LCD_RS_PIN    GPIO_PIN_15
#define LCD_RW_PORT   GPIOC
#define LCD_RW_PIN    GPIO_PIN_6
#define LCD_E_PORT    GPIOD
#define LCD_E_PIN     GPIO_PIN_2
#define LCD_DATA_PORT GPIOC
#define LCD_D4_PIN    GPIO_PIN_8
#define LCD_D5_PIN    GPIO_PIN_9
#define LCD_D6_PIN    GPIO_PIN_10
#define LCD_D7_PIN    GPIO_PIN_11

/* ---- Microsecond delay via the DWT cycle counter --------------------------
 * HAL_Delay is millisecond-only; the HD44780 enable strobe needs sub-ms timing.
 * DWT->CYCCNT counts core clocks and scales off SystemCoreClock, so the timing
 * stays correct even if the clock tree changes later.                         */
static void dwt_delay_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { }
}

/* ---- HD44780 primitives --------------------------------------------------- */
static void lcd_put_nibble(uint8_t nibble) {
    HAL_GPIO_WritePin(LCD_DATA_PORT, LCD_D4_PIN, (nibble >> 0) & 1);
    HAL_GPIO_WritePin(LCD_DATA_PORT, LCD_D5_PIN, (nibble >> 1) & 1);
    HAL_GPIO_WritePin(LCD_DATA_PORT, LCD_D6_PIN, (nibble >> 2) & 1);
    HAL_GPIO_WritePin(LCD_DATA_PORT, LCD_D7_PIN, (nibble >> 3) & 1);

    HAL_GPIO_WritePin(LCD_E_PORT, LCD_E_PIN, GPIO_PIN_SET);
    delay_us(1);                 /* enable pulse width >= 450 ns */
    HAL_GPIO_WritePin(LCD_E_PORT, LCD_E_PIN, GPIO_PIN_RESET);
    delay_us(1);
}

/* rs = 0 -> command, rs = 1 -> character data. High nibble first. */
static void lcd_send(uint8_t value, uint8_t rs) {
    HAL_GPIO_WritePin(LCD_RS_PORT, LCD_RS_PIN, rs ? GPIO_PIN_SET : GPIO_PIN_RESET);
    lcd_put_nibble(value >> 4);
    lcd_put_nibble(value & 0x0F);
    delay_us(50);                /* most instructions settle in ~37 us */
}

static void lcd_command(uint8_t cmd) { lcd_send(cmd, 0); }
static void lcd_data(uint8_t ch)     { lcd_send(ch, 1); }

/* ---- Public LCD interface ------------------------------------------------- */
void hal_lcd_init(void) {
    dwt_delay_init();
    HAL_GPIO_WritePin(LCD_RW_PORT, LCD_RW_PIN, GPIO_PIN_RESET);  /* we only ever write */

    HAL_Delay(40);               /* let Vcc settle after power-on */

    /* Reset ritual: force 8-bit three times, then drop to 4-bit (datasheet p.46). */
    lcd_put_nibble(0x3); HAL_Delay(5);
    lcd_put_nibble(0x3); delay_us(150);
    lcd_put_nibble(0x3); delay_us(150);
    lcd_put_nibble(0x2); delay_us(50);      /* now in 4-bit mode */

    lcd_command(0x28);           /* 4-bit interface, 2 lines, 5x8 font */
    lcd_command(0x08);           /* display off */
    lcd_command(0x01); HAL_Delay(2);   /* clear (needs ~1.52 ms) */
    lcd_command(0x06);           /* entry mode: cursor increments, no shift */
    lcd_command(0x0C);           /* display on, cursor off, blink off */
}

void hal_lcd_clear(void) {
    lcd_command(0x01);
    HAL_Delay(2);
}

/* Writes text to a row (0 = top, 1 = bottom) and pads the rest of the row with
 * spaces, so a shorter string never leaves stale characters behind. */
void hal_lcd_write(uint8_t row, const char *text) {
    lcd_command(row == 0 ? 0x80 : 0xC0);    /* DDRAM address of the row start */
    uint8_t i = 0;
    while (i < 16 && text[i]) { lcd_data((uint8_t)text[i]); i++; }
    while (i < 16)            { lcd_data(' ');               i++; }
}

void hal_lcd_define_char(uint8_t slot, const uint8_t rows[8]) {
    lcd_command(0x40 | ((slot & 0x07) << 3)); /* CGRAM base for this glyph slot */
    for (uint8_t i = 0; i < 8; i++) {
        lcd_data(rows[i]);
    }
    lcd_command(0x80);           /* leave the address counter back in DDRAM */
}

/* ==== Everything below is still a stub -- filled in as hardware comes up ==== */

/* ---- Card reader (PN532) ---- */
bool hal_reader_init(void) { return true; }
bool hal_reader_poll(card_uid_t *uid) { (void)uid; return false; }
bool hal_reader_read_sector(uint8_t sector, const uint8_t key[6], uint8_t block_out[16]) {
    (void)sector; (void)key; (void)block_out; return false;
}

/* ---- Light sensors (high reading = clear) ---- */
uint16_t hal_ldr_read(ldr_id_t sensor) { (void)sensor; return 4000; }

/* ---- Stepper / door ---- */
void hal_motor_set(motor_cmd_t cmd) { (void)cmd; }
bool hal_motor_done(void) { return true; }

/* ---- LED bar + direct LEDs ---- */
void hal_led_bar_set(uint32_t mask) { (void)mask; }
void hal_direct_led(uint8_t id, bool on) { (void)id; (void)on; }

/* ---- Buzzer ---- */
void hal_buzzer_tone(uint16_t freq_hz) { (void)freq_hz; }

/* ---- Keypad ---- */
char hal_keypad_scan(void) { return '\0'; }

/* ---- Timebase ---- */
uint32_t hal_millis(void) { return HAL_GetTick(); }
