/**
 * hal_wrapper.c
 * -----------------------------------------------------------------------------
 * Hardware layer behind hal_wrapper.h. Only file that touches the HAL / registers directly.
 * -----------------------------------------------------------------------------
 */
#include "hal_wrapper.h"
#include "main.h"   /* STM32 HAL + CubeMX-generated handles */
#include "i2c.h"
#include "adc.h"
#include "tim.h"
#include "stm32f3xx_hal_adc_ex.h"
#include <stdint.h>
#include <stdbool.h>

/* ---- Pin map -----------------------------------------------
 * Kept local so feature code never sees a pin number.
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
    delay_us(50);                /* most instructions settle in around 37 us */
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


/* ==== Card reader (PN532 I2C) ========================================
 * Protocol: command frame -> ACK frame -> response frame. Each I2C read is
 * prefixed by a status byte (bit0 = 1 means "ready"). Addr 0x24 on hi2c1.   */

#define PN532_ADDR      (0x24 << 1)   /* HAL uses 8-bit addresses */
#define PN532_PREAMBLE  0x00
#define PN532_STARTCODE 0xFF
#define PN532_HOSTTOPN  0xD4          /* TFI: host -> PN532 */
#define PN532_PNTOHOST  0xD5          /* TFI: PN532 -> host */

/* Send a command frame: [00 00 FF LEN LCS D4 cmd... DCS 00] */
static bool pn532_write_cmd(const uint8_t *cmd, uint8_t clen) {
    uint8_t buf[32];
    uint8_t i = 0;
    buf[i++] = PN532_PREAMBLE;
    buf[i++] = PN532_PREAMBLE;
    buf[i++] = PN532_STARTCODE;
    uint8_t len = clen + 1;                 /* +1 for the TFI byte */
    buf[i++] = len;
    buf[i++] = (uint8_t)(~len + 1);         /* length checksum */
    buf[i++] = PN532_HOSTTOPN;
    uint8_t sum = PN532_HOSTTOPN;
    for (uint8_t j = 0; j < clen; j++) { buf[i++] = cmd[j]; sum += cmd[j]; }
    buf[i++] = (uint8_t)(~sum + 1);         /* data checksum */
    buf[i++] = PN532_PREAMBLE;
    return HAL_I2C_Master_Transmit(&hi2c1, PN532_ADDR, buf, i, 100) == HAL_OK;
}

/* Poll the status byte until the PN532 signals ready, or time out. */
static bool pn532_wait_ready(uint32_t timeout_ms) {
    uint8_t status = 0;
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (HAL_I2C_Master_Receive(&hi2c1, PN532_ADDR, &status, 1, 10) == HAL_OK
            && (status & 1)) {
            return true;
        }
        HAL_Delay(1);
    }
    return false;
}

/* Read status byte + n frame bytes. Caller sizes buf to n+1. */
static bool pn532_read_frame(uint8_t *buf, uint8_t n) {
    return HAL_I2C_Master_Receive(&hi2c1, PN532_ADDR, buf, (uint16_t)(n + 1), 100) == HAL_OK;
}

/* The 6-byte ACK the PN532 sends after accepting a command. */
static bool pn532_read_ack(void) {
    static const uint8_t ack[] = {0x00,0x00,0xFF,0x00,0xFF,0x00};
    uint8_t buf[7];
    if (!pn532_read_frame(buf, 6)) return false;
    for (uint8_t i = 0; i < 6; i++) if (buf[i + 1] != ack[i]) return false;
    return true;
}

bool hal_reader_init(void)
{
    /*
     * PN532 power-up delay
     */
    HAL_Delay(100);

    /*
     * First check whether any device acknowledges address 0x24.
     */
    if (HAL_I2C_IsDeviceReady(
            &hi2c1,
            PN532_ADDR,
            3,
            100) != HAL_OK) {
        return false;
    }

    uint8_t cmd = 0x02; /* GetFirmwareVersion */

    if (!pn532_write_cmd(&cmd, 1)) {
        return false;
    }

    if (!pn532_wait_ready(200)) {
        return false;
    }

    if (!pn532_read_ack()) {
        return false;
    }

    if (!pn532_wait_ready(200)) {
        return false;
    }

    uint8_t resp[16];

    if (!pn532_read_frame(resp, 13)) {
        return false;
    }

    uint8_t sam[] = {
        0x14, 0x01, 0x00, 0x00
    };

    if (!pn532_write_cmd(sam, sizeof sam)) {
        return false;
    }

    if (!pn532_wait_ready(200)) {
        return false;
    }

    if (!pn532_read_ack()) {
        return false;
    }

    if (!pn532_wait_ready(200)) {
        return false;
    }

    uint8_t sresp[10];

    return pn532_read_frame(sresp, 8);
}

