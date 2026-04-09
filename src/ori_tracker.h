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
// ORI DATA MODEL:
//   Each ori stores a cloud of up to MAX_CLOUD_SAMPLES quaternions captured
//   during a recording session.  A matching axis may also be stored — if one
//   is detected, matching uses the world-space projection of that local axis
//   (ignoring wrist roll) rather than full quaternion distance.
//
//   sample_count == 0  →  registered but not yet sampled (pre-reg slot)
//   sample_count >= 1  →  active; all samples live in the cloud array
//
// MATCHING ALGORITHM (unified, single-pass):
//   For each ori with sample_count > 0:
//     1. Compute the minimum distance from the current orientation to any
//        sample in the cloud.
//        - If use_axis == true: distance is the angle between the world-space
//          projections of the stored axis (ignoring wrist roll around it).
//        - If use_axis == false: distance is the full geodesic quaternion angle.
//     2. score = min_distance - per_ori_tolerance   (negative = inside)
//   The ori with the lowest score wins.
//   If strict_matching and the winning score > 0, no ori is active.
//
// AUTO-AXIS DETECTION:
//   When a recording session is finalized with ≥ 2 samples, the tracker tests
//   six candidate local axes (±X, ±Y, ±Z).  For each candidate it computes
//   the variance of the world-space projections across all samples.  If the
//   most-stable candidate's variance is below AUTO_AXIS_THRESHOLD, that axis
//   is stored (use_axis = true).  Otherwise use_axis = false and full
//   quaternion matching is used (good for zone/sweep oris).
//
// RECORDING WORKFLOW:
//   1. start_recording(name) — begin capturing IMU samples.
//   2. push_sample() — called each IMU cycle while active (no-ops when full).
//   3. stop_recording() — finalize: auto-detect axis, farthest-first
//      subsample to MAX_CLOUD_SAMPLES, store in ori slot.
//   4. cancel_recording() — discard without storing.
//
// BUTTON WORKFLOW:
//   Short tap  (<300 ms) Button A — instant single-sample save (backward compat)
//   Hold       (>300 ms) Button A — start recording; release = stop + finalize
//   Button B              — cycle selected_ori_index for LED display
//
// BACKWARD COMPAT:
//   save(name, qi, qj, qk, qr) appends a single sample to the cloud (no
//   axis detection).  This powers /ori/save/{name} and remote quick-tap.
//
// OSC COMMANDS (new):
//   /ori/record/start/{name}  — begin recording
//   /ori/record/stop          — finalize and store
//   /ori/record/cancel        — discard
//   /ori/record/status        — reply: name, count, elapsed_ms, active
//
// =============================================================================

#ifndef ORI_TRACKER_H
#define ORI_TRACKER_H

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Maximum number of saved oris.
#define MAX_ORIS 32

/// Maximum quaternion samples stored per ori (after farthest-first subsampling).
#define MAX_CLOUD_SAMPLES 8

/// Recording buffer capacity (samples).  At ~50 Hz that is ~6 seconds.
#define RECORDING_BUFFER 300

/// Variance threshold (rad²) for auto-axis detection.
/// If the most-stable candidate axis has variance below this value across all
/// recording samples, use_axis = true.  Otherwise full-quaternion matching.
/// ~0.015 rad² corresponds to ~±7° spread in pointing direction — tight enough
/// to catch deliberate pointing (wrist wiggle stays fixed) but not zone sweeps.
#define AUTO_AXIS_THRESHOLD 0.015f

// ---------------------------------------------------------------------------
// Quaternion / vector helpers
// ---------------------------------------------------------------------------

/// Geodesic angular distance (radians, [0, π]) between two unit quaternions.
/// Handles the double-cover (q ≡ −q) by using the absolute dot product.
static inline float quat_angle_between(float qi1, float qj1, float qk1, float qr1,
                                        float qi2, float qj2, float qk2, float qr2) {
    float d = fabsf(qi1*qi2 + qj1*qj2 + qk1*qk2 + qr1*qr2);
    if (d > 1.0f) d = 1.0f;
    return 2.0f * acosf(d);
}

