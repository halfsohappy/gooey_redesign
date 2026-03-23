// =============================================================================
// ab7_hardware.cpp — ab7 board hardware driver implementation
// =============================================================================

#include "ab7_hardware.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------

Preferences preferences;
BNO08x     bno;

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

    if (!bno.beginSPI(BNO_CS, BNO_INT, BNO_RST, BNO_WAKE, 3000000, SPI)) {
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
    if (!bno.enableRotationVector(20))   // 20 ms → 50 Hz
        Serial.println(F("[BNO] Could not enable rotation vector."));
    if (!bno.enableLinearAccelerometer(20))
        Serial.println(F("[BNO] Could not enable linear accelerometer."));
    if (!bno.enableGyro(20))
        Serial.println(F("[BNO] Could not enable gyroscope."));

    Serial.println(F("[BNO] Reports enabled (rotation vector, linear accel, gyro @ 50 Hz)."));
}

// ---------------------------------------------------------------------------
// Data access helpers
// ---------------------------------------------------------------------------

bool bno_data_available() {
    return bno.dataAvailable();
}

void bno_get_quat(float &qi, float &qj, float &qk, float &qr) {
    qi = bno.getQuatI();
    qj = bno.getQuatJ();
    qk = bno.getQuatK();
    qr = bno.getQuatReal();
}

void bno_get_accel(float &ax, float &ay, float &az) {
    ax = bno.getLinAccelX();
    ay = bno.getLinAccelY();
    az = bno.getLinAccelZ();
}

void bno_get_gyro(float &gx, float &gy, float &gz) {
    gx = bno.getGyroX();
    gy = bno.getGyroY();
    gz = bno.getGyroZ();
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
