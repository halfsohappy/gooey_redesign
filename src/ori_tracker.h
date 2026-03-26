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
//   Point ori  — a single quaternion.  Matched by geodesic angular distance
//                (closest wins).  Created by saving a name for the first time.
//
//   Range ori  — defined by 2+ sample quaternions.  The system tracks the
//                maximum geodesic angular distance from the center quaternion
//                (angular_half_width).  Matched when the device is within
//                angular_half_width + ori_tolerance of the center.
//                Created by re-saving an existing ori name.
//
//   All matching is done purely in quaternion space — no Euler angles are
//   involved.  This avoids gimbal-lock artefacts and works correctly for any
//   device mounting orientation.
//
// MATCHING ALGORITHM:
//   1. Range oris are checked first.  If the current quaternion is within a
//      range ori's angular_half_width + tolerance, that ori is active
//      (tightest half-width wins among multiple matches).
//   2. If no range ori matches, fall back to point oris — the closest
//      geodesic angle wins.
//   3. If strict_matching is false (default), range oris also participate in
//      the point-ori fallback, so there is always an active ori.
//      If strict_matching is true, "no match" is possible.
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

// ---------------------------------------------------------------------------
// Quaternion geodesic distance helper
// ---------------------------------------------------------------------------

/// Angular distance (radians) between two unit quaternions.
/// Returns a value in [0, π].  Handles the double-cover (q ≡ -q) correctly.
static inline float quat_angle_between(float qi1, float qj1, float qk1, float qr1,
                                        float qi2, float qj2, float qk2, float qr2) {
    float d = fabsf(qi1*qi2 + qj1*qj2 + qk1*qk2 + qr1*qr2);
    if (d > 1.0f) d = 1.0f;
    return 2.0f * acosf(d);
}

// ---------------------------------------------------------------------------
// Saved orientation (quaternion + optional Euler range)
// ---------------------------------------------------------------------------

struct SavedOri {
    String  name;
    float   qi = 0.0f, qj = 0.0f, qk = 0.0f, qr = 1.0f;  // center quaternion
    bool    used = false;

    // Range data — updated from multiple save() calls on the same name.
    // sample_count == 0 or 1  →  point mode (geodesic match, half-width = 0)
    // sample_count >= 2       →  range mode (angular sphere in quaternion space)
    uint8_t sample_count = 0;
    float   angular_half_width = 0.0f;  // max angular distance from center (radians)

    // LED color for on-device ori editing workflow.
    // Auto-assigned from a default palette when an ori is created; can be
    // changed via /ori/color/{name} command.
    uint8_t color_r = 0, color_g = 0, color_b = 0;
};

