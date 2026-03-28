// =============================================================================
// osc_scene.h — OscScene class: a named group of OscMessages with a send task
// =============================================================================
//
// An OscScene groups one or more OscMessages and drives their transmission
// through a dedicated FreeRTOS task.  The task wakes at the scene's configured
// polling period and sends every enabled message in the group.
//
// OVERRIDE SYSTEM:
//   A scene carries its own ip, port, osc_address, and output bounds (low /
//   high).  For each of these fields the scene can be told to *override* the
//   corresponding field on its child messages.  Override behaviour is
//   controlled per-field via the OverrideFlags struct:
//
//     scene.overrides.ip = true   →  all messages in this scene use the
//                                     scene's IP regardless of their own.
//
//   When an override flag is OFF, each message uses its own value (with the
//   scene providing a fallback if the message's value is not set at all).
//
// ADDRESS COMPOSITION:
//   The address_mode field controls how the scene and message OSC addresses
//   are combined when sending:
//
//     ADR_FALLBACK  (default) — use the message's address; if the message has
//                               no address, fall back to the scene's address.
//     ADR_OVERRIDE            — the scene's address replaces the message's.
//     ADR_PREPEND             — scene address + message address
//                               e.g. "/mixer" + "/fader1" → "/mixer/fader1"
//     ADR_APPEND              — message address + scene address
//                               e.g. "/fader1" + "/mixer" → "/fader1/mixer"
//
// SCALE OVERRIDE:
//   Since all sensor values are normalised to [0, 1], the scene can store its
//   own output bounds (low / high).  When overrides.low or overrides.high is
//   true, the scene's bound replaces the message's bound at send time.  This
//   lets you rescale an entire group of messages uniformly — for example,
//   mapping all values in a scene to [0, 255] for a DMX fixture.
//
// TASK LIFECYCLE:
//   start()  → creates the FreeRTOS task (if not already running).
//   stop()   → deletes the task.
//   enabled  → the task still runs but skips sending when disabled.
// =============================================================================

#ifndef OSC_PATCH_H
#define OSC_PATCH_H

#include "osc_message.h"

// Maximum number of message indices a single scene can hold.
#define MAX_MSGS_PER_PATCH 64
#define MIN_PATCH_PERIOD_MS 20
#define MAX_PATCH_PERIOD_MS 60000

static inline unsigned int clamp_scene_period_ms(long ms) {
    if (ms < MIN_PATCH_PERIOD_MS) return MIN_PATCH_PERIOD_MS;
    if (ms > MAX_PATCH_PERIOD_MS) return MAX_PATCH_PERIOD_MS;
    return (unsigned int)ms;
}

// ---------------------------------------------------------------------------
// AddressMode — how scene and message OSC addresses are combined
// ---------------------------------------------------------------------------

enum AddressMode : uint8_t {
    ADR_FALLBACK = 0,   // message's own address, scene as fallback (default)
    ADR_OVERRIDE = 1,   // scene address replaces message address entirely
    ADR_PREPEND  = 2,   // scene.adr + msg.adr  (scene comes first)
    ADR_APPEND   = 3    // msg.adr + scene.adr  (message comes first)
};

/// Parse an address mode from a string (case-insensitive).
static inline AddressMode address_mode_from_string(const String& s) {
    String l = s;
    l.trim();
    l.toLowerCase();
    if (l == "override" || l == "replace" || l == "1") return ADR_OVERRIDE;
    if (l == "prepend"  || l == "pre"     || l == "2") return ADR_PREPEND;
    if (l == "append"   || l == "post"    || l == "3") return ADR_APPEND;
    return ADR_FALLBACK;  // "fallback", "default", "0", or anything else
}

/// Return a short label for an AddressMode.
static inline const char* address_mode_label(AddressMode m) {
    switch (m) {
        case ADR_OVERRIDE: return "override";
        case ADR_PREPEND:  return "prepend";
        case ADR_APPEND:   return "append";
        default:           return "fallback";
    }
}

// ---------------------------------------------------------------------------
// OverrideFlags — which message fields the scene forcibly replaces
// ---------------------------------------------------------------------------

struct OverrideFlags {
    bool ip   = false;
    bool port = false;
    bool adr  = false;   // osc_address (simple override; see also address_mode)
    bool low  = false;   // output bounds[0]
    bool high = false;   // output bounds[1]
};

// ---------------------------------------------------------------------------
// OscScene
// ---------------------------------------------------------------------------

class OscScene {
public:
    ExistFlags     exist;
    String         name;

    // Network destination (used as fallback or override for child messages).
    IPAddress      ip;
    unsigned int   port;
    String         osc_address;

    // Output bounds — when override.low / .high is true, these replace
    // the message's bounds.  Sensor values are [0, 1]; the output is
    // mapped to [bounds[0], bounds[1]].
    float          bounds[2] = {0.0f, 1.0f};

    // Address composition mode (see AddressMode enum above).
    AddressMode    address_mode = ADR_FALLBACK;

    // Sending configuration.
    unsigned int   send_period_ms = 50;   // milliseconds between send bursts
    bool           enabled        = false; // starts disabled; call start()

    // FreeRTOS task handle (nullptr when the task is not running).
    TaskHandle_t   task_handle    = nullptr;

    // Override flags: when true, the scene's value replaces the message's.
    OverrideFlags  overrides;

    // Indices into OscRegistry::messages[] for every message in this scene.
    int            msg_indices[MAX_MSGS_PER_PATCH];
    uint8_t        msg_count = 0;

    // --- Constructors -------------------------------------------------------

    OscScene()
        : ip(0, 0, 0, 0), port(0) {}

    explicit OscScene(const String& set_name)
        : name(set_name), ip(0, 0, 0, 0), port(0)
    {
        exist.name = true;
    }

    // Construct a scene from an OscMessage (copies shared fields).
    explicit OscScene(const OscMessage& msg)
        : ip(msg.ip), port(msg.port), osc_address(msg.osc_address)
    {
        name       = msg.name;
        exist.name = msg.exist.name;
        exist.ip   = msg.exist.ip;
        exist.port = msg.exist.port;
        exist.adr  = msg.exist.adr;
        bounds[0]  = msg.bounds[0];
        bounds[1]  = msg.bounds[1];
        exist.low  = msg.exist.low;
        exist.high = msg.exist.high;
    }

    // --- Message list management --------------------------------------------

    /// Add a message index to this scene.  No-op if already present or full.
    void add_msg(int idx) {
        if (msg_count >= MAX_MSGS_PER_PATCH) return;
        for (uint8_t i = 0; i < msg_count; i++) {
            if (msg_indices[i] == idx) return;  // already present
        }
        msg_indices[msg_count++] = idx;
    }

    /// Remove a message index from this scene.  No-op if not present.
    void remove_msg(int idx) {
        for (uint8_t i = 0; i < msg_count; i++) {
            if (msg_indices[i] == idx) {
                // Shift remaining indices down.
                for (uint8_t j = i; j < msg_count - 1; j++) {
                    msg_indices[j] = msg_indices[j + 1];
                }
                msg_count--;
                return;
            }
        }
    }

    /// Check whether a message index is in this scene.
    bool has_msg(int idx) const {
        for (uint8_t i = 0; i < msg_count; i++) {
            if (msg_indices[i] == idx) return true;
        }
        return false;
    }

    /// Build a human-readable summary of this scene (for list/info replies).
    String to_info_string(bool verbose = false) const;
};

#endif // OSC_PATCH_H
