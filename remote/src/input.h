#ifndef INPUT_H
#define INPUT_H

// ============================================================================
//  TheaterGWD Setup Remote — Adafruit Mini I2C Gamepad QT Input Handler
// ============================================================================
//
//  Reads the six face buttons (A, B, X, Y, Select, Start) and the 2-axis
//  thumb-stick via the seesaw I2C interface on the Gamepad QT breakout.
//
//  Public API
//  ----------
//  input_init()            initialise seesaw gamepad
//  input_read()            poll; returns InputEvent (call once per loop)
//
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_seesaw.h>
#include "config.h"

// ── Event type ──────────────────────────────────────────────────────────────

enum InputEvent {
    EVT_NONE,
    EVT_UP,
    EVT_DOWN,
    EVT_LEFT,
    EVT_RIGHT,
    EVT_SELECT,       // A button / OK zone (primary confirm)
    EVT_ENC_PRESS,    // Start button (secondary action)
    EVT_ENC_CW,       // X button / ▼ zone (cycle forward / increment)
    EVT_ENC_CCW,      // Y button / ▲ zone (cycle backward / decrement)
    EVT_TAP_ROW       // touch4: direct tap on a content row
};

// Visible-row index of the tapped item (set when EVT_TAP_ROW is returned).
// ui.h adds the current scroll offset to get the absolute item index.
static int _tapped_row = -1;

// ── Internal state ──────────────────────────────────────────────────────────

#ifndef TOUCH4_BUILD
// ── Adafruit Mini I2C Gamepad QT (original hardware) ────────────────────────

#include <Wire.h>
#include <Adafruit_seesaw.h>

static Adafruit_seesaw _ss;

static unsigned long _last_btn_time = 0;
static bool _joy_active = false;   // true while stick is deflected

// Bit mask of button pins for digitalReadBulk
static const uint32_t _BTN_MASK =
    (1UL << GP_BTN_A)      |
    (1UL << GP_BTN_B)      |
    (1UL << GP_BTN_X)      |
    (1UL << GP_BTN_Y)      |
    (1UL << GP_BTN_SELECT) |
    (1UL << GP_BTN_START);

// ── Init ────────────────────────────────────────────────────────────────────

static bool input_init() {
    if (!_ss.begin(GAMEPAD_ADDR)) return false;

    // Enable internal pull-ups on button pins
    _ss.pinModeBulk(_BTN_MASK, INPUT_PULLUP);

    return true;
}

// ── Poll input (call once per loop) ─────────────────────────────────────────

static InputEvent input_read() {
    unsigned long now = millis();

    // ── Joystick (analog → digital direction) ──────────────────────────
    int jx = 1023 - _ss.analogRead(GP_JOY_X);
    int jy = 1023 - _ss.analogRead(GP_JOY_Y);

    bool deflected = (jx < JOY_LO || jx > JOY_HI ||
                      jy < JOY_LO || jy > JOY_HI);

    if (deflected && !_joy_active && (now - _last_btn_time >= DEBOUNCE_MS)) {
        _joy_active = true;
        _last_btn_time = now;
        // Determine dominant axis
        int dx = jx - 512;
        int dy = jy - 512;
        if (abs(dx) > abs(dy)) {
            return (dx > 0) ? EVT_RIGHT : EVT_LEFT;
        } else {
            return (dy > 0) ? EVT_DOWN : EVT_UP;
        }
    }
    if (!deflected) _joy_active = false;

    // ── Buttons (with debounce) ─────────────────────────────────────────
    if (now - _last_btn_time < DEBOUNCE_MS) return EVT_NONE;

    uint32_t btns = _ss.digitalReadBulk(_BTN_MASK);
    // Buttons are active-low (pressed = 0)
    if (!(btns & (1UL << GP_BTN_A)))      { _last_btn_time = now; return EVT_SELECT; }
    if (!(btns & (1UL << GP_BTN_B)))      { _last_btn_time = now; return EVT_LEFT; }      // B = back
    if (!(btns & (1UL << GP_BTN_X)))      { _last_btn_time = now; return EVT_ENC_CW; }    // X = cycle fwd
    if (!(btns & (1UL << GP_BTN_Y)))      { _last_btn_time = now; return EVT_ENC_CCW; }   // Y = cycle bwd
    if (!(btns & (1UL << GP_BTN_START)))  { _last_btn_time = now; return EVT_ENC_PRESS; }
    if (!(btns & (1UL << GP_BTN_SELECT))) { _last_btn_time = now; return EVT_SELECT; }

    return EVT_NONE;
}

#else  // TOUCH4_BUILD
// ── GT911 capacitive touch — Waveshare ESP32-S3-Touch-LCD-4 ─────────────────
//
//  The GT911 is read directly via Wire1 (I2C1, SDA=IO15, SCL=IO7).
//  Wire1 is shared with the TCA9554 expander; no driver conflict because they
//  are on different addresses and this firmware is single-threaded.
//
//  LovyanGFX's built-in touch driver is NOT used here (it would re-install
//  the ESP-IDF I2C driver on port 1, conflicting with Arduino Wire1).
//
//  Touch → InputEvent mapping
//  ─────────────────────────
//  Tap in content area (rows 1–13)  →  EVT_TAP_ROW  (_tapped_row = visible row)
//  Tap in title bar (row 0)         →  (ignored)
//  Tap in button bar (row 14):
//    x ∈ [  0, 120)  →  EVT_LEFT     (◄ Back / cancel / delete / Yes-toggle)
//    x ∈ [120, 240)  →  EVT_ENC_CCW  (▲ Up / decrement)
//    x ∈ [240, 360)  →  EVT_ENC_CW   (▼ Down / increment)
//    x ∈ [360, 480)  →  EVT_SELECT   (OK ✓  confirm / add)
//
//  For the confirm dialog, tapping the left or right half of the content area
//  fires EVT_LEFT (→ Yes) or EVT_ENC_CW (→ No) respectively, giving a more
//  natural "tap the button" feel in addition to the bottom-bar controls.

