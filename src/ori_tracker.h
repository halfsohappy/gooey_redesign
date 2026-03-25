// =============================================================================
// ori_tracker.h — Orientation save / recall / matching system for ab7
// =============================================================================
//
// OVERVIEW:
//   The ori tracker lets users save named orientations ("oris") and then
//   continuously reports which saved ori the device's current orientation
//   best matches.  Only one ori is "active" at a time — the best match.
//
// USE CASE:
//   An actor holds the ab7 device.  Several oris are saved corresponding to
//   hand positions that point at different stage lights.  As the actor moves
//   their hand, the system reports which light they are pointing at.  OSC
//   messages can be configured to send only when a specific ori is (or is
//   not) the active match, enabling "point to turn on / off" workflows.
//
// ORI TYPES:
//   Point ori  — a single quaternion.  Matched by geodesic distance (closest
//                wins).  Created by saving a name for the first time.
//
//   Range ori  — defined by 2+ sample quaternions.  The system computes per-
//                axis Euler angle bounding boxes.  Matched when the current
//                Euler angles fall within center ± half_width + tolerance.
//                Created by re-saving an existing ori name (each save adds a
//                sample and expands the range).
//
//   Range oris let you express "don't care about rotation along one axis."
//   For example, save two orientations that differ only in yaw, and the
//   system will match any yaw between them but require similar pitch/roll.
//
// MATCHING ALGORITHM:
//   1. Range oris are checked first.  If the current Euler angles fall within
//      a range ori's bounding box, that ori is active (tightest box wins).
//   2. If no range ori matches, fall back to point oris (closest geodesic
//      angle wins).
//   3. If strict_matching is false (default), range oris whose centers are
//      closest also participate in the point-ori fallback, so there is always
//      an active ori.  If strict_matching is true, "no match" is possible.
//
// MOTION GATE:
//   When the device is rotating quickly (gyroscope magnitude above a
//   threshold), the tracker freezes its output and continues to report the
//   last stable ori.  This prevents flickering during fast sweeps and lets
//   the actor make deliberate, settled gestures.
//
// SETTING ORIS:
//   Oris can be saved in two ways:
//     1. Press Button A (GPIO 0) — saves the current orientation as the
//        next numbered ori (ori_0, ori_1, ...).
//     2. OSC command:
//          /annieData{dev}/ori/save         — auto-named (or expand if exists)
//          /annieData{dev}/ori/save/{name}   — user-named (or expand if exists)
//          /annieData{dev}/ori/reset/{name}  — reset range to current point
//          /annieData{dev}/ori/delete/{name} — remove an ori
//          /annieData{dev}/ori/clear         — remove all oris
//          /annieData{dev}/ori/list          — list saved oris
//          /annieData{dev}/ori/info/{name}   — show ori range details
//          /annieData{dev}/ori/threshold     — set motion gate threshold
//          /annieData{dev}/ori/tolerance     — set angular match tolerance
//          /annieData{dev}/ori/strict        — toggle strict matching mode
//
// CONDITIONAL MESSAGING:
//   Messages can be tagged with an ori condition via config string keys:
//
//     ori_only:{name}    — message sends ONLY when this ori is active
//     ori_not:{name}     — message sends ONLY when this ori is NOT active
//     ternori:{name}     — message sends HIGH when active, LOW when not
//
//   These are stored on the OscMessage and checked in the send task.
// =============================================================================

#ifndef ORI_TRACKER_H
#define ORI_TRACKER_H

#include <Arduino.h>
#include <math.h>

// Maximum number of saved oris.
#define MAX_ORIS 32

// Forward-declare quat_to_euler (defined in ab7_hardware.cpp / bart_hardware.cpp).
extern void quat_to_euler(float qi, float qj, float qk, float qr,
                           float &roll, float &pitch, float &yaw);

// ---------------------------------------------------------------------------
// Circular-angle math helpers (degrees)
// ---------------------------------------------------------------------------

/// Circular distance between two angles in degrees, result in [0, 180].
static inline float circular_distance_deg(float a, float b) {
    float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
    return fabsf(d);
}

/// Circular mean of two angles in degrees.
static inline float circular_mean_2_deg(float a, float b) {
    float sa = sinf(a * (float)M_PI / 180.0f) + sinf(b * (float)M_PI / 180.0f);
    float ca = cosf(a * (float)M_PI / 180.0f) + cosf(b * (float)M_PI / 180.0f);
    return atan2f(sa, ca) * (180.0f / (float)M_PI);
}

