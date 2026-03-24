// =============================================================================
// ab7_hardware.h — Hardware abstraction layer for the ab7 PCB
// =============================================================================
//
// The ab7 board carries an ESP32-S3 and a BNO085 IMU over SPI.  There is no
// barometer on this board — the baro data stream is forced to 1.0.  A single
// SK6812 addressable LED is used for status indication, and two buttons
// (active-low, shorted to GND) provide user input.
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
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_BNO08x.h>
#include <FastLED.h>

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------

// BNO085 SPI bus
static constexpr int BNO_CS   = 10;
static constexpr int BNO_MOSI = 11;
static constexpr int BNO_SCK  = 12;
static constexpr int BNO_MISO = 13;
static constexpr int BNO_INT  = 4;
static constexpr int BNO_RST  = 5;
static constexpr int BNO_WAKE = 6;

// SK6812 addressable LED
static constexpr int LED_PIN = 7;

// Buttons (active-low, shorted to GND when pressed)
static constexpr int BTN_A = 0;
static constexpr int BTN_B = 14;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern Preferences preferences;
extern Adafruit_BNO08x imu;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Initialise GPIO pins (buttons with internal pull-ups).
void begin_pins();

/// Initialise the BNO085 over SPI.  Blocks on failure.
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
