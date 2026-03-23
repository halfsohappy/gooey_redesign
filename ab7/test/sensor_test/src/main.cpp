// =============================================================================
// sensor_test — Diagnostic sketch for the ab7 board BNO-085 sensor pipeline
// =============================================================================
//
// This sketch isolates the BNO-085 sensor reading from the full TheaterGWD
// firmware (no WiFi, no OSC, no patches, no messages) so you can verify
// that the sensor hardware is working and that data flows correctly through
// each stage of the pipeline.
//
// HOW TO USE:
//   1.  cd ab7/test/sensor_test
//   2.  pio run -t upload
//   3.  pio device monitor          (115200 baud)
//   4.  Watch the serial output for pass/fail at each stage.
//
// STAGES TESTED:
//   [1] SPI bus initialisation
//   [2] BNO-085 initialisation (begin_SPI)
//   [3] Sensor report enabling
//   [4] Sensor event reception — waits for data, prints raw values
//   [5] Data stream normalisation — verifies [0, 1] range
//   [6] Value pointer indirection — simulates how the send task reads values
//   [7] FreeRTOS task test — runs sensor reads in a separate task (pinned
//       to core 1) and verifies values are visible from the main loop
//
// If any stage fails, the sketch prints a diagnostic message and halts or
// continues with degraded testing (depending on severity).
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Pin definitions (must match ab7 hardware)
// ---------------------------------------------------------------------------

static constexpr int BNO_CS   = 10;
static constexpr int BNO_MOSI = 11;
static constexpr int BNO_SCK  = 12;
static constexpr int BNO_MISO = 13;
static constexpr int BNO_INT  = 4;
static constexpr int BNO_RST  = 5;
static constexpr int BNO_WAKE = 6;

// ---------------------------------------------------------------------------
// Data streams (mirrors the main firmware's data_streams.h)
// ---------------------------------------------------------------------------

#define NUM_DATA_STREAMS 12

#define ACCELX      0
#define ACCELY      1
#define ACCELZ      2
#define ACCELLENGTH 3
#define GYROX       4
#define GYROY       5
#define GYROZ       6
#define GYROLENGTH  7
#define BARO        8
#define EULERX      9
#define EULERY     10
#define EULERZ     11

volatile float data_streams[NUM_DATA_STREAMS];

static const char* stream_names[] = {
    "accelX", "accelY", "accelZ", "accelLength",
    "gyroX",  "gyroY",  "gyroZ",  "gyroLength",
    "baro",
    "eulerX", "eulerY", "eulerZ"
};

// ---------------------------------------------------------------------------
// BNO-085 globals
// ---------------------------------------------------------------------------

Adafruit_BNO08x bno(BNO_RST);

struct BnoCache {
    float qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f;
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
};

static BnoCache bno_cache;

// ---------------------------------------------------------------------------
// Helpers (copied from firmware for isolation)
// ---------------------------------------------------------------------------

static void quat_to_euler(float qi, float qj, float qk, float qr,
                           float &roll, float &pitch, float &yaw) {
    float sinr_cosp = 2.0f * (qr * qi + qj * qk);
    float cosr_cosp = 1.0f - 2.0f * (qi * qi + qj * qj);
    roll = atan2f(sinr_cosp, cosr_cosp) * (180.0f / PI);

    float sinp = 2.0f * (qr * qj - qk * qi);
    if (fabsf(sinp) >= 1.0f)
        pitch = copysignf(90.0f, sinp);
    else
        pitch = asinf(sinp) * (180.0f / PI);

    float siny_cosp = 2.0f * (qr * qk + qi * qj);
    float cosy_cosp = 1.0f - 2.0f * (qj * qj + qk * qk);
    yaw = atan2f(siny_cosp, cosy_cosp) * (180.0f / PI);
}

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int pass_count = 0;
static int fail_count = 0;

