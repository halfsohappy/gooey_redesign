// =============================================================================
// data_streams.h — Sensor data stream definitions for the ab7 board
// =============================================================================
//
// Identical stream layout to the Bart board so all OSC message / patch code
// works unchanged.  The BNO-085 provides accelerometer, gyroscope, and
// rotation vector (Euler angles) data.  Barometer is absent on ab7 — the
// BARO stream is always zero.
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

// The global data array.  Every element is continuously updated by the
// sensor task reading the BNO-085.
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

#endif // DATA_STREAMS_H
