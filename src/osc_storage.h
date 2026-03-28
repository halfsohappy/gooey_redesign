// =============================================================================
// osc_storage.h — Non-volatile storage (NVS) for OscMessages, OscPatches, Oris
// =============================================================================
//
// STORAGE FORMAT:
//   Live state (no active show):
//     Namespace "osc_store" — patches (p_count, p_0..p_N) + messages (m_count, m_0..m_N)
//     Namespace "ori_store" — oris (o_count, o_0..o_N, o_thr, o_tol, o_str)
//
//   Shows (named snapshots of the full device state):
//     Namespace "shows_idx" — s_count (uint8), s_0..s_3 (names), active (String)
//     Namespace "sw_0".."sw_3" — p_count, p_0..., m_count, m_0..., o_count, o_0...
//       (shows store all data — osc + ori — in a single combined namespace)
//
// MAX SHOWS: 4 (on-device NVS; unlimited in Gooey library on disk)
//
// SHOW ACTIVATION:
//   When a show is loaded, it becomes the "active" slot.  Subsequent /save
//   commands write to that show's namespace.  /load reads from it.
//   If no active show is set, /save and /load use "osc_store"/"ori_store".
//
// ORI SERIALIZATION FORMAT (new — v2):
//   "name:xxx,sc:N,r:R,g:G,b:B,tol:T,axis:A,ax:X,ay:Y,az:Z,
//    q0i:F,q0j:F,q0k:F,q0r:F, q1i:F,q1j:F,q1k:F,q1r:F, ..."
//   sc   = sample_count (0 = pre-registered unsampled)
//   axis = 1 if use_axis, 0 if full-quaternion
//   ax/ay/az = local matching axis unit vector (omitted if axis=0)
//   q0..q7  = individual quaternion samples in the cloud
//
//   Backward compat (v1 format): if "q0i:" absent but "qi:" present, treated
//   as a single-sample ori with no axis.  hw: key is ignored.
//
// COMMANDS:
//   /save, /load, /nvs/clear — operate on active show or osc_store/ori_store
//   /show/save/{name}  — snapshot current state as a named show
//   /show/load/{name}  — load a named show (two-step with confirm)
//   /show/list         — list show names
//   /show/delete/{name}
//   /show/rename       — payload: "old, new"
// =============================================================================

#ifndef OSC_STORAGE_H
#define OSC_STORAGE_H

#include <Preferences.h>
#include "osc_registry.h"
#include "ori_tracker.h"

#define MAX_SHOWS 4

// ---------------------------------------------------------------------------
// Message serialisation (unchanged)
// ---------------------------------------------------------------------------

static inline String msg_to_save_string(const OscMessage& m) {
    String s;

    if (m.exist.name) { s += "name:" + m.name; }
    if (m.exist.ip)   { if (s.length()) s += ", "; s += "ip:" + m.ip.toString(); }
    if (m.exist.port) { if (s.length()) s += ", "; s += "port:" + String(m.port); }
    if (m.exist.adr)  { if (s.length()) s += ", "; s += "adr:" + m.osc_address; }
    if (m.exist.val) {
        int idx = data_stream_index_from_ptr(m.value_ptr);
        if (idx >= 0) { if (s.length()) s += ", "; s += "value:" + data_stream_name(idx); }
    }
    if (m.exist.low)  { if (s.length()) s += ", "; s += "low:" + String(m.bounds[0], 4); }
    if (m.exist.high) { if (s.length()) s += ", "; s += "high:" + String(m.bounds[1], 4); }
    if (s.length()) s += ", ";
    s += String("enabled:") + (m.enabled ? "true" : "false");
    if (m.exist.patch && m.patch) { if (s.length()) s += ", "; s += "patch:" + m.patch->name; }
    if (m.ori_only.length() > 0)  { if (s.length()) s += ", "; s += "ori_only:" + m.ori_only; }
    if (m.ori_not.length()  > 0)  { if (s.length()) s += ", "; s += "ori_not:"  + m.ori_not;  }
    if (m.ternori.length()  > 0)  { if (s.length()) s += ", "; s += "ternori:"  + m.ternori;  }
    return s;
}

// ---------------------------------------------------------------------------
// Patch serialisation (unchanged)
// ---------------------------------------------------------------------------

