#ifndef DISPLAY_H
#define DISPLAY_H

// ============================================================================
//  TheaterGWD Setup Remote — SSD1306 128×64 OLED Display Wrapper
// ============================================================================
//
//  Thin layer over Adafruit_SSD1306 providing helpers for menu rendering,
//  status bars, and the various editor overlays used by the UI.
//
//  Layout (size-1 font = 6×8 px → 21 chars × 8 rows):
//
//    Row 0        Title bar (inverted background)
//    Rows 1–6     Content area (6 visible menu items or text)
//    Row 7        Status / hint bar (inverted background)
//
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// ── Display instance ────────────────────────────────────────────────────────

static Adafruit_SSD1306 _oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Geometry constants ──────────────────────────────────────────────────────

#define CHAR_W       6
#define CHAR_H       8
#define COLS         (SCREEN_WIDTH  / CHAR_W)   // 21
#define ROWS         (SCREEN_HEIGHT / CHAR_H)   // 8
#define TITLE_Y      0
#define CONTENT_Y    1       // first content row index
#define CONTENT_ROWS VISIBLE_ITEMS  // 6
#define STATUS_Y     7       // bottom row index

// ── Init ────────────────────────────────────────────────────────────────────

static bool disp_init() {
    return _oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
}

// ── Low-level helpers ───────────────────────────────────────────────────────

static void disp_clear() { _oled.clearDisplay(); }
static void disp_show()  { _oled.display(); }

// Draw a full-width inverted text bar at pixel-row y.
static void _disp_bar(int y, const char* text) {
    _oled.fillRect(0, y, SCREEN_WIDTH, CHAR_H, SSD1306_WHITE);
    _oled.setTextColor(SSD1306_BLACK);
    _oled.setTextSize(1);
    _oled.setCursor(1, y);
    _oled.print(text);
    _oled.setTextColor(SSD1306_WHITE);
}

// ── Title & status bars ─────────────────────────────────────────────────────

static void disp_title(const char* text) {
    _disp_bar(TITLE_Y * CHAR_H, text);
}

static void disp_status(const char* text) {
    _disp_bar(STATUS_Y * CHAR_H, text);
}

// ── Menu list ───────────────────────────────────────────────────────────────

// Draw a scrollable list.  `selected` is the absolute index; `scroll` is the
// first visible index.  Items[i] is the label for absolute index i.
static void disp_menu(const char items[][MAX_ITEM_LEN], int count,
                      int selected, int scroll) {
    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);
    for (int r = 0; r < CONTENT_ROWS && (scroll + r) < count; r++) {
        int idx = scroll + r;
        int py = (CONTENT_Y + r) * CHAR_H;
        if (idx == selected) {
            _oled.fillRect(0, py, SCREEN_WIDTH, CHAR_H, SSD1306_WHITE);
            _oled.setTextColor(SSD1306_BLACK);
        }
        _oled.setCursor(2, py);
        _oled.print(items[idx]);
        if (idx == selected) {
            _oled.setTextColor(SSD1306_WHITE);
        }
    }
    // scroll indicators
    if (scroll > 0) {
        _oled.setCursor(SCREEN_WIDTH - CHAR_W, CONTENT_Y * CHAR_H);
        _oled.print((char)0x18);  // up arrow
    }
    if (scroll + CONTENT_ROWS < count) {
        int py = (CONTENT_Y + CONTENT_ROWS - 1) * CHAR_H;
        _oled.setCursor(SCREEN_WIDTH - CHAR_W, py);
        _oled.print((char)0x19);  // down arrow
    }
}

// ── Full-screen centred message ─────────────────────────────────────────────

static void disp_message(const char* line1, const char* line2 = nullptr) {
    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);
    int y = line2 ? 20 : 28;
    int x1 = (SCREEN_WIDTH - (int)strlen(line1) * CHAR_W) / 2;
    if (x1 < 0) x1 = 0;
    _oled.setCursor(x1, y);
    _oled.print(line1);
    if (line2) {
        int x2 = (SCREEN_WIDTH - (int)strlen(line2) * CHAR_W) / 2;
        if (x2 < 0) x2 = 0;
        _oled.setCursor(x2, y + 12);
        _oled.print(line2);
    }
}

// ── Wrapped text (for reply display) ────────────────────────────────────────

// Draw word-wrapped text in the content area, starting from line `scroll`.
// Returns total number of wrapped lines.
static int disp_wrapped(const char* text, int scroll_line) {
    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);

    // Pre-compute wrapped lines
    int total_lines = 0;
    int len = strlen(text);
    int pos = 0;
    char lines_buf[32][COLS + 1];   // up to 32 wrapped lines

    while (pos < len && total_lines < 32) {
        int chunk = len - pos;
        if (chunk > COLS) chunk = COLS;
        memcpy(lines_buf[total_lines], text + pos, chunk);
        lines_buf[total_lines][chunk] = 0;
        total_lines++;
        pos += chunk;
    }

    for (int r = 0; r < CONTENT_ROWS && (scroll_line + r) < total_lines; r++) {
        _oled.setCursor(0, (CONTENT_Y + r) * CHAR_H);
        _oled.print(lines_buf[scroll_line + r]);
    }
    return total_lines;
}

// ── Text-input overlay ──────────────────────────────────────────────────────

