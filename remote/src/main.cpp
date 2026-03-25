// ============================================================================
//  TheaterGWD Setup Remote — Main Entry Point
// ============================================================================
//
//  Hardware:
//    - Olimex ESP32-C3-DevKit-Lipo (or any ESP32-C3 board)
//    - SSD1306 128×64 OLED display  (I2C 0x3C)
//    - Adafruit Seesaw ANO Rotary Navigation Encoder  (I2C 0x49)
//
//  This handheld remote connects to the same WiFi as your TheaterGWD
//  devices and sends OSC commands to create/edit messages, manage patches,
//  save/reset oris, start/stop sending, and monitor status — all from a
//  tiny screen and a rotary encoder.
//
//  Build:
//    cd remote && pio run            # compile
//    cd remote && pio run -t upload  # flash
//    cd remote && pio device monitor # serial 115200
//
// ============================================================================

#include <Arduino.h>
#include <Wire.h>

// Project headers (order matters — later headers depend on earlier ones)
#include "config.h"
#include "osc_remote.h"
#include "network.h"
#include "display.h"
#include "input.h"
#include "ui.h"

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== TheaterGWD Setup Remote ===");

    // ── I2C bus ─────────────────────────────────────────────────────────
    Wire.begin(PIN_SDA, PIN_SCL);

    // ── OLED display ────────────────────────────────────────────────────
    if (!disp_init()) {
        Serial.println("SSD1306 init failed — check wiring / address");
        while (true) delay(1000);
    }
    disp_clear();
    disp_message("Setup Remote", "starting...");
    disp_show();

    // ── Seesaw ANO input ────────────────────────────────────────────────
    if (!input_init()) {
        Serial.println("Seesaw init failed — check wiring / address");
        disp_clear();
        disp_message("Seesaw FAIL", "check wiring");
        disp_show();
        while (true) delay(1000);
    }
    led_set(0, 0, 40);  // blue = booting

    // ── Load saved settings ─────────────────────────────────────────────
    net_load();
    Serial.printf("SSID : %s\n", net_ssid);
    Serial.printf("Target: %s @ %d.%d.%d.%d:%d\n",
                  target_name,
                  target_ip[0], target_ip[1], target_ip[2], target_ip[3],
                  target_port);

    // ── Connect WiFi ────────────────────────────────────────────────────
    if (net_has_credentials()) {
        net_connect();
        disp_clear();
        disp_message("Connecting WiFi...", net_ssid);
        disp_show();

        // Wait up to 10 s for connection (non-blocking afterwards)
        unsigned long t0 = millis();
        while (net_update() == NET_CONNECTING && millis() - t0 < 10000) {
            led_spin(0, 0, 60, ((millis() - t0) / 150) % SS_NEOPIX_NUM);
            delay(50);
        }

        if (net_state() == NET_CONNECTED) {
            Serial.printf("WiFi connected — IP %s\n",
                          WiFi.localIP().toString().c_str());
        } else {
            Serial.println("WiFi connection failed");
        }
    } else {
        Serial.println("No WiFi credentials — configure via Settings menu");
    }

    // ── Start OSC listener ──────────────────────────────────────────────
    osc_init(listen_port);

    // ── Initialise UI ───────────────────────────────────────────────────
    ui_init();
    Serial.println("Ready.");
}

// ── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    net_update();
    ui_update();
    delay(10);   // ~100 Hz UI refresh, keep power draw low
}
