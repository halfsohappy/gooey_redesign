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
#define MAX_OSC_SCENES  64
#define MAX_OSC_MESSAGES 256

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
    bool scene = false;
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

    // Parent scene (optional).  When set, the scene provides fallback or
    // override values for ip / port / osc_address.
    OscScene*     scene;

    // Pointer into data_streams[] for the live sensor value to send.
    // Declared volatile because data_streams[] is updated concurrently by
    // the sensor task.
    volatile float*  value_ptr;

    // Output bounds: the raw sensor value is linearly mapped from [0, 1]
    // to [bounds[0], bounds[1]] before sending.  If no bounds are set the
    // raw value is sent unmodified.
    float         bounds[2] = {0.0f, 1.0f};

    // Per-message enable flag.  Disabled messages are skipped by the scene
    // send task even though they remain in the registry.
    bool          enabled = true;

    // --- Orientation-conditional sending (ab7 only) -------------------------
    //
    // ori_only: if non-empty, this message sends ONLY when this ori is active.
    // ori_not:  if non-empty, this message sends ONLY when this ori is NOT active.
    // If both are empty, the message sends unconditionally (normal behaviour).
    String        ori_only;    // e.g. "light1" — send only when ori "light1" is active
    String        ori_not;     // e.g. "light2" — send only when ori "light2" is NOT active

    // --- Ternori (orientation ternary) — binary switch based on ori state ---
    //
    // When ternori is set (e.g. "spotlight"), the message ignores value_ptr and
    // sends bounds[1] (high) when the named ori is active, bounds[0] (low) when
    // not.  Unlike ori_only/ori_not which suppress sending, a ternori message
    // always sends — only the value changes.
    String        ternori;     // e.g. "spotlight" — high when active, low when not

    // --- Duplicate suppression cache (ephemeral, never saved to NVS) ---------
    float         _last_sent_val = 0.0f;
    bool          _has_last_sent = false;

    // --- Constructors -------------------------------------------------------

    OscMessage()
        : ip(0, 0, 0, 0), port(0), scene(nullptr), value_ptr(nullptr) {}

    explicit OscMessage(const String& set_name)
        : name(set_name), ip(0, 0, 0, 0), port(0), scene(nullptr), value_ptr(nullptr)
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
        scene      = o.scene;
        value_ptr  = o.value_ptr;
        bounds[0]  = o.bounds[0];
        bounds[1]  = o.bounds[1];
        enabled    = o.enabled;
        ori_only   = o.ori_only;
        ori_not    = o.ori_not;
        ternori    = o.ternori;
    }

    OscMessage& operator=(const OscMessage& o) {
        if (this != &o) {
            exist      = o.exist;
            name       = o.name;
            ip         = o.ip;
            port       = o.port;
            osc_address = o.osc_address;
            scene      = o.scene;
            value_ptr  = o.value_ptr;
            bounds[0]  = o.bounds[0];
            bounds[1]  = o.bounds[1];
            enabled    = o.enabled;
            ori_only   = o.ori_only;
            ori_not    = o.ori_not;
            ternori    = o.ternori;
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

        r.exist.scene = exist.scene || o.exist.scene;
        r.scene       = exist.scene ? scene : o.scene;

        r.exist.val   = exist.val   || o.exist.val;
        r.value_ptr   = exist.val   ? value_ptr : o.value_ptr;

        r.exist.low   = exist.low   || o.exist.low;
        r.bounds[0]   = exist.low   ? bounds[0] : o.bounds[0];

        r.exist.high  = exist.high  || o.exist.high;
        r.bounds[1]   = exist.high  ? bounds[1] : o.bounds[1];

        r.enabled     = enabled && o.enabled;

        // Ori-conditional fields: new config takes priority; fall back to other.
        r.ori_only = ori_only.length() > 0 ? ori_only : o.ori_only;
        r.ori_not  = ori_not.length() > 0  ? ori_not  : o.ori_not;
        r.ternori  = ternori.length() > 0  ? ternori  : o.ternori;

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

    /// Clear the dedup cache so the next send always transmits.
    void clear_dedup_cache() { _has_last_sent = false; _last_sent_val = 0.0f; }
};

#endif // OSC_MESSAGE_H
