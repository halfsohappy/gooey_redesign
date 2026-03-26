// =============================================================================
// euler_utils.h — Alternative Euler decompositions for gimbal-lock avoidance
// =============================================================================
//
// The standard ZYX decomposition (quat_to_euler in hardware files) has its
// gimbal-lock singularity when the device Y-axis aligns with world vertical.
// When the device is mounted so that Y is the most vertical axis at rest, ZYX
// produces noisy or flipped angles in normal use.
//
// This file provides an alternative:
//
//   ZXY — singular on X (roll).  Use when device Y-axis is most vertical.
//          Normalisation: EULERX=(roll+90)/180, EULERY/Z=(angle+180)/360.
//
// The firmware auto-selects the decomposition at tare time based on which
// device axis is most vertical (see osc_commands.h /tare handler).
// =============================================================================

#ifndef EULER_UTILS_H
#define EULER_UTILS_H

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------------------
// quat_to_euler_zxy — ZXY convention (singular on X / roll)
// ---------------------------------------------------------------------------
//
// Rotation order: body-Z first, then body-X (singular), then body-Y.
// Singular term: R[2][1] = 2*(qj*qk + qr*qi).  Gimbal lock fires when the
// device X-axis points straight up or down.
//
// Output angles (degrees):
//   roll  (X rotation) — from asin, range [-90, +90]   ← singular axis
//   pitch (Y rotation) — from atan2, range [-180, +180]
//   yaw   (Z rotation) — from atan2, range [-180, +180]
//
// Normalise for data_streams[]:
//   EULERX = (roll  + 90.0f)  / 180.0f   // [-90,+90] → [0,1]
//   EULERY = (pitch + 180.0f) / 360.0f   // [-180,+180] → [0,1]
//   EULERZ = (yaw   + 180.0f) / 360.0f   // [-180,+180] → [0,1]

static inline void quat_to_euler_zxy(float qi, float qj, float qk, float qr,
                                      float &roll, float &pitch, float &yaw) {
    // roll (X) — the singular asin term is R[2][1]
    float sin_roll = 2.0f * (qj * qk + qr * qi);
    if (fabsf(sin_roll) >= 1.0f)
        roll = copysignf(90.0f, sin_roll);
    else
        roll = asinf(sin_roll) * (180.0f / (float)M_PI);

    // pitch (Y) — atan2(-R[2][0], R[2][2])
    pitch = atan2f(-(2.0f * (qi * qk - qr * qj)),
                     1.0f - 2.0f * (qi * qi + qj * qj))
            * (180.0f / (float)M_PI);

    // yaw (Z) — atan2(-R[0][1], R[1][1])
    yaw   = atan2f(-(2.0f * (qi * qj - qr * qk)),
                     1.0f - 2.0f * (qi * qi + qk * qk))
            * (180.0f / (float)M_PI);
}

#endif // EULER_UTILS_H