/// Rotate a local unit vector (ax, ay, az) by quaternion (qi, qj, qk, qr)
/// and write the world-space result into (wx, wy, wz).
static inline void rotate_vec(float qi, float qj, float qk, float qr,
                                float ax, float ay, float az,
                                float& wx, float& wy, float& wz) {
    // Standard rotation: v' = q * v * q^-1 (using the Rodriguez formula)
    wx = ax*(1.0f - 2.0f*(qj*qj + qk*qk))
       + ay*(2.0f*(qi*qj - qk*qr))
       + az*(2.0f*(qi*qk + qj*qr));
    wy = ax*(2.0f*(qi*qj + qk*qr))
       + ay*(1.0f - 2.0f*(qi*qi + qk*qk))
       + az*(2.0f*(qj*qk - qi*qr));
    wz = ax*(2.0f*(qi*qk - qj*qr))
       + ay*(2.0f*(qj*qk + qi*qr))
       + az*(1.0f - 2.0f*(qi*qi + qj*qj));
}

/// Angle (radians) between the world-space projections of local axis (ax,ay,az)
/// under quaternion q1 and the sample quaternion q2.
/// This is the axis-aware distance metric: wrist roll (rotation around the axis)
/// contributes zero to this distance.
static inline float axis_angle(float qi1, float qj1, float qk1, float qr1,
                                float qi2, float qj2, float qk2, float qr2,
                                float ax,  float ay,  float az) {
    float wx1, wy1, wz1, wx2, wy2, wz2;
    rotate_vec(qi1, qj1, qk1, qr1, ax, ay, az, wx1, wy1, wz1);
    rotate_vec(qi2, qj2, qk2, qr2, ax, ay, az, wx2, wy2, wz2);
    float d = wx1*wx2 + wy1*wy2 + wz1*wz2;
    if (d >  1.0f) d =  1.0f;
    if (d < -1.0f) d = -1.0f;
    return acosf(d);
}

// ---------------------------------------------------------------------------
// Saved orientation — cloud of samples + optional matching axis
// ---------------------------------------------------------------------------

struct SavedOri {
    String  name;
    bool    used = false;

    // --- Sample cloud -------------------------------------------------------
    // Up to MAX_CLOUD_SAMPLES quaternions captured during a recording session
    // or accumulated via instant saves (backward compat).
    float   qi[MAX_CLOUD_SAMPLES] = {};
    float   qj[MAX_CLOUD_SAMPLES] = {};
    float   qk[MAX_CLOUD_SAMPLES] = {};
    float   qr[MAX_CLOUD_SAMPLES] = {};
    uint8_t sample_count = 0;  // 0 = registered but not yet sampled

    // --- Per-ori matching axis ----------------------------------------------
    // Auto-detected during recording finalization (see AUTO_AXIS_THRESHOLD).
    // When use_axis = true, matching ignores rotation around this local axis
    // (e.g., wrist roll for a pointing device).
    bool  use_axis = false;
    float axis_x   = 1.0f, axis_y = 0.0f, axis_z = 0.0f;  // local unit vector

    // Per-ori angular tolerance (degrees) around each cloud sample.
    float tolerance = 10.0f;

    bool is_sampled() const { return sample_count > 0; }
};

// ---------------------------------------------------------------------------
// RecordingSession — transient buffer for timed recording
// ---------------------------------------------------------------------------

struct RecordingSession {
    bool     active    = false;
    String   name;
    uint16_t count     = 0;
    unsigned long start_ms = 0;

    float qi_buf[RECORDING_BUFFER];
    float qj_buf[RECORDING_BUFFER];
    float qk_buf[RECORDING_BUFFER];
    float qr_buf[RECORDING_BUFFER];

    void push(float qi, float qj, float qk, float qr) {
        if (!active || count >= RECORDING_BUFFER) return;
        qi_buf[count] = qi;
        qj_buf[count] = qj;
        qk_buf[count] = qk;
        qr_buf[count] = qr;
        count++;
    }

    void begin(const String& n) {
        name     = n;
        count    = 0;
        active   = true;
        start_ms = millis();
    }

    void end() {
        active = false;
        count  = 0;
        name   = "";
    }

    unsigned long elapsed_ms() const {
        return active ? (millis() - start_ms) : 0;
    }
};

// ---------------------------------------------------------------------------
// OriTracker
// ---------------------------------------------------------------------------

class OriTracker {
public:
    // --- Global configuration -----------------------------------------------

