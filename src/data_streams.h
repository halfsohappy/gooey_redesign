// =============================================================================
// data_streams.h — Sensor data stream definitions and simulated values
// =============================================================================
//
// This file defines the global array of sensor data streams that the device
// reads from its hardware sensors (accelerometer, gyroscope, magnetometer,
// barometer) and makes available for OSC messages to reference.
//
// Each stream is identified by a compile-time index constant (e.g. ACCELX,
// GYROY, BARO). OscMessage objects hold a pointer into this array so their
// send tasks always transmit the latest reading.
//
// For development and testing without physical hardware, the function
// update_simulated_data() fills the array with deterministic sine waves at
// distinct frequencies, so each stream produces a unique, recognisable signal.
// =============================================================================

#ifndef DATA_STREAMS_H
#define DATA_STREAMS_H

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Stream count and index constants
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

// The global data array.  Every element is continuously updated either by the
// real sensor task or by update_simulated_data().
float data_streams[NUM_DATA_STREAMS];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Return a human-readable name for a data-stream index, or "unknown".
static inline String data_stream_name(int index) {
    switch (index) {
        case ACCELX:      return "accelX";
        case ACCELY:      return "accelY";
        case ACCELZ:      return "accelZ";
        case ACCELLENGTH: return "accelLength";
        case GYROX:       return "gyroX";
        case GYROY:       return "gyroY";
        case GYROZ:       return "gyroZ";
        case GYROLENGTH:  return "gyroLength";
        case BARO:        return "baro";
        case EULERX:      return "eulerX";
        case EULERY:      return "eulerY";
        case EULERZ:      return "eulerZ";
        default:          return "unknown";
    }
}

/// Map a case-insensitive value name to its data_streams index.
/// Returns -1 if the name is not recognised.
static inline int data_stream_index_from_name(const String& value_name) {
    String key = value_name;
    key.trim();
    key.toLowerCase();
    if (key == "accelx")                                              return ACCELX;
    if (key == "accely")                                              return ACCELY;
    if (key == "accelz")                                              return ACCELZ;
    if (key == "accellength" || key == "accellen" || key == "alen")   return ACCELLENGTH;
    if (key == "gyrox")                                               return GYROX;
    if (key == "gyroy")                                               return GYROY;
    if (key == "gyroz")                                               return GYROZ;
    if (key == "gyrolength" || key == "gyrolen"  || key == "glen")   return GYROLENGTH;
    if (key == "baro")                                                return BARO;
    if (key == "eulerx")                                              return EULERX;
    if (key == "eulery")                                              return EULERY;
    if (key == "eulerz")                                              return EULERZ;
    return -1;
}

/// Return the data-stream index that a given pointer refers to, or -1 if
/// the pointer does not point into the data_streams array.
static inline int data_stream_index_from_ptr(const float* ptr) {
    if (!ptr) return -1;
    ptrdiff_t offset = ptr - data_streams;
    if (offset >= 0 && offset < NUM_DATA_STREAMS) return (int)offset;
    return -1;
}

// ---------------------------------------------------------------------------
// Simulated sensor data (for testing without real hardware)
// ---------------------------------------------------------------------------
//
// All sensor values are normalised to [0, 1] before being placed in the
// data_streams array.  Each stream uses a sine wave at a distinct frequency
// so that the signals are visually distinguishable on a monitoring tool.
// The frequencies are chosen as coprime values to avoid collisions.
//
// Call this once per sensor-task iteration (e.g. every 10 ms) instead of
// reading the real IMU/barometer/magnetometer.
// ---------------------------------------------------------------------------

static inline void update_simulated_data() {
    float t = millis() * 0.001f;  // time in seconds

    // Each value oscillates in [0, 1] at a unique frequency.

    // Accelerometer channels
    data_streams[ACCELX]      = sinf(2.0f * PI * 0.5f * t) * 0.5f + 0.5f;  // 0.5 Hz
    data_streams[ACCELY]      = sinf(2.0f * PI * 0.7f * t) * 0.5f + 0.5f;  // 0.7 Hz
    data_streams[ACCELZ]      = sinf(2.0f * PI * 1.1f * t) * 0.5f + 0.5f;  // 1.1 Hz
    data_streams[ACCELLENGTH] = fabsf(sinf(2.0f * PI * 0.3f * t));          // 0.3 Hz

    // Gyroscope channels
    data_streams[GYROX]       = sinf(2.0f * PI * 1.3f * t) * 0.5f + 0.5f;  // 1.3 Hz
    data_streams[GYROY]       = sinf(2.0f * PI * 1.7f * t) * 0.5f + 0.5f;  // 1.7 Hz
    data_streams[GYROZ]       = sinf(2.0f * PI * 2.3f * t) * 0.5f + 0.5f;  // 2.3 Hz
    data_streams[GYROLENGTH]  = fabsf(sinf(2.0f * PI * 1.0f * t));          // 1.0 Hz

    // Barometer (slow drift)
    data_streams[BARO]        = sinf(2.0f * PI * 0.05f * t) * 0.5f + 0.5f; // 0.05 Hz

    // Euler angles
    data_streams[EULERX]      = sinf(2.0f * PI * 0.4f * t) * 0.5f + 0.5f;  // 0.4 Hz
    data_streams[EULERY]      = sinf(2.0f * PI * 0.6f * t) * 0.5f + 0.5f;  // 0.6 Hz
    data_streams[EULERZ]      = sinf(2.0f * PI * 0.8f * t) * 0.5f + 0.5f;  // 0.8 Hz
}

#endif // DATA_STREAMS_H
