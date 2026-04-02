#ifndef BC127_OSC_HANDLER_H
#define BC127_OSC_HANDLER_H

// =============================================================================
// osc_handler.h — OSC message parsing and DMX dispatch
// =============================================================================
//
// ADDRESS SCHEME (all addresses start with /annieData/dmx):
//
//   /annieData/dmx/{channel}                  payload: "0"–"255"
//       Set a single DMX channel directly.
//
//   /annieData/dmx/fix/{head}/dimmer          payload: "0"–"255"
//   /annieData/dmx/fix/{head}/red             payload: "0"–"255"
//   /annieData/dmx/fix/{head}/green           payload: "0"–"255"
//   /annieData/dmx/fix/{head}/blue            payload: "0"–"255"
//   /annieData/dmx/fix/{head}/shutter         payload: "0"–"255"
//       Control individual attributes of a ColorSourcePar fixture.
//       For a dimmer-only head, /dimmer sets its single channel.
//
//   /annieData/dmx/fix/{head}/color           payload: "dark pink"
//       Look up an XKCD colour name and apply RGB + dimmer=255.
//
//   /annieData/dmx/fix/{head}/rgb             payload: "c79fef"
//       Parse a hex RGB string and apply RGB + dimmer=255.
//
//   /annieData/dmx/group/{name}/dimmer        payload: "0"–"255"
//   /annieData/dmx/group/{name}/color         payload: "teal"
//   /annieData/dmx/group/{name}/rgb           payload: "aabbcc"
//       Apply a command to every fixture in a named group.
//
//   /annieData/dmx/blackout                   (any payload)
//       Output all-zero universe.  Underlying values preserved.
//
//   /annieData/dmx/restore                    (any payload)
//       Resume sending the stored channel values.
//
// =============================================================================

#include <Arduino.h>
#include <MicroOsc.h>
#include "config.h"
#include "dmx_engine.h"
#include "fixture_map.h"
#include "xkcd_colors.h"

// ==== OSC Log (circular buffer shown on display) ============================

static String osc_log_lines[OSC_LOG_LINES];
static int    osc_log_head = 0;
static int    osc_log_count = 0;

inline void osc_log(const String& line) {
    osc_log_lines[osc_log_head] = line;
    osc_log_head = (osc_log_head + 1) % OSC_LOG_LINES;
    if (osc_log_count < OSC_LOG_LINES) osc_log_count++;
}

// Return the i-th oldest log line (0 = oldest visible)
inline const String& osc_log_at(int i) {
    int start = (osc_log_count < OSC_LOG_LINES)
                ? 0
                : osc_log_head;
    int idx = (start + i) % OSC_LOG_LINES;
    return osc_log_lines[idx];
}

// ==== Normalise command segment =============================================
// Strips underscores and lowercases so camelCase, snake_case, and lowercase
// all match.

static String normalise_cmd(const String& seg) {
    String out;
    out.reserve(seg.length());
    for (unsigned int i = 0; i < seg.length(); i++) {
        char c = seg.charAt(i);
        if (c != '_') out += (char)tolower(c);
    }
    return out;
}

// ==== Fixture-level command helpers =========================================

static void apply_fixture_attr(int head, const String& attr, const String& payload) {
    const Fixture* f = get_fixture(head);
    if (!f) return;

    String cmd = normalise_cmd(attr);
    int val = constrain(payload.toInt(), 0, 255);

    if (cmd == "dimmer" || cmd == "dim" || cmd == "intensity") {
        if (f->type == FIX_COLORSOURCEPAR) {
            dmx_set_channel(par_dimmer_addr(*f), val);
        } else {
            // Generic dimmer — single channel
            dmx_set_channel(f->dmx_start, val);
        }
    } else if (cmd == "red" || cmd == "r") {
        if (f->type == FIX_COLORSOURCEPAR) dmx_set_channel(par_red_addr(*f), val);
    } else if (cmd == "green" || cmd == "g") {
        if (f->type == FIX_COLORSOURCEPAR) dmx_set_channel(par_green_addr(*f), val);
    } else if (cmd == "blue" || cmd == "b") {
        if (f->type == FIX_COLORSOURCEPAR) dmx_set_channel(par_blue_addr(*f), val);
    } else if (cmd == "shutter" || cmd == "strobe") {
        if (f->type == FIX_COLORSOURCEPAR) dmx_set_channel(par_shutter_addr(*f), val);
    } else if (cmd == "color" || cmd == "colour") {
        uint32_t rgb = xkcd_lookup(payload);
        if (rgb == XKCD_NOT_FOUND) return;
        if (f->type == FIX_COLORSOURCEPAR) {
            dmx_set_channel(par_dimmer_addr(*f),  255);
            dmx_set_channel(par_red_addr(*f),     colour_r(rgb));
            dmx_set_channel(par_green_addr(*f),   colour_g(rgb));
            dmx_set_channel(par_blue_addr(*f),    colour_b(rgb));
        } else {
            // Dimmer fixture: set to perceived brightness of the colour
            dmx_set_channel(f->dmx_start, colour_brightness(rgb));
        }
    } else if (cmd == "rgb" || cmd == "hex") {
        uint32_t rgb = parse_hex_rgb(payload);
        if (rgb == XKCD_NOT_FOUND) return;
        if (f->type == FIX_COLORSOURCEPAR) {
            dmx_set_channel(par_dimmer_addr(*f),  255);
            dmx_set_channel(par_red_addr(*f),     colour_r(rgb));
            dmx_set_channel(par_green_addr(*f),   colour_g(rgb));
            dmx_set_channel(par_blue_addr(*f),    colour_b(rgb));
        } else {
            dmx_set_channel(f->dmx_start, colour_brightness(rgb));
        }
    }
}

