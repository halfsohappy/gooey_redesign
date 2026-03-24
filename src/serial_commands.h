// =============================================================================
// serial_commands.h — Serial monitor command interface for debugging
// =============================================================================
//
// Processes simple text commands from the USB serial monitor so the device
// can be inspected and diagnosed without relying on a working network
// connection.  Type "help" in the serial monitor for a list of commands.
//
// USAGE:
//   Call serial_process() from loop() to poll for incoming serial data.
//
// COMMANDS:
//   help                 — list available commands
//   status               — show device status (WiFi, IP, provisioning)
//   streams              — show current sensor data stream values
//   config               — show provisioned network configuration
//   nvs                  — show NVS storage summary
//   registry             — show OSC registry (patches + messages)
//   serial [level]       — get/set serial debug level (error/warn/info/debug)
//   sends [on|off]       — show or toggle per-message send logging to serial
//   dedup [on|off]       — show or toggle duplicate value suppression
//   hardware             — show hardware diagnostics (voltages, sensor init)
//   restart              — reboot the device
//   provision            — erase provisioning and reboot into captive portal
//   uptime               — show time since boot
// =============================================================================

#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#ifdef AB7_BUILD
#include "ab7_hardware.h"
#else
#include "bart_hardware.h"
#endif
#include "data_streams.h"
#include "osc_engine.h"
#include "osc_registry.h"
#include "osc_status.h"

// Forward-declare the device address (defined in main.h / main.cpp).
extern String device_adr;

// ---------------------------------------------------------------------------
// Internal line buffer for serial input
// ---------------------------------------------------------------------------

static String _serial_line_buf;

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static inline void _serial_cmd_help() {
    Serial.println(F("──────────── Serial Debug Commands ────────────"));
    Serial.println(F("  help         — show this list"));
    Serial.println(F("  status       — WiFi / network / device status"));
    Serial.println(F("  streams      — current sensor data stream values"));
    Serial.println(F("  config       — provisioned network configuration"));
    Serial.println(F("  nvs          — NVS storage summary (osc_store)"));
    Serial.println(F("  registry     — OSC registry (patches + messages)"));
    Serial.println(F("  serial [lvl] — get/set serial debug level"));
    Serial.println(F("               — levels: error, warn, info, debug"));
    Serial.println(F("  sends [on|off] — show or set per-message send logging"));
    Serial.println(F("  dedup [on|off] — show or set duplicate suppression"));
    Serial.println(F("  hardware     — hardware diagnostics"));
    Serial.println(F("  restart      — reboot the device"));
    Serial.println(F("  provision    — erase config & reboot into portal"));
    Serial.println(F("  uptime       — time since boot"));
    Serial.println(F("───────────────────────────────────────────────"));
}

static inline void _serial_cmd_status() {
    Serial.println(F("──────────── Device Status ────────────"));
    Serial.print(F("  Device address : "));
    if (device_adr.length() > 0)
        Serial.println(device_adr);
    else
        Serial.println(F("(not set)"));
    Serial.print(F("  WiFi status    : "));
    switch (WiFi.status()) {
        case WL_CONNECTED:     Serial.println(F("CONNECTED")); break;
        case WL_DISCONNECTED:  Serial.println(F("DISCONNECTED")); break;
        case WL_IDLE_STATUS:   Serial.println(F("IDLE")); break;
        case WL_NO_SSID_AVAIL: Serial.println(F("NO SSID AVAILABLE")); break;
        case WL_CONNECT_FAILED:Serial.println(F("CONNECT FAILED")); break;
        default:               Serial.println(WiFi.status()); break;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("  SSID           : "));
        Serial.println(WiFi.SSID());
        Serial.print(F("  IP address     : "));
        Serial.println(WiFi.localIP().toString());
        Serial.print(F("  RSSI           : "));
        Serial.print(WiFi.RSSI());
        Serial.println(F(" dBm"));
        Serial.print(F("  MAC address    : "));
        Serial.println(WiFi.macAddress());
    }
    Serial.print(F("  Free heap      : "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));
    Serial.print(F("  Uptime         : "));
    Serial.print(millis() / 1000);
    Serial.println(F(" s"));

    OscRegistry& reg = osc_registry();
    Serial.print(F("  Patches        : "));
    Serial.print(reg.patch_count);
    Serial.print(F(" / "));
    Serial.println(MAX_OSC_PATCHES);
    Serial.print(F("  Messages       : "));
    Serial.print(reg.msg_count);
    Serial.print(F(" / "));
    Serial.println(MAX_OSC_MESSAGES);
    Serial.println(F("───────────────────────────────────────"));
}