// Default color palette for auto-assigning ori colors.
// 12 distinct hues at moderate brightness (suitable for an LED indicator).
static const uint8_t ORI_PALETTE[][3] = {
    {255,   0,   0},  // red
    {  0, 255,   0},  // green
    {  0,   0, 255},  // blue
    {255, 255,   0},  // yellow
    {255,   0, 255},  // magenta
    {  0, 255, 255},  // cyan
    {255, 128,   0},  // orange
    {128,   0, 255},  // purple
    {  0, 255, 128},  // spring green
    {255,   0, 128},  // rose
    {128, 255,   0},  // chartreuse
    {  0, 128, 255},  // sky blue
};
static constexpr uint8_t ORI_PALETTE_SIZE = sizeof(ORI_PALETTE) / sizeof(ORI_PALETTE[0]);

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

    // --- On-device ori editing (button workflow) ----------------------------

    /// Index of the ori currently selected for editing via buttons (-1 = none).
    /// Button A adds a range point to this ori; Button B advances to the next.
    int selected_ori_index = -1;

    /// Next palette color index to auto-assign when an ori is created.
    uint8_t next_color_index = 0;

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
        // --- Existing name: expand range ---
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name)) {
                // Expand angular_half_width to cover the new sample from the
                // current center.  The center stays fixed (first saved pose);
                // use reset() to reposition it.
                float ang = quat_angle_between(qi, qj, qk, qr,
                                               oris[i].qi, oris[i].qj,
                                               oris[i].qk, oris[i].qr);
                if (ang > oris[i].angular_half_width)
                    oris[i].angular_half_width = ang;
                oris[i].sample_count++;
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
                oris[i].angular_half_width = 0.0f;
                // Auto-assign a color from the palette.
                oris[i].color_r = ORI_PALETTE[next_color_index % ORI_PALETTE_SIZE][0];
                oris[i].color_g = ORI_PALETTE[next_color_index % ORI_PALETTE_SIZE][1];
                oris[i].color_b = ORI_PALETTE[next_color_index % ORI_PALETTE_SIZE][2];
                next_color_index++;
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

        oris[idx].qi = qi;
        oris[idx].qj = qj;
        oris[idx].qk = qk;
        oris[idx].qr = qr;
        oris[idx].sample_count = 1;
        oris[idx].angular_half_width = 0.0f;
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
            oris[i].angular_half_width = 0.0f;
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
        s += " q=(" + String(o.qi, 3) + "," + String(o.qj, 3) + ","
             + String(o.qk, 3) + "," + String(o.qr, 3) + ")";
        if (o.sample_count >= 2) {
            float hw_deg = o.angular_half_width * (180.0f / (float)M_PI);
            s += " half_w=" + String(hw_deg, 1) + "deg";
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

        // Convert ori_tolerance from degrees to radians for quaternion comparison.
        float tol_rad = ori_tolerance * (float)M_PI / 180.0f;

        // --- Phase 1: Check range-mode oris (sample_count >= 2) ---
        // A range ori matches if the geodesic angle to its center is within
        // angular_half_width + tolerance.  Tightest half-width wins.
        int   best_range_idx = -1;
        float best_range_hw  = 1e9f;

        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used || oris[i].sample_count < 2) continue;
            float angle = quat_angle_between(qi, qj, qk, qr,
                                             oris[i].qi, oris[i].qj,
                                             oris[i].qk, oris[i].qr);
            if (angle <= oris[i].angular_half_width + tol_rad) {
                if (oris[i].angular_half_width < best_range_hw) {
                    best_range_hw  = oris[i].angular_half_width;
                    best_range_idx = i;
                }
            }
        }

        if (best_range_idx >= 0) {
            active_ori_index = best_range_idx;
            active_ori_name = oris[best_range_idx].name;
            return;
        }

        // --- Phase 2: Fall back to point-mode oris (closest geodesic) ---
        // If strict_matching is false, range oris also participate here
        // (they didn't match their range but are the closest alternative).
        int   best_idx   = -1;
        float best_angle = 999.0f;

        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used) continue;
            if (strict_matching && oris[i].sample_count >= 2) continue;
            float angle = quat_angle_between(qi, qj, qk, qr,
                                             oris[i].qi, oris[i].qj,
                                             oris[i].qk, oris[i].qr);
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

    // --- On-device ori editing helpers --------------------------------------

    /// Set the RGB color of a named ori.  Returns true if found.
    bool set_color(const String& name, uint8_t r, uint8_t g, uint8_t b) {
        int idx = find(name);
        if (idx < 0) return false;
        oris[idx].color_r = r;
        oris[idx].color_g = g;
        oris[idx].color_b = b;
        return true;
    }

    /// Advance selected_ori_index to the next used ori (wraps around).
    /// Returns the new selected index, or -1 if no oris exist.
    int select_next() {
        if (count() == 0) {
            selected_ori_index = -1;
            return -1;
        }
        // Start searching from the slot after the current selection.
        int start = (selected_ori_index < 0) ? 0 : selected_ori_index + 1;
        for (uint8_t n = 0; n < ori_count + 1; n++) {
            int idx = (start + n) % (int)ori_count;
            // Handle case where idx could be negative if ori_count is 0
            if (idx < 0) idx = 0;
            if (idx < (int)ori_count && oris[idx].used) {
                selected_ori_index = idx;
                return idx;
            }
        }
        // Shouldn't reach here if count() > 0, but just in case.
        selected_ori_index = -1;
        return -1;
    }

    /// Get the currently selected ori (for LED display), or nullptr if none.
    const SavedOri* selected_ori() const {
        if (selected_ori_index < 0 || selected_ori_index >= (int)ori_count)
            return nullptr;
        if (!oris[selected_ori_index].used) return nullptr;
        return &oris[selected_ori_index];
    }
};

/// Global ori tracker accessor (Meyer's singleton).
static inline OriTracker& ori_tracker() {
    static OriTracker instance;
    return instance;
}

#endif // ORI_TRACKER_H
