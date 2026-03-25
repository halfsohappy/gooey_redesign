// =============================================================================
// ab7_hardware.cpp — ab7 board hardware driver implementation
// =============================================================================

#include "ab7_hardware.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------

Preferences preferences;
SlimeIMU slime;

// Cache for latest IMU readings (SlimeIMU only updates on hasNewData)
static Quat  cached_quat;
static Vector3 cached_accel;
static Vector3 cached_gyro;

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
    // SlimeIMU reads PIN_IMU_CS, PIN_SPI_SCK/MISO/MOSI, PIN_IMU_INT,
    // PIN_BNO_RST, PIN_BNO_WAK from build_flags.  It auto-detects the
    // BNO085 on the SPI bus and configures sensor fusion.
    //
    // The I2C SDA/SCL parameters are passed to begin() for any secondary
    // I2C sensor; they are unused when the primary sensor is on SPI.
    // GPIO 22 does not exist on ESP32-S3 (valid range: 0-21, 26-48).
    // Pins 15/16 are unused on this board; SlimeIMU skips I2C init in SPI
    // mode anyway (PIN_IMU_CS != 255), so these are never actually driven.
    if (!slime.begin(15, 16)) {
        Serial.println(F("[IMU] SlimeIMU failed to initialise BNO085 over SPI!"));
        Serial.println(F("[IMU] Check SPI wiring (CS=10, MOSI=11, SCK=12, MISO=13, INT=4, RST=5, WAKE=6).  Halting."));
        while (1) { delay(100); }
    }

    Serial.print(F("[IMU] BNO085 initialised via SlimeIMU ("));
    Serial.print(slime.getSensorName(0));
    Serial.println(F(")."));
}

// ---------------------------------------------------------------------------
// Data access helpers
// ---------------------------------------------------------------------------

bool imu_data_available() {
    slime.update();
    if (slime.hasNewData(0)) {
        cached_quat  = slime.getQuaternion(0);
        cached_accel = slime.getLinearAcceleration(0);
        cached_gyro  = slime.getAngularVelocity(0);
        return true;
    }
    return false;
}

void imu_get_quat(float &qi, float &qj, float &qk, float &qr) {
    qi = cached_quat.x;
    qj = cached_quat.y;
    qk = cached_quat.z;
    qr = cached_quat.w;
}

void imu_get_accel(float &ax, float &ay, float &az) {
    ax = cached_accel.x;
    ay = cached_accel.y;
    az = cached_accel.z;
}

void imu_get_gyro(float &gx, float &gy, float &gz) {
    gx = cached_gyro.x;
    gy = cached_gyro.y;
    gz = cached_gyro.z;
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
