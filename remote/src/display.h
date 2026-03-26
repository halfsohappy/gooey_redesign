#ifndef DISPLAY_H
#define DISPLAY_H

// ============================================================================
//  TheaterGWD Setup Remote — ST7789 240×240 TFT Display Wrapper
// ============================================================================
//
//  Thin layer over TFT_eSPI providing helpers for menu rendering, status
//  bars, and the various editor overlays used by the UI.
//
//  Layout (font size 2 → 12×16 px → 20 chars × 15 rows):
//
//    Row 0         Title bar (coloured background)
//    Rows 1–10     Content area (10 visible menu items or text)
//    Row 14        Status / hint bar (coloured background)
//
// ============================================================================

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// ── Display instance ────────────────────────────────────────────────────────

static TFT_eSPI _tft = TFT_eSPI(SCREEN_WIDTH, SCREEN_HEIGHT);

// ── Init ────────────────────────────────────────────────────────────────────

static bool disp_init() {
    _tft.init();
    _tft.setRotation(0);
    _tft.fillScreen(COL_BG);

    // Turn on backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    _tft.setTextSize(2);           // 12×16 px glyphs
    _tft.setTextColor(COL_FG, COL_BG);
    return true;
}

// ── Low-level helpers ───────────────────────────────────────────────────────

static void disp_clear() { _tft.fillScreen(COL_BG); }
static void disp_show()  { /* TFT_eSPI is immediate-mode — nothing to flush */ }

// Draw a full-width coloured text bar at pixel-row y.
static void _disp_bar(int y, const char* text, uint16_t bg, uint16_t fg) {
    _tft.fillRect(0, y, SCREEN_WIDTH, CHAR_H, bg);
    _tft.setTextColor(fg, bg);
    _tft.setTextSize(2);
    _tft.setCursor(2, y);
    _tft.print(text);
    _tft.setTextColor(COL_FG, COL_BG);
}

// ── Title & status bars ─────────────────────────────────────────────────────

static void disp_title(const char* text) {
    _disp_bar(TITLE_Y * CHAR_H, text, COL_TITLE_BG, COL_TITLE_FG);
}

static void disp_status(const char* text) {
    _disp_bar(STATUS_Y * CHAR_H, text, COL_STATUS_BG, COL_STATUS_FG);
}

// ── Menu list ───────────────────────────────────────────────────────────────

// Draw a scrollable list.  `selected` is the absolute index; `scroll` is the
// first visible index.  Items[i] is the label for absolute index i.
static void disp_menu(const char items[][MAX_ITEM_LEN], int count,
                      int selected, int scroll) {
    _tft.setTextSize(2);
    for (int r = 0; r < CONTENT_ROWS && (scroll + r) < count; r++) {
        int idx = scroll + r;
        int py = (CONTENT_Y + r) * CHAR_H;
        if (idx == selected) {
            _tft.fillRect(0, py, SCREEN_WIDTH, CHAR_H, COL_SEL_BG);
            _tft.setTextColor(COL_SEL_FG, COL_SEL_BG);
        } else {
            _tft.setTextColor(COL_FG, COL_BG);
        }
        _tft.setCursor(2, py);
        _tft.print(items[idx]);
    }
    _tft.setTextColor(COL_FG, COL_BG);
    // scroll indicators
    if (scroll > 0) {
        _tft.setCursor(SCREEN_WIDTH - CHAR_W, CONTENT_Y * CHAR_H);
        _tft.print("^");
    }
    if (scroll + CONTENT_ROWS < count) {
        int py = (CONTENT_Y + CONTENT_ROWS - 1) * CHAR_H;
        _tft.setCursor(SCREEN_WIDTH - CHAR_W, py);
        _tft.print("v");
    }
}

// ── Full-screen centred message ─────────────────────────────────────────────

