// =============================================================================
// ab7_hardware.cpp — ab7 board hardware driver implementation
// =============================================================================

#include "ab7_hardware.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------

Preferences preferences;
Adafruit_LSM9DS1 imu;
Adafruit_Madgwick imu_filter;

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
// LSM9DS1 initialisation (I2C)
// ---------------------------------------------------------------------------

void begin_imu() {
    Wire.begin(IMU_SDA, IMU_SCL);
    Wire.setClock(400000);  // 400 kHz fast mode

    if (!imu.begin()) {
        Serial.println(F("[IMU] Failed to initialise LSM9DS1 over I2C!"));
        Serial.println(F("[IMU] Check SDA=1, SCL=2 wiring.  Halting."));
        while (1) { delay(100); }
    }

    imu.setupAccel(imu.LSM9DS1_ACCELRANGE_4G);
    imu.setupGyro(imu.LSM9DS1_GYROSCALE_500DPS);
    imu.setupMag(imu.LSM9DS1_MAGGAIN_4GAUSS);

    // We poll the sensor task at ~100 Hz.
    imu_filter.begin(100.0f);

    Serial.println(F("[IMU] LSM9DS1 initialised (I2C, SDA=1, SCL=2)."));
}

// ---------------------------------------------------------------------------
// Data access helpers
// ---------------------------------------------------------------------------

bool imu_data_available() {
    sensors_event_t accel, gyro, mag, temp;
    imu.getEvent(&accel, &mag, &gyro, &temp);

    imu_filter.update(gyro.gyro.x, gyro.gyro.y, gyro.gyro.z,
                      accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
                      mag.magnetic.x, mag.magnetic.y, mag.magnetic.z);

    float qw, qx, qy, qz;
    imu_filter.getQuaternion(&qw, &qx, &qy, &qz);

    imu_cache.qi = qx;
    imu_cache.qj = qy;
    imu_cache.qk = qz;
    imu_cache.qr = qw;

    float grav_x, grav_y, grav_z;
    imu_filter.getGravityVector(&grav_x, &grav_y, &grav_z);

    // getGravityVector() returns a unit vector; scale by 9.80665 m/s² to
    // subtract gravity and yield gravity-free linear acceleration.
    imu_cache.ax = accel.acceleration.x - grav_x * 9.80665f;
    imu_cache.ay = accel.acceleration.y - grav_y * 9.80665f;
    imu_cache.az = accel.acceleration.z - grav_z * 9.80665f;

    imu_cache.gx = gyro.gyro.x;
    imu_cache.gy = gyro.gyro.y;
    imu_cache.gz = gyro.gyro.z;

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
