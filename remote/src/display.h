#ifndef DISPLAY_H
#define DISPLAY_H

// ============================================================================
//  TheaterGWD Setup Remote — Display Wrapper
//
//  TOUCH4_BUILD → LovyanGFX, ST7701S RGB panel, 480×480
//  default      → TFT_eSPI,  ST7789   SPI panel, 240×240
//
//  Both builds expose an identical function API so that ui.h is unaffected:
//
//    disp_init()                 initialise display hardware
//    disp_clear()                fill screen with COL_BG
//    disp_show()                 flush (no-op for immediate-mode drivers)
//    disp_title(text)            coloured title bar at row 0
//    disp_status(text)           status / button bar at STATUS_Y
//    disp_menu(items,n,sel,scr)  scrollable item list
//    disp_message(l1[,l2])       centred one- or two-line message
//    disp_wrapped(text,scroll)   word-wrapped text; returns line count
//    disp_text_input(...)        character-by-character text editor overlay
//    disp_num_input(...)         numeric value editor overlay
//    disp_ip_input(...)          IP-address editor overlay
//    disp_pick(...)              pick-list overlay (reuses menu renderer)
//    disp_confirm(text,yes_sel)  yes/no confirmation dialog
//
//  TOUCH4_BUILD — disp_status() ignores the text parameter and draws four
//  touch-button zones instead: [◄ Back | ▲ Up | ▼ Down | OK ✓].
// ============================================================================

#include <Arduino.h>
#include "config.h"

// ============================================================================
#ifdef TOUCH4_BUILD
// ============================================================================
//  LovyanGFX — Waveshare ESP32-S3-Touch-LCD-4
//  GT911 touch is read directly via Wire1 in input.h (no LovyanGFX touch driver,
//  avoids ESP-IDF I2C driver conflict with Arduino Wire1).
// ============================================================================

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ── LovyanGFX panel class ────────────────────────────────────────────────────