static inline String patch_to_save_string(const OscPatch& p) {
    OscRegistry& reg = osc_registry();
    String s;

    if (p.exist.name) s += "name:" + p.name;
    if (p.exist.ip)   { if (s.length()) s += ", "; s += "ip:" + p.ip.toString(); }
    if (p.exist.port) { if (s.length()) s += ", "; s += "port:" + String(p.port); }
    if (p.exist.adr)  { if (s.length()) s += ", "; s += "adr:" + p.osc_address; }
    if (p.exist.low)  { if (s.length()) s += ", "; s += "low:" + String(p.bounds[0], 4); }
    if (p.exist.high) { if (s.length()) s += ", "; s += "high:" + String(p.bounds[1], 4); }
    if (s.length()) s += ", "; s += "period:" + String(p.send_period_ms);
    if (s.length()) s += ", "; s += "adrmode:" + String(address_mode_label(p.address_mode));
    {
        String ov;
        if (p.overrides.ip)   { if (ov.length()) ov += "+"; ov += "ip"; }
        if (p.overrides.port) { if (ov.length()) ov += "+"; ov += "port"; }
        if (p.overrides.adr)  { if (ov.length()) ov += "+"; ov += "adr"; }
        if (p.overrides.low)  { if (ov.length()) ov += "+"; ov += "low"; }
        if (p.overrides.high) { if (ov.length()) ov += "+"; ov += "high"; }
        if (s.length()) s += ", ";
        s += "override:" + (ov.length() > 0 ? ov : String("none"));
    }
    {
        String ml;
        for (uint8_t i = 0; i < p.msg_count; i++) {
            int mi = p.msg_indices[i];
            if (mi >= 0 && mi < (int)reg.msg_count) {
                if (ml.length()) ml += "+";
                ml += reg.messages[mi].name;
            }
        }
        if (ml.length() > 0) { if (s.length()) s += ", "; s += "msgs:" + ml; }
    }
    return s;
}