bool hal_reader_poll(card_uid_t *uid) {
    uint8_t cmd[] = {0x4A, 0x01, 0x00};       /* InListPassiveTarget: 1 target, type A */
    if (!pn532_write_cmd(cmd, sizeof cmd)) return false;
    if (!pn532_wait_ready(50) || !pn532_read_ack()) return false;
    if (!pn532_wait_ready(40)) return false;  /* no card -> no reply soon */

    uint8_t r[30];
    if (!pn532_read_frame(r, 25)) return false;

    /* Locate D5 4B in the response, then read NFCID length + bytes. */
    for (uint8_t i = 0; i < 20; i++) {
        if (r[i] == PN532_PNTOHOST && r[i + 1] == 0x4B) {
            uint8_t nbtg = r[i + 2];
            if (nbtg < 1) return false;
            uint8_t id_len = r[i + 7];
            if (id_len == 0 || id_len > 7) return false;
            uid->length = id_len;
            for (uint8_t k = 0; k < id_len; k++) uid->bytes[k] = r[i + 8 + k];
            return true;
        }
    }
    return false;
}

/* ==== MIFARE sector authenticate + read =====================================
 * InDataExchange(0x40): first a MIFARE Authenticate (0x60 = key A) for the
 * target block with the 6-byte key + card UID, then a MIFARE Read (0x30) of
 * that block. The card must already be selected (poll runs InListPassiveTarget).
 * ========================================================================== */
static bool pn532_mifare_read(uint8_t block, const uint8_t key[6],
                              const uint8_t *uid, uint8_t uid_len,
                              uint8_t block_out[16]) {
    uint8_t a[18];
    uint8_t n = 0;
    a[n++] = 0x40;              /* InDataExchange */
    a[n++] = 0x01;              /* target #1 */
    a[n++] = 0x60;              /* MIFARE Authenticate, key A */
    a[n++] = block;
    for (uint8_t i = 0; i < 6; i++) a[n++] = key[i];
    for (uint8_t i = 0; i < uid_len && i < 7; i++) a[n++] = uid[i];
    if (!pn532_write_cmd(a, n)) return false;
    if (!pn532_wait_ready(50) || !pn532_read_ack()) return false;
    if (!pn532_wait_ready(80)) return false;
    uint8_t ar[16];
    if (!pn532_read_frame(ar, 12)) return false;
    bool auth_ok = false;
    for (uint8_t i = 0; i + 2 < 12; i++) {
        if (ar[i] == PN532_PNTOHOST && ar[i + 1] == 0x41) { auth_ok = (ar[i + 2] == 0x00); break; }
    }
    if (!auth_ok) return false;

    uint8_t rc[4];
    n = 0;
    rc[n++] = 0x40; rc[n++] = 0x01; rc[n++] = 0x30; rc[n++] = block;   /* MIFARE Read */
    if (!pn532_write_cmd(rc, n)) return false;
    if (!pn532_wait_ready(50) || !pn532_read_ack()) return false;
    if (!pn532_wait_ready(80)) return false;
    uint8_t rr[32];
    if (!pn532_read_frame(rr, 28)) return false;
    for (uint8_t i = 0; i + 2 < 28; i++) {
        if (rr[i] == PN532_PNTOHOST && rr[i + 1] == 0x41) {
            if (rr[i + 2] != 0x00) return false;
            for (uint8_t k = 0; k < 16; k++) block_out[k] = rr[i + 3 + k];
            return true;
        }
    }
    return false;
}

bool hal_reader_read_sector(const card_uid_t *uid, uint8_t block,
                            const uint8_t key[6], uint8_t block_out[16]) {
    return pn532_mifare_read(block, key, uid->bytes, uid->length, block_out);
}


/* ==== MIFARE authenticate + write (enrolment) ===============================
 * Same auth as the read path, then MIFARE Write (0xA0) of 16 bytes. Refuses
 * sector-trailer blocks (3,7,11,...) and sector 0 so we can never overwrite the
 * keys and brick a card.
 * ========================================================================== */