class LGFX_Touch4 : public lgfx::LGFX_Device {
    lgfx::Panel_ST7701  _panel;
    lgfx::Bus_RGB       _bus;

public:
    LGFX_Touch4() {

        // ── RGB parallel bus ─────────────────────────────────────────────
        {
            auto cfg = _bus.config();
            cfg.panel = &_panel;

            // Blue  B1–B5  (5 bits, B1=LSB)
            cfg.pin_d0  =  5;   // B1
            cfg.pin_d1  = 45;   // B2
            cfg.pin_d2  = 48;   // B3
            cfg.pin_d3  = 47;   // B4
            cfg.pin_d4  = 21;   // B5

            // Green G0–G5  (6 bits, G0=LSB)
            cfg.pin_d5  = 14;   // G0
            cfg.pin_d6  = 13;   // G1
            cfg.pin_d7  = 12;   // G2
            cfg.pin_d8  = 11;   // G3
            cfg.pin_d9  = 10;   // G4
            cfg.pin_d10 =  9;   // G5

            // Red   R1–R5  (5 bits, R1=LSB)
            cfg.pin_d11 = 46;   // R1
            cfg.pin_d12 =  3;   // R2
            cfg.pin_d13 =  8;   // R3
            cfg.pin_d14 = 18;   // R4
            cfg.pin_d15 = 17;   // R5

            // Sync / control
            cfg.pin_henable = 40;   // DE
            cfg.pin_vsync   = 39;   // VSYNC
            cfg.pin_hsync   = 38;   // HSYNC
            cfg.pin_pclk    = 41;   // LCD_PCLK

            // Pixel clock ~14 MHz → ~52 fps with these blanking values.
            // If display shows no image, try increasing hsync_back_porch
            // or reducing freq_write.
            cfg.freq_write  = 14000000;

            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 10;
            cfg.hsync_pulse_width = 8;
            cfg.hsync_back_porch  = 50;

            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 10;
            cfg.vsync_pulse_width = 8;
            cfg.vsync_back_porch  = 20;

            cfg.pclk_active_neg = 1;   // latch on falling edge (ST7701S default)
            cfg.de_idle_high    = 0;
            cfg.pclk_idle_high  = 0;

            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // ── ST7701S panel (3-wire SPI init + RGB data) ───────────────────
        {
            auto cfg = _panel.config();

            // SPI pins used only for the one-time init-command sequence
            cfg.pin_cs   = 42;  // LCD_CS
            cfg.pin_sclk =  2;  // LCD_SCL
            cfg.pin_mosi =  1;  // LCD_SDA
            cfg.pin_rst  = -1;  // reset managed via TCA9554 EXIO2 in main.cpp
            cfg.pin_busy = -1;

            cfg.panel_width   = 480;
            cfg.panel_height  = 480;
            cfg.memory_width  = 480;
            cfg.memory_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;

            cfg.readable   = false;
            cfg.invert     = false;
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;

            _panel.config(cfg);
        }

        setPanel(&_panel);
    }
};

static LGFX_Touch4 _gfx;

// ── Init ─────────────────────────────────────────────────────────────────────

static bool disp_init() {
    _gfx.init();
    _gfx.setRotation(0);
    _gfx.fillScreen(COL_BG);
    _gfx.setTextSize(4);           // 24×32 px glyphs
    _gfx.setTextColor(COL_FG, COL_BG);
    return true;
}

// ── Low-level helpers ─────────────────────────────────────────────────────────

static void disp_clear() { _gfx.fillScreen(COL_BG); }
static void disp_show()  { /* LovyanGFX RGB is immediate-mode */ }

static void _disp_bar(int y, const char* text, uint16_t bg, uint16_t fg) {
    _gfx.fillRect(0, y, SCREEN_WIDTH, CHAR_H, bg);
    _gfx.setTextSize(4);
    _gfx.setTextColor(fg, bg);
    _gfx.setCursor(4, y + 4);
    _gfx.print(text);
    _gfx.setTextColor(COL_FG, COL_BG);
}

// ── Title bar ─────────────────────────────────────────────────────────────────

static void disp_title(const char* text) {
    _disp_bar(TITLE_Y * CHAR_H, text, COL_TITLE_BG, COL_TITLE_FG);
}

// ── Status / touch-button bar ─────────────────────────────────────────────────
//
//  Draws four equal-width touch zones in the STATUS_Y row.
//  The text parameter is intentionally ignored — labels are fixed.
//  Zones (left → right):
//    [0]  ◄  →  EVT_LEFT     (back / cancel / delete / toggle-Yes)
//    [1]  ▲  →  EVT_ENC_CCW  (up / decrement)
//    [2]  ▼  →  EVT_ENC_CW   (down / increment)
//    [3]  OK →  EVT_SELECT   (confirm / add char)

static void disp_status(const char* /*text*/) {
    const int     y  = STATUS_Y * CHAR_H;
    const int     bw = SCREEN_WIDTH / 4;   // 120 px each
    const uint16_t bg[4] = { 0x2945, 0x318C, 0x318C, 0x2945 };
    const char*  lbl[4]  = { "< ", "^", "v", "OK" };

    _gfx.setTextSize(4);
    for (int i = 0; i < 4; i++) {
        int x0 = i * bw;
        _gfx.fillRect(x0, y, bw, CHAR_H, bg[i]);
        if (i > 0)
            _gfx.drawFastVLine(x0, y, CHAR_H, 0x4A69);   // subtle separator
        int lw = (int)strlen(lbl[i]) * CHAR_W;
        int lx = x0 + (bw - lw) / 2;
        int ly = y + (CHAR_H - 24) / 2;                  // 24 = actual glyph height
        _gfx.setTextColor(COL_STATUS_FG, bg[i]);
        _gfx.setCursor(lx, ly);
        _gfx.print(lbl[i]);
    }
    _gfx.setTextColor(COL_FG, COL_BG);
}

// ── Menu list ─────────────────────────────────────────────────────────────────

static void disp_menu(const char items[][MAX_ITEM_LEN], int count,
                      int selected, int scroll) {
    _gfx.setTextSize(4);
    for (int r = 0; r < CONTENT_ROWS && (scroll + r) < count; r++) {
        int idx = scroll + r;
        int py  = (CONTENT_Y + r) * CHAR_H;
        if (idx == selected) {
            _gfx.fillRect(0, py, SCREEN_WIDTH, CHAR_H, COL_SEL_BG);
            _gfx.setTextColor(COL_SEL_FG, COL_SEL_BG);
        } else {
            _gfx.setTextColor(COL_FG, COL_BG);
        }
        _gfx.setCursor(4, py + 4);
        _gfx.print(items[idx]);
    }
    _gfx.setTextColor(COL_FG, COL_BG);
}

// ── Full-screen centred message ───────────────────────────────────────────────

static void disp_message(const char* line1, const char* line2 = nullptr) {
    _gfx.setTextSize(4);
    _gfx.setTextColor(COL_FG, COL_BG);
    int y = line2 ? SCREEN_HEIGHT / 3 : SCREEN_HEIGHT * 2 / 5;
    int x1 = (SCREEN_WIDTH - (int)strlen(line1) * CHAR_W) / 2;
    if (x1 < 0) x1 = 0;
    _gfx.setCursor(x1, y);
    _gfx.print(line1);
    if (line2) {
        int x2 = (SCREEN_WIDTH - (int)strlen(line2) * CHAR_W) / 2;
        if (x2 < 0) x2 = 0;
        _gfx.setCursor(x2, y + CHAR_H + 8);
        _gfx.print(line2);
    }
}

// ── Wrapped text (for reply display) ─────────────────────────────────────────

static int disp_wrapped(const char* text, int scroll_line) {
    _gfx.setTextSize(4);
    _gfx.setTextColor(COL_FG, COL_BG);

    int total_lines = 0;
    int len = strlen(text);
    int pos = 0;
    char lines_buf[48][COLS + 1];

    while (pos < len && total_lines < 48) {
        int chunk = len - pos;
        if (chunk > COLS) chunk = COLS;
        memcpy(lines_buf[total_lines], text + pos, chunk);
        lines_buf[total_lines][chunk] = 0;
        total_lines++;
        pos += chunk;
    }
    for (int r = 0; r < CONTENT_ROWS && (scroll_line + r) < total_lines; r++) {
        _gfx.setCursor(0, (CONTENT_Y + r) * CHAR_H + 4);
        _gfx.print(lines_buf[scroll_line + r]);
    }
    return total_lines;
}

// ── Text-input overlay ────────────────────────────────────────────────────────

static void disp_text_input(const char* title, const char* text,
                            int cursor_pos, char current_char) {
    disp_title(title);

    _gfx.setTextSize(4);
    _gfx.setTextColor(COL_FG, COL_BG);
    _gfx.setCursor(4, 2 * CHAR_H + 4);
    _gfx.print(text);

    // Blinking cursor block
    int cx = 4 + cursor_pos * CHAR_W;
    if (cx > SCREEN_WIDTH - CHAR_W) cx = SCREEN_WIDTH - CHAR_W;
    _gfx.fillRect(cx, 2 * CHAR_H, CHAR_W, CHAR_H, COL_FG);
    _gfx.setTextColor(COL_BG, COL_FG);
    _gfx.setCursor(cx, 2 * CHAR_H + 4);
    char tmp[2] = { current_char, 0 };
    _gfx.print(tmp);
    _gfx.setTextColor(COL_FG, COL_BG);

    // Charset picker — show ±3 neighbours around current char
    int ci = 0;
    for (int i = 0; i < CHARSET_LEN; i++) {
        if (CHARSET[i] == current_char) { ci = i; break; }
    }
    _gfx.setCursor(4, 5 * CHAR_H + 4);
    for (int d = -3; d <= 3; d++) {
        int idx = (ci + d + CHARSET_LEN) % CHARSET_LEN;
        if (d == 0) {
            _gfx.setTextColor(COL_TITLE_BG, COL_BG);
            _gfx.print('[');
            _gfx.print(CHARSET[idx]);
            _gfx.print(']');
            _gfx.setTextColor(COL_FG, COL_BG);
        } else {
            _gfx.print(' ');
            _gfx.print(CHARSET[idx]);
        }
    }

    disp_status("");   // draws the 4 touch buttons (text arg ignored)
}

// ── Number-input overlay ──────────────────────────────────────────────────────

static void disp_num_input(const char* title, int value, int lo, int hi) {
    disp_title(title);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", value);

    _gfx.setTextSize(6);   // large digit display
    _gfx.setTextColor(COL_FG, COL_BG);
    int x = (SCREEN_WIDTH - (int)strlen(buf) * 36) / 2;   // size-6 glyph ≈ 36 px wide
    if (x < 0) x = 0;
    _gfx.setCursor(x, SCREEN_HEIGHT / 3);
    _gfx.print(buf);

    _gfx.setTextSize(4);
    snprintf(buf, sizeof(buf), "%d..%d", lo, hi);
    int rx = (SCREEN_WIDTH - (int)strlen(buf) * CHAR_W) / 2;
    if (rx < 0) rx = 0;
    _gfx.setCursor(rx, SCREEN_HEIGHT * 4 / 7);
    _gfx.print(buf);

    disp_status("");
}

// ── IP-address input overlay ──────────────────────────────────────────────────

static void disp_ip_input(const char* title, const uint8_t octets[4],
                          int active_octet) {
    disp_title(title);

    _gfx.setTextSize(4);
    _gfx.setTextColor(COL_FG, COL_BG);

    char buf[4];
    int x = 20;
    int y = SCREEN_HEIGHT * 2 / 5;
    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "%3d", octets[i]);
        if (i == active_octet) {
            _gfx.fillRect(x - 2, y - 4, 3 * CHAR_W + 4, CHAR_H + 8, COL_SEL_BG);
            _gfx.setTextColor(COL_SEL_FG, COL_SEL_BG);
        }
        _gfx.setCursor(x, y);
        _gfx.print(buf);
        _gfx.setTextColor(COL_FG, COL_BG);
        x += 3 * CHAR_W + 4;
        if (i < 3) { _gfx.setCursor(x, y); _gfx.print('.'); x += CHAR_W + 2; }
    }

    disp_status("");
}

