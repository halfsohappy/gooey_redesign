// =============================================================================
// ab7_hardware.cpp — ab7 board hardware driver implementation
// =============================================================================

#include "ab7_hardware.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------

Preferences preferences;
Adafruit_BNO08x imu(BNO_RST);
static sh2_SensorValue_t imu_value;

struct ImuCache {
    float qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f;
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
};

static ImuCache imu_cache;

// ---------------------------------------------------------------------------
// Pin initialisation
// ---------------------------------------------------------------------------

void begin_pins() {
    // Buttons: active-low with internal pull-ups.
    pinMode(BTN_A, INPUT_PULLUP);
    pinMode(BTN_B, INPUT_PULLUP);
}

// ---------------------------------------------------------------------------
// IMU initialisation
// ---------------------------------------------------------------------------

void begin_imu() {
    // ESP32 SPI signature: begin(int8_t sck, int8_t miso, int8_t mosi, int8_t ss=-1);
    // here we intentionally omit the optional SS parameter. Chip select is handled by the
    // Adafruit driver via begin_SPI() below (args: CS, INT, SPI*).
    SPI.begin(BNO_SCK, BNO_MISO, BNO_MOSI);

    pinMode(BNO_WAKE, OUTPUT);
    digitalWrite(BNO_WAKE, HIGH);

    if (!imu.begin_SPI(BNO_CS, BNO_INT, &SPI)) {
        Serial.println(F("[IMU] Failed to initialise BNO085 over SPI!"));
        Serial.println(F("[IMU] Check SPI wiring (CS=10, MOSI=11, SCK=12, MISO=13, INT=4, RST=5, WAKE=6).  Halting."));
        while (1) { delay(100); }
    }

    imu.enableReport(SH2_ROTATION_VECTOR);
    imu.enableReport(SH2_LINEAR_ACCELERATION);
    imu.enableReport(SH2_GYROSCOPE_CALIBRATED);

    Serial.println(F("[IMU] BNO085 initialised (SPI)."));
}

// ---------------------------------------------------------------------------
// Data access helpers
// ---------------------------------------------------------------------------

bool imu_data_available() {
    if (!imu.getSensorEvent(&imu_value)) {
        return false;
    }

    switch (imu_value.sensorId) {
        case SH2_ROTATION_VECTOR:
            imu_cache.qi = imu_value.un.rotationVector.i;
            imu_cache.qj = imu_value.un.rotationVector.j;
            imu_cache.qk = imu_value.un.rotationVector.k;
            imu_cache.qr = imu_value.un.rotationVector.real;
            break;

        case SH2_LINEAR_ACCELERATION:
            imu_cache.ax = imu_value.un.linearAcceleration.x;
            imu_cache.ay = imu_value.un.linearAcceleration.y;
            imu_cache.az = imu_value.un.linearAcceleration.z;
            break;

        case SH2_GYROSCOPE_CALIBRATED:
            imu_cache.gx = imu_value.un.gyroscope.x;
            imu_cache.gy = imu_value.un.gyroscope.y;
            imu_cache.gz = imu_value.un.gyroscope.z;
            break;

        default:
            break;
    }

    return true;
}

void imu_get_quat(float &qi, float &qj, float &qk, float &qr) {
    qi = imu_cache.qi;
    qj = imu_cache.qj;
    qk = imu_cache.qk;
    qr = imu_cache.qr;
}

void imu_get_accel(float &ax, float &ay, float &az) {
    ax = imu_cache.ax;
    ay = imu_cache.ay;
    az = imu_cache.az;
}

void imu_get_gyro(float &gx, float &gy, float &gz) {
    gx = imu_cache.gx;
    gy = imu_cache.gy;
    gz = imu_cache.gz;
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
