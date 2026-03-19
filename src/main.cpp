// =============================================================================
// main.cpp — Entry point for the TheaterGWD firmware
// =============================================================================
//
// BOOT SEQUENCE:
//   1. Initialise serial, GPIO pins, SPI bus.
//   2. Initialise sensors (barometer, IMU, magnetometer).
//   3. Check if the device has been provisioned (WiFi credentials stored).
//      - Yes → connect to WiFi, start UDP listener.
//      - No  → launch the captive-portal provisioner and wait.
//   4. Create a FreeRTOS task that continuously reads sensors (or, for
//      development, fills data_streams[] with simulated sine waves).
//   5. Enter the main loop, which polls for incoming OSC messages and
//      dispatches them through osc_handle_message().
//
// The actual sending of outbound OSC data is handled by per-patch FreeRTOS
// tasks created in osc_engine.h.  The main loop only processes *incoming*
// commands.
// =============================================================================

#include "main.h"

/// True once the device has connected to WiFi and UDP is ready for OSC.
static bool network_ready = false;

void setup() {
    Serial.begin(115200);
    delay(1000);  // brief pause for serial monitor to connect
    Serial.println();
    Serial.println(F("════════════════════════════════════════════════"));
    Serial.println(F("  TheaterGWD — booting..."));
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
    Serial.println(F("[BOOT] Initialising GPIO pins..."));
    begin_pins(0, 0, 0, 0);

    Serial.println(F("[BOOT] Initialising SPI bus..."));
    SPI.begin(SCK_PIN, SDO_PIN, SDI_PIN, CS_IMU);

    Serial.println(F("[BOOT] Initialising barometer (BMP5xx)..."));
    begin_baro(CS_BAR);

    Serial.println(F("[BOOT] Initialising IMU (ISM330DHCX) + magnetometer (MMC5983MA)..."));
    begin_imu(CS_IMU, CS_MAG);

    Serial.println(F("[BOOT] Hardware initialised."));
    Serial.println();

    // --- Provisioning / network ---------------------------------------------
    preferences.begin("device_config", true);  // read-only

    if (preferences.getBool("provisioned", false)) {
        Serial.println(F("[BOOT] Device is provisioned — loading config..."));

        String ssid     = preferences.getString("ssid", "");
        String password = preferences.getString("network_password", "");
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

        Serial.print(F("[BOOT] Device address: "));
        Serial.println(device_adr);
    } else {
        preferences.end();

        Serial.println(F("[BOOT] Not provisioned — starting captive portal..."));
        Serial.println(F("[BOOT] Connect to WiFi AP 'annieData Setup' and open a browser."));
        Serial.println();

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
    //
    // This task runs on a separate core and continuously updates the global
    // data_streams[] array.  During development the simulated-data function
    // is used; replace the body with real sensor reads when deploying.

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
    Serial.println();
    Serial.println(F("════════════════════════════════════════════════"));
    if (network_ready) {
        Serial.println(F("  TheaterGWD ready."));
        Serial.print(F("  Listening for OSC on /annieData"));
        Serial.print(device_adr);
        Serial.println(F("/..."));
    } else {
        Serial.println(F("  TheaterGWD ready (no network)."));
    }
    Serial.println(F("  Type 'help' in serial monitor for debug commands."));
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
}