static inline void patch_from_save_string(OscPatch* p, const String& saved) {
    if (!p) return;
    OscRegistry& reg = osc_registry();
    OscMessage tmp;
    String err;
    tmp.from_config_str(saved, &err);
    if (tmp.exist.ip)   { p->ip = tmp.ip;               p->exist.ip   = true; }
    if (tmp.exist.port) { p->port = tmp.port;           p->exist.port = true; }
    if (tmp.exist.adr)  { p->osc_address = tmp.osc_address; p->exist.adr = true; }
    if (tmp.exist.low)  { p->bounds[0] = tmp.bounds[0]; p->exist.low  = true; }
    if (tmp.exist.high) { p->bounds[1] = tmp.bounds[1]; p->exist.high = true; }

    String input = saved;
    input.trim();
    size_t start = 0;
    while (start < input.length()) {
        int comma = input.indexOf(',', start);
        String token = (comma < 0) ? input.substring(start) : input.substring(start, comma);
        token.trim();
        start = (comma < 0) ? input.length() : (size_t)(comma + 1);
        if (token.length() == 0) continue;
        int colon = token.indexOf(':');
        if (colon < 0) continue;
        String key = token.substring(0, colon); key.trim(); key.toLowerCase();
        String value = token.substring(colon + 1); value.trim();

        if (key == "period") {
            int ms = value.toInt();
            if (ms > 0) p->send_period_ms = clamp_patch_period_ms(ms);
        } else if (key == "adrmode") {
            p->address_mode = address_mode_from_string(value);
        } else if (key == "override") {
            p->overrides.ip = p->overrides.port = p->overrides.adr = false;
            p->overrides.low = p->overrides.high = false;
            if (value != "none") {
                int s2 = 0;
                while (s2 < (int)value.length()) {
                    int plus = value.indexOf('+', s2);
                    String f = (plus < 0) ? value.substring(s2) : value.substring(s2, plus);
                    f.trim(); f.toLowerCase();
                    s2 = (plus < 0) ? value.length() : plus + 1;
                    if (f == "ip")   p->overrides.ip   = true;
                    if (f == "port") p->overrides.port = true;
                    if (f == "adr")  p->overrides.adr  = true;
                    if (f == "low")  p->overrides.low  = true;
                    if (f == "high") p->overrides.high = true;
                }
            }
        } else if (key == "msgs") {
            int s2 = 0;
            while (s2 < (int)value.length()) {
                int plus = value.indexOf('+', s2);
                String mname = (plus < 0) ? value.substring(s2) : value.substring(s2, plus);
                mname.trim();
                s2 = (plus < 0) ? value.length() : plus + 1;
                if (mname.length() == 0) continue;
                OscMessage* m = reg.find_msg(mname);
                if (m) { int mi = reg.msg_index(m); p->add_msg(mi); m->patch = p; m->exist.patch = true; }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Ori serialisation — v2 format (cloud + axis)
// ---------------------------------------------------------------------------

/// Serialise one SavedOri to a storable string (v2 format).
static inline String ori_to_save_string(const SavedOri& o) {
    String s;
    s  = "name:" + o.name;
    s += ",sc:"  + String(o.sample_count);
    s += ",r:"   + String(o.color_r);
    s += ",g:"   + String(o.color_g);
    s += ",b:"   + String(o.color_b);
    s += ",tol:" + String(o.tolerance, 2);
    s += ",axis:" + String(o.use_axis ? 1 : 0);
    if (o.use_axis) {
        s += ",ax:" + String(o.axis_x, 4);
        s += ",ay:" + String(o.axis_y, 4);
        s += ",az:" + String(o.axis_z, 4);
    }
    for (uint8_t i = 0; i < o.sample_count; i++) {
        s += ",q" + String(i) + "i:" + String(o.qi[i], 6);
        s += ",q" + String(i) + "j:" + String(o.qj[i], 6);
        s += ",q" + String(i) + "k:" + String(o.qk[i], 6);
        s += ",q" + String(i) + "r:" + String(o.qr[i], 6);
    }
    return s;
}

/// Parse an ori from a save string and store in the tracker.
/// Handles both v2 format (q0i:...) and v1 format (qi:...) for backward compat.
static inline void ori_from_save_string(OriTracker& ot, const String& s) {
    if (s.length() == 0) return;

    auto extract = [&](const String& key) -> String {
        String k = key + ":";
        int start = s.indexOf(k);
        if (start < 0) return "";
        start += k.length();
        int end = s.indexOf(',', start);
        return (end < 0) ? s.substring(start) : s.substring(start, end);
    };

    String name = extract("name");
    if (name.length() == 0) return;

    uint8_t sc = (uint8_t)extract("sc").toInt();
    uint8_t r  = (uint8_t)extract("r").toInt();
    uint8_t g  = (uint8_t)extract("g").toInt();
    uint8_t b  = (uint8_t)extract("b").toInt();

    if (sc == 0) {
        ot.register_ori(name, r, g, b);
        return;
    }

    // Detect format version.
    bool v2 = (s.indexOf(",q0i:") >= 0 || s.startsWith("q0i:"));

    if (!v2) {
        // --- v1 backward compat: single center quaternion, no axis ---
        float qi = extract("qi").toFloat();
        float qj = extract("qj").toFloat();
        float qk = extract("qk").toFloat();
        float qr = extract("qr").toFloat();
        int idx = ot.save(name, qi, qj, qk, qr);
        if (idx >= 0) {
            ot.oris[idx].color_r  = r;
            ot.oris[idx].color_g  = g;
            ot.oris[idx].color_b  = b;
            ot.oris[idx].use_axis = false;
            // sample_count already set to 1 by save()
        }
        return;
    }

    // --- v2 format: cloud of samples + axis ---
    float tol = extract("tol").toFloat();
    if (tol <= 0.0f) tol = 10.0f;
    bool use_axis = (extract("axis").toInt() == 1);
    float ax = use_axis ? extract("ax").toFloat() : 1.0f;
    float ay = use_axis ? extract("ay").toFloat() : 0.0f;
    float az = use_axis ? extract("az").toFloat() : 0.0f;

    // Register with color first so slot exists.
    ot.register_ori(name, r, g, b);
    int idx = ot.find(name);
    if (idx < 0) return;

    SavedOri& o = ot.oris[idx];
    o.tolerance = tol;
    o.use_axis  = use_axis;
    o.axis_x    = ax;
    o.axis_y    = ay;
    o.axis_z    = az;

    // Load each sample.
    uint8_t loaded = 0;
    for (uint8_t i = 0; i < sc && i < MAX_CLOUD_SAMPLES; i++) {
        String si = String(i);
        String qi_s = extract("q" + si + "i");
        String qj_s = extract("q" + si + "j");
        String qk_s = extract("q" + si + "k");
        String qr_s = extract("q" + si + "r");
        if (qi_s.length() == 0) break;
        o.qi[loaded] = qi_s.toFloat();
        o.qj[loaded] = qj_s.toFloat();
        o.qk[loaded] = qk_s.toFloat();
        o.qr[loaded] = qr_s.toFloat();
        loaded++;
    }
    o.sample_count = loaded;
    if (o.sample_count > 0 && idx >= (int)ot.ori_count)
        ot.ori_count = idx + 1;
}

// ---------------------------------------------------------------------------
// Show index helpers
// ---------------------------------------------------------------------------

/// Returns the active show name ("" if none).
static inline String nvs_get_active_show() {
    Preferences prefs;
    prefs.begin("shows_idx", true);
    String active = prefs.getString("active", "");
    prefs.end();
    return active;
}

/// Sets the active show name ("" to clear).
static inline void nvs_set_active_show(const String& name) {
    Preferences prefs;
    prefs.begin("shows_idx", false);
    prefs.putString("active", name);
    prefs.end();
}

/// Returns the namespace for the active show, or "" if none.
/// Format: "sw_0".."sw_3"
static inline String _show_namespace(const String& show_name) {
    if (show_name.length() == 0) return "";
    Preferences prefs;
    prefs.begin("shows_idx", true);
    uint8_t count = prefs.getUChar("s_count", 0);
    for (uint8_t i = 0; i < count && i < MAX_SHOWS; i++) {
        String key = "s_" + String(i);
        String sn = prefs.getString(key.c_str(), "");
        if (sn.equalsIgnoreCase(show_name)) {
            prefs.end();
            return "sw_" + String(i);
        }
    }
    prefs.end();
    return "";
}

/// Returns the NVS namespace to use for /save and /load.
/// If a show is active and has a namespace, returns that; else "osc_store".
static inline String nvs_active_osc_ns() {
    String active = nvs_get_active_show();
    if (active.length() == 0) return "osc_store";
    String ns = _show_namespace(active);
    return ns.length() > 0 ? ns : "osc_store";
}

// ---------------------------------------------------------------------------
// Internal: save/load osc+ori data to/from a given namespace
// ---------------------------------------------------------------------------

/// Save all registry data (patches + messages + oris) to a single namespace.
static inline int _nvs_save_combined(const String& ns) {
    OscRegistry& reg = osc_registry();
    OriTracker&  ot  = ori_tracker();
    Preferences  prefs;
    prefs.begin(ns.c_str(), false);
    prefs.clear();

    prefs.putUShort("p_count", reg.patch_count);
    for (uint16_t i = 0; i < reg.patch_count; i++) {
        prefs.putString(("p_" + String(i)).c_str(), patch_to_save_string(reg.patches[i]));
    }
    prefs.putUShort("m_count", reg.msg_count);
    for (uint16_t i = 0; i < reg.msg_count; i++) {
        prefs.putString(("m_" + String(i)).c_str(), msg_to_save_string(reg.messages[i]));
    }

    // Oris — same keys as ori_store, embedded in the same namespace.
    uint8_t saved_oris = 0;
    for (uint8_t i = 0; i < ot.ori_count; i++) {
        if (!ot.oris[i].used) continue;
        prefs.putString(("o_" + String(saved_oris)).c_str(), ori_to_save_string(ot.oris[i]));
        saved_oris++;
    }
    prefs.putUChar("o_count", saved_oris);
    prefs.putFloat("o_thr", ot.motion_threshold);
    prefs.putFloat("o_tol", ot.ori_tolerance);
    prefs.putBool ("o_str", ot.strict_matching);

    prefs.end();
    return (int)(reg.patch_count + reg.msg_count + saved_oris);
}

/// Load all registry data (patches + messages + oris) from a single namespace.
/// Returns total objects loaded. Stops running patches before clearing.
static inline int _nvs_load_combined(const String& ns) {
    OscRegistry& reg = osc_registry();
    OriTracker&  ot  = ori_tracker();
    Preferences  prefs;
    prefs.begin(ns.c_str(), true);

    uint16_t p_count = prefs.getUShort("p_count", 0);
    uint16_t m_count = prefs.getUShort("m_count", 0);

    if (p_count == 0 && m_count == 0) { prefs.end(); return 0; }

    // Stop running tasks.
    for (uint16_t i = 0; i < reg.patch_count; i++) {
        if (reg.patches[i].task_handle) {
            vTaskDelete(reg.patches[i].task_handle);
            reg.patches[i].task_handle = nullptr;
        }
    }
    reg.patch_count = 0;
    reg.msg_count   = 0;

    // Pass 1: patches.
    for (uint16_t i = 0; i < p_count && i < MAX_OSC_PATCHES; i++) {
        String saved = prefs.getString(("p_" + String(i)).c_str(), "");
        if (saved.length() == 0) continue;
        String pname;
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            pname = (end < 0) ? saved.substring(ni + 5) : saved.substring(ni + 5, end);
            pname.trim();
        }
        if (pname.length() == 0) pname = "patch_" + String(i);
        OscPatch* p = reg.get_or_create_patch(pname);
        if (p) patch_from_save_string(p, saved);
    }

    // Pass 2: messages.
    for (uint16_t i = 0; i < m_count && i < MAX_OSC_MESSAGES; i++) {
        String saved = prefs.getString(("m_" + String(i)).c_str(), "");
        if (saved.length() == 0) continue;
        String mname;
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            mname = (end < 0) ? saved.substring(ni + 5) : saved.substring(ni + 5, end);
            mname.trim();
        }
        if (mname.length() == 0) mname = "msg_" + String(i);
        OscMessage* m = reg.get_or_create_msg(mname);
        if (!m) continue;
        OscMessage parsed; String err;
        if (parsed.from_config_str(saved, &err)) {
            *m = parsed * (*m);
            m->name = mname; m->exist.name = true;
        }
        int en_idx = saved.indexOf("enabled:");
        if (en_idx >= 0) {
            int end = saved.indexOf(',', en_idx);
            String ev = (end < 0) ? saved.substring(en_idx + 8) : saved.substring(en_idx + 8, end);
            ev.trim(); ev.toLowerCase();
            m->enabled = (ev == "true" || ev == "1" || ev == "yes");
        }
        if (m->exist.patch && m->patch) {
            int mi = reg.msg_index(m);
            m->patch->add_msg(mi);
        }
    }

    // Pass 3: re-link patch message lists.
    for (uint16_t i = 0; i < p_count && i < MAX_OSC_PATCHES; i++) {
        String saved = prefs.getString(("p_" + String(i)).c_str(), "");
        if (saved.length() == 0) continue;
        int msgs_idx = saved.indexOf("msgs:");
        if (msgs_idx < 0) continue;
        String pname;
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            pname = (end < 0) ? saved.substring(ni + 5) : saved.substring(ni + 5, end);
            pname.trim();
        }
        OscPatch* p = reg.find_patch(pname);
        if (!p) continue;
        int end = saved.indexOf(',', msgs_idx);
        String mv = (end < 0) ? saved.substring(msgs_idx + 5) : saved.substring(msgs_idx + 5, end);
        mv.trim();
        int s2 = 0;
        while (s2 < (int)mv.length()) {
            int plus = mv.indexOf('+', s2);
            String mname = (plus < 0) ? mv.substring(s2) : mv.substring(s2, plus);
            mname.trim();
            s2 = (plus < 0) ? mv.length() : plus + 1;
            if (mname.length() == 0) continue;
            OscMessage* m = reg.find_msg(mname);
            if (m) { int mi = reg.msg_index(m); p->add_msg(mi); m->patch = p; m->exist.patch = true; }
        }
    }

    // Load oris from the same namespace.
    uint8_t o_count = prefs.getUChar("o_count", 0);
    ot.clear();
    for (uint8_t i = 0; i < o_count && i < MAX_ORIS; i++) {
        String saved = prefs.getString(("o_" + String(i)).c_str(), "");
        if (saved.length() > 0) ori_from_save_string(ot, saved);
    }
    ot.motion_threshold = prefs.getFloat("o_thr", 1.5f);
    ot.ori_tolerance    = prefs.getFloat("o_tol", 10.0f);
    ot.strict_matching  = prefs.getBool ("o_str", false);

    prefs.end();
    return (int)(p_count + m_count + o_count);
}

// ---------------------------------------------------------------------------
// Ori NVS persistence — legacy dual-namespace (used when no active show)
// ---------------------------------------------------------------------------

static inline int nvs_save_oris() {
    OriTracker& ot = ori_tracker();
    Preferences prefs;
    prefs.begin("ori_store", false);
    prefs.clear();
    uint8_t saved = 0;
    for (uint8_t i = 0; i < ot.ori_count; i++) {
        if (!ot.oris[i].used) continue;
        prefs.putString(("o_" + String(saved)).c_str(), ori_to_save_string(ot.oris[i]));
        saved++;
    }
    prefs.putUChar("o_count", saved);
    prefs.putFloat("o_thr",  ot.motion_threshold);
    prefs.putFloat("o_tol",  ot.ori_tolerance);
    prefs.putBool ("o_str",  ot.strict_matching);
    prefs.end();
    return saved;
}

static inline int nvs_load_oris() {
    OriTracker& ot = ori_tracker();
    Preferences prefs;
    prefs.begin("ori_store", true);
    uint8_t count = prefs.getUChar("o_count", 0);
    if (count == 0) { prefs.end(); return 0; }
    ot.clear();
    int loaded = 0;
    for (uint8_t i = 0; i < count && i < MAX_ORIS; i++) {
        String saved = prefs.getString(("o_" + String(i)).c_str(), "");
        if (saved.length() > 0) { ori_from_save_string(ot, saved); loaded++; }
    }
    ot.motion_threshold = prefs.getFloat("o_thr", 1.5f);
    ot.ori_tolerance    = prefs.getFloat("o_tol", 10.0f);
    ot.strict_matching  = prefs.getBool ("o_str", false);
    prefs.end();
    return loaded;
}

static inline void nvs_clear_oris() {
    Preferences prefs;
    prefs.begin("ori_store", false);
    prefs.clear();
    prefs.end();
}

// ---------------------------------------------------------------------------
// Save / load all — respects active show
// ---------------------------------------------------------------------------

static inline int nvs_save_all() {
    String ns = nvs_active_osc_ns();
    if (ns == "osc_store") {
        // Legacy dual-namespace path.
        OscRegistry& reg = osc_registry();
        Preferences prefs;
        prefs.begin("osc_store", false);
        prefs.clear();
        prefs.putUShort("p_count", reg.patch_count);
        for (uint16_t i = 0; i < reg.patch_count; i++)
            prefs.putString(("p_" + String(i)).c_str(), patch_to_save_string(reg.patches[i]));
        prefs.putUShort("m_count", reg.msg_count);
        for (uint16_t i = 0; i < reg.msg_count; i++)
            prefs.putString(("m_" + String(i)).c_str(), msg_to_save_string(reg.messages[i]));
        prefs.end();
        nvs_save_oris();
        return (int)(reg.patch_count + reg.msg_count);
    } else {
        // Active show path: save to combined namespace.
        return _nvs_save_combined(ns);
    }
}

static inline int nvs_load_all() {
    String ns = nvs_active_osc_ns();
    if (ns == "osc_store") {
        // Legacy dual-namespace path (identical to original nvs_load_all logic).
        OscRegistry& reg = osc_registry();
        Preferences prefs;
        prefs.begin("osc_store", true);
        uint16_t p_count = prefs.getUShort("p_count", 0);
        uint16_t m_count = prefs.getUShort("m_count", 0);
        if (p_count == 0 && m_count == 0) { prefs.end(); return 0; }
        for (uint16_t i = 0; i < reg.patch_count; i++) {
            if (reg.patches[i].task_handle) {
                vTaskDelete(reg.patches[i].task_handle);
                reg.patches[i].task_handle = nullptr;
            }
        }
        reg.patch_count = 0; reg.msg_count = 0;
        for (uint16_t i = 0; i < p_count && i < MAX_OSC_PATCHES; i++) {
            String saved = prefs.getString(("p_" + String(i)).c_str(), "");
            if (!saved.length()) continue;
            String pname; int ni = saved.indexOf("name:");
            if (ni >= 0) { int e = saved.indexOf(',', ni); pname = (e<0)?saved.substring(ni+5):saved.substring(ni+5,e); pname.trim(); }
            if (!pname.length()) pname = "patch_" + String(i);
            OscPatch* p = reg.get_or_create_patch(pname);
            if (p) patch_from_save_string(p, saved);
        }
        for (uint16_t i = 0; i < m_count && i < MAX_OSC_MESSAGES; i++) {
            String saved = prefs.getString(("m_" + String(i)).c_str(), "");
            if (!saved.length()) continue;
            String mname; int ni = saved.indexOf("name:");
            if (ni >= 0) { int e = saved.indexOf(',', ni); mname = (e<0)?saved.substring(ni+5):saved.substring(ni+5,e); mname.trim(); }
            if (!mname.length()) mname = "msg_" + String(i);
            OscMessage* m = reg.get_or_create_msg(mname);
            if (!m) continue;
            OscMessage parsed; String err;
            if (parsed.from_config_str(saved, &err)) { *m = parsed * (*m); m->name = mname; m->exist.name = true; }
            int en_idx = saved.indexOf("enabled:");
            if (en_idx >= 0) {
                int e = saved.indexOf(',', en_idx);
                String ev = (e<0)?saved.substring(en_idx+8):saved.substring(en_idx+8,e);
                ev.trim(); ev.toLowerCase();
                m->enabled = (ev=="true"||ev=="1"||ev=="yes");
            }
            if (m->exist.patch && m->patch) { int mi = reg.msg_index(m); m->patch->add_msg(mi); }
        }
        for (uint16_t i = 0; i < p_count && i < MAX_OSC_PATCHES; i++) {
            String saved = prefs.getString(("p_" + String(i)).c_str(), "");
            if (!saved.length()) continue;
            int msgs_idx = saved.indexOf("msgs:"); if (msgs_idx < 0) continue;
            String pname; int ni = saved.indexOf("name:");
            if (ni >= 0) { int e = saved.indexOf(',', ni); pname = (e<0)?saved.substring(ni+5):saved.substring(ni+5,e); pname.trim(); }
            OscPatch* p = reg.find_patch(pname); if (!p) continue;
            int e = saved.indexOf(',', msgs_idx);
            String mv = (e<0)?saved.substring(msgs_idx+5):saved.substring(msgs_idx+5,e); mv.trim();
            int s2 = 0;
            while (s2 < (int)mv.length()) {
                int plus = mv.indexOf('+', s2);
                String mname = (plus<0)?mv.substring(s2):mv.substring(s2,plus); mname.trim();
                s2 = (plus<0)?mv.length():plus+1; if (!mname.length()) continue;
                OscMessage* m = reg.find_msg(mname);
                if (m) { int mi = reg.msg_index(m); p->add_msg(mi); m->patch = p; m->exist.patch = true; }
            }
        }
        prefs.end();
        nvs_load_oris();
        return (int)(p_count + m_count);
    } else {
        return _nvs_load_combined(ns);
    }
}

// ---------------------------------------------------------------------------
// Single-object save helpers (respect active show)
// ---------------------------------------------------------------------------

static inline bool nvs_save_msg(const String& name) {
    OscRegistry& reg = osc_registry();
    OscMessage* m = reg.find_msg(name);
    if (!m) return false;
    String ns = nvs_active_osc_ns();
    Preferences prefs;
    prefs.begin(ns.c_str(), false);
    uint16_t count = prefs.getUShort("m_count", 0);
    int slot = -1;
    for (uint16_t i = 0; i < count; i++) {
        String saved = prefs.getString(("m_" + String(i)).c_str(), "");
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            String sn = (end<0)?saved.substring(ni+5):saved.substring(ni+5,end); sn.trim();
            if (osc_lower_copy(sn) == osc_lower_copy(name)) { slot = i; break; }
        }
    }
    if (slot < 0) { slot = count; prefs.putUShort("m_count", count + 1); }
    prefs.putString(("m_" + String(slot)).c_str(), msg_to_save_string(*m));
    prefs.end();
    return true;
}