static inline void _serial_cmd_streams() {
    Serial.println(F("──────────── Data Streams ────────────"));
    for (int i = 0; i < NUM_DATA_STREAMS; i++) {
        Serial.print(F("  ["));
        if (i < 10) Serial.print(' ');
        Serial.print(i);
        Serial.print(F("] "));
        String name = data_stream_name(i);
        // Pad name to 12 chars for alignment.
        while (name.length() < 12) name += ' ';
        Serial.print(name);
        Serial.print(F(" = "));
        Serial.println((float)data_streams[i], 4);
    }
    Serial.println(F("──────────────────────────────────────"));
}

static inline void _serial_cmd_config() {
    Serial.println(F("──────────── Provisioned Config ────────────"));
    Preferences prefs;
    prefs.begin("device_config", true);  // read-only

    bool provisioned = prefs.getBool("provisioned", false);
    Serial.print(F("  Provisioned : "));
    Serial.println(provisioned ? "YES" : "NO");

    if (provisioned) {
        Serial.print(F("  SSID        : "));
        Serial.println(prefs.getString("ssid", "(empty)"));
        Serial.print(F("  Password    : "));
        String pw = prefs.getString("net_pass", "");
        Serial.println(pw.length() > 0 ? "(set)" : "(empty)");
        bool use_dhcp = prefs.getBool("use_dhcp", true);
        Serial.print(F("  DHCP        : "));
        Serial.println(use_dhcp ? "YES" : "NO");
        if (!use_dhcp) {
            Serial.print(F("  Static IP   : "));
            Serial.println(prefs.getString("static_ip", "(empty)"));
        }
        Serial.print(F("  Port        : "));
        Serial.println(prefs.getInt("port", 0));
        Serial.print(F("  Device name : "));
        Serial.println(prefs.getString("device_adr", "(empty)"));
    }
    prefs.end();
    Serial.println(F("────────────────────────────────────────────"));
}

static inline void _serial_cmd_nvs() {
    Serial.println(F("──────────── NVS osc_store ────────────"));
    Preferences prefs;
    prefs.begin("osc_store", true);

    uint16_t p_count = prefs.getUShort("p_count", 0);
    uint16_t m_count = prefs.getUShort("m_count", 0);
    Serial.print(F("  Saved patches  : "));
    Serial.println(p_count);
    Serial.print(F("  Saved messages : "));
    Serial.println(m_count);

    for (uint16_t i = 0; i < p_count; i++) {
        String key = "p_" + String(i);
        String val = prefs.getString(key.c_str(), "");
        Serial.print(F("  ["));
        Serial.print(key);
        Serial.print(F("] "));
        Serial.println(val);
    }
    for (uint16_t i = 0; i < m_count; i++) {
        String key = "m_" + String(i);
        String val = prefs.getString(key.c_str(), "");
        Serial.print(F("  ["));
        Serial.print(key);
        Serial.print(F("] "));
        Serial.println(val);
    }

    prefs.end();
    Serial.println(F("────────────────────────────────────────"));
}

