// =============================================================================
// osc_message.h — OscMessage class: one sensor-to-OSC-destination mapping
// =============================================================================
//
// An OscMessage represents a single outbound OSC value.  It binds:
//   - a sensor data stream  (value_ptr  → element of data_streams[])
//   - a network destination  (ip, port, osc_address)
//   - optional output bounds  (low / high for value scaling)
//   - an optional parent OscScene (provides defaults / overrides)
//
// Each field has a corresponding flag in the `exist` struct.  Fields that are
// not explicitly set can be inherited from the parent OscScene at send time.
//
// OscMessages are stored in the OscRegistry's fixed array and are never
// heap-allocated.  Pointers to them are stable for the device's lifetime.
// =============================================================================

#ifndef OSC_MESSAGE_H
#define OSC_MESSAGE_H

#include <Arduino.h>
#include <IPAddress.h>
#include "data_streams.h"

// Forward declarations — full definitions in osc_scene.h / osc_registry.h.
class OscScene;
class OscRegistry;

// Maximum number of messages and scenes the registry can hold.  These
// determine the size of the fixed arrays allocated at compile time.
#define MAX_OSC_SCENES     64
#define MAX_OSC_MESSAGES   256
#define MAX_SCENES_PER_MSG 8

// ---------------------------------------------------------------------------
// ExistFlags — tracks which fields have been explicitly set on an object
// ---------------------------------------------------------------------------
//
// Both OscMessage and OscScene use this struct.  A field whose flag is false
// has not been configured and should fall back to a default or parent value.

struct ExistFlags {
    bool name  = false;
    bool ip    = false;
    bool port  = false;
    bool adr   = false;   // osc_address
    bool val   = false;   // value_ptr
    bool low   = false;   // bounds[0]
    bool high  = false;   // bounds[1]
};

// ---------------------------------------------------------------------------
// String helpers (case-insensitive matching, whitespace trimming)
// ---------------------------------------------------------------------------

static inline String osc_trim_copy(String s) {
    s.trim();
    return s;
}

static inline String osc_lower_copy(String s) {
    s.toLowerCase();
    return s;
}

// ---------------------------------------------------------------------------
// OscMessage
// ---------------------------------------------------------------------------

class OscMessage {
public:
    ExistFlags    exist;

    String        name;

    // Network destination (can be overridden by parent scene).
    IPAddress     ip;
    unsigned int  port;
    String        osc_address;

    // Parent scenes.  A message can belong to multiple scenes simultaneously.
    // Each scene provides its own fallback/override values when sending.
    OscScene*     scenes[MAX_SCENES_PER_MSG] = {};
    uint8_t       scene_count = 0;

    void add_scene(OscScene* p) {
        if (!p) return;
        for (uint8_t i = 0; i < scene_count; i++) { if (scenes[i] == p) return; }
        if (scene_count < MAX_SCENES_PER_MSG) scenes[scene_count++] = p;
    }
    void remove_scene(OscScene* p) {
        for (uint8_t i = 0; i < scene_count; i++) {
            if (scenes[i] == p) {
                for (uint8_t j = i; j < scene_count - 1; j++) scenes[j] = scenes[j + 1];
                scenes[--scene_count] = nullptr;
                return;
            }
        }
    }
    bool has_scene(const OscScene* p) const {
        for (uint8_t i = 0; i < scene_count; i++) { if (scenes[i] == p) return true; }
        return false;
    }
    bool in_any_scene() const { return scene_count > 0; }
    OscScene* first_scene() const { return scene_count > 0 ? scenes[0] : nullptr; }
    void clear_scenes() { for (uint8_t i = 0; i < scene_count; i++) scenes[i] = nullptr; scene_count = 0; }

    // Pointer into data_streams[] for the live sensor value to send.
    // Declared volatile because data_streams[] is updated concurrently by
    // the sensor task.  Null for string-type messages.
    volatile float*  value_ptr;

    // Pointer into string_pool().values[] for string-type messages.
    // When non-null, the message sends a string instead of a float.
    // Null for normal float-sensor messages.
    String*          string_value_ptr = nullptr;

    // Output bounds: the raw sensor value is linearly mapped from [0, 1]
    // to [bounds[0], bounds[1]] before sending.  If no bounds are set the
    // raw value is sent unmodified.
    float         bounds[2] = {0.0f, 1.0f};

    // Per-message enable flag.  Disabled messages are skipped by the scene
    // send task even though they remain in the registry.
    bool          enabled = true;

    // --- Gate system (generalises ori_only / ori_not / ternori) ---------------
    //
    // gate_source: the input that drives the gate.
    //   "ori:<name>" — orientation-based gate (active when named ori matches)
    //   "<stream>"   — data-stream gate (active per threshold)
    //   ""           — no gate (message sends unconditionally)
    //
    // gate_mode:
    //   GATE_ONLY (1): send only when gate is active
    //   GATE_NOT  (2): send only when gate is NOT active
    //   GATE_TOGGLE (3): always send; value = high when active, low when not
    //   GATE_RISING  (4): scene-level — fire once on low→high transition
    //   GATE_FALLING (5): scene-level — fire once on high→low transition
    //
    // gate_lo / gate_hi: thresholds for data-stream gates (NaN = unset).
    //   For ONLY/NOT/TOGGLE:
    //     Both set → active when value is in [lo, hi]
    //     Only lo  → active when value >= lo  ("greater than")
    //     Only hi  → active when value <= hi  ("less than")
    //     Neither  → active when value >= 0.5
    //   For RISING/FALLING:
    //     gate_lo → trigger threshold (value must cross this level)
    //     gate_hi → delta minimum (delta between samples must exceed this)

#define GATE_NONE    0
#define GATE_ONLY    1
#define GATE_NOT     2
#define GATE_TOGGLE  3
#define GATE_RISING  4
#define GATE_FALLING 5

