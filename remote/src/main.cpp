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
#include "wifi_net.h"
#include "display.h"
#include "input.h"
#include "ui.h"

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== TheaterGWD Setup Remote ===");

#ifdef TOUCH4_BUILD
    // ── TCA9554PWR I2C GPIO expander ────────────────────────────────────
    //  Controls LCD reset, touch reset, and backlight enable via EXIO0–2.
    //  Uses Wire1 (SDA=IO15, SCL=IO7) — the "new board" I2C1 wiring that
    //  shares the bus with GT911 touch and the RTC.
    //
    //  Old-board owners: the expander is on I2C0 (SDA=IO8, SCL=IO9).
    //  Change Wire1 to Wire in this block and in input_read() if needed.

    Wire1.begin(PIN_TP_SDA, PIN_TP_SCL);

    // Helper: write one byte to a TCA9554 register
    auto tca_write = [](uint8_t reg, uint8_t val) {
        Wire1.beginTransmission(TCA9554_ADDR);
        Wire1.write(reg);
        Wire1.write(val);
        Wire1.endTransmission();
    };

    // EXIO0/1/2 as outputs (0 = output in TCA9554 config register 0x03)
    tca_write(0x03, 0xF8);   // bits 0-2 = output, bits 3-7 = input (unused)
    // Assert all resets, backlight off
    tca_write(0x01, 0x00);
    delay(10);
    // Release LCD reset (EXIO2) and touch reset (EXIO0)
    tca_write(0x01, (1 << EXIO_LCD_RST) | (1 << EXIO_TP_RST));   // 0x05
    delay(120);   // GT911 requires ≥100 ms after reset before I2C is ready
    Serial.println("TCA9554 init done");
#endif

    // ── TFT / LCD display ────────────────────────────────────────────────
    if (!disp_init()) {
        Serial.println("TFT init failed");
        while (true) delay(1000);
    }

#ifdef TOUCH4_BUILD
    // Enable backlight now that the panel is initialised
    tca_write(0x01, (1 << EXIO_LCD_RST) | (1 << EXIO_TP_RST) | (1 << EXIO_BL_EN)); // 0x07
    Serial.println("Backlight enabled");
#endif
    disp_clear();
    disp_message("Setup Remote", "starting...");

#ifndef TOUCH4_BUILD
    // ── I2C bus (for Gamepad QT) ────────────────────────────────────────
    Wire.begin(PIN_SDA, PIN_SCL);

    // ── Gamepad QT input ────────────────────────────────────────────────
    if (!input_init()) {
        Serial.println("Gamepad QT init failed — check wiring / address");
        disp_clear();
        disp_message("Gamepad FAIL", "check wiring");
        while (true) delay(1000);
    }
#else
    // Touch4: GT911 is already active (reset released above); input_init()
    // is a no-op but called for consistency.
    input_init();
    Serial.println("GT911 touch ready");
#endif

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
