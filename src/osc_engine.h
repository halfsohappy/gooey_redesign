// =============================================================================
// osc_engine.h — FreeRTOS send tasks and the MicroOsc transport layer
// =============================================================================
//
// This file provides:
//   1. The WiFiUDP + MicroOscUdp globals used for all OSC I/O.
//   2. A send mutex that serialises access to the UDP socket across tasks.
//   3. The FreeRTOS task function that each OscScene runs to send its
//      messages at the configured polling period.
//   4. Helpers: start_scene / stop_scene / blackout_all / restore_all.
//   5. The StatusReporter::send() implementation (needs the MicroOsc global).
//   6. begin_udp() — initialises WiFi and the UDP listener.
//
// SENDING FLOW (per scene task iteration):
//   1. Sleep for send_period_ms.
//   2. If the scene is disabled, go back to sleep.
//   3. Lock the registry mutex.
//   4. For each message index in the scene:
//      a. Skip if the message is disabled.
//      b. Resolve effective IP, port, and OSC address (message's own value,
//         or the scene's value if the scene overrides that field, or the
//         scene's value as a fallback if the message has no value set).
//      c. Read the live sensor value from data_streams[].
//      d. Lock the send mutex.
//      e. Set the MicroOsc destination and call sendFloat().
//      f. Unlock the send mutex.
//   5. Unlock the registry mutex and loop.
// =============================================================================

#ifndef OSC_ENGINE_H
#define OSC_ENGINE_H

#include <MicroOsc.h>
#include <MicroOscUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "osc_registry.h"
#include "osc_status.h"
#ifdef AB7_BUILD
#include "ab7_hardware.h"
#else
#include "bart_hardware.h"
#endif
#include "ori_tracker.h"

// ---------------------------------------------------------------------------
// Global transport objects
// ---------------------------------------------------------------------------

WiFiUDP        Udp;                    // shared UDP socket
MicroOscUdp<1024> osc(&Udp);          // OSC codec (1024-byte receive buffer)

// ---------------------------------------------------------------------------
// Send mutex — serialises all outbound UDP writes across FreeRTOS tasks
// ---------------------------------------------------------------------------

static inline SemaphoreHandle_t& osc_send_mutex() {
    static SemaphoreHandle_t mtx = xSemaphoreCreateMutex();
    return mtx;
}

// ---------------------------------------------------------------------------
// Optional serial logging of every outbound message
// ---------------------------------------------------------------------------

static bool _send_logging_enabled = false;

static inline void set_send_logging_enabled(bool enabled) { _send_logging_enabled = enabled; }
static inline bool get_send_logging_enabled() { return _send_logging_enabled; }

// ---------------------------------------------------------------------------
// Optional duplicate suppression — skip sends when value hasn't changed
// ---------------------------------------------------------------------------

static bool _dedup_enabled = false;

static inline bool get_dedup_enabled() { return _dedup_enabled; }

/// Clear the dedup cache on every message in the registry.
static inline void clear_all_dedup_caches() {
    OscRegistry& reg = osc_registry();
    for (uint16_t i = 0; i < reg.msg_count; i++) {
        reg.messages[i].clear_dedup_cache();
    }
}

static inline void set_dedup_enabled(bool enabled) {
    _dedup_enabled = enabled;
    clear_all_dedup_caches();
}

// ---------------------------------------------------------------------------
// StatusReporter::send() implementation
// ---------------------------------------------------------------------------
//
// Defined here (not in osc_status.h) because it needs the osc global and
// the send mutex.

inline void StatusReporter::send(StatusLevel lvl, const String& category,
                                  const String& message) {
    // Build the payload: "[LEVEL] category: message"
    String payload = String("[") + status_level_label(lvl) + "] "
                   + category + ": " + message;

    // Always echo to serial if the level passes the serial filter.
    if (would_serial(lvl)) {
        Serial.println(payload);
    }

    // Send over OSC only if configured and level passes the OSC filter.
    if (!would_send(lvl)) return;
    if (dest_port == 0) return;

    xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
    osc.setDestination(dest_ip, dest_port);
    osc.sendString(dest_address.c_str(), payload.c_str());
    xSemaphoreGive(osc_send_mutex());
}

