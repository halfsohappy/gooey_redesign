// =============================================================================
// main.cpp — Entry point for the bc127 DMX controller
// =============================================================================
//
// BOOT SEQUENCE:
//   1. Initialise M5Stack CoreS3 (display, touch, power).
//   2. Check if the device has been provisioned (WiFi credentials in NVS).
//      - Yes → connect to WiFi, start UDP listener.
//      - No  → launch the captive-portal provisioner and wait.
//   3. Initialise the DMX output via the DMX Base (esp_dmx on UART1).
//   4. Start a FreeRTOS task that sends DMX frames at ~40 fps.
//   5. Enter the main loop: poll OSC, update display, check touch.
//
// HARDWARE:
//   - M5Stack CoreS3 (ESP32-S3, 320×240 LCD, capacitive touch)
//   - M5Stack DMX Base (RS-485 transceiver on M-BUS: TX=7, RX=10, EN=6)
//
// =============================================================================

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <MicroOsc.h>
#include <MicroOscUdp.h>
#include <Preferences.h>

#include "config.h"
#include "dmx_engine.h"
#include "fixture_map.h"
#include "xkcd_colors.h"
#include "osc_handler.h"
#include "display.h"
#include "network_setup.h"

// ==== Globals ===============================================================

Preferences preferences;

WiFiUDP        Udp;
MicroOscUdp<OSC_RECV_BUF> osc(&Udp);

static bool network_ready = false;
static int  osc_port = 8000;

// ==== WiFi + UDP initialisation =============================================

static void begin_udp(const String& ip_str, const String& ssid,
                       const String& password, int port) {
    if (ip_str != "dhcp") {
        IPAddress static_ip;
        static_ip.fromString(ip_str);
        WiFi.config(static_ip);
    }

    WiFi.begin(ssid.c_str(), password.c_str());

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, 100);
    M5.Display.print("Connecting to WiFi");

    Serial.print("Connecting to WiFi");
    const unsigned long WIFI_TIMEOUT_MS = 20000UL;
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        M5.Display.print(".");
        if (millis() - t0 >= WIFI_TIMEOUT_MS) {
            Serial.println("\n[BOOT] WiFi timeout — clearing credentials, restarting...");
            if (preferences.begin("device_config", false)) {
                preferences.putBool("provisioned", false);
                preferences.end();
            }
            ESP.restart();
        }
    }
    Serial.println();
    Serial.println("WiFi connected — IP: " + WiFi.localIP().toString());

    Udp.begin(port);
    Serial.println("UDP listening on port " + String(port));

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 100);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.print("IP: ");
    M5.Display.println(WiFi.localIP().toString());
    M5.Display.print("Port: ");
    M5.Display.println(port);
    delay(1500);
}

// ==== DMX Send Task (FreeRTOS) ==============================================
// Runs on core 1, sends DMX frames at ~40 fps (25 ms period).

static void dmx_send_task(void* param) {
    (void)param;
    for (;;) {
        dmx_transmit();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

// ==== Display Refresh Task ==================================================
// Runs on core 0, refreshes the LCD at ~10 fps to keep UI responsive.

static unsigned long last_display_ms = 0;
static const unsigned long DISPLAY_PERIOD_MS = 100;

// ==== Setup =================================================================

void setup() {
    // --- M5 Initialisation --------------------------------------------------
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);   // landscape
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(4, 10);
    M5.Display.println("annieData DMX");
    M5.Display.setTextSize(1);
    M5.Display.setCursor(4, 40);
    M5.Display.println("bc127 controller booting...");

    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("════════════════════════════════════════════════"));
    Serial.println(F("  annieData DMX — bc127 controller booting..."));
    Serial.println(F("════════════════════════════════════════════════"));
    Serial.print(F("  Chip     : "));  Serial.println(ESP.getChipModel());
    Serial.print(F("  CPU      : "));  Serial.print(ESP.getCpuFreqMHz());
    Serial.println(F(" MHz"));
    Serial.print(F("  Free heap: "));  Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));
    Serial.println();

    // --- Provisioning / Network ---------------------------------------------
    preferences.begin("device_config", true);  // read-only

    if (preferences.getBool("provisioned", false)) {
        Serial.println(F("[BOOT] Device is provisioned — loading config..."));

        String ssid     = preferences.getString("ssid", "");
        String password = preferences.getString("net_pass", "");
        bool   use_dhcp = preferences.getBool("use_dhcp", true);
        String ip_str   = use_dhcp ? "dhcp" : preferences.getString("static_ip", "dhcp");
        osc_port        = preferences.getInt("port", 8000);

        Serial.print(F("  SSID : "));  Serial.println(ssid);
        Serial.print(F("  IP   : "));  Serial.println(use_dhcp ? F("DHCP") : ip_str);
        Serial.print(F("  Port : "));  Serial.println(osc_port);

        preferences.end();

        begin_udp(ip_str, ssid, password, osc_port);
        network_ready = true;
    } else {
        preferences.end();

        Serial.println(F("[BOOT] Not provisioned — starting captive portal..."));
        M5.Display.setCursor(4, 60);
        M5.Display.println("Starting WiFi setup...");
        M5.Display.println("Connect to AP:");
        M5.Display.println("  'annieData DMX Setup'");

        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);
        delay(100);

        network_config();
        // network_config() calls ESP.restart() on success.
    }

    // --- DMX Output ---------------------------------------------------------
    Serial.println(F("[BOOT] Initialising DMX output (DMX Base)..."));
    M5.Display.setCursor(4, 80);
    M5.Display.println("Init DMX output...");
    dmx_init();
    Serial.println(F("[BOOT] DMX ready."));

    // --- Start DMX Send Task ------------------------------------------------
    xTaskCreatePinnedToCore(dmx_send_task, "dmx_tx", 4096, nullptr, 2, nullptr, 1);
    Serial.println(F("[BOOT] DMX send task started (core 1, 40 fps)."));

    // --- Display ------------------------------------------------------------
    display_init();

    Serial.println(F("[BOOT] Ready."));
    Serial.println(F("════════════════════════════════════════════════"));
    osc_log("bc127 DMX ready — IP: " + WiFi.localIP().toString());
    osc_log("Listening on port " + String(osc_port));
}

// ==== Main Loop =============================================================

void loop() {
    // Poll for incoming OSC messages
    if (network_ready) {
        osc.onOscMessageReceived(osc_handle_dmx);
    }

    // Check touch input (toggle display view)
    display_check_touch();

    // Refresh display at ~10 fps
    unsigned long now = millis();
    if (now - last_display_ms >= DISPLAY_PERIOD_MS) {
        last_display_ms = now;
        display_update();
    }
}