static void disp_message(const char* line1, const char* line2 = nullptr) {
    _tft.setTextSize(2);
    _tft.setTextColor(COL_FG, COL_BG);
    int y = line2 ? 80 : 112;
    int x1 = (SCREEN_WIDTH - (int)strlen(line1) * CHAR_W) / 2;
    if (x1 < 0) x1 = 0;
    _tft.setCursor(x1, y);
    _tft.print(line1);
    if (line2) {
        int x2 = (SCREEN_WIDTH - (int)strlen(line2) * CHAR_W) / 2;
        if (x2 < 0) x2 = 0;
        _tft.setCursor(x2, y + 24);
        _tft.print(line2);
    }
}

// ── Wrapped text (for reply display) ────────────────────────────────────────

// Draw word-wrapped text in the content area, starting from line `scroll`.
// Returns total number of wrapped lines.
static int disp_wrapped(const char* text, int scroll_line) {
    _tft.setTextSize(2);
    _tft.setTextColor(COL_FG, COL_BG);

    // Pre-compute wrapped lines
    int total_lines = 0;
    int len = strlen(text);
    int pos = 0;
    char lines_buf[64][COLS + 1];   // up to 64 wrapped lines

    while (pos < len && total_lines < 64) {
        int chunk = len - pos;
        if (chunk > COLS) chunk = COLS;
        memcpy(lines_buf[total_lines], text + pos, chunk);
        lines_buf[total_lines][chunk] = 0;
        total_lines++;
        pos += chunk;
    }

    for (int r = 0; r < CONTENT_ROWS && (scroll_line + r) < total_lines; r++) {
        _tft.setCursor(0, (CONTENT_Y + r) * CHAR_H);
        _tft.print(lines_buf[scroll_line + r]);
    }
    return total_lines;
}

// ── Text-input overlay ──────────────────────────────────────────────────────

static void disp_text_input(const char* title, const char* text,
                            int cursor_pos, char current_char) {
    disp_title(title);

    // Show the entered text so far
    _tft.setTextSize(2);
    _tft.setTextColor(COL_FG, COL_BG);
    _tft.setCursor(2, 2 * CHAR_H);
    _tft.print(text);

    // Blinking cursor character
    int cx = 2 + cursor_pos * CHAR_W;
    if (cx > SCREEN_WIDTH - CHAR_W) cx = SCREEN_WIDTH - CHAR_W;
    _tft.fillRect(cx, 2 * CHAR_H, CHAR_W, CHAR_H, COL_FG);
    _tft.setTextColor(COL_BG, COL_FG);
    _tft.setCursor(cx, 2 * CHAR_H);
    char tmp[2] = { current_char, 0 };
    _tft.print(tmp);
    _tft.setTextColor(COL_FG, COL_BG);

    // Show nearby characters for context
    int ci = 0;
    for (int i = 0; i < CHARSET_LEN; i++) {
        if (CHARSET[i] == current_char) { ci = i; break; }
    }
    _tft.setCursor(2, 5 * CHAR_H);
    for (int d = -6; d <= 6; d++) {
        int idx = (ci + d + CHARSET_LEN) % CHARSET_LEN;
        if (d == 0) {
            _tft.setTextColor(COL_TITLE_BG, COL_BG);
            _tft.print('[');
            _tft.print(CHARSET[idx]);
            _tft.print(']');
            _tft.setTextColor(COL_FG, COL_BG);
        } else {
            _tft.print(' ');
            _tft.print(CHARSET[idx]);
        }
    }

    disp_status("A:add B:del X/Y:chr");
}

// ── Number-input overlay ────────────────────────────────────────────────────

static void disp_num_input(const char* title, int value, int lo, int hi) {
    disp_title(title);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", value);

    _tft.setTextSize(4);
    _tft.setTextColor(COL_FG, COL_BG);
    int x = (SCREEN_WIDTH - (int)strlen(buf) * 24) / 2;
    if (x < 0) x = 0;
    _tft.setCursor(x, 80);
    _tft.print(buf);

    _tft.setTextSize(2);
    snprintf(buf, sizeof(buf), "range: %d .. %d", lo, hi);
    int rx = (SCREEN_WIDTH - (int)strlen(buf) * CHAR_W) / 2;
    if (rx < 0) rx = 0;
    _tft.setCursor(rx, 140);
    _tft.print(buf);

    disp_status("X/Y:val  A:done");
}