static void test_pass(const char* name) {
    pass_count++;
    Serial.print(F("  ✓ PASS: "));
    Serial.println(name);
}

static void test_fail(const char* name, const char* detail = nullptr) {
    fail_count++;
    Serial.print(F("  ✗ FAIL: "));
    Serial.print(name);
    if (detail) {
        Serial.print(F(" — "));
        Serial.print(detail);
    }
    Serial.println();
}

// Flag set by the FreeRTOS task to signal it has populated data_streams[].
static volatile bool task_wrote_data = false;
static volatile unsigned long task_read_count = 0;

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(2000);  // give the serial monitor time to connect

    Serial.println();
    Serial.println(F("════════════════════════════════════════════════════════"));
    Serial.println(F("  ab7 Sensor Test — BNO-085 Diagnostic"));
    Serial.println(F("════════════════════════════════════════════════════════"));
    Serial.print(F("  Chip  : "));
    Serial.println(ESP.getChipModel());
    Serial.print(F("  CPU   : "));
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(F(" MHz"));
    Serial.print(F("  Heap  : "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));
    Serial.print(F("  Core  : "));
    Serial.println(xPortGetCoreID());
    Serial.println();

    // =====================================================================
    // STAGE 1: SPI bus initialisation
    // =====================================================================
    Serial.println(F("── Stage 1: SPI Bus ──────────────────────────────────"));
    Serial.print(F("  Pins: SCK="));
    Serial.print(BNO_SCK);
    Serial.print(F(" MISO="));
    Serial.print(BNO_MISO);
    Serial.print(F(" MOSI="));
    Serial.println(BNO_MOSI);
    Serial.println(F("  (CS pin NOT passed to SPI.begin — managed by library)"));

    SPI.begin(BNO_SCK, BNO_MISO, BNO_MOSI);
    test_pass("SPI.begin() completed");

    // =====================================================================
    // STAGE 2: BNO-085 initialisation
    // =====================================================================
    Serial.println();
    Serial.println(F("── Stage 2: BNO-085 Init ─────────────────────────────"));

    // WAKE pin: active-low.  Deassert (HIGH) for normal operation.
    pinMode(BNO_WAKE, OUTPUT);
    digitalWrite(BNO_WAKE, HIGH);
    Serial.println(F("  WAKE pin set HIGH (deasserted)."));

    Serial.println(F("  Calling bno.begin_SPI(CS=10, INT=4, &SPI)..."));
    if (!bno.begin_SPI(BNO_CS, BNO_INT, &SPI)) {
        test_fail("bno.begin_SPI()",
                  "BNO-085 did not respond.  Check SPI wiring (CS=10, "
                  "MOSI=11, SCK=12, MISO=13, INT=4, RST=5, WAKE=6).");
        Serial.println(F("  >>> Cannot continue without BNO-085.  Halting. <<<"));
        Serial.println();
        while (1) delay(1000);
    }
    test_pass("bno.begin_SPI() — BNO-085 responded");

    // =====================================================================
    // STAGE 3: Enable sensor reports
    // =====================================================================
    Serial.println();
    Serial.println(F("── Stage 3: Enable Reports ───────────────────────────"));

    static constexpr uint32_t REPORT_INTERVAL_US = 20000;  // 50 Hz

    bool rot_ok  = bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US);
    bool acc_ok  = bno.enableReport(SH2_LINEAR_ACCELERATION, REPORT_INTERVAL_US);
    bool gyro_ok = bno.enableReport(SH2_GYROSCOPE_CALIBRATED, REPORT_INTERVAL_US);

    if (rot_ok)  test_pass("Rotation vector report enabled (50 Hz)");
    else         test_fail("Rotation vector report", "enableReport returned false");

    if (acc_ok)  test_pass("Linear acceleration report enabled (50 Hz)");
    else         test_fail("Linear acceleration report", "enableReport returned false");

    if (gyro_ok) test_pass("Gyroscope report enabled (50 Hz)");
    else         test_fail("Gyroscope report", "enableReport returned false");

    // =====================================================================
    // STAGE 4: Wait for sensor events (up to 5 seconds)
    // =====================================================================
    Serial.println();
    Serial.println(F("── Stage 4: Receive Sensor Events (polling, same core) ──"));
    Serial.println(F("  Waiting up to 5 seconds for data..."));

    bool got_rot  = false;
    bool got_acc  = false;
    bool got_gyro = false;
    unsigned long t0 = millis();

    while (millis() - t0 < 5000 && !(got_rot && got_acc && got_gyro)) {
        if (bno.wasReset()) {
            Serial.println(F("  [!] Sensor reset detected — re-enabling reports."));
            bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US);
            bno.enableReport(SH2_LINEAR_ACCELERATION, REPORT_INTERVAL_US);
            bno.enableReport(SH2_GYROSCOPE_CALIBRATED, REPORT_INTERVAL_US);
        }

        sh2_SensorValue_t sv;
        const int MAX_EVENT_DRAIN = 20;
        for (int drain = 0; drain < MAX_EVENT_DRAIN && bno.getSensorEvent(&sv); drain++) {
            switch (sv.sensorId) {
                case SH2_ROTATION_VECTOR:
                    bno_cache.qi = sv.un.rotationVector.i;
                    bno_cache.qj = sv.un.rotationVector.j;
                    bno_cache.qk = sv.un.rotationVector.k;
                    bno_cache.qr = sv.un.rotationVector.real;
                    if (!got_rot) {
                        got_rot = true;
                        Serial.print(F("  Rotation vector: qi="));
                        Serial.print(bno_cache.qi, 4);
                        Serial.print(F(" qj="));
                        Serial.print(bno_cache.qj, 4);
                        Serial.print(F(" qk="));
                        Serial.print(bno_cache.qk, 4);
                        Serial.print(F(" qr="));
                        Serial.println(bno_cache.qr, 4);
                    }
                    break;

                case SH2_LINEAR_ACCELERATION:
                    bno_cache.ax = sv.un.linearAcceleration.x;
                    bno_cache.ay = sv.un.linearAcceleration.y;
                    bno_cache.az = sv.un.linearAcceleration.z;
                    if (!got_acc) {
                        got_acc = true;
                        Serial.print(F("  Linear accel: ax="));
                        Serial.print(bno_cache.ax, 4);
                        Serial.print(F(" ay="));
                        Serial.print(bno_cache.ay, 4);
                        Serial.print(F(" az="));
                        Serial.println(bno_cache.az, 4);
                    }
                    break;

                case SH2_GYROSCOPE_CALIBRATED:
                    bno_cache.gx = sv.un.gyroscope.x;
                    bno_cache.gy = sv.un.gyroscope.y;
                    bno_cache.gz = sv.un.gyroscope.z;
                    if (!got_gyro) {
                        got_gyro = true;
                        Serial.print(F("  Gyroscope: gx="));
                        Serial.print(bno_cache.gx, 4);
                        Serial.print(F(" gy="));
                        Serial.print(bno_cache.gy, 4);
                        Serial.print(F(" gz="));
                        Serial.println(bno_cache.gz, 4);
                    }
                    break;
            }
            if (got_rot && got_acc && got_gyro) {
                break;
            }
        }
        delay(5);
    }

    if (got_rot)  test_pass("Received rotation vector event");
    else          test_fail("Rotation vector", "No events received within 5 seconds");

    if (got_acc)  test_pass("Received linear acceleration event");
    else          test_fail("Linear acceleration", "No events received within 5 seconds");

    if (got_gyro) test_pass("Received gyroscope event");
    else          test_fail("Gyroscope", "No events received within 5 seconds");

    if (!got_rot && !got_acc && !got_gyro) {
        Serial.println();
        Serial.println(F("  >>> No sensor data at all.  Possible causes:"));
        Serial.println(F("      - SPI wiring issue (check CS, INT, RST, WAKE)"));
        Serial.println(F("      - BNO-085 not powered or damaged"));
        Serial.println(F("      - INT pin not connected (GPIO 4)"));
        Serial.println(F("      - WAKE pin held LOW (should be HIGH)"));
        Serial.println(F("  >>> Continuing tests with zero values. <<<"));
        Serial.println();
    }

    // =====================================================================
    // STAGE 5: Data stream normalisation
    // =====================================================================
    Serial.println();
    Serial.println(F("── Stage 5: Data Stream Normalisation ────────────────"));

    // Euler angles
    float roll, pitch, yaw;
    quat_to_euler(bno_cache.qi, bno_cache.qj, bno_cache.qk, bno_cache.qr,
                  roll, pitch, yaw);
    data_streams[EULERX] = (roll  + 180.0f) / 360.0f;
    data_streams[EULERY] = (pitch + 90.0f)  / 180.0f;
    data_streams[EULERZ] = (yaw   + 180.0f) / 360.0f;

    // Linear acceleration
    const float ACCEL_SCALE = 4.0f;
    data_streams[ACCELX]      = constrain((bno_cache.ax / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    data_streams[ACCELY]      = constrain((bno_cache.ay / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    data_streams[ACCELZ]      = constrain((bno_cache.az / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    float accel_len = sqrtf(bno_cache.ax * bno_cache.ax + bno_cache.ay * bno_cache.ay + bno_cache.az * bno_cache.az);
    data_streams[ACCELLENGTH] = constrain(accel_len / ACCEL_SCALE, 0.0f, 1.0f);

    // Gyroscope
    const float GYRO_SCALE = 4.0f;
    data_streams[GYROX]       = constrain((bno_cache.gx / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    data_streams[GYROY]       = constrain((bno_cache.gy / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    data_streams[GYROZ]       = constrain((bno_cache.gz / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    float gyro_len = sqrtf(bno_cache.gx * bno_cache.gx + bno_cache.gy * bno_cache.gy + bno_cache.gz * bno_cache.gz);
    data_streams[GYROLENGTH]  = constrain(gyro_len / GYRO_SCALE, 0.0f, 1.0f);

    data_streams[BARO] = 0.0f;

    // Print all streams and verify range.
    bool all_in_range = true;
    bool any_nonzero  = false;
    for (int i = 0; i < NUM_DATA_STREAMS; i++) {
        float v = (float)data_streams[i];
        Serial.print(F("  ["));
        if (i < 10) Serial.print(' ');
        Serial.print(i);
        Serial.print(F("] "));
        Serial.print(stream_names[i]);
        // Pad
        int pad = 12 - strlen(stream_names[i]);
        while (pad-- > 0) Serial.print(' ');
        Serial.print(F(" = "));
        Serial.print(v, 4);

        if (v < 0.0f || v > 1.0f) {
            Serial.print(F("  ← OUT OF RANGE"));
            all_in_range = false;
        }
        if (v != 0.0f && i != BARO) {
            any_nonzero = true;
        }
        Serial.println();
    }

    if (all_in_range) test_pass("All streams in [0, 1] range");
    else              test_fail("Range check", "Some streams outside [0, 1]");

    if (any_nonzero) test_pass("At least one non-baro stream is non-zero");
    else             test_fail("Non-zero check",
                               "All non-baro streams are zero — sensor likely not producing data");

    // =====================================================================
    // STAGE 6: Value pointer indirection
    // =====================================================================
    Serial.println();
    Serial.println(F("── Stage 6: Value Pointer Indirection ────────────────"));
    Serial.println(F("  (Simulates how the patch send task reads sensor values)"));

    bool ptr_ok = true;
    for (int i = 0; i < NUM_DATA_STREAMS; i++) {
        volatile float* ptr = &data_streams[i];
        float read_val = *ptr;
        float direct_val = (float)data_streams[i];
        if (read_val != direct_val) {
            Serial.print(F("  MISMATCH at index "));
            Serial.print(i);
            Serial.print(F(": ptr="));
            Serial.print(read_val, 4);
            Serial.print(F(" direct="));
            Serial.println(direct_val, 4);
            ptr_ok = false;
        }
    }
    if (ptr_ok) test_pass("All value_ptr reads match direct reads");
    else        test_fail("Value pointer mismatch detected");

    // =====================================================================
    // STAGE 7: FreeRTOS task — sensor reads from core 1
    // =====================================================================
    Serial.println();
    Serial.println(F("── Stage 7: FreeRTOS Task (pinned to core 1) ──────────"));
    Serial.println(F("  Creating sensor task pinned to core 1..."));

    // Reset all streams to a sentinel value so we can detect writes.
    for (int i = 0; i < NUM_DATA_STREAMS; i++) {
        data_streams[i] = -1.0f;
    }

    // Create a task that reads the BNO and populates data_streams[].
    xTaskCreatePinnedToCore([](void*) {
        for (int iter = 0; iter < 200; iter++) {   // run for ~2 seconds
            if (bno.wasReset()) {
                bno.enableReport(SH2_ROTATION_VECTOR, 20000);
                bno.enableReport(SH2_LINEAR_ACCELERATION, 20000);
                bno.enableReport(SH2_GYROSCOPE_CALIBRATED, 20000);
            }

            sh2_SensorValue_t sv;
            bool got_any = false;
            for (int drain = 0; drain < 10 && bno.getSensorEvent(&sv); drain++) {
                switch (sv.sensorId) {
                    case SH2_ROTATION_VECTOR:
                        bno_cache.qi = sv.un.rotationVector.i;
                        bno_cache.qj = sv.un.rotationVector.j;
                        bno_cache.qk = sv.un.rotationVector.k;
                        bno_cache.qr = sv.un.rotationVector.real;
                        got_any = true;
                        break;
                    case SH2_LINEAR_ACCELERATION:
                        bno_cache.ax = sv.un.linearAcceleration.x;
                        bno_cache.ay = sv.un.linearAcceleration.y;
                        bno_cache.az = sv.un.linearAcceleration.z;
                        got_any = true;
                        break;
                    case SH2_GYROSCOPE_CALIBRATED:
                        bno_cache.gx = sv.un.gyroscope.x;
                        bno_cache.gy = sv.un.gyroscope.y;
                        bno_cache.gz = sv.un.gyroscope.z;
                        got_any = true;
                        break;
                }
            }

            if (got_any) {
                task_read_count++;

                float roll, pitch, yaw;
                quat_to_euler(bno_cache.qi, bno_cache.qj, bno_cache.qk,
                              bno_cache.qr, roll, pitch, yaw);

                data_streams[EULERX] = (roll  + 180.0f) / 360.0f;
                data_streams[EULERY] = (pitch + 90.0f)  / 180.0f;
                data_streams[EULERZ] = (yaw   + 180.0f) / 360.0f;

                const float AS = 4.0f;
                data_streams[ACCELX] = constrain((bno_cache.ax / AS) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELY] = constrain((bno_cache.ay / AS) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELZ] = constrain((bno_cache.az / AS) * 0.5f + 0.5f, 0.0f, 1.0f);
                float al = sqrtf(bno_cache.ax * bno_cache.ax + bno_cache.ay * bno_cache.ay + bno_cache.az * bno_cache.az);
                data_streams[ACCELLENGTH] = constrain(al / AS, 0.0f, 1.0f);

                const float GS = 4.0f;
                data_streams[GYROX] = constrain((bno_cache.gx / GS) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROY] = constrain((bno_cache.gy / GS) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROZ] = constrain((bno_cache.gz / GS) * 0.5f + 0.5f, 0.0f, 1.0f);
                float gl = sqrtf(bno_cache.gx * bno_cache.gx + bno_cache.gy * bno_cache.gy + bno_cache.gz * bno_cache.gz);
                data_streams[GYROLENGTH] = constrain(gl / GS, 0.0f, 1.0f);

                data_streams[BARO] = 0.0f;
                task_wrote_data = true;
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Signal done.
        vTaskDelete(nullptr);
    }, "test_sensor", 16384, nullptr, 1, nullptr, 1);

    // Wait up to 3 seconds for the task to write data.
    unsigned long deadline = millis() + 3000;
    while (!task_wrote_data && millis() < deadline) {
        delay(50);
    }

    // Give a little more time for multiple reads.
    delay(500);

    Serial.print(F("  Task read count: "));
    Serial.println((unsigned long)task_read_count);

    if (task_wrote_data) {
        test_pass("FreeRTOS task wrote data to data_streams[]");
    } else {
        test_fail("FreeRTOS task",
                  "Task did not write data — BNO getSensorEvent() may fail "
                  "when called from a FreeRTOS task");
    }

    // Check that values are visible from the main loop (cross-task read).
    bool cross_task_ok = false;
    for (int i = 0; i < NUM_DATA_STREAMS; i++) {
        if (i == BARO) continue;
        if ((float)data_streams[i] != -1.0f) {
            cross_task_ok = true;
            break;
        }
    }

    if (cross_task_ok) {
        test_pass("Data written by task is visible from main loop");
    } else if (task_wrote_data) {
        test_fail("Cross-task visibility",
                  "Task claims to have written data but main loop sees sentinel values — "
                  "possible volatile / cache coherency issue");
    }

    Serial.println();
    Serial.println(F("  Final data_streams[] values (from main loop):"));
    for (int i = 0; i < NUM_DATA_STREAMS; i++) {
        float v = (float)data_streams[i];
        Serial.print(F("    ["));
        if (i < 10) Serial.print(' ');
        Serial.print(i);
        Serial.print(F("] "));
        Serial.print(stream_names[i]);
        int pad = 12 - strlen(stream_names[i]);
        while (pad-- > 0) Serial.print(' ');
        Serial.print(F(" = "));
        Serial.println(v, 4);
    }

    // =====================================================================
    // SUMMARY
    // =====================================================================
    Serial.println();
    Serial.println(F("════════════════════════════════════════════════════════"));
    Serial.print(F("  RESULTS:  "));
    Serial.print(pass_count);
    Serial.print(F(" passed,  "));
    Serial.print(fail_count);
    Serial.println(F(" failed"));
    Serial.println();

    if (fail_count == 0) {
        Serial.println(F("  All tests passed!  The sensor hardware and data pipeline"));
        Serial.println(F("  are working correctly.  If the main firmware still sends"));
        Serial.println(F("  zero values, check:"));
        Serial.println(F("    - Message configuration: does the message have value:accelX (etc.)?"));
        Serial.println(F("    - Patch running: is the patch started and enabled?"));
        Serial.println(F("    - Network config: is the destination IP/port/address correct?"));
        Serial.println(F("    - Bounds: are low and high set to the same value (e.g. both 0)?"));
    } else {
        Serial.println(F("  Some tests failed.  Review the output above for diagnostics."));
        Serial.println(F("  Common issues:"));
        Serial.println(F("    - SPI wiring (CS=10, MOSI=11, SCK=12, MISO=13)"));
        Serial.println(F("    - INT pin not connected (GPIO 4)"));
        Serial.println(F("    - RST pin not connected (GPIO 5)"));
        Serial.println(F("    - WAKE pin held LOW — must be HIGH for normal operation"));
        Serial.println(F("    - BNO-085 not powered or damaged"));
    }
    Serial.println(F("════════════════════════════════════════════════════════"));
}

void loop() {
    // Nothing to do — all tests run in setup().
    delay(10000);
}