static bool pn532_mifare_write(uint8_t block, const uint8_t key[6],
                               const uint8_t *uid, uint8_t uid_len,
                               const uint8_t data[16]) {
    if ((block & 0x03) == 0x03) return false;   /* trailer block -> refuse */
    if (block < 4)             return false;    /* keep out of sector 0 */

    uint8_t a[18];
    uint8_t n = 0;
    a[n++] = 0x40; a[n++] = 0x01; a[n++] = 0x60; a[n++] = block;   /* Authenticate key A */
    for (uint8_t i = 0; i < 6; i++) a[n++] = key[i];
    for (uint8_t i = 0; i < uid_len && i < 7; i++) a[n++] = uid[i];
    if (!pn532_write_cmd(a, n)) return false;
    if (!pn532_wait_ready(50) || !pn532_read_ack()) return false;
    if (!pn532_wait_ready(80)) return false;
    uint8_t ar[16];
    if (!pn532_read_frame(ar, 12)) return false;
    bool auth_ok = false;
    for (uint8_t i = 0; i + 2 < 12; i++)
        if (ar[i] == PN532_PNTOHOST && ar[i + 1] == 0x41) { auth_ok = (ar[i + 2] == 0x00); break; }
    if (!auth_ok) return false;

    uint8_t wc[20];
    n = 0;
    wc[n++] = 0x40; wc[n++] = 0x01; wc[n++] = 0xA0; wc[n++] = block;   /* MIFARE Write */
    for (uint8_t i = 0; i < 16; i++) wc[n++] = data[i];
    if (!pn532_write_cmd(wc, n)) return false;
    if (!pn532_wait_ready(50) || !pn532_read_ack()) return false;
    if (!pn532_wait_ready(80)) return false;
    uint8_t wr[16];
    if (!pn532_read_frame(wr, 12)) return false;
    for (uint8_t i = 0; i + 2 < 12; i++)
        if (wr[i] == PN532_PNTOHOST && wr[i + 1] == 0x41) return (wr[i + 2] == 0x00);
    return false;
}

bool hal_reader_write_sector(const card_uid_t *uid, uint8_t block,
                             const uint8_t key[6], const uint8_t data[16]) {
    return pn532_mifare_write(block, key, uid->bytes, uid->length, data);
}


/* ---- Light sensors (high reading = clear) ---- */
static bool s_ldr_read_ok[2] = {false, false};   /* true if the most recent reading from this sensor succeeded */

bool hal_ldr_init(void)
{
    bool success = true;

    if (HAL_ADCEx_Calibration_Start(
            &hadc2,
            ADC_SINGLE_ENDED) != HAL_OK) {
        success = false;
    }

    if (HAL_ADCEx_Calibration_Start(
            &hadc3,
            ADC_SINGLE_ENDED) != HAL_OK) {
        success = false;
    }

    return success;
}

uint16_t hal_ldr_read(ldr_id_t sensor)
{
    ADC_HandleTypeDef *adc =
        sensor == LDR_OUTER
        ? &hadc2
        : &hadc3;

    uint16_t result = 0;

    s_ldr_read_ok[sensor] = false;

    if (HAL_ADC_Start(adc) != HAL_OK) {
        return 0;
    }

    if (HAL_ADC_PollForConversion(adc, 2u) == HAL_OK) {
        result = (uint16_t)HAL_ADC_GetValue(adc);
        s_ldr_read_ok[sensor] = true;
    }

    (void)HAL_ADC_Stop(adc);

    return result;
}

bool hal_ldr_last_read_ok(ldr_id_t sensor)
{
    return s_ldr_read_ok[sensor];
}

/* ---- Stepper / door ------------------------------------------------------ */

/*
 *
 */
#define STEPPER_STEPS_PER_REVOLUTION 4096L

#define STEPPER_QUARTER_TURN_STEPS \
    (STEPPER_STEPS_PER_REVOLUTION / 4L)

#define STEPPER_STEP_INTERVAL_MS 2u

#define MOTOR_COIL_A_PORT GPIOC
#define MOTOR_COIL_A_PIN  GPIO_PIN_5

#define MOTOR_COIL_B_PORT GPIOA
#define MOTOR_COIL_B_PIN  GPIO_PIN_12

#define MOTOR_COIL_C_PORT GPIOC
#define MOTOR_COIL_C_PIN  GPIO_PIN_7

#define MOTOR_COIL_D_PORT GPIOB
#define MOTOR_COIL_D_PIN  GPIO_PIN_2

/*
 * Eight-stage half-step sequence.
 */
static const uint8_t s_step_sequence[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1}
};

static int32_t s_motor_position = 0;
static int32_t s_motor_target = 0;

static uint8_t s_motor_phase = 0;

static uint32_t s_last_motor_step_ms = 0;

static bool s_motor_active = false;

