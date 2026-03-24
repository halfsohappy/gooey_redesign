// =============================================================================
// main.cpp — Entry point for the TheaterGWD firmware
// =============================================================================
//
// BOOT SEQUENCE:
//   1. Initialise serial, GPIO pins, sensors.
//      - Bart: barometer (BMP5xx), IMU (ISM330DHCX), magnetometer (MMC5983MA)
//      - ab7:  BNO085 IMU (SPI), SK6812 status LED, two buttons
//   2. Check if the device has been provisioned (WiFi credentials stored).
//      - Yes → connect to WiFi, start UDP listener.
//      - No  → launch the captive-portal provisioner and wait.
//   3. Create a FreeRTOS task that continuously reads sensors (or, for
//      Bart development, fills data_streams[] with simulated sine waves).
//   4. Enter the main loop, which polls for incoming OSC messages and
//      dispatches them through osc_handle_message().
//
// Build with -DAB7_BUILD for the ab7 board.  Without it the Bart board is
// targeted.
// =============================================================================

#include "main.h"

#ifdef AB7_BUILD

// Current quaternion — shared with osc_commands.h for ori save commands
float cur_qi = 0.0f, cur_qj = 0.0f, cur_qk = 0.0f, cur_qr = 1.0f;

// SK6812 LED (one pixel)
#define NUM_LEDS 1
static CRGB leds[NUM_LEDS];

// Button debounce helpers
static unsigned long btn_a_last = 0;
static unsigned long btn_b_last = 0;
static constexpr unsigned long BTN_DEBOUNCE_MS = 300;
#endif // AB7_BUILD

/// True once the device has connected to WiFi and UDP is ready for OSC.
static bool network_ready = false;

