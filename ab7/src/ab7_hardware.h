// =============================================================================
// ab7_hardware.h — Hardware abstraction layer for the ab7 PCB
// =============================================================================
//
// The ab7 board carries an ESP32-S3 and an IMU.  Default build targets the
// LSM9DS1 over I2C (Adafruit breakout).  A selectable BNO085 (SPI) build is
// available via the AB7_IMU_BNO085 build flag / environment.  There is no
// barometer on this board — the baro data stream returns zero.  A single
// SK6812 addressable LED is used for status indication, and two buttons
// (active-low, shorted to GND) provide user input.
//
// LSM9DS1 I2C wiring (GPIO numbers):
//   SDA = 1
//   SCL = 2
//
// BNO085 SPI wiring (GPIO numbers):
//   CS   = 10
//   MOSI = 11
//   SCK  = 12
//   MISO = 13
//   INT  = 4
//   RST  = 5
//   WAKE = 6
//
// SK6812 LED:   GPIO 7
// Button A:     GPIO 0   (active-low, internal pull-up)
// Button B:     GPIO 14  (active-low, internal pull-up)
// =============================================================================

#ifndef AB7_HARDWARE_H
#define AB7_HARDWARE_H

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#if defined(AB7_IMU_BNO085)
#include <Adafruit_BNO08x.h>
#else
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_AHRS_Madgwick.h>
#endif
#include <FastLED.h>

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

#if defined(AB7_IMU_BNO085)
// BNO085 SPI bus
static constexpr int BNO_CS   = 10;
static constexpr int BNO_MOSI = 11;
static constexpr int BNO_SCK  = 12;
static constexpr int BNO_MISO = 13;
static constexpr int BNO_INT  = 4;
static constexpr int BNO_RST  = 5;
static constexpr int BNO_WAKE = 6;
#else
// LSM9DS1 I2C bus
static constexpr int IMU_SDA = 1;
static constexpr int IMU_SCL = 2;
#endif

// SK6812 addressable LED
static constexpr int LED_PIN = 7;

// Buttons (active-low, shorted to GND when pressed)
static constexpr int BTN_A = 0;
static constexpr int BTN_B = 14;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern Preferences preferences;
#if defined(AB7_IMU_BNO085)
extern Adafruit_BNO08x imu;
#else
extern Adafruit_LSM9DS1 imu;
extern Adafruit_Madgwick imu_filter;
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Initialise GPIO pins (buttons with internal pull-ups).
void begin_pins();

/// Initialise the LSM9DS1 over I2C.  Blocks on failure.
void begin_imu();

/// Poll the IMU; returns true if fresh data was read and cached.
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

#endif // AB7_HARDWARE_H