static inline void _serial_cmd_registry() {
    OscRegistry& reg = osc_registry();
    Serial.println(F("──────────── OSC Registry ────────────"));

    Serial.print(F("  Patches ("));
    Serial.print(reg.patch_count);
    Serial.println(F("):"));
    for (uint16_t i = 0; i < reg.patch_count; i++) {
        OscPatch& p = reg.patches[i];
        Serial.print(F("    ["));
        Serial.print(i);
        Serial.print(F("] \""));
        Serial.print(p.name);
        Serial.print(F("\"  msgs:"));
        Serial.print(p.msg_count);
        Serial.print(F("  period:"));
        Serial.print(p.send_period_ms);
        Serial.print(F("ms  "));
        Serial.print(p.enabled ? F("ENABLED") : F("DISABLED"));
        Serial.println(p.task_handle ? F("  RUNNING") : F("  STOPPED"));
    }

    Serial.print(F("  Messages ("));
    Serial.print(reg.msg_count);
    Serial.println(F("):"));
    for (uint16_t i = 0; i < reg.msg_count; i++) {
        OscMessage& m = reg.messages[i];
        Serial.print(F("    ["));
        Serial.print(i);
        Serial.print(F("] \""));
        Serial.print(m.name);
        Serial.print(F("\"  "));
        if (m.exist.val) {
            int idx = data_stream_index_from_ptr(m.value_ptr);
            Serial.print(F("val:"));
            if (idx >= 0)
                Serial.print(data_stream_name(idx));
            else
                Serial.print(F("?"));
            Serial.print(F("  "));
        }
        if (m.exist.ip) {
            Serial.print(F("ip:"));
            Serial.print(m.ip.toString());
            Serial.print(F("  "));
        }
        if (m.exist.port) {
            Serial.print(F("port:"));
            Serial.print(m.port);
            Serial.print(F("  "));
        }
        if (m.exist.adr) {
            Serial.print(F("adr:"));
            Serial.print(m.osc_address);
            Serial.print(F("  "));
        }
        Serial.println(m.enabled ? F("ENABLED") : F("DISABLED"));
    }

    Serial.println(F("──────────────────────────────────────"));
}

static inline void _serial_cmd_serial(const String& arg) {
    if (arg.length() == 0) {
        Serial.print(F("  Serial debug level: "));
        Serial.println(status_level_label(status_reporter().serial_level));
        Serial.print(F("  OSC status level  : "));
        Serial.println(status_level_label(status_reporter().min_level));
    } else {
        StatusLevel lvl = status_level_from_string(arg);
        status_reporter().set_serial_level(lvl);
        Serial.print(F("  Serial debug level set to: "));
        Serial.println(status_level_label(lvl));
    }
}

static inline void _serial_cmd_sends(const String& arg) {
    if (arg.length() == 0) {
        Serial.print(F("  Send logging: "));
        Serial.println(get_send_logging_enabled() ? F("ON") : F("OFF"));
        return;
    }

    String a = arg;
    a.toLowerCase();
    if (a == "on" || a == "1" || a == "true") {
        set_send_logging_enabled(true);
        Serial.println(F("  Send logging enabled."));
    } else if (a == "off" || a == "0" || a == "false") {
        set_send_logging_enabled(false);
        Serial.println(F("  Send logging disabled."));
    } else {
        Serial.println(F("  Usage: sends [on|off]"));
    }
}

static inline void _serial_cmd_dedup(const String& arg) {
    if (arg.length() == 0) {
        Serial.print(F("  Duplicate suppression: "));
        Serial.println(get_dedup_enabled() ? F("ON") : F("OFF"));
        return;
    }

    String a = arg;
    a.toLowerCase();
    if (a == "on" || a == "1" || a == "true") {
        set_dedup_enabled(true);
        Serial.println(F("  Duplicate suppression enabled."));
    } else if (a == "off" || a == "0" || a == "false") {
        set_dedup_enabled(false);
        Serial.println(F("  Duplicate suppression disabled."));
    } else {
        Serial.println(F("  Usage: dedup [on|off]"));
    }
}

static inline void _serial_cmd_hardware() {
#ifdef AB7_BUILD
    Serial.println(F("──────────── Hardware Diagnostics (ab7) ────────────"));
#else
    Serial.println(F("──────────── Hardware Diagnostics (bart) ────────────"));
#endif

    // ESP32 info
    Serial.print(F("  Chip model   : "));
    Serial.println(ESP.getChipModel());
    Serial.print(F("  CPU freq     : "));
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(F(" MHz"));
    Serial.print(F("  Flash size   : "));
    Serial.print(ESP.getFlashChipSize() / 1024);
    Serial.println(F(" KB"));
    Serial.print(F("  Free heap    : "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));
    Serial.print(F("  Min free heap: "));
    Serial.print(ESP.getMinFreeHeap());
    Serial.println(F(" bytes"));

#ifdef AB7_BUILD
    // Button states (active-low)
    Serial.print(F("  BTN_A (GPIO0)  : "));
    Serial.println(digitalRead(BTN_A) == LOW ? "PRESSED" : "released");
    Serial.print(F("  BTN_B (GPIO14) : "));
    Serial.println(digitalRead(BTN_B) == LOW ? "PRESSED" : "released");
#else
    // Bart-specific pin states
    Serial.print(F("  SEL13 (GPIO11) : "));
    Serial.println(digitalRead(SEL13));
    Serial.print(F("  SEL46 (GPIO12) : "));
    Serial.println(digitalRead(SEL46));
    Serial.print(F("  CC_EN1 (GPIO13): "));
    Serial.println(digitalRead(CC_EN1));
    Serial.print(F("  CC_EN2 (GPIO14): "));
    Serial.println(digitalRead(CC_EN2));
#endif

    Serial.println(F("──────────────────────────────────────────────────"));
}