static inline bool nvs_save_patch(const String& name) {
    OscRegistry& reg = osc_registry();
    OscPatch* p = reg.find_patch(name);
    if (!p) return false;
    String ns = nvs_active_osc_ns();
    Preferences prefs;
    prefs.begin(ns.c_str(), false);
    uint16_t count = prefs.getUShort("p_count", 0);
    int slot = -1;
    for (uint16_t i = 0; i < count; i++) {
        String saved = prefs.getString(("p_" + String(i)).c_str(), "");
        int ni = saved.indexOf("name:");
        if (ni >= 0) {
            int end = saved.indexOf(',', ni);
            String sn = (end<0)?saved.substring(ni+5):saved.substring(ni+5,end); sn.trim();
            if (osc_lower_copy(sn) == osc_lower_copy(name)) { slot = i; break; }
        }
    }
    if (slot < 0) { slot = count; prefs.putUShort("p_count", count + 1); }
    prefs.putString(("p_" + String(slot)).c_str(), patch_to_save_string(*p));
    prefs.end();
    return true;
}

// ---------------------------------------------------------------------------
// Show management
// ---------------------------------------------------------------------------

/// Save current RAM state as a named show.
/// Returns false if shows index is full.
static inline bool nvs_save_show(const String& show_name) {
    if (show_name.length() == 0) return false;

    Preferences idx_prefs;
    idx_prefs.begin("shows_idx", false);
    uint8_t count = idx_prefs.getUChar("s_count", 0);

    // Find existing slot or allocate new one.
    int slot = -1;
    for (uint8_t i = 0; i < count && i < MAX_SHOWS; i++) {
        String sn = idx_prefs.getString(("s_" + String(i)).c_str(), "");
        if (sn.equalsIgnoreCase(show_name)) { slot = i; break; }
    }
    if (slot < 0) {
        if (count >= MAX_SHOWS) { idx_prefs.end(); return false; }
        slot = count;
        idx_prefs.putString(("s_" + String(slot)).c_str(), show_name);
        idx_prefs.putUChar("s_count", count + 1);
    }
    idx_prefs.end();

    // Save all data to the show's namespace.
    String ns = "sw_" + String(slot);
    _nvs_save_combined(ns);
    return true;
}