// ── Pick-list overlay ─────────────────────────────────────────────────────────

static void disp_pick(const char* title, const char* const* options,
                      int count, int selected, int scroll) {
    disp_title(title);
    _gfx.setTextSize(4);
    for (int r = 0; r < CONTENT_ROWS && (scroll + r) < count; r++) {
        int idx = scroll + r;
        int py  = (CONTENT_Y + r) * CHAR_H;
        if (idx == selected) {
            _gfx.fillRect(0, py, SCREEN_WIDTH, CHAR_H, COL_SEL_BG);
            _gfx.setTextColor(COL_SEL_FG, COL_SEL_BG);
        } else {
            _gfx.setTextColor(COL_FG, COL_BG);
        }
        _gfx.setCursor(4, py + 4);
        _gfx.print(options[idx]);
    }
    _gfx.setTextColor(COL_FG, COL_BG);
    disp_status("");
}

// ── Confirm dialog ────────────────────────────────────────────────────────────
//
//  Draws large YES / NO buttons.  Touch zones for the buttons are detected
//  in input.h by checking taps in the left / right halves of the content area.
//  ◄ button → EVT_LEFT → toggle Yes;  ▼ → EVT_ENC_CW → toggle No;
//  OK button → EVT_SELECT → fire callback with current choice.