    /// Gyroscope magnitude threshold (rad/s).  Matching freezes above this.
    float motion_threshold = 1.5f;   // ~86 °/s

    /// Extra angular margin (degrees) applied globally on top of each ori's
    /// per-sample tolerance.  Use ori.tolerance for per-ori tuning.
    /// (Kept for backward compat — added to each ori's tolerance at match time.)
    float ori_tolerance = 10.0f;

    /// When true, if the winning score > 0, no ori is active.
    /// When false (default), the closest ori always wins.
    bool strict_matching = false;

    /// When strict_matching is on and no exact match is found, this ori (if set)
    /// is used as the active ori instead of returning no match.
    String general_ori_name;

    /// Hysteresis margin (degrees).  A challenger ori must beat the current
    /// active ori's score by at least this margin to trigger a switch.
    /// Set to 0 to disable (original behaviour: always pick the best scorer).
    float hysteresis_margin = 5.0f;

    // --- Current state ------------------------------------------------------

    volatile int active_ori_index = -1;
    String       active_ori_name;

    // --- Ori-watch: push active-ori changes to status config ----------------
    bool         ori_watch_enabled = false;

    // --- On-device editing state --------------------------------------------

    int     selected_ori_index = -1;

    // --- Storage ------------------------------------------------------------

    SavedOri oris[MAX_ORIS];
    uint8_t  ori_count = 0;

    // --- Recording session --------------------------------------------------

    RecordingSession session;

    // === Recording API ======================================================

    /// Begin a recording session for the named ori.
    /// Returns false if a session is already active.
    bool start_recording(const String& name) {
        if (session.active) return false;
        // Auto-register the ori slot if it doesn't exist yet.
        if (find(name) < 0) {
            register_ori(name);
        }
        session.begin(name);
        return true;
    }

    /// Finalize the recording session: run auto-axis detection, farthest-first
    /// subsampling, and store the result in the ori slot.
    /// Returns the slot index, or -1 on error.
    int stop_recording() {
        if (!session.active) return -1;
        session.active = false;
        int idx = _finalize(session.name, session.qi_buf, session.qj_buf,
                            session.qk_buf, session.qr_buf, session.count);
        session.end();
        return idx;
    }

    /// Discard the current recording without storing anything.
    void cancel_recording() { session.end(); }

    // === Instant-save API (backward compat) =================================

    /// Append a single sample to an ori's cloud.
    /// - New name → creates the ori with 1 sample, use_axis = false.
    /// - Existing name with room → appends; if count goes from 1 to 2, re-runs
    ///   auto-axis detection on the two samples (minimal signal but available).
    /// - Cloud full → no-op (returns index).
    int save(const String& name, float qi, float qj, float qk, float qr) {
        int idx = find(name);
        if (idx >= 0) {
            SavedOri& o = oris[idx];
            if (o.sample_count == 0) {
                // Pre-registered slot gets its first sample.
                o.qi[0] = qi; o.qj[0] = qj; o.qk[0] = qk; o.qr[0] = qr;
                o.sample_count = 1;
                o.use_axis = false;
                return idx;
            }
            if (o.sample_count >= MAX_CLOUD_SAMPLES) return idx; // full, ignore
            uint8_t s = o.sample_count;
            o.qi[s] = qi; o.qj[s] = qj; o.qk[s] = qk; o.qr[s] = qr;
            o.sample_count++;
            // With ≥ 2 samples attempt axis detection so quick-taps also benefit.
            if (o.sample_count >= 2) _detect_axis(o);
            return idx;
        }

        // New ori — find a free slot.
        for (uint8_t i = 0; i < MAX_ORIS; i++) {
            if (!oris[i].used) {
                oris[i].name  = name;
                oris[i].used  = true;
                oris[i].qi[0] = qi; oris[i].qj[0] = qj;
                oris[i].qk[0] = qk; oris[i].qr[0] = qr;
                oris[i].sample_count = 1;
                oris[i].use_axis = false;
                if (i >= ori_count) ori_count = i + 1;
                return i;
            }
        }
        return -1;  // full
    }

    /// Pre-register a named slot with no orientation data.
    int register_ori(const String& name) {
        int existing = find(name);
        if (existing >= 0) return existing;
        for (uint8_t i = 0; i < MAX_ORIS; i++) {
            if (!oris[i].used) {
                oris[i].name         = name;
                oris[i].used         = true;
                oris[i].sample_count = 0;
                oris[i].use_axis     = false;
                if (i >= ori_count) ori_count = i + 1;
                return i;
            }
        }
        return -1;
    }

