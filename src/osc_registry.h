// =============================================================================
// osc_registry.h — Central owner of all OscPatch and OscMessage objects
// =============================================================================
//
// The OscRegistry is a Meyer's singleton that owns every OscPatch and
// OscMessage in fixed-size arrays.  All pointers returned by the registry
// point into these arrays and are stable for the device's lifetime.
//
// MEMORY MODEL (ESP32):
//   No heap allocation — capacity is fixed at compile time.  This avoids
//   fragmentation and gives deterministic memory usage.  Increase
//   MAX_OSC_PATCHES / MAX_OSC_MESSAGES and recompile if you need more slots.
//
// THREAD SAFETY:
//   A FreeRTOS mutex (reg_mutex) protects all registry modifications.
//   Send tasks that only *read* messages do so under brief mutex holds.
//   The main loop (which processes incoming OSC commands) also takes the
//   mutex before modifying the registry.
//
// USAGE:
//   OscRegistry& reg = osc_registry();
//   OscMessage*  m   = reg.get_or_create_msg("accelX_out");
//   OscPatch*    p   = reg.get_or_create_patch("mixer1");
//   p->add_msg(reg.msg_index(m));
// =============================================================================

#ifndef OSC_REGISTRY_H
#define OSC_REGISTRY_H

#include "osc_patch.h"

class OscRegistry {
public:
    // --- Storage arrays (fixed, never reallocated) --------------------------
    OscPatch   patches[MAX_OSC_PATCHES];
    uint16_t   patch_count = 0;

    OscMessage messages[MAX_OSC_MESSAGES];
    uint16_t   msg_count = 0;

    // Mutex for thread-safe access.  Initialised lazily on first use.
    SemaphoreHandle_t reg_mutex = nullptr;

    void ensure_mutex() {
        if (!reg_mutex) reg_mutex = xSemaphoreCreateMutex();
    }

