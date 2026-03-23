// =============================================================================
// ori_tracker.h — Orientation save / recall / matching system for ab7
// =============================================================================
//
// OVERVIEW:
//   The ori tracker lets users save named orientations ("oris") and then
//   continuously reports which saved ori the device's current orientation
//   is closest to.  Only one ori is "active" at a time — the best match.
//
// USE CASE:
//   An actor holds the ab7 device.  Several oris are saved corresponding to
//   hand positions that point at different stage lights.  As the actor moves
//   their hand, the system reports which light they are pointing at.  OSC
//   messages can be configured to send only when a specific ori is (or is
//   not) the active match, enabling "point to turn on / off" workflows.
//
// MATCHING ALGORITHM:
//   Orientations are stored as unit quaternions.  The "distance" between two
//   orientations is the geodesic angle between them on the unit sphere:
//
//     angle = 2 · acos( |q_saved · q_current| )
//
//   The dot product is clamped to [0, 1] (absolute value handles the double
//   cover of SO(3)).  The ori with the smallest angle wins.
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
//          /annieData{dev}/ori/save         — auto-named
//          /annieData{dev}/ori/save/{name}   — user-named
//          /annieData{dev}/ori/delete/{name} — remove an ori
//          /annieData{dev}/ori/clear         — remove all oris
//          /annieData{dev}/ori/list          — list saved oris
//          /annieData{dev}/ori/threshold     — set motion gate threshold
//
// CONDITIONAL MESSAGING:
//   Messages can be tagged with an ori condition via config string keys:
//
//     ori_only:{name}    — message sends ONLY when this ori is active
//     ori_not:{name}     — message sends ONLY when this ori is NOT active
//
//   These are stored on the OscMessage and checked in the send task.
// =============================================================================

#ifndef ORI_TRACKER_H
#define ORI_TRACKER_H

#include <Arduino.h>
#include <math.h>

// Maximum number of saved oris.
#define MAX_ORIS 32

// ---------------------------------------------------------------------------
// Saved orientation (quaternion + name)
// ---------------------------------------------------------------------------

struct SavedOri {
    String name;
    float qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f;  // identity
    bool  used = false;
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

    // --- Current state (read by the send task) ------------------------------

    /// Index of the currently matched ori (-1 = none).
    volatile int  active_ori_index = -1;

    /// Name of the currently matched ori ("" = none).
    String active_ori_name;

    // --- Saved oris ---------------------------------------------------------

    SavedOri oris[MAX_ORIS];
    uint8_t  ori_count = 0;

    // --- Methods ------------------------------------------------------------

    /// Save the current orientation under the given name.  If name already
    /// exists, its quaternion is overwritten.
    int save(const String& name, float qi, float qj, float qk, float qr) {
        // Overwrite if name already exists.
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name)) {
                oris[i].qi = qi;
                oris[i].qj = qj;
                oris[i].qk = qk;
                oris[i].qr = qr;
                return i;
            }
        }
        // Find a free slot.
        for (uint8_t i = 0; i < MAX_ORIS; i++) {
            if (!oris[i].used) {
                oris[i].name = name;
                oris[i].qi = qi;
                oris[i].qj = qj;
                oris[i].qk = qk;
                oris[i].qr = qr;
                oris[i].used = true;
                if (i >= ori_count) ori_count = i + 1;
                return i;
            }
        }
        return -1;  // full
    }

    /// Save with auto-generated name: ori_0, ori_1, ...
    int save_auto(float qi, float qj, float qk, float qr) {
        // Find the next free auto-name.
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

    /// Delete an ori by name.  Returns true if found and deleted.
    bool remove(const String& name) {
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name)) {
                oris[i].used = false;
                oris[i].name = "";
                // If this was the active ori, clear it.
                if (active_ori_index == i) {
                    active_ori_index = -1;
                    active_ori_name = "";
                }
                // Shrink ori_count if last slot was freed.
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
    String list() const {
        String s;
        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used) continue;
            if (s.length() > 0) s += ", ";
            s += oris[i].name;
            if (active_ori_index == i) s += " (*)";
        }
        return s.length() > 0 ? s : String("(none)");
    }

    // --- Main update — call every sensor cycle ------------------------------
    //
    // Arguments:
    //   qi..qr  — current rotation quaternion from BNO-085
    //   gyro_mag — magnitude of gyroscope vector (rad/s)

    void update(float qi, float qj, float qk, float qr, float gyro_mag) {
        // Motion gate: if spinning fast, keep the last match.
        if (gyro_mag > motion_threshold) return;

        if (count() == 0) {
            active_ori_index = -1;
            active_ori_name = "";
            return;
        }

        // Find the closest saved ori using geodesic angle.
        int   best_idx   = -1;
        float best_angle = 999.0f;

        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used) continue;

            float dot = fabsf(qi * oris[i].qi + qj * oris[i].qj
                            + qk * oris[i].qk + qr * oris[i].qr);
            if (dot > 1.0f) dot = 1.0f;
            float angle = 2.0f * acosf(dot);  // radians

            if (angle < best_angle) {
                best_angle = angle;
                best_idx = i;
            }
        }

        if (best_idx >= 0) {
            active_ori_index = best_idx;
            active_ori_name = oris[best_idx].name;
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
