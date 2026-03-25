#ifndef INPUT_H
#define INPUT_H

// ============================================================================
//  TheaterGWD Setup Remote — Adafruit Seesaw ANO Input Handler
// ============================================================================
//
//  Reads the five directional buttons, the rotary encoder (position +
//  push-button), and drives the 8-pixel NeoPixel ring for status feedback.
//
//  Public API
//  ----------
//  input_init()            initialise seesaw + NeoPixels
//  input_read()            poll; returns InputEvent (call once per loop)
//  led_set(r, g, b)       fill all NeoPixels with a solid colour
//  led_off()               turn off all NeoPixels
//  led_spin(r, g, b, pos) light one pixel at `pos` in the given colour
//
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>
#include "config.h"

// ── Event type ──────────────────────────────────────────────────────────────

enum InputEvent {
    EVT_NONE,
    EVT_UP,
    EVT_DOWN,
    EVT_LEFT,
    EVT_RIGHT,
    EVT_SELECT,       // centre button
    EVT_ENC_PRESS,    // encoder push-button
    EVT_ENC_CW,       // encoder clockwise tick
    EVT_ENC_CCW        // encoder counter-clockwise tick
};

// ── Internal state ──────────────────────────────────────────────────────────

static Adafruit_seesaw _ss;
static seesaw_NeoPixel _pixels(SS_NEOPIX_NUM, SS_NEOPIX_PIN, NEO_GRB + NEO_KHZ800);

static int32_t _enc_pos = 0;
static unsigned long _last_btn_time = 0;

// Bit mask of button pins for digitalReadBulk
static const uint32_t _BTN_MASK =
    (1UL << SS_BTN_SELECT) |
    (1UL << SS_BTN_UP)     |
    (1UL << SS_BTN_LEFT)   |
    (1UL << SS_BTN_DOWN)   |
    (1UL << SS_BTN_RIGHT)  |
    (1UL << SS_ENC_SWITCH);

// ── Init ────────────────────────────────────────────────────────────────────

static bool input_init() {
    if (!_ss.begin(SEESAW_ADDR)) return false;

    // Enable internal pull-ups on all button pins
    _ss.pinMode(SS_BTN_SELECT, INPUT_PULLUP);
    _ss.pinMode(SS_BTN_UP,     INPUT_PULLUP);
    _ss.pinMode(SS_BTN_LEFT,   INPUT_PULLUP);
    _ss.pinMode(SS_BTN_DOWN,   INPUT_PULLUP);
    _ss.pinMode(SS_BTN_RIGHT,  INPUT_PULLUP);
    _ss.pinMode(SS_ENC_SWITCH, INPUT_PULLUP);

    // Encoder — read initial position
    _enc_pos = _ss.getEncoderPosition();
    _ss.enableEncoderInterrupt();

    // NeoPixel ring
    _pixels.begin(SEESAW_ADDR);
    _pixels.setBrightness(30);
    _pixels.show();

    return true;
}

// ── Poll input (call once per loop) ─────────────────────────────────────────

static InputEvent input_read() {
    unsigned long now = millis();

    // ── Encoder rotation (no debounce — already tick-based) ─────────────
    int32_t pos = _ss.getEncoderPosition();
    if (pos != _enc_pos) {
        InputEvent ev = (pos > _enc_pos) ? EVT_ENC_CW : EVT_ENC_CCW;
        _enc_pos = pos;
        return ev;
    }

    // ── Buttons (with debounce) ─────────────────────────────────────────
    if (now - _last_btn_time < DEBOUNCE_MS) return EVT_NONE;

    uint32_t btns = _ss.digitalReadBulk(_BTN_MASK);
    // Buttons are active-low (pressed = 0)
    if (!(btns & (1UL << SS_ENC_SWITCH))) { _last_btn_time = now; return EVT_ENC_PRESS; }
    if (!(btns & (1UL << SS_BTN_SELECT))) { _last_btn_time = now; return EVT_SELECT; }
    if (!(btns & (1UL << SS_BTN_UP)))     { _last_btn_time = now; return EVT_UP; }
    if (!(btns & (1UL << SS_BTN_DOWN)))   { _last_btn_time = now; return EVT_DOWN; }
    if (!(btns & (1UL << SS_BTN_LEFT)))   { _last_btn_time = now; return EVT_LEFT; }
    if (!(btns & (1UL << SS_BTN_RIGHT)))  { _last_btn_time = now; return EVT_RIGHT; }

    return EVT_NONE;
}

// ── NeoPixel helpers ────────────────────────────────────────────────────────

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < SS_NEOPIX_NUM; i++)
        _pixels.setPixelColor(i, _pixels.Color(r, g, b));
    _pixels.show();
}

static void led_off() { led_set(0, 0, 0); }

// Light a single pixel (for spinning animation).
static void led_spin(uint8_t r, uint8_t g, uint8_t b, int pos) {
    for (int i = 0; i < SS_NEOPIX_NUM; i++)
        _pixels.setPixelColor(i, 0);
    _pixels.setPixelColor(pos % SS_NEOPIX_NUM, _pixels.Color(r, g, b));
    _pixels.show();
}

#endif // INPUT_H