/** Write one half-step pattern to the four motor coils. */
static void motor_write_phase(uint8_t phase)
{
    HAL_GPIO_WritePin(MOTOR_COIL_A_PORT,MOTOR_COIL_A_PIN, 
        s_step_sequence[phase][0] ? GPIO_PIN_SET : GPIO_PIN_RESET
    );

    HAL_GPIO_WritePin(MOTOR_COIL_B_PORT, MOTOR_COIL_B_PIN,
        s_step_sequence[phase][1] ? GPIO_PIN_SET : GPIO_PIN_RESET
    );

    HAL_GPIO_WritePin(MOTOR_COIL_C_PORT, MOTOR_COIL_C_PIN,
        s_step_sequence[phase][2] ? GPIO_PIN_SET : GPIO_PIN_RESET
    );

    HAL_GPIO_WritePin(MOTOR_COIL_D_PORT, MOTOR_COIL_D_PIN,
        s_step_sequence[phase][3] ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
}

/** Release all motor coils. */
static void motor_coils_off(void)
{
    HAL_GPIO_WritePin(MOTOR_COIL_A_PORT, MOTOR_COIL_A_PIN,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_COIL_B_PORT, MOTOR_COIL_B_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_COIL_C_PORT, MOTOR_COIL_C_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_COIL_D_PORT, MOTOR_COIL_D_PIN, GPIO_PIN_RESET);
}

void hal_motor_set(motor_cmd_t cmd)
{
    switch (cmd) {
    case MOTOR_OPEN_ENTRY:
        /*
         * 90 degrees to the right for entry
         */
        s_motor_target = STEPPER_QUARTER_TURN_STEPS;
        break;

    case MOTOR_OPEN_EXIT:
        /*
         *  90 degrees to the right for exit
         */
        s_motor_target = -STEPPER_QUARTER_TURN_STEPS;
        break;

    case MOTOR_CLOSE:
        /*
         * Position zero is original closed position
         */
        s_motor_target = 0;
        break;

    case MOTOR_STOP:
    default:
        s_motor_target = s_motor_position;
        s_motor_active = false;
        motor_coils_off();
        return;
    }

    s_motor_active =
        s_motor_position != s_motor_target;

    s_last_motor_step_ms = hal_millis();
}

void hal_motor_task(void)
{
    uint32_t now;
    int8_t direction;

    if (!s_motor_active) {
        return;
    }

    now = hal_millis();

    if ((now - s_last_motor_step_ms) <
        STEPPER_STEP_INTERVAL_MS) {
        return;
    }

    s_last_motor_step_ms = now;

    direction =
        s_motor_target > s_motor_position
        ? 1
        : -1;

    /*
     * Adding seven is equivalent to subtracting one modulo eight.
     */
    s_motor_phase = (uint8_t)(
        (s_motor_phase +
         (direction > 0 ? 1u : 7u))
        & 0x07u
    );

    motor_write_phase(s_motor_phase);

    s_motor_position += direction;

    if (s_motor_position == s_motor_target) {
        s_motor_active = false;

        /*
         * Keep the coils energised while open to hold the door position.
         * Release them after returning to the closed position.
         */
        if (s_motor_target == 0) {
            motor_coils_off();
        }
    }
}

bool hal_motor_done(void)
{
    return !s_motor_active;
}

/* ---- LED bar + direct LEDs ---- */

#define LED_RCLK_PORT GPIOB
#define LED_RCLK_PIN  GPIO_PIN_7

#define LED_SRCLK_PORT GPIOC
#define LED_SRCLK_PIN  GPIO_PIN_0

#define LED_SER_PORT GPIOB
#define LED_SER_PIN  GPIO_PIN_15

/*
 * Reverses LED bar
 */
#define LED_BAR_REVERSE_ORDER 0

/*
 * Switch between active low/high
 */
#define LED_BAR_ACTIVE_LOW 0

static uint16_t reverse_led_bits(uint16_t value)
{
    uint16_t reversed = 0;

    for (uint8_t i = 0; i < 16u; i++) {
        reversed = (uint16_t)(
            (reversed << 1) |
            (value & 1u)
        );

        value >>= 1;
    }

    return reversed;
}

void hal_led_bar_set(uint32_t mask)
{
    uint16_t value = (uint16_t)mask;

#if LED_BAR_REVERSE_ORDER
    value = reverse_led_bits(value);
#else
    (void)reverse_led_bits;
#endif

#if LED_BAR_ACTIVE_LOW
    value = (uint16_t)~value;
#endif

    /* Hold the latch low while the 16 bits are shifted.*/
    HAL_GPIO_WritePin(LED_RCLK_PORT, LED_RCLK_PIN, GPIO_PIN_RESET);

    for (int8_t bit = 15; bit >= 0; bit--) {
        HAL_GPIO_WritePin(LED_SRCLK_PORT, LED_SRCLK_PIN, GPIO_PIN_RESET);

        HAL_GPIO_WritePin(LED_SER_PORT, LED_SER_PIN, 
            value & (uint16_t)(1u << bit) ? GPIO_PIN_SET : GPIO_PIN_RESET);

        HAL_GPIO_WritePin(LED_SRCLK_PORT, LED_SRCLK_PIN, GPIO_PIN_SET);
    }

    /* Transfer the shifted data to D5-D20. */
    HAL_GPIO_WritePin(LED_RCLK_PORT, LED_RCLK_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_RCLK_PORT, LED_RCLK_PIN, GPIO_PIN_RESET);
}

void hal_direct_led(uint8_t id, bool on)
{
    uint16_t pin;

    switch (id) {
    case 0:
        pin = GPIO_PIN_10; /* D1 */
        break;

    case 1:
        pin = GPIO_PIN_4;  /* D2 */
        break;

    case 2:
        pin = GPIO_PIN_5;  /* D3 */
        break;

    case 3:
        pin = GPIO_PIN_3;  /* D4 */
        break;

    default:
        return;
    }

    HAL_GPIO_WritePin(GPIOB, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---- Buzzer ---- */
static uint16_t s_current_buzzer_frequency = 0;

void hal_buzzer_tone(uint16_t frequency_hz){
    uint32_t timer_clock;
    uint32_t prescaler_division;
    uint32_t timer_tick;
    uint32_t period_counts;

    if (frequency_hz == s_current_buzzer_frequency) {
        return;
    }

    s_current_buzzer_frequency = frequency_hz;

    if (frequency_hz == 0u) {
        (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0u);

        return;
    }

    /*
     * TIM1 uses HCLK in the existing clock configuration.
     */
    timer_clock = HAL_RCC_GetHCLKFreq();

    /* Reduce the timer clock to approximately 1 MHz. */
    prescaler_division = timer_clock / 1000000u;

    if (prescaler_division == 0u) {
        prescaler_division = 1u;
    }

    timer_tick = timer_clock / prescaler_division;

    /*
     * PWM frequency:
     * frequency = timer tick / period
     * period = timer tick / frequency
     */
    period_counts = timer_tick / frequency_hz;

    if (period_counts < 2u) {
        period_counts = 2u;
    }

    if (period_counts > 65536u) {
        period_counts = 65536u;
    }

    __HAL_TIM_SET_PRESCALER(&htim1, prescaler_division - 1u);
    __HAL_TIM_SET_AUTORELOAD(&htim1, period_counts - 1u);

    /* Half the period provides approximately 50% duty cycle. */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, period_counts / 2u);
    __HAL_TIM_SET_COUNTER(&htim1, 0u);
    (void)HAL_TIM_GenerateEvent(&htim1, TIM_EVENTSOURCE_UPDATE);
    (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
}

/* ---- Keypad ---- */
/* Non-blocking single-key scan with debounce. Returns a key exactly once per
 * physical press (on release), or '\0' if nothing new. Rows idle high, we drive
 * one low at a time and look for a column pulled low through the pressed key. */
char hal_keypad_scan(void) {
    GPIO_TypeDef *row_port[4] = { GPIOB, GPIOB, GPIOB, GPIOB };
    uint16_t      row_pin[4]  = { GPIO_PIN_11, GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14 };
    GPIO_TypeDef *col_port[4] = { GPIOA, GPIOA, GPIOA, GPIOA };
    uint16_t      col_pin[4]  = { GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10, GPIO_PIN_11 };
    static const char keymap[4][4] = {
        { '1', '2', '3', 'A' },
        { '4', '5', '6', 'B' },
        { '7', '8', '9', 'C' },
        { '*', '0', '#', 'D' },
    };

    static char held = '\0';
    char found = '\0';

    for (uint8_t r = 0; r < 4 && found == '\0'; r++) {
        for (uint8_t i = 0; i < 4; i++) {
            HAL_GPIO_WritePin(row_port[i], row_pin[i],
                              i == r ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
        delay_us(5);
        for (uint8_t c = 0; c < 4; c++) {
            if (HAL_GPIO_ReadPin(col_port[c], col_pin[c]) == GPIO_PIN_RESET) {
                found = keymap[r][c];
            }
        }
    }

    if (found != '\0') { held = found; return '\0'; }

    char released = held;
    held = '\0';
    return released;
}

/* ---- Timebase ---- */
uint32_t hal_millis(void) { return HAL_GetTick(); }
