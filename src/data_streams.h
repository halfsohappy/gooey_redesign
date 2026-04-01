// =============================================================================
// data_streams.h — Sensor data stream definitions
// =============================================================================
//
// This file defines the global array of sensor data streams that the device
// reads from its hardware sensors (accelerometer, gyroscope, barometer,
// Euler angles, swing-twist) and makes available for OSC messages to reference.
//
// Each stream is identified by a compile-time index constant (e.g. ACCELX,
// GYROY, BARO). OscMessage objects hold a pointer into this array so their
// send tasks always transmit the latest reading.
//
// For Bart development/testing without physical hardware, the function
// update_simulated_data() fills the array with deterministic sine waves at
// distinct frequencies, so each stream produces a unique, recognisable signal.
// This function is not available when building for ab7 (AB7_BUILD defined).
// =============================================================================

#ifndef DATA_STREAMS_H
#define DATA_STREAMS_H

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Stream count and index constants
// ---------------------------------------------------------------------------

#define NUM_DATA_STREAMS 29

#define ACCELX       0
#define ACCELY       1
#define ACCELZ       2
#define ACCELLENGTH  3
#define GYROX        4
#define GYROY        5
#define GYROZ        6
#define GYROLENGTH   7
#define BARO         8
#define ROLL         9
#define PITCH       10
#define YAW         11
#define GACCELX     12  // global-frame (rotation-compensated) linear acceleration
#define GACCELY     13
#define GACCELZ     14
#define GACCELLENGTH 15
#define CONST_ZERO  16  // always 0.0 — maps through bounds to output the low value
#define CONST_ONE   17  // always 1.0 — maps through bounds to output the high value
#define QUAT_I      18  // raw quaternion components — normalised via *0.5+0.5 → [0,1]
#define QUAT_J      19  // set low:-1 high:1 on the OscMessage to recover the [-1,1] range
#define QUAT_K      20
#define QUAT_R      21
#define TWIST       22  // swing-twist: rotation around arm axis (wrist twist)
#define HEADING     23  // swing-twist: horizontal pointing direction
#define TILT        24  // swing-twist: vertical angle above/below horizon
#define LIMB_FWD    25  // swing-twist accel: along limb axis (forward/back)
#define LIMB_LAT    26  // swing-twist accel: lateral (perpendicular horizontal)
#define LIMB_VERT   27  // swing-twist accel: vertical (up/down)
#define TWITCH      28  // swing-twist accel: magnitude (overall limb movement)

// The global data array.  Elements 0–15 are updated continuously by the sensor
// task.  Elements 16–17 (CONST_ZERO / CONST_ONE) are fixed at 0.0 / 1.0 and
// never written by the sensor task — they exist so messages can send constants.
// Elements 18–21 (QUAT_I/J/K/R) hold the raw (untared) quaternion each cycle.
// Elements 22–24 (TWIST/HEADING/TILT) are swing-twist decomposition outputs.
// Elements 25–28 (LIMB_FWD/LAT/VERT + TWITCH) are swing-twist frame accelerations.
// Declared volatile because the sensor task (writer) and scene send tasks
// (readers) run concurrently without a mutex protecting individual element access.
volatile float data_streams[NUM_DATA_STREAMS];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Return a human-readable name for a data-stream index, or "unknown".
static inline String data_stream_name(int index) {
    switch (index) {
        case ACCELX:       return "accelX";
        case ACCELY:       return "accelY";
        case ACCELZ:       return "accelZ";
        case ACCELLENGTH:  return "accelLength";
        case GYROX:        return "gyroX";
        case GYROY:        return "gyroY";
        case GYROZ:        return "gyroZ";
        case GYROLENGTH:   return "gyroLength";
        case BARO:         return "baro";
        case ROLL:         return "roll";
        case PITCH:        return "pitch";
        case YAW:          return "yaw";
        case GACCELX:      return "gaccelX";
        case GACCELY:      return "gaccelY";
        case GACCELZ:      return "gaccelZ";
        case GACCELLENGTH: return "gaccelLength";
        case CONST_ZERO:   return "low";
        case CONST_ONE:    return "high";
        case QUAT_I:       return "quatI";
        case QUAT_J:       return "quatJ";
        case QUAT_K:       return "quatK";
        case QUAT_R:       return "quatR";
        case TWIST:        return "twist";
        case HEADING:      return "heading";
        case TILT:         return "tilt";
        case LIMB_FWD:     return "limbFwd";
        case LIMB_LAT:     return "limbLat";
        case LIMB_VERT:    return "limbVert";
        case TWITCH:       return "twitch";
        default:           return "unknown";
    }
}