    void lock()   { ensure_mutex(); xSemaphoreTake(reg_mutex, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(reg_mutex); }

    // --- Lookup by name (case-insensitive) ----------------------------------

    OscPatch* find_patch(const String& n) {
        String key = osc_lower_copy(osc_trim_copy(n));
        for (uint16_t i = 0; i < patch_count; i++) {
            if (osc_lower_copy(osc_trim_copy(patches[i].name)) == key)
                return &patches[i];
        }
        return nullptr;
    }

    OscMessage* find_msg(const String& n) {
        String key = osc_lower_copy(osc_trim_copy(n));
        for (uint16_t i = 0; i < msg_count; i++) {
            if (osc_lower_copy(osc_trim_copy(messages[i].name)) == key)
                return &messages[i];
        }
        return nullptr;
    }

    // --- Index helpers (pointers ↔ indices) ----------------------------------

    int patch_index(const OscPatch* p) const {
        if (!p) return -1;
        ptrdiff_t off = p - patches;
        return (off >= 0 && off < patch_count) ? (int)off : -1;
    }

    int msg_index(const OscMessage* m) const {
        if (!m) return -1;
        ptrdiff_t off = m - messages;
        return (off >= 0 && off < msg_count) ? (int)off : -1;
    }

    // --- Create or find -----------------------------------------------------

    OscPatch* get_or_create_patch(const String& n) {
        OscPatch* found = find_patch(n);
        if (found) return found;
        if (patch_count >= MAX_OSC_PATCHES) return nullptr;
        patches[patch_count] = OscPatch(n);
        return &patches[patch_count++];
    }

    OscMessage* get_or_create_msg(const String& n) {
        OscMessage* found = find_msg(n);
        if (found) return found;
        if (msg_count >= MAX_OSC_MESSAGES) return nullptr;
        messages[msg_count] = OscMessage(n);
        return &messages[msg_count++];
    }

    // --- Merge (update existing or create, copying only "exist" fields) ------

    OscPatch* update_patch(const OscPatch& src) {
        OscPatch* p = get_or_create_patch(src.name);
        if (!p) return nullptr;
        if (src.exist.ip)   { p->ip = src.ip;                   p->exist.ip   = true; }
        if (src.exist.port) { p->port = src.port;               p->exist.port = true; }
        if (src.exist.adr)  { p->osc_address = src.osc_address; p->exist.adr  = true; }
        if (src.exist.low)  { p->bounds[0] = src.bounds[0];     p->exist.low  = true; }
        if (src.exist.high) { p->bounds[1] = src.bounds[1];     p->exist.high = true; }
        return p;
    }

    OscMessage* update_msg(const OscMessage& src) {
        OscMessage* m = get_or_create_msg(src.name);
        if (!m) return nullptr;
        if (src.exist.ip)    { m->ip = src.ip;                   m->exist.ip    = true; }
        if (src.exist.port)  { m->port = src.port;               m->exist.port  = true; }
        if (src.exist.adr)   { m->osc_address = src.osc_address; m->exist.adr   = true; }
        if (src.exist.patch) { m->patch = src.patch;             m->exist.patch = true; }
        if (src.exist.val)   { m->value_ptr = src.value_ptr;     m->exist.val   = true; }
        if (src.exist.low)   { m->bounds[0] = src.bounds[0];     m->exist.low   = true; }
        if (src.exist.high)  { m->bounds[1] = src.bounds[1];     m->exist.high  = true; }
        if (src.ori_only.length() > 0) { m->ori_only = src.ori_only; }
        if (src.ori_not.length() > 0)  { m->ori_not  = src.ori_not;  }
        if (src.ternori.length() > 0)  { m->ternori  = src.ternori;  }
        return m;
    }

    // --- Delete (swap-and-shrink) -------------------------------------------

    /// Delete a patch by name.  Any messages referencing this patch have their
    /// patch pointer cleared.  Returns true if found and deleted.
    bool delete_patch(const String& n) {
        OscPatch* p = find_patch(n);
        if (!p) return false;

        // Stop the task if running.
        if (p->task_handle) {
            vTaskDelete(p->task_handle);
            p->task_handle = nullptr;
        }

        int idx = patch_index(p);

        // Clear patch pointers in all messages that reference this patch.
        for (uint16_t i = 0; i < msg_count; i++) {
            if (messages[i].patch == p) {
                messages[i].patch = nullptr;
                messages[i].exist.patch = false;
            }
        }

        // Swap with last and shrink.
        if (idx < (int)(patch_count - 1)) {
            patches[idx] = patches[patch_count - 1];

            // Fix message indices and patch pointers referencing the moved patch.
            OscPatch* moved = &patches[idx];
            int old_idx = patch_count - 1;
            for (uint16_t i = 0; i < msg_count; i++) {
                if (messages[i].patch == &patches[old_idx]) {
                    messages[i].patch = moved;
                }
            }
            // Fix msg_indices inside other patches that pointed at old_idx.
            for (uint16_t pi = 0; pi < patch_count - 1; pi++) {
                for (uint8_t mi = 0; mi < patches[pi].msg_count; mi++) {
                    // No msg_indices reference patch indices, they reference message indices.
                    // So nothing to fix here for patches.
                }
            }
        }
        patch_count--;
        return true;
    }

    /// Delete a message by name.  Removes it from any patch that references it.
    /// Returns true if found and deleted.
    bool delete_msg(const String& n) {
        OscMessage* m = find_msg(n);
        if (!m) return false;

        int idx = msg_index(m);

        // Remove from any patches that list this message index.
        for (uint16_t pi = 0; pi < patch_count; pi++) {
            patches[pi].remove_msg(idx);
        }

        // Swap with last and shrink.
        int last = msg_count - 1;
        if (idx < last) {
            messages[idx] = messages[last];

            // Update all patch msg_indices that pointed at `last` to point at `idx`.
            for (uint16_t pi = 0; pi < patch_count; pi++) {
                for (uint8_t mi = 0; mi < patches[pi].msg_count; mi++) {
                    if (patches[pi].msg_indices[mi] == last) {
                        patches[pi].msg_indices[mi] = idx;
                    }
                }
            }

            // Update the patch pointer if the moved message references a patch.
            // (Patch pointers are stable since patches don't move here.)
        }
        msg_count--;
        return true;
    }

    // --- Listing helpers ----------------------------------------------------

    uint16_t count_patches() const { return patch_count; }
    uint16_t count_msgs()    const { return msg_count; }
};

// ---------------------------------------------------------------------------
// Global registry accessor (Meyer's singleton, created on first call).
// ---------------------------------------------------------------------------

static inline OscRegistry& osc_registry() {
    static OscRegistry instance;
    return instance;
}

// =============================================================================
// OscMessage method implementations (need OscPatch and OscRegistry complete)
// =============================================================================

/// True if there is enough information to actually transmit an OSC packet:
/// a value to send, and a resolvable destination (ip, port, address).
inline bool OscMessage::sendable() const {
    bool has_val = (value_ptr != nullptr) || (ternori.length() > 0);
    bool has_ip  = exist.ip  || (patch && patch->exist.ip);
    bool has_port = exist.port || (patch && patch->exist.port);
    bool has_adr = exist.adr || (patch && patch->exist.adr);
    return has_val && has_ip && has_port && has_adr;
}

/// Parse a CSV config string and populate this message's fields.
///
/// Format: "key:value, key:value, ..."
///   Direct keys:  name, ip, port, adr/addr/address, patch, value, low/min/lo,
///                 high/max/hi, enabled
///                 period — accepted but ignored (patch-level field, handled by caller)
///   Reference keys (use '-' separator):  ip-refName, port-refName, etc.
///   Special:  default-refName / all-refName  copies all set fields from a
///             registered patch or message as fallback values.
///
/// Returns false with *error filled in on parse failure.
inline bool OscMessage::from_config_str(const String& config, String* error) {
    // Reset parseable fields.
    exist      = ExistFlags{};
    ip         = IPAddress(0, 0, 0, 0);
    port       = 0;
    osc_address = "";
    patch      = nullptr;
    value_ptr  = nullptr;
    bounds[0]  = 0.0f;
    bounds[1]  = 1.0f;
    ori_only   = "";
    ori_not    = "";
    ternori    = "";

    String input = config;
    input.trim();
    if (input.length() == 0) {
        if (error) *error = "Empty config string";
        return false;
    }

    size_t start = 0;
    while (start < input.length()) {
        // Find the next comma-separated token.
        int comma = input.indexOf(',', start);
        String token = (comma < 0) ? input.substring(start) : input.substring(start, comma);
        token = osc_trim_copy(token);
        start = (comma < 0) ? input.length() : (size_t)(comma + 1);

        if (token.length() == 0) continue;

        // Determine separator: ':' = direct value, '-' = registry reference.
        int colon = token.indexOf(':');
        int dash  = token.indexOf('-');

        bool is_ref = false;
        int  sep    = -1;
        if (colon >= 0 && (dash < 0 || colon <= dash)) {
            sep    = colon;
            is_ref = false;
        } else if (dash >= 0) {
            sep    = dash;
            is_ref = true;
        } else {
            if (error) *error = "Missing ':' or '-' in token: " + token;
            return false;
        }

        String key   = osc_lower_copy(osc_trim_copy(token.substring(0, sep)));
        String value = osc_trim_copy(token.substring(sep + 1));

        // ----- Reference mode: value is a registered name to look up --------
        if (is_ref) {
            OscRegistry& reg = osc_registry();
            OscPatch*   ref_p = reg.find_patch(value);
            OscMessage* ref_m = reg.find_msg(value);

            if (key == "ip") {
                if      (ref_p && ref_p->exist.ip) { ip = ref_p->ip; exist.ip = true; }
                else if (ref_m && ref_m->exist.ip) { ip = ref_m->ip; exist.ip = true; }
                else { if (error) *error = "Ref '" + value + "' has no ip"; return false; }
            } else if (key == "port") {
                if      (ref_p && ref_p->exist.port) { port = ref_p->port; exist.port = true; }
                else if (ref_m && ref_m->exist.port) { port = ref_m->port; exist.port = true; }
                else { if (error) *error = "Ref '" + value + "' has no port"; return false; }
            } else if (key == "adr" || key == "addr" || key == "address") {
                if      (ref_p && ref_p->exist.adr) { osc_address = ref_p->osc_address; exist.adr = true; }
                else if (ref_m && ref_m->exist.adr) { osc_address = ref_m->osc_address; exist.adr = true; }
                else { if (error) *error = "Ref '" + value + "' has no adr"; return false; }
            } else if (key == "patch") {
                patch = ref_p ? ref_p : reg.get_or_create_patch(value);
                exist.patch = true;
            } else if (key == "value") {
                if (ref_m && ref_m->exist.val) { value_ptr = ref_m->value_ptr; exist.val = true; }
                else { if (error) *error = "Ref '" + value + "' has no value"; return false; }
            } else if (key == "low" || key == "min" || key == "lo") {
                if (ref_m && ref_m->exist.low) { bounds[0] = ref_m->bounds[0]; exist.low = true; }
                else { if (error) *error = "Ref '" + value + "' has no low"; return false; }
            } else if (key == "high" || key == "max" || key == "hi") {
                if (ref_m && ref_m->exist.high) { bounds[1] = ref_m->bounds[1]; exist.high = true; }
                else { if (error) *error = "Ref '" + value + "' has no high"; return false; }
            } else if (key == "default" || key == "all") {
                if (!ref_p && !ref_m) {
                    if (error) *error = "default/all: no object named '" + value + "'";
                    return false;
                }
                if (ref_p) {
                    if (ref_p->exist.ip   && !exist.ip)   { ip = ref_p->ip;                   exist.ip   = true; }
                    if (ref_p->exist.port && !exist.port)  { port = ref_p->port;               exist.port = true; }
                    if (ref_p->exist.adr  && !exist.adr)   { osc_address = ref_p->osc_address; exist.adr  = true; }
                }
                if (ref_m) {
                    if (ref_m->exist.ip    && !exist.ip)    { ip = ref_m->ip;                   exist.ip    = true; }
                    if (ref_m->exist.port  && !exist.port)  { port = ref_m->port;               exist.port  = true; }
                    if (ref_m->exist.adr   && !exist.adr)   { osc_address = ref_m->osc_address; exist.adr   = true; }
                    if (ref_m->exist.patch && !exist.patch) { patch = ref_m->patch;             exist.patch = true; }
                    if (ref_m->exist.val   && !exist.val)   { value_ptr = ref_m->value_ptr;     exist.val   = true; }
                    if (ref_m->exist.low   && !exist.low)   { bounds[0] = ref_m->bounds[0];     exist.low   = true; }
                    if (ref_m->exist.high  && !exist.high)  { bounds[1] = ref_m->bounds[1];     exist.high  = true; }
                }
            } else {
                if (error) *error = "Unknown key: " + key;
                return false;
            }
            continue;
        }

        // ----- Direct mode: value is a literal ------------------------------
        if (key == "name") {
            name = value;
            exist.name = true;
        } else if (key == "ip") {
            IPAddress parsed;
            if (!parsed.fromString(value)) {
                if (error) *error = "Invalid IP: " + value;
                return false;
            }
            ip = parsed;
            exist.ip = true;
        } else if (key == "port") {
            long p = value.toInt();
            if (p < 1 || p > 65535) {
                if (error) *error = "Invalid port: " + value;
                return false;
            }
            port = (unsigned int)p;
            exist.port = true;
        } else if (key == "adr" || key == "addr" || key == "address") {
            osc_address = value;
            exist.adr = true;
        } else if (key == "patch") {
            OscRegistry& reg = osc_registry();
            patch = reg.find_patch(value);
            if (!patch) {
                patch = reg.get_or_create_patch(value);
            }
            exist.patch = true;
        } else if (key == "value" || key == "val") {
            int vi = data_stream_index_from_name(value);
            if (vi < 0 || vi >= NUM_DATA_STREAMS) {
                if (error) *error = "Unknown value: " + value;
                return false;
            }
            value_ptr = &data_streams[vi];
            exist.val = true;
        } else if (key == "low" || key == "min" || key == "lo") {
            bounds[0] = value.toFloat();
            exist.low = true;
        } else if (key == "high" || key == "max" || key == "hi") {
            bounds[1] = value.toFloat();
            exist.high = true;
        } else if (key == "enabled") {
            String v = osc_lower_copy(value);
            enabled = (v == "true" || v == "1" || v == "yes" || v == "on");
        } else if (key == "orionly" || key == "ori_only") {
            ori_only = value;
        } else if (key == "orinot" || key == "ori_not") {
            ori_not = value;
        } else if (key == "ternori") {
            ternori = value;
        } else if (key == "period") {
            // Recognised but handled outside from_config_str (patch-level field).
        } else {
            if (error) *error = "Unknown key: " + key;
            return false;
        }
    }

    return true;
}

/// Build a human-readable info string describing this message.
inline String OscMessage::to_info_string(bool verbose) const {
    String s = name;
    if (!verbose) return s;

    s += " [";
    s += enabled ? "ON" : "OFF";
    s += "]";

    if (exist.ip)   { s += " ip:" + ip.toString(); }
    if (exist.port) { s += " port:" + String(port); }
    if (exist.adr)  { s += " adr:" + osc_address; }
    if (exist.val) {
        int idx = data_stream_index_from_ptr(value_ptr);
        s += " val:" + (idx >= 0 ? data_stream_name(idx) : String("custom"));
    }
    if (exist.low)  { s += " low:" + String(bounds[0], 2); }
    if (exist.high) { s += " high:" + String(bounds[1], 2); }
    if (exist.patch && patch) { s += " patch:" + patch->name; }
    if (ori_only.length() > 0) { s += " ori_only:" + ori_only; }
    if (ori_not.length() > 0)  { s += " ori_not:" + ori_not; }
    if (ternori.length() > 0)  { s += " ternori:" + ternori; }

    return s;
}

/// Build a human-readable info string for this patch.
inline String OscPatch::to_info_string(bool verbose) const {
    String s = name;
    if (!verbose) return s;

    s += " [";
    s += enabled ? "RUNNING" : "STOPPED";
    s += ", " + String(send_period_ms) + "ms";
    s += ", " + String(msg_count) + " msgs";
    s += "]";

    if (exist.ip)   { s += " ip:" + ip.toString(); }
    if (exist.port) { s += " port:" + String(port); }
    if (exist.adr)  { s += " adr:" + osc_address; }
    if (exist.low)  { s += " low:" + String(bounds[0], 2); }
    if (exist.high) { s += " high:" + String(bounds[1], 2); }

    s += " adr_mode:" + String(address_mode_label(address_mode));

    if (overrides.ip || overrides.port || overrides.adr
        || overrides.low || overrides.high) {
        s += " override:";
        if (overrides.ip)   s += "ip,";
        if (overrides.port) s += "port,";
        if (overrides.adr)  s += "adr,";
        if (overrides.low)  s += "low,";
        if (overrides.high) s += "high,";
        s.remove(s.length() - 1); // trailing comma
    }

    return s;
}

#endif // OSC_REGISTRY_H