/// Expand a circular half-width so that `sample` is within center ± result.
static inline float expand_half_width_deg(float center, float hw, float sample) {
    float dist = circular_distance_deg(center, sample);
    return (dist > hw) ? dist : hw;
}

// ---------------------------------------------------------------------------
// Saved orientation (quaternion + optional Euler range)
// ---------------------------------------------------------------------------

struct SavedOri {
    String  name;
    float   qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f;  // identity
    bool    used = false;

    // Range data — computed from multiple save() calls on the same name.
    // sample_count == 0 or 1  →  point mode (geodesic match)
    // sample_count >= 2       →  range mode (per-axis Euler bounding box)
    uint8_t sample_count = 0;
    float   euler_center[3]     = {0.0f, 0.0f, 0.0f};  // [roll, pitch, yaw] degrees
    float   euler_half_width[3] = {0.0f, 0.0f, 0.0f};  // per-axis half-width degrees
};

// ---------------------------------------------------------------------------
// OriTracker
// ---------------------------------------------------------------------------

class OriTracker {
public:
    // --- Configuration ------------------------------------------------------

    /// Gyroscope magnitude threshold (rad/s).  If the combined gyro magnitude
    /// exceeds this value, the tracker holds the last stable match.
    float motion_threshold = 1.5f;   // ~86 °/s

    /// Extra angular margin (degrees) added around a range ori's bounding box
    /// when checking for a match.  Separate from motion_threshold.
    float ori_tolerance = 10.0f;

    /// When true, if no range ori matches and there are no point oris, the
    /// tracker reports "no active ori" (active_ori_index = -1).  When false
    /// (default), range oris also participate as point oris in the geodesic
    /// fallback, so there is always an active ori.
    bool strict_matching = false;

    // --- Current state (read by the send task) ------------------------------

    /// Index of the currently matched ori (-1 = none).
    volatile int  active_ori_index = -1;

    /// Name of the currently matched ori ("" = none).
    String active_ori_name;

    // --- Saved oris ---------------------------------------------------------

    SavedOri oris[MAX_ORIS];
    uint8_t  ori_count = 0;

    // --- Methods ------------------------------------------------------------