/// Load a named show into RAM.
/// Does NOT set the active show — caller must call nvs_set_active_show() after confirm.
/// Returns total objects loaded, or -1 if show not found.
static inline int nvs_load_show(const String& show_name) {
    String ns = _show_namespace(show_name);
    if (ns.length() == 0) return -1;
    return _nvs_load_combined(ns);
}

/// List saved shows as a CSV string.
static inline String nvs_list_shows() {
    Preferences prefs;
    prefs.begin("shows_idx", true);
    uint8_t count = prefs.getUChar("s_count", 0);
    String result;
    for (uint8_t i = 0; i < count && i < MAX_SHOWS; i++) {
        String sn = prefs.getString(("s_" + String(i)).c_str(), "");
        if (sn.length() == 0) continue;
        if (result.length()) result += ", ";
        result += sn;
    }
    prefs.end();
    return result.length() > 0 ? result : String("(none)");
}

/// Delete a named show from NVS.  Returns false if not found.
static inline bool nvs_delete_show(const String& show_name) {
    Preferences idx_prefs;
    idx_prefs.begin("shows_idx", false);
    uint8_t count = idx_prefs.getUChar("s_count", 0);

    int slot = -1;
    for (uint8_t i = 0; i < count && i < MAX_SHOWS; i++) {
        String sn = idx_prefs.getString(("s_" + String(i)).c_str(), "");
        if (sn.equalsIgnoreCase(show_name)) { slot = i; break; }
    }
    if (slot < 0) { idx_prefs.end(); return false; }

    // Clear the show namespace.
    String ns = "sw_" + String(slot);
    { Preferences sp; sp.begin(ns.c_str(), false); sp.clear(); sp.end(); }

    // Remove from index: shift entries down.
    for (uint8_t i = slot; i < count - 1 && i < MAX_SHOWS - 1; i++) {
        String next = idx_prefs.getString(("s_" + String(i + 1)).c_str(), "");
        idx_prefs.putString(("s_" + String(i)).c_str(), next);
        // Also move the data namespace.
        // (NVS doesn't support rename; data stays in sw_{i+1} but index now points to sw_{i}.
        //  This leaves a gap.  Simplest fix: rebuild namespaces on load.
        //  For now, copy data from sw_{i+1} to sw_{i}.)
        Preferences src, dst;
        src.begin(("sw_" + String(i + 1)).c_str(), true);
        dst.begin(("sw_" + String(i)).c_str(), false);
        dst.clear();
        uint16_t pc = src.getUShort("p_count", 0);
        dst.putUShort("p_count", pc);
        for (uint16_t j = 0; j < pc; j++) {
            String v = src.getString(("p_" + String(j)).c_str(), "");
            dst.putString(("p_" + String(j)).c_str(), v);
        }
        uint16_t mc = src.getUShort("m_count", 0);
        dst.putUShort("m_count", mc);
        for (uint16_t j = 0; j < mc; j++) {
            String v = src.getString(("m_" + String(j)).c_str(), "");
            dst.putString(("m_" + String(j)).c_str(), v);
        }
        uint8_t oc = src.getUChar("o_count", 0);
        dst.putUChar("o_count", oc);
        for (uint8_t j = 0; j < oc; j++) {
            String v = src.getString(("o_" + String(j)).c_str(), "");
            dst.putString(("o_" + String(j)).c_str(), v);
        }
        dst.putFloat("o_thr", src.getFloat("o_thr", 1.5f));
        dst.putFloat("o_tol", src.getFloat("o_tol", 10.0f));
        dst.putBool ("o_str", src.getBool ("o_str", false));
        src.end(); dst.end();
        // Clear the old slot.
        Preferences old; old.begin(("sw_" + String(i + 1)).c_str(), false); old.clear(); old.end();
    }
    idx_prefs.putUChar("s_count", count - 1);

    // If the deleted show was active, clear the active slot.
    String active = idx_prefs.getString("active", "");
    if (active.equalsIgnoreCase(show_name)) idx_prefs.putString("active", "");

    idx_prefs.end();
    return true;
}