static void disp_confirm(const char* text, bool yes_selected) {
    _gfx.setTextSize(4);
    _gfx.setTextColor(COL_FG, COL_BG);

    int tx = (SCREEN_WIDTH - (int)strlen(text) * CHAR_W) / 2;
    if (tx < 0) tx = 0;
    _gfx.setCursor(tx, SCREEN_HEIGHT / 4);
    _gfx.print(text);

    const int bw = 140, bh = 64, gap = 40;
    const int by = SCREEN_HEIGHT / 2 - bh / 2;
    const int x_yes = SCREEN_WIDTH / 2 - bw - gap / 2;
    const int x_no  = SCREEN_WIDTH / 2 + gap / 2;

    if (yes_selected)
        _gfx.fillRoundRect(x_yes, by, bw, bh, 12, COL_SEL_BG);
    else
        _gfx.drawRoundRect(x_yes, by, bw, bh, 12, COL_FG);
    _gfx.setTextColor(yes_selected ? COL_SEL_FG : COL_FG,
                      yes_selected ? COL_SEL_BG : COL_BG);
    _gfx.setCursor(x_yes + (bw - 3 * CHAR_W) / 2, by + (bh - 24) / 2);
    _gfx.print("Yes");

    if (!yes_selected)
        _gfx.fillRoundRect(x_no, by, bw, bh, 12, COL_SEL_BG);
    else
        _gfx.drawRoundRect(x_no, by, bw, bh, 12, COL_FG);
    _gfx.setTextColor(!yes_selected ? COL_SEL_FG : COL_FG,
                      !yes_selected ? COL_SEL_BG : COL_BG);
    _gfx.setCursor(x_no + (bw - 2 * CHAR_W) / 2, by + (bh - 24) / 2);
    _gfx.print("No");

    _gfx.setTextColor(COL_FG, COL_BG);
    disp_status("");   // always draw button bar for confirm too
}

// ============================================================================
#else   // original TFT_eSPI / ST7789 240×240 implementation
// ============================================================================

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
    char lines_buf[48][COLS + 1];   // up to 48 wrapped lines

    while (pos < len && total_lines < 48) {
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

#endif // TOUCH4_BUILD
#endif // DISPLAY_H