// ==== Main OSC message handler ==============================================

static void osc_handle_dmx(MicroOscMessage& msg) {
    String address = msg.getOscAddress();

    // Parse payload — first argument (string, int, or float)
    String payload;
    const char* types = msg.getTypeTags();
    if (types && types[0] == 's') {
        payload = msg.nextAsString();
    } else if (types && types[0] == 'i') {
        payload = String(msg.nextAsInt());
    } else if (types && types[0] == 'f') {
        payload = String((int)msg.nextAsFloat());
    } else {
        payload = msg.nextAsString();  // fallback
    }

    // Log the message
    osc_log(String(address) + " " + payload);
    Serial.printf("[OSC] %s %s\n", address.c_str(), payload.c_str());

    // ------------------------------------------------------------------
    // Tokenise the address into segments.
    // Expected prefix: /annieData/dmx/...
    // We split after the prefix.
    // ------------------------------------------------------------------

    // Find "/dmx/" in the address
    int dmx_idx = address.indexOf("/dmx");
    if (dmx_idx < 0) return;

    // Get everything after /dmx
    String rest = address.substring(dmx_idx + 4);   // e.g. "/12", "/fix/3/color", "/blackout"
    if (rest.startsWith("/")) rest = rest.substring(1);
    if (rest.length() == 0) return;

    // Split rest by '/'
    static constexpr int MAX_OSC_SEGMENTS = 4;
    String segments[MAX_OSC_SEGMENTS];
    int seg_count = 0;
    int start = 0;
    for (unsigned int i = 0; i <= rest.length() && seg_count < MAX_OSC_SEGMENTS; i++) {
        if (i == rest.length() || rest.charAt(i) == '/') {
            if ((int)i > start) {
                segments[seg_count++] = rest.substring(start, i);
            }
            start = i + 1;
        }
    }

    if (seg_count == 0) return;

    String seg0 = normalise_cmd(segments[0]);

    // ------------------------------------------------------------------
    // /annieData/dmx/blackout
    // ------------------------------------------------------------------
    if (seg0 == "blackout") {
        dmx_blackout_on();
        osc_log(">>> BLACKOUT");
        Serial.println("[DMX] Blackout ON");
        return;
    }

    // ------------------------------------------------------------------
    // /annieData/dmx/restore
    // ------------------------------------------------------------------
    if (seg0 == "restore") {
        dmx_blackout_off();
        osc_log(">>> RESTORE");
        Serial.println("[DMX] Blackout OFF (restored)");
        return;
    }

    // ------------------------------------------------------------------
    // /annieData/dmx/{channel}  — direct channel set
    // ------------------------------------------------------------------
    {
        int ch = segments[0].toInt();
        if (ch >= 1 && ch <= DMX_UNIVERSE_SIZE) {
            int val = constrain(payload.toInt(), 0, 255);
            dmx_set_channel(ch, val);
            return;
        }
    }

    // ------------------------------------------------------------------
    // /annieData/dmx/fix/{head}/{attr}
    // ------------------------------------------------------------------
    if (seg0 == "fix" || seg0 == "fixture") {
        if (seg_count < 3) return;
        int head = segments[1].toInt();
        apply_fixture_attr(head, segments[2], payload);
        return;
    }

    // ------------------------------------------------------------------
    // /annieData/dmx/group/{name}/{attr}
    // ------------------------------------------------------------------
    if (seg0 == "group" || seg0 == "grp") {
        if (seg_count < 3) return;
        const FixtureGroup* grp = find_group(segments[1]);
        if (!grp) return;
        for (uint8_t i = 0; i < grp->count; i++) {
            apply_fixture_attr(grp->heads[i], segments[2], payload);
        }
        return;
    }
}

#endif // BC127_OSC_HANDLER_H
