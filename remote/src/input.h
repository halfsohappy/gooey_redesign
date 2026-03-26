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
    EVT_SELECT,       // A button (primary confirm)
    EVT_ENC_PRESS,    // Start button (secondary action)
    EVT_ENC_CW,       // X button (cycle forward / increment)
    EVT_ENC_CCW        // Y button (cycle backward / decrement)
};

// ── Internal state ──────────────────────────────────────────────────────────

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

#endif // INPUT_H
