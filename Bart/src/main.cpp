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

void setup() {
    Serial.begin(115200);
    delay(500);  // brief pause for serial monitor to connect
    Serial.println("TheaterGWD — booting...");

    // --- Hardware initialisation --------------------------------------------
    begin_pins(0, 0, 0, 0);
    SPI.begin(SCK_PIN, SDO_PIN, SDI_PIN, CS_IMU);
    begin_baro(CS_BAR);
    begin_imu(CS_IMU, CS_MAG);
    Serial.println("Hardware initialised.");

    // --- Provisioning / network ---------------------------------------------
    preferences.begin("device_config", true);

    if (preferences.getBool("provisioned", false)) {
        begin_udp(
            preferences.getString("ip"),
            preferences.getString("ssid"),
            preferences.getString("network_password"),
            preferences.getInt("port")
        );

        device_adr = preferences.getString("device_adr");

        // Normalise: must start with '/' and not end with '/'.
        if (!device_adr.startsWith("/")) device_adr = "/" + device_adr;
        if (device_adr.endsWith("/"))    device_adr.remove(device_adr.length() - 1);

        Serial.println("Device address: " + device_adr);
    } else {
        Serial.println("Not provisioned — starting captive portal...");
        network_config();
    }

    preferences.end();

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

    Serial.println("Sensor task started.");
    Serial.println("TheaterGWD ready.  Listening for OSC commands on /annieData"
                   + device_adr + "/...");
}

void loop() {
    // Process any incoming OSC message.  MicroOsc calls osc_handle_message()
    // for each complete message received on the UDP port.
    osc.onOscMessageReceived(osc_handle_message);
}