    /// Auto-named instant save: ori_0, ori_1, ...
    int save_auto(float qi, float qj, float qk, float qr) {
        for (int n = 0; n < MAX_ORIS; n++) {
            String candidate = "ori_" + String(n);
            if (find(candidate) < 0) return save(candidate, qi, qj, qk, qr);
        }
        return -1;
    }

    /// Clear all samples from a named ori (sets sample_count = 0, ready for
    /// a new recording session or a fresh save).
    int reset(const String& name) {
        int idx = find(name);
        if (idx < 0) return -1;
        oris[idx].sample_count = 0;
        oris[idx].use_axis     = false;
        if (active_ori_index == idx) {
            active_ori_index = -1;
            active_ori_name  = "";
        }
        return idx;
    }

    // === Query / list =======================================================

    bool remove(const String& name) {
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name)) {
                oris[i].used = false;
                oris[i].name = "";
                oris[i].sample_count = 0;
                oris[i].use_axis     = false;
                if (active_ori_index == i) {
                    active_ori_index = -1;
                    active_ori_name  = "";
                }
                while (ori_count > 0 && !oris[ori_count - 1].used) ori_count--;
                return true;
            }
        }
        return false;
    }

    void clear() {
        for (uint8_t i = 0; i < MAX_ORIS; i++) {
            oris[i].used         = false;
            oris[i].name         = "";
            oris[i].sample_count = 0;
            oris[i].use_axis     = false;
        }
        ori_count        = 0;
        active_ori_index = -1;
        active_ori_name  = "";
    }

    int find(const String& name) const {
        for (uint8_t i = 0; i < ori_count; i++) {
            if (oris[i].used && oris[i].name.equalsIgnoreCase(name)) return i;
        }
        return -1;
    }

    uint8_t count() const {
        uint8_t c = 0;
        for (uint8_t i = 0; i < ori_count; i++) if (oris[i].used) c++;
        return c;
    }

    uint8_t sampled_count() const {
        uint8_t c = 0;
        for (uint8_t i = 0; i < ori_count; i++)
            if (oris[i].used && oris[i].sample_count > 0) c++;
        return c;
    }

    /// Comma-separated ori list with annotations:
    ///   [P]    — unsampled (pre-registered)
    ///   [N]    — N samples in cloud
    ///   (AX)   — axis-aware matching
    ///   (*)    — currently active
    String list() const {
        String s;
        for (uint8_t i = 0; i < ori_count; i++) {
            if (!oris[i].used) continue;
            if (s.length() > 0) s += ", ";
            s += oris[i].name;
            if (oris[i].sample_count == 0) {
                s += " [P]";
            } else {
                s += " [" + String(oris[i].sample_count) + "]";
                if (oris[i].use_axis) s += " (AX)";
            }
            if (active_ori_index == i) s += " (*)";
        }
        return s.length() > 0 ? s : String("(none)");
    }

    /// Detailed info string for a named ori.
    String info(const String& name) const {
        int idx = find(name);
        if (idx < 0) return "Ori '" + name + "' not found";
        const SavedOri& o = oris[idx];

        String s = o.name + ": samples=" + String(o.sample_count);

        if (o.sample_count == 0) {
            s += " (unsampled)";
            return s;
        }

        if (o.use_axis) {
            s += " axis=(" + String(o.axis_x, 2) + "," + String(o.axis_y, 2)
                 + "," + String(o.axis_z, 2) + ")";
        } else {
            s += " axis=fullQ";
        }
        s += " tol=" + String(o.tolerance, 1) + "deg";

        // Show up to first 3 sample quaternions.
        uint8_t show = o.sample_count < 3 ? o.sample_count : 3;
        for (uint8_t si = 0; si < show; si++) {
            s += " q" + String(si) + "=("
                 + String(o.qi[si], 3) + "," + String(o.qj[si], 3) + ","
                 + String(o.qk[si], 3) + "," + String(o.qr[si], 3) + ")";
        }
        if (o.sample_count > 3) s += " ...";

        if (active_ori_index == idx) s += " (ACTIVE)";
        return s;
    }

    bool is_active(const String& name) const {
        return active_ori_index >= 0
            && oris[active_ori_index].used
            && oris[active_ori_index].name.equalsIgnoreCase(name);
    }


    // === Main update — call every sensor cycle ==============================

    void update(float qi, float qj, float qk, float qr, float gyro_mag) {
        // Feed sample into an active recording session.
        if (session.active) session.push(qi, qj, qk, qr);

        // Motion gate: clear active match during fast rotation so listeners
        // see no-match rather than the stale pre-motion ori.
        if (gyro_mag > motion_threshold) {
            active_ori_index = -1;
            active_ori_name  = "";
            return;
        }

        if (sampled_count() == 0) {
            active_ori_index = -1;
            active_ori_name  = "";
            return;
        }

        float best_score = 1e9f;
        int   best_idx   = -1;

        for (uint8_t i = 0; i < ori_count; i++) {
            const SavedOri& o = oris[i];
            if (!o.used || o.sample_count == 0) continue;

            float tol_rad = (o.tolerance + ori_tolerance) * ((float)M_PI / 180.0f);

            // Minimum distance from current orientation to any cloud sample.
            float min_dist = 1e9f;
            for (uint8_t s = 0; s < o.sample_count; s++) {
                float d;
                if (o.use_axis) {
                    d = axis_angle(qi, qj, qk, qr,
                                   o.qi[s], o.qj[s], o.qk[s], o.qr[s],
                                   o.axis_x, o.axis_y, o.axis_z);
                } else {
                    d = quat_angle_between(qi, qj, qk, qr,
                                           o.qi[s], o.qj[s], o.qk[s], o.qr[s]);
                }
                if (d < min_dist) min_dist = d;
            }

            float score = min_dist - tol_rad;  // negative = inside tolerance
            if (score < best_score) {
                best_score = score;
                best_idx   = i;
            }
        }

        if (strict_matching && best_score > 0.0f) {
            // No exact match — use general ori if defined.
            if (general_ori_name.length() > 0) {
                int gi = find(general_ori_name);
                best_idx = (gi >= 0) ? gi : -1;
            } else {
                best_idx = -1;
            }
        }

        // Apply hysteresis: only switch from the current ori if the challenger
        // beats it by at least hysteresis_margin degrees.
        if (hysteresis_margin > 0.0f && best_idx >= 0
                && active_ori_index >= 0 && best_idx != active_ori_index) {
            const SavedOri& cur = oris[active_ori_index];
            if (cur.used && cur.sample_count > 0) {
                float cur_tol = (cur.tolerance + ori_tolerance) * ((float)M_PI / 180.0f);
                float cur_min = 1e9f;
                for (uint8_t s = 0; s < cur.sample_count; s++) {
                    float d = cur.use_axis
                        ? axis_angle(qi, qj, qk, qr,
                                     cur.qi[s], cur.qj[s], cur.qk[s], cur.qr[s],
                                     cur.axis_x, cur.axis_y, cur.axis_z)
                        : quat_angle_between(qi, qj, qk, qr,
                                             cur.qi[s], cur.qj[s], cur.qk[s], cur.qr[s]);
                    if (d < cur_min) cur_min = d;
                }
                float cur_score = cur_min - cur_tol;
                float hyst_rad  = hysteresis_margin * ((float)M_PI / 180.0f);
                if (best_score > cur_score - hyst_rad) {
                    best_idx = active_ori_index;  // stay on current
                }
            }
        }

        active_ori_index = best_idx;
        active_ori_name  = (best_idx >= 0) ? oris[best_idx].name : "";
    }

    // === On-device button helpers ============================================

    int select_next() {
        if (count() == 0) { selected_ori_index = -1; return -1; }
        int start = (selected_ori_index < 0) ? 0 : selected_ori_index + 1;
        for (uint8_t n = 0; n < ori_count + 1; n++) {
            int idx = (start + (int)n) % (int)ori_count;
            if (idx < 0) idx = 0;
            if (idx < (int)ori_count && oris[idx].used) {
                selected_ori_index = idx;
                return idx;
            }
        }
        selected_ori_index = -1;
        return -1;
    }

    const SavedOri* selected_ori() const {
        if (selected_ori_index < 0 || selected_ori_index >= (int)ori_count)
            return nullptr;
        if (!oris[selected_ori_index].used) return nullptr;
        return &oris[selected_ori_index];
    }

