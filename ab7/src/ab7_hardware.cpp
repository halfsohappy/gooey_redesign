// =============================================================================
// ab7_hardware.cpp — ab7 board hardware driver implementation
// =============================================================================

#include "ab7_hardware.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------

Preferences preferences;
Adafruit_BNO08x bno(BNO_RST);

struct BnoCache {
    float qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f;
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
};

static BnoCache bno_cache;

// ---------------------------------------------------------------------------
// Pin initialisation
// ---------------------------------------------------------------------------

void begin_pins() {
    // Buttons: active-low with internal pull-ups.
    pinMode(BTN_A, INPUT_PULLUP);
    pinMode(BTN_B, INPUT_PULLUP);
}

// ---------------------------------------------------------------------------
// BNO-085 initialisation (SPI)
// ---------------------------------------------------------------------------

void begin_bno() {
    // Use the default SPI bus but with our pin assignments.
    SPI.begin(BNO_SCK, BNO_MISO, BNO_MOSI, BNO_CS);

    // WAKE is active-low on BNO-085. Keep it deasserted (HIGH) for normal run.
    pinMode(BNO_WAKE, OUTPUT);
    digitalWrite(BNO_WAKE, HIGH);

    if (!bno.begin_SPI(BNO_CS, BNO_INT, &SPI)) {
        Serial.println(F("[BNO] Failed to initialise BNO-085 over SPI!"));
        Serial.println(F("[BNO] Check wiring.  Halting."));
        while (1) { delay(100); }
    }

    Serial.println(F("[BNO] BNO-085 initialised OK."));
}

// ---------------------------------------------------------------------------
// Enable the reports we use
// ---------------------------------------------------------------------------
//
// The BNO-085 is a "smart" sensor hub that runs sensor fusion internally.
// We request three report types:
//   - Rotation Vector (quaternion, fused from accel+gyro+mag) — 50 Hz
//   - Linear Acceleration (gravity-free, fused) — 50 Hz
//   - Calibrated Gyroscope — 50 Hz

void enable_bno_reports() {
    static constexpr uint32_t REPORT_INTERVAL_US = 20000;  // 50 Hz

    if (!bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US))
        Serial.println(F("[BNO] Could not enable rotation vector."));
    if (!bno.enableReport(SH2_LINEAR_ACCELERATION, REPORT_INTERVAL_US))
        Serial.println(F("[BNO] Could not enable linear accelerometer."));
    if (!bno.enableReport(SH2_GYROSCOPE_CALIBRATED, REPORT_INTERVAL_US))
        Serial.println(F("[BNO] Could not enable gyroscope."));

    Serial.println(F("[BNO] Reports enabled (rotation vector, linear accel, gyro @ 50 Hz)."));
}

// ---------------------------------------------------------------------------
// Data access helpers
// ---------------------------------------------------------------------------

bool bno_data_available() {
    if (bno.wasReset()) {
        Serial.println(F("[BNO] Sensor reset detected; re-enabling reports."));
        enable_bno_reports();
    }

    bool updated = false;
    sh2_SensorValue_t sensor_value;

    while (bno.getSensorEvent(&sensor_value)) {
        updated = true;

        switch (sensor_value.sensorId) {
            case SH2_ROTATION_VECTOR:
                bno_cache.qi = sensor_value.un.rotationVector.i;
                bno_cache.qj = sensor_value.un.rotationVector.j;
                bno_cache.qk = sensor_value.un.rotationVector.k;
                bno_cache.qr = sensor_value.un.rotationVector.real;
                break;

            case SH2_LINEAR_ACCELERATION:
                bno_cache.ax = sensor_value.un.linearAcceleration.x;
                bno_cache.ay = sensor_value.un.linearAcceleration.y;
                bno_cache.az = sensor_value.un.linearAcceleration.z;
                break;

            case SH2_GYROSCOPE_CALIBRATED:
                bno_cache.gx = sensor_value.un.gyroscope.x;
                bno_cache.gy = sensor_value.un.gyroscope.y;
                bno_cache.gz = sensor_value.un.gyroscope.z;
                break;
        }
    }

    return updated;
}

void bno_get_quat(float &qi, float &qj, float &qk, float &qr) {
    qi = bno_cache.qi;
    qj = bno_cache.qj;
    qk = bno_cache.qk;
    qr = bno_cache.qr;
}

void bno_get_accel(float &ax, float &ay, float &az) {
    ax = bno_cache.ax;
    ay = bno_cache.ay;
    az = bno_cache.az;
}

void bno_get_gyro(float &gx, float &gy, float &gz) {
    gx = bno_cache.gx;
    gy = bno_cache.gy;
    gz = bno_cache.gz;
}

// ---------------------------------------------------------------------------
// Quaternion → Euler conversion
// ---------------------------------------------------------------------------
//
// Returns roll (X), pitch (Y), yaw (Z) in degrees.

void quat_to_euler(float qi, float qj, float qk, float qr,
                   float &roll, float &pitch, float &yaw) {
    // Roll (X-axis rotation)
    float sinr_cosp = 2.0f * (qr * qi + qj * qk);
    float cosr_cosp = 1.0f - 2.0f * (qi * qi + qj * qj);
    roll = atan2f(sinr_cosp, cosr_cosp) * (180.0f / PI);

    // Pitch (Y-axis rotation)
    float sinp = 2.0f * (qr * qj - qk * qi);
    if (fabsf(sinp) >= 1.0f)
        pitch = copysignf(90.0f, sinp);   // clamp at ±90°
    else
        pitch = asinf(sinp) * (180.0f / PI);

    // Yaw (Z-axis rotation)
    float siny_cosp = 2.0f * (qr * qk + qi * qj);
    float cosy_cosp = 1.0f - 2.0f * (qj * qj + qk * qk);
    yaw = atan2f(siny_cosp, cosy_cosp) * (180.0f / PI);
}