// ── IP-address input overlay ────────────────────────────────────────────────

static void disp_ip_input(const char* title, const uint8_t octets[4],
                          int active_octet) {
    disp_title(title);

    _tft.setTextSize(2);
    _tft.setTextColor(COL_FG, COL_BG);

    // Draw each octet, highlighting the active one
    char buf[4];
    int x = 16;
    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "%3d", octets[i]);
        if (i == active_octet) {
            _tft.fillRect(x - 1, 98, 3 * CHAR_W + 2, CHAR_H + 2,
                           COL_SEL_BG);
            _tft.setTextColor(COL_SEL_FG, COL_SEL_BG);
        }
        _tft.setCursor(x, 99);
        _tft.print(buf);
        _tft.setTextColor(COL_FG, COL_BG);
        x += 3 * CHAR_W + 4;
        if (i < 3) { _tft.setCursor(x, 99); _tft.print('.'); x += CHAR_W + 2; }
    }

    disp_status("X/Y:val LR:oct A:ok");
}

// ── Pick-list overlay (reuse menu renderer) ─────────────────────────────────

static void disp_pick(const char* title, const char* const* options,
                      int count, int selected, int scroll) {
    disp_title(title);
    _tft.setTextSize(2);
    for (int r = 0; r < CONTENT_ROWS && (scroll + r) < count; r++) {
        int idx = scroll + r;
        int py = (CONTENT_Y + r) * CHAR_H;
        if (idx == selected) {
            _tft.fillRect(0, py, SCREEN_WIDTH, CHAR_H, COL_SEL_BG);
            _tft.setTextColor(COL_SEL_FG, COL_SEL_BG);
        } else {
            _tft.setTextColor(COL_FG, COL_BG);
        }
        _tft.setCursor(2, py);
        _tft.print(options[idx]);
    }
    _tft.setTextColor(COL_FG, COL_BG);
    disp_status("X/Y:scroll A:sel");
}

// ── Confirm dialog ──────────────────────────────────────────────────────────

static void disp_confirm(const char* text, bool yes_selected) {
    _tft.setTextSize(2);
    _tft.setTextColor(COL_FG, COL_BG);

    int tx = (SCREEN_WIDTH - (int)strlen(text) * CHAR_W) / 2;
    if (tx < 0) tx = 0;
    _tft.setCursor(tx, 60);
    _tft.print(text);

    int bw = 60, bh = 28, gap = 24;
    int y = 120;
    int x_yes = SCREEN_WIDTH / 2 - bw - gap / 2;
    int x_no  = SCREEN_WIDTH / 2 + gap / 2;

    // YES box
    if (yes_selected)
        _tft.fillRoundRect(x_yes, y, bw, bh, 6, COL_SEL_BG);
    else
        _tft.drawRoundRect(x_yes, y, bw, bh, 6, COL_FG);
    _tft.setTextColor(yes_selected ? COL_SEL_FG : COL_FG, yes_selected ? COL_SEL_BG : COL_BG);
    _tft.setCursor(x_yes + 12, y + 6);
    _tft.print("Yes");

    // NO box
    if (!yes_selected)
        _tft.fillRoundRect(x_no, y, bw, bh, 6, COL_SEL_BG);
    else
        _tft.drawRoundRect(x_no, y, bw, bh, 6, COL_FG);
    _tft.setTextColor(!yes_selected ? COL_SEL_FG : COL_FG, !yes_selected ? COL_SEL_BG : COL_BG);
    _tft.setCursor(x_no + 18, y + 6);
    _tft.print("No");

    _tft.setTextColor(COL_FG, COL_BG);
}

#endif // DISPLAY_H