static inline void _serial_cmd_uptime() {
    unsigned long ms = millis();
    unsigned long secs = ms / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs  = mins / 60;
    Serial.print(F("  Uptime: "));
    Serial.print(hrs);
    Serial.print(F("h "));
    Serial.print(mins % 60);
    Serial.print(F("m "));
    Serial.print(secs % 60);
    Serial.print(F("s  ("));
    Serial.print(ms);
    Serial.println(F(" ms)"));
}

// ---------------------------------------------------------------------------
// Main serial processor — call this from loop()
// ---------------------------------------------------------------------------

/// Read incoming serial bytes and process complete lines as commands.
/// Non-blocking: returns immediately if no data is available.
static inline void serial_process() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            _serial_line_buf.trim();
            if (_serial_line_buf.length() > 0) {
                // Split into command and argument.
                String cmd = _serial_line_buf;
                String arg;
                int space = cmd.indexOf(' ');
                if (space >= 0) {
                    arg = cmd.substring(space + 1);
                    cmd = cmd.substring(0, space);
                    arg.trim();
                }
                cmd.toLowerCase();

                // Dispatch.
                if (cmd == "help" || cmd == "?") {
                    _serial_cmd_help();
                } else if (cmd == "status") {
                    _serial_cmd_status();
                } else if (cmd == "streams") {
                    _serial_cmd_streams();
                } else if (cmd == "config") {
                    _serial_cmd_config();
                } else if (cmd == "nvs") {
                    _serial_cmd_nvs();
                } else if (cmd == "registry") {
                    _serial_cmd_registry();
                } else if (cmd == "serial") {
                    _serial_cmd_serial(arg);
                } else if (cmd == "sends") {
                    _serial_cmd_sends(arg);
                } else if (cmd == "dedup") {
                    _serial_cmd_dedup(arg);
                } else if (cmd == "hardware" || cmd == "hw") {
                    _serial_cmd_hardware();
                } else if (cmd == "restart" || cmd == "reboot") {
                    Serial.println(F("Restarting..."));
                    delay(500);
                    ESP.restart();
                } else if (cmd == "provision") {
                    Serial.println(F("This will erase all provisioning data and reboot."));
                    Serial.println(F("Type 'yes' to confirm:"));
                    // Wait up to 10 seconds for confirmation.
                    unsigned long t0 = millis();
                    String confirm;
                    while (millis() - t0 < 10000) {
                        if (Serial.available()) {
                            char ch = Serial.read();
                            if (ch == '\n' || ch == '\r') break;
                            confirm += ch;
                        }
                        delay(10);
                    }
                    confirm.trim();
                    confirm.toLowerCase();
                    if (confirm == "yes" || confirm == "y") {
                        Serial.println(F("Erasing provisioning data and rebooting..."));
                        Preferences prefs;
                        prefs.begin("device_config", false);
                        prefs.clear();
                        prefs.end();
                        delay(500);
                        ESP.restart();
                    } else {
                        Serial.println(F("  Cancelled."));
                    }
                } else if (cmd == "uptime") {
                    _serial_cmd_uptime();
                } else {
                    Serial.print(F("Unknown command: '"));
                    Serial.print(cmd);
                    Serial.println(F("'.  Type 'help' for a list of commands."));
                }
            }
            _serial_line_buf = "";
        } else {
            _serial_line_buf += c;
        }
    }
}

#endif // SERIAL_COMMANDS_H
