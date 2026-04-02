#ifndef BC127_DISPLAY_H
#define BC127_DISPLAY_H

// =============================================================================
// display.h — M5Stack CoreS3 display: OSC feed + DMX channel monitor
// =============================================================================
//
// Two views, toggled by touching the screen:
//   1. OSC Feed   — scrolling log of recent OSC messages
//   2. DMX Monitor — grid of all 64 active channels with live values
//
// =============================================================================

#include <M5Unified.h>
#include "config.h"
#include "dmx_engine.h"
#include "osc_handler.h"   // for osc_log_at / osc_log_count
#include "fixture_map.h"

// ==== View Mode =============================================================

enum DisplayView : uint8_t {
    VIEW_OSC_FEED,
    VIEW_DMX_MONITOR
};

static DisplayView current_view = VIEW_OSC_FEED;

// ==== Colours (16-bit 565) ==================================================

static const uint16_t COL_BG       = 0x0000;  // black
static const uint16_t COL_HEADER   = 0xD69A;  // lavender-ish (DAC7FF approx)
static const uint16_t COL_TEXT     = 0xFFFF;  // white
static const uint16_t COL_DIM      = 0x7BEF;  // grey
static const uint16_t COL_ACCENT   = 0xB1BE;  // accent purple
static const uint16_t COL_ALERT    = 0xF800;  // red (blackout indicator)
static const uint16_t COL_GREEN    = 0x07E0;  // green
static const uint16_t COL_CELL_BG  = 0x18E3;  // dark grey cell background

// ==== Init ==================================================================

inline void display_init() {
    M5.Display.fillScreen(COL_BG);
    M5.Display.setTextColor(COL_TEXT, COL_BG);
    M5.Display.setTextSize(1);
}

// ==== Header Bar ============================================================

static void draw_header() {
    M5.Display.fillRect(0, 0, DISPLAY_WIDTH, 20, COL_HEADER);
    M5.Display.setTextColor(TFT_BLACK, COL_HEADER);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(4, 4);
    M5.Display.print("annieData DMX");

    // Blackout indicator
    if (dmx_is_blackout()) {
        M5.Display.setTextColor(COL_ALERT, COL_HEADER);
        M5.Display.setCursor(DISPLAY_WIDTH - 70, 4);
        M5.Display.print("BLACKOUT");
    } else {
        M5.Display.setTextColor(COL_GREEN, COL_HEADER);
        M5.Display.setCursor(DISPLAY_WIDTH - 40, 4);
        M5.Display.print("LIVE");
    }

    // View label
    M5.Display.setTextColor(TFT_BLACK, COL_HEADER);
    M5.Display.setCursor(DISPLAY_WIDTH / 2 - 20, 4);
    if (current_view == VIEW_OSC_FEED)
        M5.Display.print("OSC");
    else
        M5.Display.print("DMX");
}

// ==== OSC Feed View =========================================================

static void draw_osc_feed() {
    int y = 24;
    int line_h = 15;
    int max_lines = (DISPLAY_HEIGHT - y) / line_h;
    if (max_lines > osc_log_count) max_lines = osc_log_count;

    M5.Display.fillRect(0, 22, DISPLAY_WIDTH, DISPLAY_HEIGHT - 22, COL_BG);
    M5.Display.setTextColor(COL_TEXT, COL_BG);
    M5.Display.setTextSize(1);

    // Show most recent lines at the bottom
    int first = osc_log_count - max_lines;
    for (int i = 0; i < max_lines; i++) {
        const String& line = osc_log_at(first + i);
        M5.Display.setCursor(4, y + i * line_h);
        // Truncate lines that exceed display width
        static constexpr int MAX_DISPLAY_LINE_LEN = 50;
        if ((int)line.length() > MAX_DISPLAY_LINE_LEN) {
            M5.Display.print(line.substring(0, MAX_DISPLAY_LINE_LEN));
        } else {
            M5.Display.print(line);
        }
    }
}

// ==== DMX Monitor View ======================================================
// Shows the 64 active channels (1–64) in a grid with values.

static void draw_dmx_monitor() {
    M5.Display.fillRect(0, 22, DISPLAY_WIDTH, DISPLAY_HEIGHT - 22, COL_BG);
    M5.Display.setTextSize(1);

    // 8 columns × 8 rows = 64 channels
    int cols = 8;
    int rows = 8;
    int cell_w = DISPLAY_WIDTH / cols;
    int cell_h = (DISPLAY_HEIGHT - 24) / rows;
    int y_off = 24;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int ch = r * cols + c + 1;  // 1-based channel

            int x = c * cell_w;
            int y = y_off + r * cell_h;

            uint8_t val = dmx_get_channel(ch);

            // Background intensity proportional to value
            uint8_t br = val >> 3;  // 0–31 range for 565 green channel
            uint16_t bg = (br > 0) ? M5.Display.color565(0, val / 4, val / 8) : COL_CELL_BG;
            M5.Display.fillRect(x + 1, y + 1, cell_w - 2, cell_h - 2, bg);

            // Channel number (top-left of cell, small)
            M5.Display.setTextColor(COL_DIM, bg);
            M5.Display.setCursor(x + 2, y + 2);
            M5.Display.printf("%d", ch);

            // Value (centred in cell)
            M5.Display.setTextColor(COL_TEXT, bg);
            M5.Display.setCursor(x + 2, y + cell_h / 2);
            M5.Display.printf("%3d", val);
        }
    }
}

// ==== Main Draw =============================================================

inline void display_update() {
    draw_header();
    if (current_view == VIEW_OSC_FEED) {
        draw_osc_feed();
    } else {
        draw_dmx_monitor();
    }
}

// ==== Touch Toggle ==========================================================

inline void display_check_touch() {
    M5.update();
    if (M5.Touch.getCount() > 0) {
        auto t = M5.Touch.getDetail();
        if (t.wasPressed()) {
            current_view = (current_view == VIEW_OSC_FEED)
                           ? VIEW_DMX_MONITOR
                           : VIEW_OSC_FEED;
            M5.Display.fillScreen(COL_BG);
        }
    }
}

#endif // BC127_DISPLAY_H