    /// Save the current orientation under the given name.
    ///
    /// If the name already exists, the new quaternion is added as a range
    /// sample — the Euler bounding box expands to encompass both the old
    /// center and the new point.  Use reset() to overwrite instead.
    ///
    /// If the name is new, a single-point ori is created.
    int save(const String& name, float qi, float qj, float qk, float qr) {
        float roll, pitch, yaw;
        quat_to_euler(qi, qj, qk, qr, roll, pitch, yaw);
        float new_euler[3] = {roll, pitch, yaw};

        // --- Existing name: expand range ---
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name)) {
                if (oris[i].sample_count <= 1) {
                    // Transition from point → range.
                    // Compute new center as circular mean of old center + new sample,
                    // and half_width encompassing both.
                    for (int ax = 0; ax < 3; ax++) {
                        float old_c = oris[i].euler_center[ax];
                        oris[i].euler_center[ax] = circular_mean_2_deg(old_c, new_euler[ax]);
                        oris[i].euler_half_width[ax] = 0.0f;
                        oris[i].euler_half_width[ax] = expand_half_width_deg(
                            oris[i].euler_center[ax], 0.0f, old_c);
                        oris[i].euler_half_width[ax] = expand_half_width_deg(
                            oris[i].euler_center[ax], oris[i].euler_half_width[ax], new_euler[ax]);
                    }
                } else {
                    // Already a range — update circular mean incrementally and expand.
                    float N = (float)oris[i].sample_count;
                    for (int ax = 0; ax < 3; ax++) {
                        float old_sin = sinf(oris[i].euler_center[ax] * (float)M_PI / 180.0f) * N;
                        float old_cos = cosf(oris[i].euler_center[ax] * (float)M_PI / 180.0f) * N;
                        float new_sin = old_sin + sinf(new_euler[ax] * (float)M_PI / 180.0f);
                        float new_cos = old_cos + cosf(new_euler[ax] * (float)M_PI / 180.0f);
                        oris[i].euler_center[ax] = atan2f(new_sin, new_cos) * (180.0f / (float)M_PI);
                        oris[i].euler_half_width[ax] = expand_half_width_deg(
                            oris[i].euler_center[ax], oris[i].euler_half_width[ax], new_euler[ax]);
                    }
                }
                oris[i].sample_count++;
                // Keep the latest quaternion for backward compat / point fallback.
                oris[i].qi = qi;
                oris[i].qj = qj;
                oris[i].qk = qk;
                oris[i].qr = qr;
                return i;
            }
        }

        // --- New name: create single-point ori ---
        for (uint8_t i = 0; i < MAX_ORIS; i++) {
            if (!oris[i].used) {
                oris[i].name = name;
                oris[i].qi = qi;
                oris[i].qj = qj;
                oris[i].qk = qk;
                oris[i].qr = qr;
                oris[i].used = true;
                oris[i].sample_count = 1;
                oris[i].euler_center[0] = roll;
                oris[i].euler_center[1] = pitch;
                oris[i].euler_center[2] = yaw;
                oris[i].euler_half_width[0] = 0.0f;
                oris[i].euler_half_width[1] = 0.0f;
                oris[i].euler_half_width[2] = 0.0f;
                if (i >= ori_count) ori_count = i + 1;
                return i;
            }
        }
        return -1;  // full
    }

    /// Save with auto-generated name: ori_0, ori_1, ...
    int save_auto(float qi, float qj, float qk, float qr) {
        for (int n = 0; n < MAX_ORIS; n++) {
            String candidate = "ori_" + String(n);
            bool exists = false;
            for (uint8_t i = 0; i < ori_count; i++) {
                if (oris[i].used && oris[i].name.equalsIgnoreCase(candidate)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) return save(candidate, qi, qj, qk, qr);
        }
        return -1;
    }

    /// Reset a named ori to a fresh single-point from the given quaternion.
    /// Unlike save(), this discards any existing range data.
    int reset(const String& name, float qi, float qj, float qk, float qr) {
        int idx = find(name);
        if (idx < 0) return -1;

        float roll, pitch, yaw;
        quat_to_euler(qi, qj, qk, qr, roll, pitch, yaw);

        oris[idx].qi = qi;
        oris[idx].qj = qj;
        oris[idx].qk = qk;
        oris[idx].qr = qr;
        oris[idx].sample_count = 1;
        oris[idx].euler_center[0] = roll;
        oris[idx].euler_center[1] = pitch;
        oris[idx].euler_center[2] = yaw;
        oris[idx].euler_half_width[0] = 0.0f;
        oris[idx].euler_half_width[1] = 0.0f;
        oris[idx].euler_half_width[2] = 0.0f;
        return idx;
    }

    /// Delete an ori by name.  Returns true if found and deleted.
    bool remove(const String& name) {
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name)) {
                oris[i].used = false;
                oris[i].name = "";
                oris[i].sample_count = 0;
                if (active_ori_index == i) {
                    active_ori_index = -1;
                    active_ori_name = "";
                }
                while (ori_count > 0 && !oris[ori_count - 1].used) ori_count--;
                return true;
            }
        }
        return false;
    }

    /// Remove all oris.
    void clear() {
        for (uint8_t i = 0; i < MAX_ORIS; i++) {
            oris[i].used = false;
            oris[i].name = "";
            oris[i].sample_count = 0;
            oris[i].euler_half_width[0] = 0.0f;
            oris[i].euler_half_width[1] = 0.0f;
            oris[i].euler_half_width[2] = 0.0f;
        }
        ori_count = 0;
        active_ori_index = -1;
        active_ori_name = "";
    }

    /// Find an ori by name.  Returns index or -1.
    int find(const String& name) const {
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name))
                return i;
        }
        return -1;
    }

    /// Return number of saved oris.
    uint8_t count() const {
        uint8_t c = 0;
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used) c++;
        }
        return c;
    }

    /// Build a list of ori names (comma-separated).
    /// Range oris are annotated with [R<N>] showing the sample count.
    String list() const {
        String s;
        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used) continue;
            if (s.length() > 0) s += ", ";
            s += oris[i].name;
            if (oris[i].sample_count >= 2) {
                s += " [R" + String(oris[i].sample_count) + "]";
            }
            if (active_ori_index == i) s += " (*)";
        }
        return s.length() > 0 ? s : String("(none)");
    }

    /// Build a detailed info string for a named ori.
    String info(const String& name) const {
        int idx = find(name);
        if (idx < 0) return "Ori '" + name + "' not found";
        const SavedOri& o = oris[idx];
        String s = o.name + ": samples=" + String(o.sample_count);
        if (o.sample_count >= 2) {
            s += " center=[" + String(o.euler_center[0], 1) + ", "
                 + String(o.euler_center[1], 1) + ", "
                 + String(o.euler_center[2], 1) + "]";
            s += " half_w=[" + String(o.euler_half_width[0], 1) + ", "
                 + String(o.euler_half_width[1], 1) + ", "
                 + String(o.euler_half_width[2], 1) + "]";
        } else {
            s += " point q=(" + String(o.qi, 3) + "," + String(o.qj, 3) + ","
                 + String(o.qk, 3) + "," + String(o.qr, 3) + ")";
            s += " euler=[" + String(o.euler_center[0], 1) + ", "
                 + String(o.euler_center[1], 1) + ", "
                 + String(o.euler_center[2], 1) + "]";
        }
        if (active_ori_index == idx) s += " (ACTIVE)";
        return s;
    }

    // --- Main update — call every sensor cycle ------------------------------
    //
    // Arguments:
    //   qi..qr  — current rotation quaternion from the IMU
    //   gyro_mag — magnitude of gyroscope vector (rad/s)

    void update(float qi, float qj, float qk, float qr, float gyro_mag) {
        // Motion gate: if spinning fast, keep the last match.
        if (gyro_mag > motion_threshold) return;

        if (count() == 0) {
            active_ori_index = -1;
            active_ori_name = "";
            return;
        }

        // Compute current Euler angles for range matching.
        float cur_roll, cur_pitch, cur_yaw;
        quat_to_euler(qi, qj, qk, qr, cur_roll, cur_pitch, cur_yaw);
        float cur_euler[3] = {cur_roll, cur_pitch, cur_yaw};

        // --- Phase 1: Check range-mode oris (sample_count >= 2) ---
        // A range ori matches if the current Euler angles fall within its
        // bounding box (center ± half_width + tolerance) on all three axes.
        // Among multiple matches, the tightest bounding box wins.
        int   best_range_idx  = -1;
        float best_range_area = 1e9f;

        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used) continue;
            if (oris[i].sample_count < 2) continue;  // skip point oris

            bool in_range = true;
            float area = 1.0f;
            for (int ax = 0; ax < 3; ax++) {
                float dist = circular_distance_deg(cur_euler[ax], oris[i].euler_center[ax]);
                float threshold = oris[i].euler_half_width[ax] + ori_tolerance;
                if (dist > threshold) {
                    in_range = false;
                    break;
                }
                area *= (oris[i].euler_half_width[ax] + 0.01f);  // avoid zero
            }
            if (in_range && area < best_range_area) {
                best_range_area = area;
                best_range_idx = i;
            }
        }

        if (best_range_idx >= 0) {
            active_ori_index = best_range_idx;
            active_ori_name = oris[best_range_idx].name;
            return;
        }

        // --- Phase 2: Fall back to point-mode oris (geodesic distance) ---
        // If strict_matching is false, range oris also participate here
        // using their stored quaternion, so there is always a closest match.
        int   best_idx   = -1;
        float best_angle = 999.0f;

        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used) continue;
            // In strict mode, skip range oris (they didn't match their box).
            if (strict_matching && oris[i].sample_count >= 2) continue;

            float dot = fabsf(qi * oris[i].qi + qj * oris[i].qj
                            + qk * oris[i].qk + qr * oris[i].qr);
            if (dot > 1.0f) dot = 1.0f;
            float angle = 2.0f * acosf(dot);

            if (angle < best_angle) {
                best_angle = angle;
                best_idx = i;
            }
        }

        if (best_idx >= 0) {
            active_ori_index = best_idx;
            active_ori_name = oris[best_idx].name;
        } else {
            active_ori_index = -1;
            active_ori_name = "";
        }
    }

    /// Check if a named ori is currently the active match.
    bool is_active(const String& name) const {
        return active_ori_index >= 0
            && oris[active_ori_index].used
            && oris[active_ori_index].name.equalsIgnoreCase(name);
    }
};

/// Global ori tracker accessor (Meyer's singleton).
static inline OriTracker& ori_tracker() {
    static OriTracker instance;
    return instance;
}

#endif // ORI_TRACKER_H