/// Map a case-insensitive value name to its data_streams index.
/// Returns -1 if the name is not recognised.
static inline int data_stream_index_from_name(const String& value_name) {
    String key = value_name;
    key.trim();
    key.toLowerCase();
    if (key == "accelx")                                               return ACCELX;
    if (key == "accely")                                               return ACCELY;
    if (key == "accelz")                                               return ACCELZ;
    if (key == "accellength" || key == "accellen" || key == "alen")    return ACCELLENGTH;
    if (key == "gyrox")                                                return GYROX;
    if (key == "gyroy")                                                return GYROY;
    if (key == "gyroz")                                                return GYROZ;
    if (key == "gyrolength" || key == "gyrolen"  || key == "glen")    return GYROLENGTH;
    if (key == "baro")                                                 return BARO;
    if (key == "roll"   || key == "eulerx")                             return ROLL;
    if (key == "pitch"  || key == "eulery")                             return PITCH;
    if (key == "yaw"    || key == "eulerz")                             return YAW;
    if (key == "gaccelx")                                              return GACCELX;
    if (key == "gaccely")                                              return GACCELY;
    if (key == "gaccelz")                                              return GACCELZ;
    if (key == "gaccellength" || key == "gaccellen" || key == "galen") return GACCELLENGTH;
    if (key == "low"  || key == "lo"  || key == "min")                return CONST_ZERO;
    if (key == "high" || key == "hi"  || key == "max")                return CONST_ONE;
    if (key == "quati" || key == "quat_i" || key == "qi")             return QUAT_I;
    if (key == "quatj" || key == "quat_j" || key == "qj")             return QUAT_J;
    if (key == "quatk" || key == "quat_k" || key == "qk")             return QUAT_K;
    if (key == "quatr" || key == "quat_r" || key == "qr")             return QUAT_R;
    if (key == "twist")                                               return TWIST;
    if (key == "heading" || key == "hdg")                             return HEADING;
    if (key == "tilt")                                                return TILT;
    if (key == "limbfwd"  || key == "limb_fwd"  || key == "armfwd")    return LIMB_FWD;
    if (key == "limblat"  || key == "limb_lat"  || key == "armlat")  return LIMB_LAT;
    if (key == "limbvert" || key == "limb_vert" || key == "armvert") return LIMB_VERT;
    if (key == "twitch" || key == "armlength" || key == "armlen")    return TWITCH;
    return -1;
}

/// Return the data-stream index that a given pointer refers to, or -1 if
/// the pointer does not point into the data_streams array.
static inline int data_stream_index_from_ptr(const volatile float* ptr) {
    if (!ptr) return -1;
    ptrdiff_t offset = ptr - data_streams;
    if (offset >= 0 && offset < NUM_DATA_STREAMS) return (int)offset;
    return -1;
}

// ---------------------------------------------------------------------------
// Simulated sensor data (Bart only — for testing without real hardware)
// ---------------------------------------------------------------------------
#ifndef AB7_BUILD

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
    data_streams[ROLL]        = sinf(2.0f * PI * 0.4f * t) * 0.5f + 0.5f;  // 0.4 Hz
    data_streams[PITCH]       = sinf(2.0f * PI * 0.6f * t) * 0.5f + 0.5f;  // 0.6 Hz
    data_streams[YAW]         = sinf(2.0f * PI * 0.8f * t) * 0.5f + 0.5f;  // 0.8 Hz

    // Global-frame (rotation-compensated) acceleration channels
    data_streams[GACCELX]      = sinf(2.0f * PI * 0.55f * t) * 0.5f + 0.5f; // 0.55 Hz
    data_streams[GACCELY]      = sinf(2.0f * PI * 0.75f * t) * 0.5f + 0.5f; // 0.75 Hz
    data_streams[GACCELZ]      = sinf(2.0f * PI * 1.15f * t) * 0.5f + 0.5f; // 1.15 Hz
    data_streams[GACCELLENGTH] = fabsf(sinf(2.0f * PI * 0.35f * t));         // 0.35 Hz

    // Quaternion channels — simulate a slow pure-Z rotation (unit quaternion).
    float qangle = 2.0f * PI * 0.1f * t;  // 0.1 Hz full rotation
    data_streams[QUAT_I] = 0.0f * 0.5f + 0.5f;           // qi = 0 (no X component)
    data_streams[QUAT_J] = 0.0f * 0.5f + 0.5f;           // qj = 0 (no Y component)
    data_streams[QUAT_K] = sinf(qangle * 0.5f) * 0.5f + 0.5f;  // qk = sin(θ/2)
    data_streams[QUAT_R] = cosf(qangle * 0.5f) * 0.5f + 0.5f;  // qr = cos(θ/2)

    // Swing-twist channels
    data_streams[TWIST]   = sinf(2.0f * PI * 0.25f * t) * 0.5f + 0.5f;  // 0.25 Hz
    data_streams[HEADING] = sinf(2.0f * PI * 0.15f * t) * 0.5f + 0.5f;  // 0.15 Hz
    data_streams[TILT]    = sinf(2.0f * PI * 0.2f  * t) * 0.5f + 0.5f;  // 0.2 Hz

    // Swing-twist acceleration channels
    data_streams[LIMB_FWD]   = sinf(2.0f * PI * 0.45f * t) * 0.5f + 0.5f;  // 0.45 Hz
    data_streams[LIMB_LAT]   = sinf(2.0f * PI * 0.65f * t) * 0.5f + 0.5f;  // 0.65 Hz
    data_streams[LIMB_VERT]  = sinf(2.0f * PI * 0.85f * t) * 0.5f + 0.5f;  // 0.85 Hz
    data_streams[TWITCH]     = fabsf(sinf(2.0f * PI * 0.4f * t));           // 0.4 Hz
}

#endif // !AB7_BUILD

#endif // DATA_STREAMS_H
