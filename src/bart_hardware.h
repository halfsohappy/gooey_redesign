// =============================================================================
// bart_hardware.h — Hardware abstraction layer for the Bart PCB
// =============================================================================
//
// The Bart board carries an ESP32-S3 with:
//   - ISM330DHCX IMU (6DoF accel+gyro) over SPI — VQF fusion via SlimeIMU
//   - MMC5983MA magnetometer over SPI (separate CS)
//   - BMP5xx barometer over SPI
//
// IMU processing is handled by the SlimeIMU library (slime_swipe), which
// provides VQF sensor fusion, automatic sensor detection, and calibration.
//
// SPI bus wiring (GPIO numbers):
//   SCK  = 36
//   SDI  = 35  (MOSI)
//   SDO  = 37  (MISO)
//   CS_IMU = 42
//   CS_MAG = 39
//   CS_BAR = 48
// =============================================================================

#ifndef BART_HARDWARE_H
#define BART_HARDWARE_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BMP5xx.h"
#include <SparkFun_MMC5983MA_Arduino_Library.h>
#include <SlimeIMU.h>
#include <FastLED.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

static constexpr int SDO_PIN = 37;
static constexpr int SCK_PIN = 36;
static constexpr int SDI_PIN = 35;
static constexpr int CS_IMU = 42;
static constexpr int CS_MAG = 39;
static constexpr int CS_UWB = 38;
static constexpr int CS_BAR = 48;
static constexpr int INT_IMU = 41;
static constexpr int INT_BAR = 34;
static constexpr int NEO = 21;
static constexpr int UMON = 7;
static constexpr int BMON = 8;
static constexpr int SEL13 = 11;
static constexpr int SEL46 = 12;
static constexpr int CC_EN1 = 13;
static constexpr int CC_EN2 = 14;
static constexpr int CC_PWM1 = 9;
static constexpr int CC_PWM2 = 10;

#define SEALEVELPRESSURE_HPA (1013.25)

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern Preferences preferences;
extern Adafruit_BMP5xx bmp;
extern SFE_MMC5983MA mmc;
extern SlimeIMU slime;

/// Baseline altitude (metres) captured at boot for relative baro normalisation.
extern float baro_baseline_alt;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void begin_pins(bool b13, bool b46, bool cen1, bool cen2);
void begin_baro(uint16_t BCS);
void begin_imu();
void begin_mag();

/// Poll the IMU; returns true if fresh data was read.
bool imu_data_available();

/// Read current rotation as a quaternion (i, j, k, real).
void imu_get_quat(float &qi, float &qj, float &qk, float &qr);

/// Read linear acceleration in m/s² (x, y, z).
void imu_get_accel(float &ax, float &ay, float &az);

/// Read calibrated gyroscope in rad/s (x, y, z).
void imu_get_gyro(float &gx, float &gy, float &gz);

/// Convert a quaternion to Euler angles (roll, pitch, yaw) in degrees.
void quat_to_euler(float qi, float qj, float qk, float qr,
                   float &roll, float &pitch, float &yaw);

/// Read barometer and return altitude normalised to [0, 1].
/// Uses baro_baseline_alt as the centre point; ±BARO_RANGE_M maps to [0, 1].
float read_baro_normalized();

/// Read magnetometer XYZ into the provided references.
/// Values are normalised to [0, 1] (raw 18-bit unsigned / 131071.0).
void mag_get_xyz(float &mx, float &my, float &mz);

#endif
