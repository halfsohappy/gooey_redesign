#ifndef REMOTE_CONFIG_H
#define REMOTE_CONFIG_H

// ============================================================================
//  TheaterGWD Setup Remote — Configuration & Pin Definitions
// ============================================================================

// ── I2C pins (for Adafruit Gamepad QT) ──────────────────────────────────────
#define PIN_SDA  1
#define PIN_SCL  2

// ── ST7789 240×240 TFT Display (SPI — defined via build flags) ──────────────
// Pin definitions are set in platformio.ini build_flags (TFT_eSPI config).
// The display is built into the ESP32-S3 board.
#define SCREEN_WIDTH   240
#define SCREEN_HEIGHT  240

// ── Adafruit Mini I2C Gamepad QT (seesaw) ──────────────────────────────────
#define GAMEPAD_ADDR      0x50

// Seesaw GPIO pins — face buttons
#define GP_BTN_A       5
#define GP_BTN_B       1
#define GP_BTN_X       6
#define GP_BTN_Y       2
#define GP_BTN_SELECT  0
#define GP_BTN_START   16

// Seesaw analog pins — thumb-stick
#define GP_JOY_X       14
#define GP_JOY_Y       15

// Joystick thresholds (10-bit ADC, centre ≈ 512)
#define JOY_DEADZONE   200     // ignore centre noise
#define JOY_LO         (512 - JOY_DEADZONE)
#define JOY_HI         (512 + JOY_DEADZONE)

// ── Network defaults ────────────────────────────────────────────────────────
#define DEFAULT_LISTEN_PORT   9000
#define DEFAULT_TARGET_PORT   8000

// ── NVS namespace ───────────────────────────────────────────────────────────
#define NVS_NAMESPACE  "remote_cfg"

// ── UI limits ───────────────────────────────────────────────────────────────
#define MAX_MENU_ITEMS    32
#define MAX_ITEM_LEN      24
#define MAX_INPUT_LEN     32
#define MAX_REPLY_LEN     512
#define VISIBLE_ITEMS     10    // menu rows visible on 240×240 TFT
#define REPLY_TIMEOUT_MS  3000
#define DEBOUNCE_MS       180

// ── Display geometry (font-dependent) ───────────────────────────────────────
// Using TFT_eSPI default font (size 2 → 12×16 px glyphs, ~20 chars × 15 rows)
#define CHAR_W       12
#define CHAR_H       16
#define COLS         (SCREEN_WIDTH  / CHAR_W)   // 20
#define ROWS         (SCREEN_HEIGHT / CHAR_H)   // 15
#define TITLE_Y      0
#define CONTENT_Y    1       // first content row index
#define CONTENT_ROWS VISIBLE_ITEMS  // 10
#define STATUS_Y     (ROWS - 1)     // bottom row index

// ── OSC buffer ──────────────────────────────────────────────────────────────
#define OSC_BUF_SIZE      512

// ── Waveshare ESP32-S3-Touch-LCD-4 overrides (TOUCH4_BUILD) ─────────────────
//
//  Replaces screen dimensions, char/row geometry, and adds hardware pin defs.
//  All other config (network defaults, NVS, sensors, palette …) is shared.
//
//  I2C topology (confirmed from Waveshare HTML pin table):
//    I2C 1  Wire1  SDA=IO15 SCL=IO7  → GT911 touch, TCA9554 expander (new boards)
//    I2C 0  Wire   SDA=IO8  SCL=IO9  → TCA9554 expander (old boards only)
//                                       (IO8/IO9 also used as RGB R3/G5)
//  This build targets new-board wiring; Wire1 is used for both devices.
//
#ifdef TOUCH4_BUILD

// Touch (GT911)
#define PIN_TP_SDA     15   // TP_SDA
#define PIN_TP_SCL      7   // TP_SCL
#define PIN_TP_INT     16   // TP_INT (active-low, input)

// TCA9554PWR I2C GPIO expander (addr = 0x20 when A0=A1=A2=GND)
#define TCA9554_ADDR  0x20
#define EXIO_TP_RST    0    // EXIO0 — touch panel reset (active-low)
#define EXIO_BL_EN     1    // EXIO1 — backlight enable (active-high)
#define EXIO_LCD_RST   2    // EXIO2 — LCD reset (active-low)

// GT911 default 7-bit address when INT is held LOW during reset
#define GT911_ADDR    0x5D

// 480×480 screen
#undef  SCREEN_WIDTH
#undef  SCREEN_HEIGHT
#define SCREEN_WIDTH   480
#define SCREEN_HEIGHT  480

// textSize(4) → 24×32 px per glyph — same 20-column width as the original
#undef  CHAR_W
#undef  CHAR_H
#undef  COLS
#undef  ROWS
#undef  VISIBLE_ITEMS
#undef  CONTENT_ROWS
#undef  STATUS_Y
#define CHAR_W         24
#define CHAR_H         32
#define COLS           (SCREEN_WIDTH  / CHAR_W)   // 20
#define ROWS           (SCREEN_HEIGHT / CHAR_H)   // 15
#define VISIBLE_ITEMS  13   // rows 1–13 (row 0 = title, row 14 = button bar)
#define CONTENT_ROWS   VISIBLE_ITEMS
#define STATUS_Y       14   // bottom row → touch-button bar

#endif // TOUCH4_BUILD

// ── Sensor value names (for pick list) ──────────────────────────────────────
static const char* const SENSOR_NAMES[] = {
    "accelX", "accelY", "accelZ", "accelLength",
    "gaccelX", "gaccelY", "gaccelZ", "gaccelLength",
    "gyroX",  "gyroY",  "gyroZ",  "gyroLength",
    "baro",
    "eulerX", "eulerY", "eulerZ",
    "quatI", "quatJ", "quatK", "quatR",
    "high", "low"
};
static const int SENSOR_COUNT = sizeof(SENSOR_NAMES) / sizeof(SENSOR_NAMES[0]);

// ── Address mode names ──────────────────────────────────────────────────────
static const char* const ADR_MODES[] = {
    "fallback", "override", "prepend", "append"
};
static const int ADR_MODE_COUNT = 4;

// ── Character set for text input (hex + essential punctuation) ──────────────
static const char CHARSET[] =
    "0123456789abcdef"
    "._-/: ";
static const int CHARSET_LEN = sizeof(CHARSET) - 1;  // exclude null

// ── Display colour palette ──────────────────────────────────────────────────
#define COL_BG       0x0000   // black
#define COL_FG       0xFFFF   // white
#define COL_TITLE_BG 0x630C   // lavender-ish (matches Gooey header #DAC7FF)
#define COL_TITLE_FG 0x0000   // black
#define COL_SEL_BG   0x630C   // highlight
#define COL_SEL_FG   0x0000   // black
#define COL_STATUS_BG 0x2945  // dark grey
#define COL_STATUS_FG 0xFFFF  // white
#define COL_OK       0x07E0   // green
#define COL_WARN     0xFD20   // orange
#define COL_ERR      0xF800   // red

#endif // REMOTE_CONFIG_H