/// Rename a show.  Returns false if old name not found or new name already exists.
static inline bool nvs_rename_show(const String& old_name, const String& new_name) {
    if (old_name.length() == 0 || new_name.length() == 0) return false;
    Preferences prefs;
    prefs.begin("shows_idx", false);
    uint8_t count = prefs.getUChar("s_count", 0);
    int slot = -1;
    for (uint8_t i = 0; i < count && i < MAX_SHOWS; i++) {
        String sn = prefs.getString(("s_" + String(i)).c_str(), "");
        if (sn.equalsIgnoreCase(new_name)) { prefs.end(); return false; }  // new name taken
        if (sn.equalsIgnoreCase(old_name)) slot = i;
    }
    if (slot < 0) { prefs.end(); return false; }
    prefs.putString(("s_" + String(slot)).c_str(), new_name);
    String active = prefs.getString("active", "");
    if (active.equalsIgnoreCase(old_name)) prefs.putString("active", new_name);
    prefs.end();
    return true;
}

// ---------------------------------------------------------------------------
// Clear all OSC data
// ---------------------------------------------------------------------------

static inline void nvs_clear_osc_data() {
    Preferences prefs;
    prefs.begin("osc_store", false); prefs.clear(); prefs.end();
    nvs_clear_oris();
    // Also clear any active show.
    nvs_set_active_show("");
}

#endif // OSC_STORAGE_H
