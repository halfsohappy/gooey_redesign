#ifndef REMOTE_CONFIG_H
#define REMOTE_CONFIG_H

// ============================================================================
//  TheaterGWD Setup Remote — Configuration & Pin Definitions
// ============================================================================

// ── I2C pins (Olimex ESP32-C3-DevKit-Lipo) ─────────────────────────────────
// Both the SSD1306 display and Seesaw ANO share the I2C bus.
// Change these if your board uses different defaults.
#define PIN_SDA  8
#define PIN_SCL  9

// ── SSD1306 OLED Display ────────────────────────────────────────────────────
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_ADDR      0x3C
#define OLED_RESET     -1       // share reset with MCU (no dedicated pin)

// ── Adafruit Seesaw ANO Rotary Encoder Breakout ────────────────────────────
// Pin assignments match Adafruit product #5740 (ANO Rotary Navigation
// Encoder to I2C Stemma QT Adapter).  If your breakout uses different
// seesaw firmware, adjust these constants to match.
#define SEESAW_ADDR       0x49

// Seesaw GPIO pins — directional buttons
#define SS_BTN_SELECT     1
#define SS_BTN_UP         2
#define SS_BTN_LEFT       3
#define SS_BTN_DOWN       4
#define SS_BTN_RIGHT      5

// Seesaw encoder push-button pin
#define SS_ENC_SWITCH     24

// Seesaw NeoPixel ring
#define SS_NEOPIX_PIN     18
#define SS_NEOPIX_NUM     8

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
#define VISIBLE_ITEMS     6     // menu rows visible on 128x64 OLED
#define REPLY_TIMEOUT_MS  3000
#define DEBOUNCE_MS       180

// ── OSC buffer ──────────────────────────────────────────────────────────────
#define OSC_BUF_SIZE      512

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

// ── Character set for text input ────────────────────────────────────────────
static const char CHARSET[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "._-/: ,";
static const int CHARSET_LEN = sizeof(CHARSET) - 1;  // exclude null

#endif // REMOTE_CONFIG_H