#include <Wire.h>

// GT911 register addresses
#define _GT911_STATUS  0x814E
#define _GT911_PT1     0x8150

// Touch state machine
static bool          _touch_pressed  = false;
static int16_t       _touch_x0       = 0;
static int16_t       _touch_y0       = 0;
static unsigned long _last_touch_ms  = 0;

// ── Low-level GT911 helpers ───────────────────────────────────────────────────

// Write one byte to a 16-bit register address.
static void _gt911_write8(uint16_t reg, uint8_t val) {
    Wire1.beginTransmission(GT911_ADDR);
    Wire1.write((uint8_t)(reg >> 8));
    Wire1.write((uint8_t)(reg & 0xFF));
    Wire1.write(val);
    Wire1.endTransmission();
}

// Read `len` bytes from a 16-bit register address into `buf`.
// Returns true on success.
static bool _gt911_read(uint16_t reg, uint8_t* buf, uint8_t len) {
    Wire1.beginTransmission(GT911_ADDR);
    Wire1.write((uint8_t)(reg >> 8));
    Wire1.write((uint8_t)(reg & 0xFF));
    if (Wire1.endTransmission(false) != 0) return false;
    Wire1.requestFrom((uint8_t)GT911_ADDR, len);
    if (Wire1.available() < len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
    return true;
}

// Poll the GT911 for the first touch point.
// Returns true when a fresh touch is present; coordinates written to tx/ty.
static bool _gt911_poll(int16_t& tx, int16_t& ty) {
    uint8_t status;
    if (!_gt911_read(_GT911_STATUS, &status, 1)) return false;

    bool ready  = (status & 0x80) != 0;
    uint8_t npt = status & 0x0F;

    if (!ready || npt == 0) return false;

    uint8_t pt[6];
    bool ok = _gt911_read(_GT911_PT1, pt, 6);

    // Clear status register so GT911 can report next event
    _gt911_write8(_GT911_STATUS, 0x00);

    if (!ok) return false;
    tx = (int16_t)((uint16_t)pt[0] | ((uint16_t)pt[1] << 8));
    ty = (int16_t)((uint16_t)pt[2] | ((uint16_t)pt[3] << 8));
    return true;
}

// ── Init ─────────────────────────────────────────────────────────────────────
//  Wire1 is already started in main.cpp (before disp_init).
//  GT911 resets were handled by TCA9554; nothing more to do here.

static bool input_init() {
    return true;
}

// ── Map a tap coordinate to an InputEvent ─────────────────────────────────────

static InputEvent _process_tap(int16_t x, int16_t y) {
    // ── Button bar (STATUS_Y row) ───────────────────────────────────────
    if (y >= STATUS_Y * CHAR_H) {
        if      (x < SCREEN_WIDTH / 4)       return EVT_LEFT;
        else if (x < SCREEN_WIDTH / 2)       return EVT_ENC_CCW;
        else if (x < 3 * SCREEN_WIDTH / 4)   return EVT_ENC_CW;
        else                                  return EVT_SELECT;
    }

    // ── Content area ────────────────────────────────────────────────────
    if (y >= CONTENT_Y * CHAR_H) {
        // For the confirm dialog, treat left/right halves as Yes/No
        // (the button bar also works, but tapping the drawn buttons is natural)
        // We can't know the current mode here, so we use a dual signal:
        // content-area taps fire EVT_TAP_ROW for menus, and left/right
        // half fires EVT_LEFT / EVT_ENC_CW for other modes.
        _tapped_row = (y - CONTENT_Y * CHAR_H) / CHAR_H;
        return EVT_TAP_ROW;
    }

    // Title bar — ignore
    return EVT_NONE;
}

// ── Poll input (call once per loop) ──────────────────────────────────────────

static InputEvent input_read() {
    int16_t tx, ty;
    bool touching = _gt911_poll(tx, ty);

    if (touching) {
        if (!_touch_pressed) {
            // Touch-down: record start position
            _touch_pressed = true;
            _touch_x0 = tx;
            _touch_y0 = ty;
        }
        // While held: no event (edge-triggered only)
        return EVT_NONE;
    }

    // Touch-up
    if (_touch_pressed) {
        _touch_pressed = false;

        // Debounce: ignore if too soon after last event
        unsigned long now = millis();
        if (now - _last_touch_ms < DEBOUNCE_MS) return EVT_NONE;
        _last_touch_ms = now;

        return _process_tap(_touch_x0, _touch_y0);
    }

    return EVT_NONE;
}

#endif  // TOUCH4_BUILD

#endif // INPUT_H