// ---------------------------------------------------------------------------
// Effective-value resolution helpers
// ---------------------------------------------------------------------------
//
// These decide what IP / port / address / bounds a message actually uses,
// taking into account the scene override flags and fallback logic:
//
//   1. If the scene overrides the field AND the scene has it set → scene wins.
//   2. Else if the message has it set → message wins.
//   3. Else if the scene has it set (no override, but no message value) → scene.
//   4. Else → empty / zero (not sendable).

static inline IPAddress resolve_ip(const OscMessage& m, const OscScene& p) {
    if (p.overrides.ip && p.exist.ip) return p.ip;
    if (m.exist.ip)                   return m.ip;
    if (p.exist.ip)                   return p.ip;
    return IPAddress(0, 0, 0, 0);
}

static inline unsigned int resolve_port(const OscMessage& m, const OscScene& p) {
    if (p.overrides.port && p.exist.port) return p.port;
    if (m.exist.port)                     return m.port;
    if (p.exist.port)                     return p.port;
    return 0;
}

/// Resolve the effective OSC address using the scene's address_mode:
///
///   ADR_FALLBACK — message's own address, scene as fallback if msg has none.
///   ADR_OVERRIDE — scene address replaces message address entirely.
///   ADR_PREPEND  — scene.adr + msg.adr  (e.g. "/mixer" + "/fader1" → "/mixer/fader1")
///   ADR_APPEND   — msg.adr + scene.adr  (e.g. "/fader1" + "/mixer" → "/fader1/mixer")
///
/// For PREPEND and APPEND, if either side is empty it gracefully degrades
/// to just the non-empty side.  A leading double-slash is avoided by
/// stripping trailing '/' from the first part.
static inline String resolve_address(const OscMessage& m, const OscScene& p) {
    String m_adr = m.exist.adr ? m.osc_address : String("");
    String p_adr = p.exist.adr ? p.osc_address : String("");

    switch (p.address_mode) {
        case ADR_OVERRIDE:
            // Scene address replaces message address unconditionally.
            return p_adr.length() > 0 ? p_adr : m_adr;

        case ADR_PREPEND: {
            // scene.adr + msg.adr  (scene first, message appended).
            if (p_adr.length() == 0) return m_adr;
            if (m_adr.length() == 0) return p_adr;
            // Strip trailing '/' from scene to avoid "//".
            String prefix = p_adr;
            if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
            // Ensure msg part starts with '/'.
            if (!m_adr.startsWith("/")) m_adr = "/" + m_adr;
            return prefix + m_adr;
        }

        case ADR_APPEND: {
            // msg.adr + scene.adr  (message first, scene appended).
            if (m_adr.length() == 0) return p_adr;
            if (p_adr.length() == 0) return m_adr;
            // Strip trailing '/' from message to avoid "//".
            String prefix = m_adr;
            if (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
            // Ensure scene part starts with '/'.
            if (!p_adr.startsWith("/")) p_adr = "/" + p_adr;
            return prefix + p_adr;
        }

        case ADR_FALLBACK:
        default:
            // Message's address wins if set; otherwise use scene as fallback.
            // The overrides.adr flag is handled here for backward compatibility:
            // if overrides.adr is true, the scene address is forced (like OVERRIDE).
            if (p.overrides.adr && p_adr.length() > 0) return p_adr;
            return m_adr.length() > 0 ? m_adr : p_adr;
    }
}

/// Resolve the effective low bound (bounds[0]).
/// Scene override → message's own → scene fallback → 0.0.
static inline float resolve_low(const OscMessage& m, const OscScene& p) {
    if (p.overrides.low && p.exist.low)  return p.bounds[0];
    if (m.exist.low)                     return m.bounds[0];
    if (p.exist.low)                     return p.bounds[0];
    return 0.0f;
}

/// Resolve the effective high bound (bounds[1]).
/// Scene override → message's own → scene fallback → 1.0.
static inline float resolve_high(const OscMessage& m, const OscScene& p) {
    if (p.overrides.high && p.exist.high) return p.bounds[1];
    if (m.exist.high)                     return m.bounds[1];
    if (p.exist.high)                     return p.bounds[1];
    return 1.0f;
}

// ---------------------------------------------------------------------------
// Scene send task — the FreeRTOS function each scene runs
// ---------------------------------------------------------------------------
//
// The task receives a pointer to its OscScene (which lives in the registry's
// fixed array and never moves).  It loops forever, sending all assigned
// messages at the configured rate.

void scene_send_task(void* param) {
    OscScene* scene = static_cast<OscScene*>(param);
    OscRegistry& reg = osc_registry();

    int last_ori_index = -1;   // matches OriTracker default (no active ori)

    for (;;) {
        // Sleep for the polling period.
        vTaskDelay(pdMS_TO_TICKS(clamp_scene_period_ms((long)scene->send_period_ms)));

        if (!scene->enabled) continue;

        // Reset dedup caches whenever the active ori changes so that messages
        // always fire at least once after each ori transition.
        if (_dedup_enabled) {
            int cur_ori_index = ori_tracker().active_ori_index;
            if (cur_ori_index != last_ori_index) {
                last_ori_index = cur_ori_index;
                clear_all_dedup_caches();
            }
        }

        reg.lock();

        for (uint8_t i = 0; i < scene->msg_count; i++) {
            int mi = scene->msg_indices[i];
            if (mi < 0 || mi >= (int)reg.msg_count) continue;

            OscMessage& msg = reg.messages[mi];
            if (!msg.enabled) continue;

            // Determine if this is a ternori (binary ori switch) message.
            bool is_ternori = (msg.ternori.length() > 0);

            // Normal messages need a sensor value pointer.
            if (!is_ternori && !msg.value_ptr) continue;

            // --- Ori-conditional check ---
            // Ternori messages always send (the value changes, not the send decision).
            if (!is_ternori) {
                OriTracker& ot = ori_tracker();
                if (msg.ori_only.length() > 0 && !ot.is_active(msg.ori_only)) continue;
                if (msg.ori_not.length() > 0  &&  ot.is_active(msg.ori_not))  continue;
            }

            // Resolve effective destination.
            IPAddress    eff_ip   = resolve_ip(msg, *scene);
            unsigned int eff_port = resolve_port(msg, *scene);
            String       eff_adr  = resolve_address(msg, *scene);

            if (eff_ip == IPAddress(0, 0, 0, 0) || eff_port == 0 || eff_adr.length() == 0) {
                continue;  // not enough info to send
            }

            // Read the value: ternori → binary from ori state, normal → sensor.
            float val;
            if (is_ternori) {
                val = ori_tracker().is_active(msg.ternori) ? 1.0f : 0.0f;
            } else {
                val = *(msg.value_ptr);
            }

            // Resolve effective output bounds (scene may override message's).
            float eff_low  = resolve_low(msg, *scene);
            float eff_high = resolve_high(msg, *scene);

            // Map [0, 1] → [low, high].  When low == high the formula
            // correctly outputs that constant value (eff_low + 0).
            val = eff_low + val * (eff_high - eff_low);

            // Duplicate suppression: skip if value hasn't changed.
            if (_dedup_enabled && msg._has_last_sent && msg._last_sent_val == val) {
                continue;
            }

            // Send the float value over OSC.
            char log_buf[160];
            bool should_log = get_send_logging_enabled();
            if (should_log) {
                snprintf(log_buf, sizeof(log_buf), "[SEND] %s:%u %s = %.6f",
                         eff_ip.toString().c_str(), eff_port,
                         eff_adr.c_str(), static_cast<double>(val));
            }

            xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
            osc.setDestination(eff_ip, eff_port);
            osc.sendFloat(eff_adr.c_str(), val);
            xSemaphoreGive(osc_send_mutex());

            msg._last_sent_val = val;
            msg._has_last_sent = true;

            if (should_log) {
                Serial.println(log_buf);
            }
        }

        reg.unlock();
    }
}

// ---------------------------------------------------------------------------
// Scene lifecycle helpers
// ---------------------------------------------------------------------------

/// Create and start the FreeRTOS task for a scene.  No-op if already running.
static inline void start_scene(OscScene* p) {
    if (!p) return;
    if (p->task_handle) return;  // already running

    p->enabled = true;

    // Task name: truncate to 15 chars (FreeRTOS limit).
    char tname[16];
    snprintf(tname, sizeof(tname), "p_%s", p->name.c_str());

    xTaskCreate(
        scene_send_task,   // task function
        tname,             // name (for debugging)
        4096,              // stack size (bytes)
        p,                 // parameter: pointer to the scene
        1,                 // priority
        &p->task_handle    // handle stored back in the scene
    );

    status_reporter().info("scene", "Started scene '" + p->name + "'");
}

/// Stop the FreeRTOS task for a scene.
static inline void stop_scene(OscScene* p) {
    if (!p) return;
    p->enabled = false;
    if (p->task_handle) {
        vTaskDelete(p->task_handle);
        p->task_handle = nullptr;
    }
    status_reporter().info("scene", "Stopped scene '" + p->name + "'");
}

/// Stop all scene tasks (theater "blackout").
static inline void blackout_all() {
    OscRegistry& reg = osc_registry();
    reg.lock();
    for (uint16_t i = 0; i < reg.scene_count; i++) {
        stop_scene(&reg.scenes[i]);
    }
    reg.unlock();
    status_reporter().info("engine", "BLACKOUT — all scenes stopped");
}

/// Re-enable and start all scenes that have at least one message.
static inline void restore_all() {
    OscRegistry& reg = osc_registry();
    reg.lock();
    for (uint16_t i = 0; i < reg.scene_count; i++) {
        if (reg.scenes[i].msg_count > 0) {
            start_scene(&reg.scenes[i]);
        }
    }
    reg.unlock();
    status_reporter().info("engine", "RESTORE — all scenes restarted");
}

// ---------------------------------------------------------------------------
// UDP / WiFi initialisation
// ---------------------------------------------------------------------------

/// Connect to WiFi and start the UDP listener on `start_port`.
/// Called once during setup() after provisioning.
static inline void begin_udp(const String& start_ip, const String& start_ssid,
                              const String& start_pass, int start_port) {
    IPAddress static_ip;
    if (start_ip != "dhcp") {
        static_ip.fromString(start_ip);
        WiFi.config(static_ip);
    }

    WiFi.begin(start_ssid.c_str(), start_pass.c_str());
    Serial.print("Connecting to WiFi");
    const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - startTime >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println();
            Serial.println(F("[BOOT] WiFi connection timed out. Clearing credentials and restarting..."));
            // Clear provisioned flag so the device re-enters provisioning mode.
            // Guard against begin() failure (e.g. corrupted NVS): if begin fails,
            // getBool("provisioned") will also fail and return the default (false),
            // so provisioning will start correctly on the next boot anyway.
            if (preferences.begin("device_config", false)) {
                preferences.putBool("provisioned", false);
                preferences.end();
            }
            ESP.restart();
        }
    }
    Serial.println();
    Serial.println("WiFi connected — IP: " + WiFi.localIP().toString());

    Udp.begin(start_port);
    Serial.println("UDP listening on port " + String(start_port));

    // Initialise the send mutex and registry mutex early.
    osc_send_mutex();
    osc_registry().ensure_mutex();

    status_reporter().info("network", "UDP ready on " + WiFi.localIP().toString()
                           + ":" + String(start_port));
}

#endif // OSC_ENGINE_H