private:
    // === Internal helpers ====================================================

    /// Finalize a set of raw recording samples into an ori slot.
    int _finalize(const String& name,
                  const float* qi_buf, const float* qj_buf,
                  const float* qk_buf, const float* qr_buf,
                  uint16_t n_samples) {
        if (n_samples == 0) return -1;
        int idx = find(name);
        if (idx < 0) return -1;
        SavedOri& o = oris[idx];

        if (n_samples == 1) {
            // Single sample: just store it directly, no axis detection.
            o.qi[0] = qi_buf[0]; o.qj[0] = qj_buf[0];
            o.qk[0] = qk_buf[0]; o.qr[0] = qr_buf[0];
            o.sample_count = 1;
            o.use_axis = false;
            return idx;
        }

        // --- Auto-axis detection ---
        // Test ±X, ±Y, ±Z plus 8 body-diagonal axes so diagonal device mounts
        // (e.g. at 45°) can also be detected as having a stable rotation axis.
        static const float INV_SQRT3 = 0.57735027f;
        static const float CANDIDATES[14][3] = {
            // Cardinal axes (6)
            { 1,  0,  0}, {-1,  0,  0},
            { 0,  1,  0}, { 0, -1,  0},
            { 0,  0,  1}, { 0,  0, -1},
            // Body-diagonal axes (8) — 1/√3 ≈ 0.5774
            { INV_SQRT3,  INV_SQRT3,  INV_SQRT3},
            { INV_SQRT3,  INV_SQRT3, -INV_SQRT3},
            { INV_SQRT3, -INV_SQRT3,  INV_SQRT3},
            { INV_SQRT3, -INV_SQRT3, -INV_SQRT3},
            {-INV_SQRT3,  INV_SQRT3,  INV_SQRT3},
            {-INV_SQRT3,  INV_SQRT3, -INV_SQRT3},
            {-INV_SQRT3, -INV_SQRT3,  INV_SQRT3},
            {-INV_SQRT3, -INV_SQRT3, -INV_SQRT3}
        };

        float best_var  = 1e9f;
        int   best_cand = -1;

        for (int c = 0; c < 14; c++) {
            float ax = CANDIDATES[c][0];
            float ay = CANDIDATES[c][1];
            float az = CANDIDATES[c][2];

            // Compute mean world-space direction of this axis across all samples.
            float mx = 0, my = 0, mz = 0;
            for (uint16_t s = 0; s < n_samples; s++) {
                float wx, wy, wz;
                rotate_vec(qi_buf[s], qj_buf[s], qk_buf[s], qr_buf[s],
                           ax, ay, az, wx, wy, wz);
                mx += wx; my += wy; mz += wz;
            }
            float mlen = sqrtf(mx*mx + my*my + mz*mz);
            if (mlen < 1e-6f) continue;
            mx /= mlen; my /= mlen; mz /= mlen;

            // Compute variance: mean of (1 - dot(world_vec, mean))^2
            float var = 0;
            for (uint16_t s = 0; s < n_samples; s++) {
                float wx, wy, wz;
                rotate_vec(qi_buf[s], qj_buf[s], qk_buf[s], qr_buf[s],
                           ax, ay, az, wx, wy, wz);
                float d = wx*mx + wy*my + wz*mz;
                if (d >  1.0f) d =  1.0f;
                if (d < -1.0f) d = -1.0f;
                float ang = acosf(d);
                var += ang * ang;
            }
            var /= (float)n_samples;

            if (var < best_var) {
                best_var  = var;
                best_cand = c;
            }
        }

        if (best_cand >= 0 && best_var < AUTO_AXIS_THRESHOLD) {
            o.use_axis = true;
            o.axis_x   = CANDIDATES[best_cand][0];
            o.axis_y   = CANDIDATES[best_cand][1];
            o.axis_z   = CANDIDATES[best_cand][2];
        } else {
            o.use_axis = false;
            o.axis_x = 1.0f; o.axis_y = 0.0f; o.axis_z = 0.0f;
        }

        // --- Farthest-first subsampling to MAX_CLOUD_SAMPLES ---
        // Operates in axis-projected space if axis detected, quaternion space otherwise.
        uint16_t kept[MAX_CLOUD_SAMPLES];
        uint8_t  n_kept = 0;
        kept[n_kept++] = 0;  // always start with first sample

        while (n_kept < MAX_CLOUD_SAMPLES && n_kept < n_samples) {
            float  max_min_dist = -1.0f;
            uint16_t farthest  = 0;

            for (uint16_t s = 0; s < n_samples; s++) {
                // Skip already-kept samples.
                bool already_kept = false;
                for (uint8_t k = 0; k < n_kept; k++) {
                    if (kept[k] == s) { already_kept = true; break; }
                }
                if (already_kept) continue;

                // Find minimum distance from s to any kept sample.
                float min_d = 1e9f;
                for (uint8_t k = 0; k < n_kept; k++) {
                    uint16_t ki = kept[k];
                    float d;
                    if (o.use_axis) {
                        d = axis_angle(qi_buf[s],  qj_buf[s],  qk_buf[s],  qr_buf[s],
                                       qi_buf[ki], qj_buf[ki], qk_buf[ki], qr_buf[ki],
                                       o.axis_x, o.axis_y, o.axis_z);
                    } else {
                        d = quat_angle_between(qi_buf[s],  qj_buf[s],  qk_buf[s],  qr_buf[s],
                                               qi_buf[ki], qj_buf[ki], qk_buf[ki], qr_buf[ki]);
                    }
                    if (d < min_d) min_d = d;
                }
                if (min_d > max_min_dist) {
                    max_min_dist = min_d;
                    farthest     = s;
                }
            }
            kept[n_kept++] = farthest;
        }

        // Store the kept samples.
        o.sample_count = n_kept;
        for (uint8_t k = 0; k < n_kept; k++) {
            uint16_t src = kept[k];
            o.qi[k] = qi_buf[src]; o.qj[k] = qj_buf[src];
            o.qk[k] = qk_buf[src]; o.qr[k] = qr_buf[src];
        }

        return idx;
    }

    /// Attempt axis detection on an ori's existing cloud (called after
    /// appending a sample via the instant-save path).
    void _detect_axis(SavedOri& o) {
        if (o.sample_count < 2) return;

        static const float CANDIDATES[6][3] = {
            { 1, 0, 0}, {-1, 0, 0},
            { 0, 1, 0}, { 0,-1, 0},
            { 0, 0, 1}, { 0, 0,-1}
        };

        float best_var  = 1e9f;
        int   best_cand = -1;

        for (int c = 0; c < 6; c++) {
            float ax = CANDIDATES[c][0];
            float ay = CANDIDATES[c][1];
            float az = CANDIDATES[c][2];

            float mx = 0, my = 0, mz = 0;
            for (uint8_t s = 0; s < o.sample_count; s++) {
                float wx, wy, wz;
                rotate_vec(o.qi[s], o.qj[s], o.qk[s], o.qr[s], ax, ay, az, wx, wy, wz);
                mx += wx; my += wy; mz += wz;
            }
            float mlen = sqrtf(mx*mx + my*my + mz*mz);
            if (mlen < 1e-6f) continue;
            mx /= mlen; my /= mlen; mz /= mlen;

            float var = 0;
            for (uint8_t s = 0; s < o.sample_count; s++) {
                float wx, wy, wz;
                rotate_vec(o.qi[s], o.qj[s], o.qk[s], o.qr[s], ax, ay, az, wx, wy, wz);
                float d = wx*mx + wy*my + wz*mz;
                if (d >  1.0f) d =  1.0f;
                if (d < -1.0f) d = -1.0f;
                float ang = acosf(d);
                var += ang * ang;
            }
            var /= (float)o.sample_count;

            if (var < best_var) { best_var = var; best_cand = c; }
        }

        if (best_cand >= 0 && best_var < AUTO_AXIS_THRESHOLD) {
            o.use_axis = true;
            o.axis_x   = CANDIDATES[best_cand][0];
            o.axis_y   = CANDIDATES[best_cand][1];
            o.axis_z   = CANDIDATES[best_cand][2];
        } else {
            o.use_axis = false;
        }
    }
};

/// Global ori tracker accessor (Meyer's singleton).
static inline OriTracker& ori_tracker() {
    static OriTracker instance;
    return instance;
}

#endif // ORI_TRACKER_H
