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

// ── Sensor value names (for pick list) ──────────────────────────────────────
static const char* const SENSOR_NAMES[] = {
    "accelX", "accelY", "accelZ", "accelLength",
    "gyroX",  "gyroY",  "gyroZ",  "gyroLength",
    "baro",
    "eulerX", "eulerY", "eulerZ"
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
