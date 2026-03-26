// ============================================================================
//  TheaterGWD Setup Remote — Main Entry Point
// ============================================================================
//
//  Hardware:
//    - ESP32-S3 with built-in 1.3" ST7789V2 240×240 color LCD
//    - Adafruit Mini I2C Gamepad QT  (I2C 0x50, SDA=IO1, SCL=IO2)
//
//  On first boot the device enters WiFi provisioning mode — a captive
//  portal lets the user configure WiFi credentials and target device
//  info from any browser.  On subsequent boots it connects directly.
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

    // ── TFT display ─────────────────────────────────────────────────────
    if (!disp_init()) {
        Serial.println("TFT init failed");
        while (true) delay(1000);
    }
    disp_clear();
    disp_message("Setup Remote", "starting...");

    // ── I2C bus (for Gamepad QT) ────────────────────────────────────────
    Wire.begin(PIN_SDA, PIN_SCL);

    // ── Gamepad QT input ────────────────────────────────────────────────
    if (!input_init()) {
        Serial.println("Gamepad QT init failed — check wiring / address");
        disp_clear();
        disp_message("Gamepad FAIL", "check wiring");
        while (true) delay(1000);
    }

    // ── Load saved settings ─────────────────────────────────────────────
    net_load();

    // ── WiFi provisioning or connect ────────────────────────────────────
    if (!net_is_provisioned()) {
        Serial.println("Not provisioned — starting captive portal");
        disp_clear();
        disp_message("WiFi Setup", "Connect to AP:");
        disp_clear();
        disp_message("annieData Remote", "then open browser");
        net_provision();   // blocks until complete, then restarts
        return;            // never reached
    }

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

        // Wait up to 10 s for connection
        unsigned long t0 = millis();
        while (net_update() == NET_CONNECTING && millis() - t0 < 10000) {
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