void setup() {
    Serial.begin(115200);
    delay(1000);  // brief pause for serial monitor to connect
    Serial.println();
    Serial.println(F("════════════════════════════════════════════════"));
#ifdef AB7_BUILD
    Serial.println(F("  TheaterGWD ab7 — booting..."));
#else
    Serial.println(F("  TheaterGWD — booting..."));
#endif
    Serial.println(F("════════════════════════════════════════════════"));
    Serial.print(F("  Chip     : "));
    Serial.println(ESP.getChipModel());
    Serial.print(F("  CPU      : "));
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(F(" MHz"));
    Serial.print(F("  Free heap: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));
    Serial.println();

    // --- Hardware initialisation --------------------------------------------
#ifdef AB7_BUILD
    Serial.println(F("[BOOT] Initialising GPIO pins..."));
    begin_pins();

    Serial.println(F("[BOOT] Initialising SK6812 LED..."));
    FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);
    leds[0] = CRGB(40, 0, 40);  // dim purple = booting
    FastLED.show();

    Serial.println(F("[BOOT] Initialising BNO085 (SPI)..."));
    begin_imu();
#else
    Serial.println(F("[BOOT] Initialising GPIO pins..."));
    begin_pins(0, 0, 0, 0);

    Serial.println(F("[BOOT] Initialising SPI bus..."));
    SPI.begin(SCK_PIN, SDO_PIN, SDI_PIN, CS_IMU);

    Serial.println(F("[BOOT] Initialising barometer (BMP5xx)..."));
    begin_baro(CS_BAR);

    Serial.println(F("[BOOT] Initialising IMU (ISM330DHCX) + magnetometer (MMC5983MA)..."));
    begin_imu(CS_IMU, CS_MAG);
#endif

    Serial.println(F("[BOOT] Hardware initialised."));
    Serial.println();

    // --- Provisioning / network ---------------------------------------------
    preferences.begin("device_config", true);  // read-only

    if (preferences.getBool("provisioned", false)) {
        Serial.println(F("[BOOT] Device is provisioned — loading config..."));

        String ssid     = preferences.getString("ssid", "");
        String password = preferences.getString("net_pass", "");
        bool   use_dhcp = preferences.getBool("use_dhcp", true);
        String ip_str   = use_dhcp ? "dhcp" : preferences.getString("static_ip", "dhcp");
        int    port     = preferences.getInt("port", 8000);

        device_adr = preferences.getString("device_adr", "");

        Serial.print(F("  SSID       : "));
        Serial.println(ssid);
        Serial.print(F("  IP mode    : "));
        Serial.println(use_dhcp ? F("DHCP") : ip_str);
        Serial.print(F("  Port       : "));
        Serial.println(port);
        Serial.print(F("  Device name: "));
        Serial.println(device_adr);

        // Normalise: must start with '/' and not end with '/'.
        if (device_adr.length() > 0) {
            if (!device_adr.startsWith("/")) device_adr = "/" + device_adr;
            if (device_adr.endsWith("/"))    device_adr.remove(device_adr.length() - 1);
        }

        preferences.end();

        Serial.println();
        begin_udp(ip_str, ssid, password, port);
        network_ready = true;

#ifdef AB7_BUILD
        leds[0] = CRGB(0, 40, 0);  // green = connected
        FastLED.show();
#endif

        Serial.print(F("[BOOT] Device address: "));
        Serial.println(device_adr);
    } else {
        preferences.end();

        Serial.println(F("[BOOT] Not provisioned — starting captive portal..."));
        Serial.println(F("[BOOT] Connect to WiFi AP 'annieData Setup' and open a browser."));
        Serial.println();

#ifdef AB7_BUILD
        leds[0] = CRGB(40, 20, 0);  // orange = provisioning
        FastLED.show();
#endif

        // Initialise WiFi STA first so the WiFiProvisioner's internal
        // WiFi.disconnect() call does not trigger the ESP-IDF error
        // "[E][STA.cpp:530] disconnect(): STA not started!".
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);
        delay(100);

        network_config();
        // network_config() calls ESP.restart() on success, so we only reach
        // here if provisioning is still in progress (blocking call).
    }

    // --- Sensor reading task ------------------------------------------------

#ifdef AB7_BUILD
    // ab7: real IMU data, pinned to core 1 (SPI interrupt handler on core 1).
    xTaskCreatePinnedToCore([](void*) {
        OriTracker& ot = ori_tracker();
        bool first_data = true;
        unsigned long no_data_count = 0;
        unsigned long total_reads = 0;
        unsigned long last_heartbeat_ms = 0;
        static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 10000;

        for (;;) {
            if (imu_data_available()) {
                no_data_count = 0;
                total_reads++;

                if (first_data) {
                    first_data = false;
                    Serial.println(F("[IMU] First sensor data received."));
                }

                // ── Rotation vector (quaternion) ───────────────────────
                float qi, qj, qk, qr;
                imu_get_quat(qi, qj, qk, qr);

                // Store globally for ori save commands.
                cur_qi = qi;
                cur_qj = qj;
                cur_qk = qk;
                cur_qr = qr;

                // Convert to Euler angles in degrees, then normalise to [0, 1].
                float roll, pitch, yaw;
                quat_to_euler(qi, qj, qk, qr, roll, pitch, yaw);

                data_streams[EULERX] = (roll  + 180.0f) / 360.0f;
                data_streams[EULERY] = (pitch + 90.0f)  / 180.0f;
                data_streams[EULERZ] = (yaw   + 180.0f) / 360.0f;

                // ── Linear acceleration (gravity-free, m/s²) ──────────
                float ax, ay, az;
                imu_get_accel(ax, ay, az);

                const float ACCEL_SCALE = 4.0f;
                data_streams[ACCELX]      = constrain((ax / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELY]      = constrain((ay / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELZ]      = constrain((az / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                float accel_len = sqrtf(ax * ax + ay * ay + az * az);
                data_streams[ACCELLENGTH] = constrain(accel_len / ACCEL_SCALE, 0.0f, 1.0f);

                // ── Gyroscope (rad/s) ─────────────────────────────────
                float gx, gy, gz;
                imu_get_gyro(gx, gy, gz);

                const float GYRO_SCALE = 4.0f;
                data_streams[GYROX]       = constrain((gx / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROY]       = constrain((gy / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROZ]       = constrain((gz / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                float gyro_len = sqrtf(gx * gx + gy * gy + gz * gz);
                data_streams[GYROLENGTH]  = constrain(gyro_len / GYRO_SCALE, 0.0f, 1.0f);

                // ── Barometer — not present on ab7 ────────────────────
                data_streams[BARO] = 0.0f;

                // ── Update orientation tracker ────────────────────────
                ot.update(qi, qj, qk, qr, gyro_len);
            } else {
                no_data_count++;
                // Warn once after ~5 seconds of no data (500 × 10ms).
                if (no_data_count == 500) {
                    Serial.print(F("[IMU] WARNING: No sensor data for 5 seconds — "));
                    Serial.println(F("Check SPI wiring (CS=10, MOSI=11, SCK=12, MISO=13, INT=4, RST=5, WAKE=6)."));
                }
            }

            // ── Periodic heartbeat — print sensor summary every 10 s ──
            unsigned long now = millis();
            if (now - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
                last_heartbeat_ms = now;
                Serial.print(F("[IMU] Heartbeat — reads: "));
                Serial.print(total_reads);
                Serial.print(F("  aX:"));
                Serial.print((float)data_streams[ACCELX], 3);
                Serial.print(F("  gX:"));
                Serial.print((float)data_streams[GYROX], 3);
                Serial.print(F("  eX:"));
                Serial.println((float)data_streams[EULERX], 3);
            }

            vTaskDelay(pdMS_TO_TICKS(10));  // ~100 Hz update rate
        }
    }, "sensor_task", 16384, nullptr, 1, nullptr, 1);  // 16 KB — BNO085 driver  |  pinned to core 1

    Serial.println(F("[BOOT] Sensor task started (BNO085 real data)."));
#else
    // Bart: simulated sensor data for development/testing.
    xTaskCreate([](void*) {
        for (;;) {
            // ── Real hardware reads (uncomment when hardware is connected) ──
            // IMU.getAccel(&accel_data);
            // IMU.getGyro(&gyro_data);
            // mmc.getMeasurementXYZ(&mag_data[0], &mag_data[1], &mag_data[2]);
            // process_imu_data(&accel_data, &my_a_data, ACCEL_NORM, true);
            // process_imu_data(&gyro_data,  &my_g_data, GYRO_NORM, false);
            // data_streams[ACCELX] = my_a_data.xData;
            // data_streams[ACCELY] = my_a_data.yData;
            // data_streams[ACCELZ] = my_a_data.zData;
            // data_streams[ACCELLENGTH] = my_a_data.length;
            // data_streams[GYROX] = my_g_data.xData;
            // data_streams[GYROY] = my_g_data.yData;
            // data_streams[GYROZ] = my_g_data.zData;
            // data_streams[GYROLENGTH] = my_g_data.length;
            // TODO: read barometer, compute Euler angles from sensor fusion.

            // ── Simulated data (distinct sine waves for testing) ────────────
            update_simulated_data();

            vTaskDelay(pdMS_TO_TICKS(10));  // ~100 Hz update rate
        }
    }, "sensor_task", 4096, nullptr, 1, nullptr);

    Serial.println(F("[BOOT] Sensor task started (simulated data)."));
#endif // AB7_BUILD

    Serial.println();
    Serial.println(F("════════════════════════════════════════════════"));
    if (network_ready) {
#ifdef AB7_BUILD
        Serial.println(F("  TheaterGWD ab7 ready."));
#else
        Serial.println(F("  TheaterGWD ready."));
#endif
        Serial.print(F("  Listening for OSC on /annieData"));
        Serial.print(device_adr);
        Serial.println(F("/..."));
    } else {
#ifdef AB7_BUILD
        Serial.println(F("  TheaterGWD ab7 ready (no network)."));
#else
        Serial.println(F("  TheaterGWD ready (no network)."));
#endif
    }
    Serial.println(F("  Type 'help' in serial monitor for debug commands."));
#ifdef AB7_BUILD
    Serial.println(F("  Press BTN_A to save an ori, BTN_B to query active ori."));
#endif
    Serial.println(F("════════════════════════════════════════════════"));
    Serial.println();
}

void loop() {
    // Process serial debug commands (non-blocking).
    serial_process();

    // Process any incoming OSC message.  MicroOsc calls osc_handle_message()
    // for each complete message received on the UDP port.
    if (network_ready) {
        osc.onOscMessageReceived(osc_handle_message);
    }

#ifdef AB7_BUILD
    // --- Button handling (ab7 only) -----------------------------------------

    // Button A (GPIO 0): Save current orientation as a new ori.
    if (digitalRead(BTN_A) == LOW && millis() - btn_a_last > BTN_DEBOUNCE_MS) {
        btn_a_last = millis();
        int idx = ori_tracker().save_auto(cur_qi, cur_qj, cur_qk, cur_qr);
        if (idx >= 0) {
            String n = ori_tracker().oris[idx].name;
            Serial.println("[BTN_A] Saved ori: " + n);
            status_reporter().info("ori", "Button saved ori '" + n + "'");

            // Flash LED white briefly.
            leds[0] = CRGB(80, 80, 80);
            FastLED.show();
            delay(100);
            leds[0] = CRGB(0, 40, 0);
            FastLED.show();
        } else {
            Serial.println(F("[BTN_A] Ori slots full!"));
        }
    }

    // Button B (GPIO 14): Print active ori to serial.
    if (digitalRead(BTN_B) == LOW && millis() - btn_b_last > BTN_DEBOUNCE_MS) {
        btn_b_last = millis();
        OriTracker& ot = ori_tracker();
        if (ot.active_ori_index >= 0) {
            Serial.println("[BTN_B] Active ori: " + ot.active_ori_name);
        } else {
            Serial.println(F("[BTN_B] No active ori (none saved or moving too fast)."));
        }
        Serial.println("[BTN_B] Saved oris: " + ot.list());
    }
#endif // AB7_BUILD
}