    String        gate_source;
    uint8_t       gate_mode = GATE_NONE;
    float         gate_lo   = NAN;
    float         gate_hi   = NAN;

    // --- Duplicate suppression cache (ephemeral, never saved to NVS) ---------
    float         _last_sent_val = 0.0f;
    bool          _has_last_sent = false;

    // --- Constructors -------------------------------------------------------

    OscMessage()
        : ip(0, 0, 0, 0), port(0), value_ptr(nullptr) {}

    explicit OscMessage(const String& set_name)
        : name(set_name), ip(0, 0, 0, 0), port(0), value_ptr(nullptr)
    {
        exist.name = true;
    }

    // Copy constructor / assignment — memberwise copy including ExistFlags.
    OscMessage(const OscMessage& o) {
        exist      = o.exist;
        name       = o.name;
        ip         = o.ip;
        port       = o.port;
        osc_address = o.osc_address;
        scene_count = o.scene_count;
        for (uint8_t i = 0; i < o.scene_count; i++) scenes[i] = o.scenes[i];
        value_ptr  = o.value_ptr;
        bounds[0]  = o.bounds[0];
        bounds[1]  = o.bounds[1];
        enabled    = o.enabled;
        gate_source = o.gate_source;
        gate_mode   = o.gate_mode;
        gate_lo     = o.gate_lo;
        gate_hi     = o.gate_hi;
    }

    OscMessage& operator=(const OscMessage& o) {
        if (this != &o) {
            exist      = o.exist;
            name       = o.name;
            ip         = o.ip;
            port       = o.port;
            osc_address = o.osc_address;
            clear_scenes();
            scene_count = o.scene_count;
            for (uint8_t i = 0; i < o.scene_count; i++) scenes[i] = o.scenes[i];
            value_ptr  = o.value_ptr;
            bounds[0]  = o.bounds[0];
            bounds[1]  = o.bounds[1];
            enabled    = o.enabled;
            gate_source = o.gate_source;
            gate_mode   = o.gate_mode;
            gate_lo     = o.gate_lo;
            gate_hi     = o.gate_hi;
        }
        return *this;
    }

    // --- Merge operator * ---------------------------------------------------
    //
    // `a * b` produces a new OscMessage whose fields come from `a` where set,
    // falling back to `b` otherwise.  This lets you overlay a sparse config
    // on top of an existing message:   new_cfg * existing → merged.

    OscMessage operator*(const OscMessage& o) const {
        OscMessage r;
        r.exist.name  = exist.name  || o.exist.name;
        r.name        = exist.name  ? name  : o.name;

        r.exist.ip    = exist.ip    || o.exist.ip;
        r.ip          = exist.ip    ? ip    : o.ip;

        r.exist.port  = exist.port  || o.exist.port;
        r.port        = exist.port  ? port  : o.port;

        r.exist.adr   = exist.adr   || o.exist.adr;
        r.osc_address = exist.adr   ? osc_address : o.osc_address;

        // Merge scene lists: take all scenes from both, on_change via add_scene.
        for (uint8_t i = 0; i < scene_count; i++)   r.add_scene(scenes[i]);
        for (uint8_t i = 0; i < o.scene_count; i++) r.add_scene(o.scenes[i]);

        r.exist.val   = exist.val   || o.exist.val;
        r.value_ptr   = exist.val   ? value_ptr : o.value_ptr;

        r.exist.low   = exist.low   || o.exist.low;
        r.bounds[0]   = exist.low   ? bounds[0] : o.bounds[0];

        r.exist.high  = exist.high  || o.exist.high;
        r.bounds[1]   = exist.high  ? bounds[1] : o.bounds[1];

        r.enabled     = enabled && o.enabled;

        // Gate: new config takes priority; fall back to other.
        if (gate_mode != GATE_NONE) {
            r.gate_source = gate_source;
            r.gate_mode   = gate_mode;
            r.gate_lo     = gate_lo;
            r.gate_hi     = gate_hi;
        } else {
            r.gate_source = o.gate_source;
            r.gate_mode   = o.gate_mode;
            r.gate_lo     = o.gate_lo;
            r.gate_hi     = o.gate_hi;
        }

        return r;
    }

    // --- Declared here, defined after OscScene is complete -------------------

    /// Returns true if enough information is present to actually send.
    bool sendable() const;

    /// Parse a CSV config string ("key:value, key:value, ...") and populate
    /// this message's fields.  Returns false with an error description on
    /// parse failure.
    bool from_config_str(const String& config, String* error = nullptr);

    /// Build a human-readable summary of this message (for list/info replies).
    String to_info_string(bool verbose = false) const;

    /// Clear the on_change cache so the next send always transmits.
    void clear_on_change_cache() { _has_last_sent = false; _last_sent_val = 0.0f; }
};

#endif // OSC_MESSAGE_H