static void disp_text_input(const char* title, const char* text,
                            int cursor_pos, char current_char) {
    disp_title(title);

    // Show the entered text so far
    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);
    _oled.setCursor(2, 2 * CHAR_H);
    _oled.print(text);

    // Blinking cursor character
    int cx = 2 + cursor_pos * CHAR_W;
    if (cx > SCREEN_WIDTH - CHAR_W) cx = SCREEN_WIDTH - CHAR_W;
    _oled.fillRect(cx, 2 * CHAR_H, CHAR_W, CHAR_H, SSD1306_WHITE);
    _oled.setTextColor(SSD1306_BLACK);
    _oled.setCursor(cx, 2 * CHAR_H);
    char tmp[2] = { current_char, 0 };
    _oled.print(tmp);
    _oled.setTextColor(SSD1306_WHITE);

    // Show nearby characters for context
    int ci = 0;
    for (int i = 0; i < CHARSET_LEN; i++) {
        if (CHARSET[i] == current_char) { ci = i; break; }
    }
    _oled.setCursor(2, 4 * CHAR_H);
    for (int d = -6; d <= 6; d++) {
        int idx = (ci + d + CHARSET_LEN) % CHARSET_LEN;
        if (d == 0) {
            _oled.print('[');
            _oled.print(CHARSET[idx]);
            _oled.print(']');
        } else {
            _oled.print(' ');
            _oled.print(CHARSET[idx]);
        }
    }

    disp_status("\x12""add \x11""del \x19""done \x18""cancel");
}

// ── Number-input overlay ────────────────────────────────────────────────────

static void disp_num_input(const char* title, int value, int lo, int hi) {
    disp_title(title);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", value);

    _oled.setTextSize(2);
    _oled.setTextColor(SSD1306_WHITE);
    int x = (SCREEN_WIDTH - (int)strlen(buf) * 12) / 2;
    if (x < 0) x = 0;
    _oled.setCursor(x, 24);
    _oled.print(buf);

    _oled.setTextSize(1);
    snprintf(buf, sizeof(buf), "range: %d .. %d", lo, hi);
    int rx = (SCREEN_WIDTH - (int)strlen(buf) * CHAR_W) / 2;
    if (rx < 0) rx = 0;
    _oled.setCursor(rx, 46);
    _oled.print(buf);

    disp_status("\x12""scroll  \x19""done");
}

// ── IP-address input overlay ────────────────────────────────────────────────

static void disp_ip_input(const char* title, const uint8_t octets[4],
                          int active_octet) {
    disp_title(title);

    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);

    // Draw each octet, highlighting the active one
    char buf[4];
    int x = 8;
    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "%3d", octets[i]);
        if (i == active_octet) {
            _oled.fillRect(x - 1, 26, 3 * CHAR_W + 2, CHAR_H + 2,
                           SSD1306_WHITE);
            _oled.setTextColor(SSD1306_BLACK);
        }
        _oled.setCursor(x, 27);
        _oled.print(buf);
        _oled.setTextColor(SSD1306_WHITE);
        x += 3 * CHAR_W + 2;
        if (i < 3) { _oled.setCursor(x, 27); _oled.print('.'); x += CHAR_W; }
    }

    disp_status("\x12""val \x11""\x12""oct \x19""done");
}

// ── Pick-list overlay (reuse menu renderer) ─────────────────────────────────

static void disp_pick(const char* title, const char* const* options,
                      int count, int selected, int scroll) {
    disp_title(title);
    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);
    for (int r = 0; r < CONTENT_ROWS && (scroll + r) < count; r++) {
        int idx = scroll + r;
        int py = (CONTENT_Y + r) * CHAR_H;
        if (idx == selected) {
            _oled.fillRect(0, py, SCREEN_WIDTH, CHAR_H, SSD1306_WHITE);
            _oled.setTextColor(SSD1306_BLACK);
        }
        _oled.setCursor(2, py);
        _oled.print(options[idx]);
        if (idx == selected) _oled.setTextColor(SSD1306_WHITE);
    }
    disp_status("\x12""scroll  \x19""select");
}

// ── Confirm dialog ──────────────────────────────────────────────────────────

static void disp_confirm(const char* text, bool yes_selected) {
    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);

    int tx = (SCREEN_WIDTH - (int)strlen(text) * CHAR_W) / 2;
    if (tx < 0) tx = 0;
    _oled.setCursor(tx, 16);
    _oled.print(text);

    int bw = 30, bh = 14, gap = 16;
    int y = 36;
    int x_yes = SCREEN_WIDTH / 2 - bw - gap / 2;
    int x_no  = SCREEN_WIDTH / 2 + gap / 2;

    // YES box
    if (yes_selected)
        _oled.fillRoundRect(x_yes, y, bw, bh, 3, SSD1306_WHITE);
    else
        _oled.drawRoundRect(x_yes, y, bw, bh, 3, SSD1306_WHITE);
    _oled.setTextColor(yes_selected ? SSD1306_BLACK : SSD1306_WHITE);
    _oled.setCursor(x_yes + 6, y + 3);
    _oled.print("Yes");

    // NO box
    if (!yes_selected)
        _oled.fillRoundRect(x_no, y, bw, bh, 3, SSD1306_WHITE);
    else
        _oled.drawRoundRect(x_no, y, bw, bh, 3, SSD1306_WHITE);
    _oled.setTextColor(!yes_selected ? SSD1306_BLACK : SSD1306_WHITE);
    _oled.setCursor(x_no + 9, y + 3);
    _oled.print("No");

    _oled.setTextColor(SSD1306_WHITE);
}

#endif // DISPLAY_H
