// =============================================================================
// osc_commands.h — Incoming OSC command dispatcher
// =============================================================================
//
// This file contains the main handler that is called every time an OSC
// message arrives.  It parses the address, identifies the target (message,
// scene, or system command), and dispatches to the appropriate action.
//
// CASE HANDLING:
//   All command segments accept camelCase, snake_case, and lowercase.
//   For example, "addMsg", "add_msg", and "addmsg" are all equivalent.
//   User-defined names (message and scene names) preserve their original case.
//
// PAYLOAD FORMAT:
//   All incoming payloads are a single string (CSV when multiple values are
//   needed) or a single float/integer.  Commands that need two names (e.g.
//   clone, rename, move) accept them as "name1, name2" in one string.
//
// ADDRESS FORMAT:
//   /annieData{device_adr}/{category}/{name}/{command}
//
//   {device_adr}  — the device's provisioned name (e.g. "/bart")
//   {category}    — "msg", "scene", "list", "clone", "rename", "save",
//                    "load", "nvs", or a top-level command
//   {name}        — the name of the target message or scene
//   {command}     — the action to perform (defaults to "assign" if omitted)
//
// COMMAND REFERENCE  (see docs/osc_guide.md for full documentation):
// ─────────────────────────────────────────────────────────────────────────────
//
// ── MESSAGE COMMANDS (/annieData{dev}/msg/{name}/...) ──────────────────────
//   (no command / assign)  — create or update a message from config string
//   delete                 — remove message from registry
//   enable                 — enable message for sending
//   disable / mute         — disable message (skip during send)
//   info                   — reply with message details
//
// ── SCENE COMMANDS (/annieData{dev}/scene/{name}/...) ──────────────────────
//   (no command / assign)  — create or update a scene from config string
//   delete                 — remove scene and its task
//   start                  — create FreeRTOS task and begin sending
//   stop                   — stop the send task
//   enable                 — re-enable a stopped scene (same as start)
//   disable / mute         — stop sending without deleting the task
//   addMsg                 — add message(s) to this scene (CSV payload)
//   removeMsg              — remove a message from this scene
//   period                 — set the send period in milliseconds
//   override               — set which fields the scene forces on its messages
//   adrMode                — set address composition mode
//   setAll                 — set a property on all messages in this scene
//   solo                   — enable one message, disable all others in scene
//   unsolo                 — re-enable all messages in this scene
//   enableAll              — enable all messages in this scene
//   info                   — reply with scene details
//
// ── CLONE COMMANDS (/annieData{dev}/clone/...) ─────────────────────────────
//   msg     "src, dest"    — duplicate a message under a new name
//   scene   "src, dest"    — duplicate a scene (and optionally its messages)
//
// ── RENAME COMMANDS (/annieData{dev}/rename/...) ───────────────────────────
//   msg     "old, new"     — rename a message
//   scene   "old, new"     — rename a scene
//
// ── MOVE COMMAND (/annieData{dev}/move) ────────────────────────────────────
//   "msgName, sceneName"   — move a message into a different scene
//
// ── LIST COMMANDS (/annieData{dev}/list/...) ───────────────────────────────
//   msgs     [verbose?]    — reply with all message names (+ params if verbose)
//   scenes  [verbose?]    — reply with all scene names (+ params if verbose)
//   all      [verbose?]    — reply with everything
//
// ── GLOBAL COMMANDS (/annieData{dev}/...) ──────────────────────────────────
//   blackout               — stop ALL scene tasks immediately
//   restore                — restart all scenes that have messages
//   dedup                  — toggle duplicate value suppression (payload: on/off/1/0)
//
// ── DIRECT COMMAND (/annieData{dev}/direct/{name}) ────────────────────────
//   (config string payload) — one-step: create msg + scene, add, and start
//
// ── STATUS COMMANDS (/annieData{dev}/status/...) ───────────────────────────
//   config   [config_str]  — set status destination (ip, port, address)
//   level    [level_str]   — set minimum importance level
//
// ── SAVE / LOAD COMMANDS (/annieData{dev}/...) ─────────────────────────────
//   save                   — save all scenes and messages to NVS
//   save/all               — same as save
//   save/msg    "name"     — save one message to NVS
//   save/scene  "name"     — save one scene to NVS
//   load                   — load all scenes and messages from NVS
//   load/all               — same as load
//   nvs/clear              — erase all saved OSC data from NVS
//
// ── ORI RECORDING COMMANDS (/annieData{dev}/ori/record/...) ────────────────
//   start/{name}           — begin timed recording session for named ori
//   stop                   — stop and commit (auto-axis detect + subsample)
//   cancel                 — discard recording without saving
//   status                 — reply: active, name, sample_count, elapsed_ms
//
// ── FLUSH COMMAND (/annieData{dev}/flush) ──────────────────────────────────
//   Replies "OK" once all preceding commands have been processed.
//   Used by gooey for transactional library-to-device push.
//
// ── SHOW COMMANDS (/annieData{dev}/show/...) ────────────────────────────────
//   save/{name}            — snapshot current state as named show in NVS
//   load/{name}            — load named show (two-step: pending → confirm)
//   load/confirm           — execute the pending show load
//   list                   — reply: CSV of saved show names
//   delete/{name}          — delete named show from NVS
//   rename  "old, new"     — rename a saved show
//
// =============================================================================

#ifndef OSC_COMMANDS_H
#define OSC_COMMANDS_H

#include "osc_engine.h"
#include "ori_tracker.h"

// Forward-declare the device address (defined in main.h / main.cpp).
extern String device_adr;

// Forward-declare the current quaternion globals (defined in main.cpp).
extern float cur_qi, cur_qj, cur_qk, cur_qr;

// Forward-declare the tare globals (defined in main.cpp).
extern float tare_qi, tare_qj, tare_qk, tare_qr;
extern bool  tare_active;

// Forward-declare the Euler decomposition selector (defined in main.cpp).
// 0 = ZYX (default, singular on Y), 1 = ZXY (singular on X).
extern int euler_order;

// Swing-twist decomposition axes (defined in main.cpp).
extern float twist_nx, twist_ny, twist_nz;
extern float tare_up_x, tare_up_y, tare_up_z;

// ---------------------------------------------------------------------------
// Command normaliser — accepts camelCase, snake_case, and lowercase
// ---------------------------------------------------------------------------
//
// Strips underscores and lowercases everything, so "addMsg", "add_msg",
// "addmsg", and "ADDMSG" all normalise to "addmsg".

static inline String normalise_cmd(const String& raw) {
    String out;
    out.reserve(raw.length());
    for (unsigned int i = 0; i < raw.length(); i++) {
        char c = raw.charAt(i);
        if (c != '_') out += (char)tolower(c);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Reply helper — sends a string back to whoever sent the command
// ---------------------------------------------------------------------------
//
// We send replies to the source IP on the same port, under an address that
// mirrors the incoming address with "/reply" prepended.  This is a pragmatic
// convention for OSC — the receiver can filter on /reply/*.

static inline void osc_reply(const IPAddress& dest_ip, unsigned int dest_port,
                              const String& reply_address, const String& text) {
    xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
    osc.setDestination(dest_ip, dest_port);
    osc.sendString(reply_address.c_str(), text.c_str());
    xSemaphoreGive(osc_send_mutex());
}

// ---------------------------------------------------------------------------
// Main command handler — called from loop() via osc.onOscMessageReceived()
// ---------------------------------------------------------------------------

void osc_handle_message(MicroOscMessage& osc_msg) {
    // Persists between calls — holds a pending show name for the two-step
    // /show/load confirm flow (clears on confirm, cancel, or 30-second timeout).
    static String pending_show_name = "";
    static unsigned long pending_show_ms = 0;

    String address = osc_msg.getOscAddress();
    Serial.println("[OSC] " + address);

    // ── Check device prefix ────────────────────────────────────────────────
    String prefix = "/annieData" + device_adr;
    if (!address.startsWith(prefix)) {
        Serial.println("  → not for this device, ignoring");
        return;
    }
    address = address.substring(prefix.length());  // strip prefix

    // Create a normalised copy for command matching (lowercase, no underscores).
    // The original `address` is still used to extract user-defined names.
    String norm_adr = normalise_cmd(address);

    // Source address for replies (remote IP + port the message came from).
    IPAddress sender_ip = Udp.remoteIP();
    unsigned int sender_port = Udp.remotePort();
    String reply_adr = "/reply" + device_adr;

    OscRegistry& reg = osc_registry();

    // ── TOP-LEVEL COMMANDS (no name required) ──────────────────────────────

    if (norm_adr == "/blackout") {
        blackout_all();
        osc_reply(sender_ip, sender_port, reply_adr, "BLACKOUT");
        return;
    }

    if (norm_adr == "/restore") {
        restore_all();
        osc_reply(sender_ip, sender_port, reply_adr, "RESTORE");
        return;
    }

    // ── TARE COMMANDS ──────────────────────────────────────────────────────
    //    /tare          — capture current orientation as Euler zero reference
    //    /tare/reset    — clear tare, return to absolute world-frame Euler
    //    /tare/status   — report whether a tare is currently active

    if (norm_adr == "/tare") {
        tare_qi = cur_qi; tare_qj = cur_qj; tare_qk = cur_qk; tare_qr = cur_qr;
        tare_active = true;

        // Auto-select Euler decomposition to minimise gimbal-lock risk.
        // vx/vy/vz = how vertical each device axis is at the tare pose.
        // R[2][*] gives the world-Z (vertical) direction in device-frame coords.
        float vx = fabsf(2.0f*(tare_qi*tare_qk - tare_qr*tare_qj));  // |R[2][0]|
        float vy = fabsf(2.0f*(tare_qj*tare_qk + tare_qr*tare_qi));  // |R[2][1]|
        float vz = fabsf(1.0f - 2.0f*(tare_qi*tare_qi + tare_qj*tare_qj));  // |R[2][2]|
        // ZYX is singular when device-Y is vertical (vy ≈ 1).
        // ZXY is singular when device-X is vertical (vx ≈ 1).
        // Choose ZXY when Y is most vertical (avoid ZYX singularity), else ZYX.
        euler_order = (vy > vx && vy > vz) ? 1 : 0;

        // Swing-twist: up axis = device axis most aligned with world vertical.
        float svx = 2.0f*(tare_qi*tare_qk - tare_qr*tare_qj);
        float svy = 2.0f*(tare_qj*tare_qk + tare_qr*tare_qi);
        float svz = 1.0f - 2.0f*(tare_qi*tare_qi + tare_qj*tare_qj);
        if (vx >= vy && vx >= vz) {
            tare_up_x = copysignf(1.0f, svx); tare_up_y = 0; tare_up_z = 0;
        } else if (vy >= vx && vy >= vz) {
            tare_up_x = 0; tare_up_y = copysignf(1.0f, svy); tare_up_z = 0;
        } else {
            tare_up_x = 0; tare_up_y = 0; tare_up_z = copysignf(1.0f, svz);
        }
        // Twist axis = device axis least aligned with vertical (most horizontal).
        if (vx <= vy && vx <= vz) {
            twist_nx = 1.0f; twist_ny = 0; twist_nz = 0;
        } else if (vy <= vx && vy <= vz) {
            twist_nx = 0; twist_ny = 1.0f; twist_nz = 0;
        } else {
            twist_nx = 0; twist_ny = 0; twist_nz = 1.0f;
        }

        const char* order_name = (euler_order == 1) ? "ZXY" : "ZYX";
        osc_reply(sender_ip, sender_port, reply_adr,
                  String("TARE SET (") + order_name + ")");
        status_reporter().info("tare", String("Tare captured — decomposition: ") + order_name);
        return;
    }

    if (norm_adr == "/tare/reset") {
        tare_qi = 0.0f; tare_qj = 0.0f; tare_qk = 0.0f; tare_qr = 1.0f;
        tare_active = false;
        euler_order = 0;  // back to default ZYX
        twist_nx = 1.0f; twist_ny = 0.0f; twist_nz = 0.0f;
        tare_up_x = 0.0f; tare_up_y = 0.0f; tare_up_z = 1.0f;
        osc_reply(sender_ip, sender_port, reply_adr, "TARE RESET");
        status_reporter().info("tare", "Tare cleared — decomposition: ZYX");
        return;
    }

    if (norm_adr == "/tare/status") {
        osc_reply(sender_ip, sender_port, reply_adr,
                  tare_active ? "TARE ACTIVE" : "TARE INACTIVE");
        return;
    }

    if (norm_adr == "/dedup") {
        String arg = osc_msg.nextAsString();
        String a = osc_lower_copy(osc_trim_copy(String(arg)));
        if (a == "on" || a == "1" || a == "true") {
            set_dedup_enabled(true);
            osc_reply(sender_ip, sender_port, reply_adr, "DEDUP ON");
            status_reporter().info("engine", "Duplicate suppression enabled");
        } else if (a == "off" || a == "0" || a == "false") {
            set_dedup_enabled(false);
            osc_reply(sender_ip, sender_port, reply_adr, "DEDUP OFF");
            status_reporter().info("engine", "Duplicate suppression disabled");
        } else {
            String state = get_dedup_enabled() ? "ON" : "OFF";
            osc_reply(sender_ip, sender_port, reply_adr, "DEDUP " + state);
        }
        return;
    }

    // ── DIRECT COMMAND ────────────────────────────────────────────────────
    //    One-step: create a message + scene, add the message, and start
    //    sending — all from a single OSC command.
    //
    //    Address:  /annieData{dev}/direct/{name}
    //    Payload:  config string with value, ip, port, adr, and optionally
    //              period, low, high.
    //
    //    Creates a message named "{name}" and a scene named "{name}" (or
    //    updates them if they exist).  The message gets the sensor value and
    //    destination fields.  The scene gets the destination and period.
    //    The message is added to the scene, and the scene is started.
    //
    //    Example:
    //      /annieData/bart/direct/mySetup
    //      "value:accelX, ip:192.168.1.50, port:9000, adr:/sensor/x, period:50"

    if (norm_adr.startsWith("/direct")) {
        // Extract the name from the original address (preserves user's case).
        String dname = address.substring(8);  // after "/direct/"
        // Strip leading slash if present.
        if (dname.startsWith("/")) dname = dname.substring(1);
        dname.trim();
        if (dname.length() == 0) {
            status_reporter().error("cmd", "direct requires a name: /direct/{name}");
            return;
        }

        // Parse the config string.
        String cfg_str = osc_msg.nextAsString();
        OscMessage parsed;
        String err;
        if (!parsed.from_config_str(cfg_str, &err)) {
            status_reporter().warning("direct", "Parse warning: " + err);
        }

        // Extract period from config string (from_config_str doesn't handle it).
        unsigned int period_ms = 50;  // default 50 ms = 20 Hz
        {
            String lower_cfg = cfg_str;
            lower_cfg.toLowerCase();
            int pi = lower_cfg.indexOf("period:");
            if (pi < 0) pi = lower_cfg.indexOf("period-");
            if (pi >= 0) {
                int vstart = pi + 7;
                int vend = lower_cfg.indexOf(',', vstart);
                String pval = (vend < 0) ? cfg_str.substring(vstart)
                                         : cfg_str.substring(vstart, vend);
                pval.trim();
                int pms = pval.toInt();
                if (pms > 0) period_ms = clamp_scene_period_ms(pms);
            }
        }

        reg.lock();

        // Create or update the message.
        OscMessage* m = reg.get_or_create_msg(dname);
        if (!m) {
            status_reporter().error("direct", "Registry full (messages)");
            reg.unlock();
            return;
        }
        // Apply parsed fields to the message.
        if (parsed.exist.ip)   { m->ip = parsed.ip;                   m->exist.ip   = true; }
        if (parsed.exist.port) { m->port = parsed.port;               m->exist.port = true; }
        if (parsed.exist.adr)  { m->osc_address = parsed.osc_address; m->exist.adr  = true; }
        if (parsed.exist.val)  { m->value_ptr = parsed.value_ptr;     m->exist.val  = true; }
        if (parsed.exist.low)  { m->bounds[0] = parsed.bounds[0];     m->exist.low  = true; }
        if (parsed.exist.high) { m->bounds[1] = parsed.bounds[1];     m->exist.high = true; }
        m->enabled = true;

        // Create or update the scene.
        OscScene* p = reg.get_or_create_scene(dname);
        if (!p) {
            status_reporter().error("direct", "Registry full (scenes)");
            reg.unlock();
            return;
        }
        if (parsed.exist.ip)   { p->ip = parsed.ip;                   p->exist.ip   = true; }
        if (parsed.exist.port) { p->port = parsed.port;               p->exist.port = true; }
        if (parsed.exist.adr)  { p->osc_address = parsed.osc_address; p->exist.adr  = true; }
        if (parsed.exist.low)  { p->bounds[0] = parsed.bounds[0];     p->exist.low  = true; }
        if (parsed.exist.high) { p->bounds[1] = parsed.bounds[1];     p->exist.high = true; }
        p->send_period_ms = period_ms;

        // Add the message to the scene (no-op if already there).
        int mi = reg.msg_index(m);
        p->add_msg(mi);
        m->add_scene(p);

        // Start the scene (stop first if already running to pick up changes).
        if (p->task_handle) {
            stop_scene(p);
        }
        start_scene(p);

        reg.unlock();

        status_reporter().info("direct", "Created and started '" + dname
                               + "' → " + (parsed.exist.ip ? parsed.ip.toString() : "?")
                               + ":" + (parsed.exist.port ? String(parsed.port) : "?")
                               + " @ " + String(period_ms) + "ms");
        return;
    }

    // ── STATUS COMMANDS ────────────────────────────────────────────────────

    if (norm_adr.startsWith("/status")) {
        String sub = normalise_cmd(address.substring(7));  // after "/status"
        if (sub == "/config" || sub == "/configure") {
            // Payload: "ip:x.x.x.x, port:NNNN, adr:/some/address"
            OscMessage cfg;
            String err;
            if (!cfg.from_config_str(osc_msg.nextAsString(), &err)) {
                status_reporter().error("status", "Bad config: " + err);
                return;
            }
            IPAddress    sip  = cfg.exist.ip   ? cfg.ip   : sender_ip;
            unsigned int sp   = cfg.exist.port ? cfg.port : sender_port;
            String       sadr = cfg.exist.adr  ? cfg.osc_address : "/status";
            status_reporter().configure(sip, sp, sadr);
            status_reporter().info("status", "Status reporter configured");
            return;
        }
        if (sub == "/level") {
            StatusLevel lvl = status_level_from_string(osc_msg.nextAsString());
            status_reporter().set_level(lvl);
            status_reporter().info("status", String("Level set to ") + status_level_label(lvl));
            return;
        }
        status_reporter().warning("cmd", "Unknown status command: " + sub);
        return;
    }

    // ── LIST COMMANDS ──────────────────────────────────────────────────────

    if (norm_adr.startsWith("/list")) {
        String sub = normalise_cmd(address.substring(5));  // after "/list"
        // Optional "verbose" flag: any string argument → verbose.
        bool verbose = false;
        // Check if there is a string argument.
        const char* arg = nullptr;
        // MicroOsc: try to read a string argument (safe even if none present).
        arg = osc_msg.nextAsString();
        if (arg && (String(arg) == "verbose" || String(arg) == "v"
                    || String(arg) == "1" || String(arg) == "true")) {
            verbose = true;
        }

        IPAddress reply_ip = sender_ip;
        unsigned int reply_port = sender_port;
        if (status_reporter().configured && status_reporter().dest_port != 0) {
            reply_ip = status_reporter().dest_ip;
            reply_port = status_reporter().dest_port;
        }
        // Defensive: drop replies if the sender port was missing/invalid or a bad status config left port at 0.
        if (reply_port == 0) {
            Serial.println("  → list: reply port is 0; not sending response");
            status_reporter().warning("list", "No reply port available for list response");
            return;
        }
        const size_t LIST_LOG_RESERVE_BYTES = 160;  // ~80 label chars + two IPs + two ports + sub name
        String sub_label = sub.isEmpty() ? "(all)" : sub;
        String log;
        log.reserve(LIST_LOG_RESERVE_BYTES);
        log += "  → list sub='";
        log += sub_label;
        log += "' verbose=";
        log += verbose ? "true" : "false";
        log += " sender=";
        log += sender_ip.toString();
        log += ":";
        log += sender_port;
        log += " dest=";
        log += reply_ip.toString();
        log += ":";
        log += reply_port;
        Serial.println(log);

        reg.lock();

        if (sub == "/msgs" || sub == "/messages") {
            String result = "Messages (" + String(reg.msg_count) + "):";
            if (reg.msg_count == 0) {
                result += " none\n";
            } else {
                result += "\n";
            }
            for (uint16_t i = 0; i < reg.msg_count; i++) {
                result += "  " + reg.messages[i].to_info_string(verbose) + "\n";
            }
            osc_reply(reply_ip, reply_port, reply_adr + "/list/msgs", result);
        } else if (sub == "/scenes") {
            String result = "Scenes (" + String(reg.scene_count) + "):";
            if (reg.scene_count == 0) {
                result += " none\n";
            } else {
                result += "\n";
            }
            for (uint16_t i = 0; i < reg.scene_count; i++) {
                result += "  " + reg.scenes[i].to_info_string(verbose) + "\n";
            }
            osc_reply(reply_ip, reply_port, reply_adr + "/list/scenes", result);
        } else if (sub == "/all" || sub == "") {
            String result = "";
            if (verbose) {
                result += "dedup:" + String(get_dedup_enabled() ? "on" : "off") + "\n";
            }
            result += "Scenes (" + String(reg.scene_count) + "):";
            if (reg.scene_count == 0) {
                result += " none\n";
            } else {
                result += "\n";
            }
            for (uint16_t i = 0; i < reg.scene_count; i++) {
                result += "  " + reg.scenes[i].to_info_string(verbose) + "\n";
            }
            result += "Messages (" + String(reg.msg_count) + "):";
            if (reg.msg_count == 0) {
                result += " none\n";
            } else {
                result += "\n";
            }
            for (uint16_t i = 0; i < reg.msg_count; i++) {
                result += "  " + reg.messages[i].to_info_string(verbose) + "\n";
            }
            osc_reply(reply_ip, reply_port, reply_adr + "/list/all", result);
        } else {
            Serial.println("  → list: unknown target '" + sub + "'");
            status_reporter().warning("cmd", "Unknown list target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── CLONE COMMANDS ─────────────────────────────────────────────────────
    //    Payload: single CSV string "sourceName, destName"

    if (norm_adr.startsWith("/clone")) {
        String sub = normalise_cmd(address.substring(6));  // after "/clone"
        String arg = osc_msg.nextAsString();
        arg.trim();
        int comma = arg.indexOf(',');
        if (comma < 0) {
            status_reporter().error("cmd", "clone requires a CSV string: \"source, destination\"");
            return;
        }
        String src_name  = osc_trim_copy(arg.substring(0, comma));
        String dest_name = osc_trim_copy(arg.substring(comma + 1));
        if (src_name.length() == 0 || dest_name.length() == 0) {
            status_reporter().error("cmd", "clone requires two non-empty names: \"source, destination\"");
            return;
        }

        reg.lock();

        if (sub == "/msg" || sub == "/message") {
            OscMessage* src = reg.find_msg(src_name);
            if (!src) {
                status_reporter().error("cmd", "clone/msg: source '" + src_name + "' not found");
                reg.unlock();
                return;
            }
            OscMessage* dest = reg.get_or_create_msg(dest_name);
            if (!dest) {
                status_reporter().error("cmd", "clone/msg: registry full");
                reg.unlock();
                return;
            }
            // Copy all fields except name.
            *dest = *src;
            dest->name = dest_name;
            dest->exist.name = true;
            status_reporter().info("cmd", "Cloned msg '" + src_name + "' → '" + dest_name + "'");
        } else if (sub == "/scene") {
            OscScene* src = reg.find_scene(src_name);
            if (!src) {
                status_reporter().error("cmd", "clone/scene: source '" + src_name + "' not found");
                reg.unlock();
                return;
            }
            OscScene* dest = reg.get_or_create_scene(dest_name);
            if (!dest) {
                status_reporter().error("cmd", "clone/scene: registry full");
                reg.unlock();
                return;
            }
            // Copy config but not task state.
            dest->ip             = src->ip;
            dest->port           = src->port;
            dest->osc_address    = src->osc_address;
            dest->bounds[0]      = src->bounds[0];
            dest->bounds[1]      = src->bounds[1];
            dest->send_period_ms = clamp_scene_period_ms(src->send_period_ms);
            dest->address_mode   = src->address_mode;
            dest->overrides      = src->overrides;
            dest->exist          = src->exist;
            dest->exist.name     = true;
            dest->name           = dest_name;
            // Copy message list.
            dest->msg_count = src->msg_count;
            memcpy(dest->msg_indices, src->msg_indices,
                   src->msg_count * sizeof(int));
            status_reporter().info("cmd", "Cloned scene '" + src_name + "' → '" + dest_name + "'");
        } else {
            status_reporter().warning("cmd", "Unknown clone target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── RENAME COMMANDS ────────────────────────────────────────────────────
    //    Payload: single CSV string "oldName, newName"

    if (norm_adr.startsWith("/rename")) {
        String sub = normalise_cmd(address.substring(7));  // after "/rename"
        String arg = osc_msg.nextAsString();
        arg.trim();
        int comma = arg.indexOf(',');
        if (comma < 0) {
            status_reporter().error("cmd", "rename requires a CSV string: \"oldName, newName\"");
            return;
        }
        String old_name = osc_trim_copy(arg.substring(0, comma));
        String new_name = osc_trim_copy(arg.substring(comma + 1));
        if (old_name.length() == 0 || new_name.length() == 0) {
            status_reporter().error("cmd", "rename requires two non-empty names: \"oldName, newName\"");
            return;
        }

        reg.lock();

        if (sub == "/msg" || sub == "/message") {
            OscMessage* m = reg.find_msg(old_name);
            if (!m) {
                status_reporter().error("cmd", "rename/msg: '" + old_name + "' not found");
            } else {
                m->name = new_name;
                status_reporter().info("cmd", "Renamed msg '" + old_name + "' → '" + new_name + "'");
            }
        } else if (sub == "/scene") {
            OscScene* p = reg.find_scene(old_name);
            if (!p) {
                status_reporter().error("cmd", "rename/scene: '" + old_name + "' not found");
            } else {
                p->name = new_name;
                status_reporter().info("cmd", "Renamed scene '" + old_name + "' → '" + new_name + "'");
            }
        } else {
            status_reporter().warning("cmd", "Unknown rename target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── MOVE COMMAND ───────────────────────────────────────────────────────
    //    Payload: single CSV string "msgName, sceneName"

    if (norm_adr == "/move") {
        String arg = osc_msg.nextAsString();
        arg.trim();
        int comma = arg.indexOf(',');
        if (comma < 0) {
            status_reporter().error("cmd", "move requires a CSV string: \"msgName, sceneName\"");
            return;
        }
        String msg_name   = osc_trim_copy(arg.substring(0, comma));
        String scene_name = osc_trim_copy(arg.substring(comma + 1));
        if (msg_name.length() == 0 || scene_name.length() == 0) {
            status_reporter().error("cmd", "move requires two non-empty names: \"msgName, sceneName\"");
            return;
        }

        reg.lock();

        OscMessage* m = reg.find_msg(msg_name);
        OscScene*   p = reg.find_scene(scene_name);
        if (!m) {
            status_reporter().error("cmd", "move: msg '" + msg_name + "' not found");
            reg.unlock();
            return;
        }
        if (!p) {
            status_reporter().error("cmd", "move: scene '" + scene_name + "' not found");
            reg.unlock();
            return;
        }

        int mi = reg.msg_index(m);

        // Remove from all current scenes.
        for (uint8_t si = 0; si < m->scene_count; si++) {
            if (m->scenes[si]) m->scenes[si]->remove_msg(mi);
        }
        m->clear_scenes();

        // Add to new scene.
        p->add_msg(mi);
        m->add_scene(p);

        status_reporter().info("cmd", "Moved msg '" + msg_name
                               + "' → scene '" + scene_name + "'");
        reg.unlock();
        return;
    }

    // ── SAVE COMMANDS ──────────────────────────────────────────────────────
    //    /save          — save all scenes and messages to NVS
    //    /save/all      — same as /save
    //    /save/msg      — save one message (payload: name)
    //    /save/scene    — save one scene (payload: name)

    if (norm_adr.startsWith("/save")) {
        String sub = normalise_cmd(address.substring(5));  // after "/save"
        reg.lock();

        if (sub == "" || sub == "/all") {
            int n = nvs_save_all();
            status_reporter().info("nvs", "Saved " + String(n) + " objects to NVS");
            osc_reply(sender_ip, sender_port, reply_adr + "/save", "Saved " + String(n) + " objects");
        } else if (sub == "/msg" || sub == "/message") {
            String name = osc_trim_copy(osc_msg.nextAsString());
            if (name.length() == 0) {
                status_reporter().error("nvs", "save/msg requires a message name");
            } else if (nvs_save_msg(name)) {
                status_reporter().info("nvs", "Saved msg '" + name + "' to NVS");
            } else {
                status_reporter().error("nvs", "save/msg: '" + name + "' not found");
            }
        } else if (sub == "/scene") {
            String name = osc_trim_copy(osc_msg.nextAsString());
            if (name.length() == 0) {
                status_reporter().error("nvs", "save/scene requires a scene name");
            } else if (nvs_save_scene(name)) {
                status_reporter().info("nvs", "Saved scene '" + name + "' to NVS");
            } else {
                status_reporter().error("nvs", "save/scene: '" + name + "' not found");
            }
        } else {
            status_reporter().warning("cmd", "Unknown save target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── LOAD COMMANDS ──────────────────────────────────────────────────────
    //    /load          — load all scenes and messages from NVS
    //    /load/all      — same as /load

    if (norm_adr.startsWith("/load")) {
        String sub = normalise_cmd(address.substring(5));  // after "/load"
        reg.lock();

        if (sub == "" || sub == "/all") {
            int n = nvs_load_all();
            status_reporter().info("nvs", "Loaded " + String(n) + " objects from NVS");
            osc_reply(sender_ip, sender_port, reply_adr + "/load", "Loaded " + String(n) + " objects");
        } else {
            status_reporter().warning("cmd", "Unknown load target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── NVS COMMANDS ───────────────────────────────────────────────────────
    //    /nvs/clear     — erase all saved OSC data from NVS

    if (norm_adr.startsWith("/nvs")) {
        String sub = normalise_cmd(address.substring(4));  // after "/nvs"
        if (sub == "/clear") {
            nvs_clear_osc_data();
            status_reporter().info("nvs", "Cleared all saved OSC data from NVS");
            osc_reply(sender_ip, sender_port, reply_adr + "/nvs", "NVS cleared");
        } else {
            status_reporter().warning("cmd", "Unknown nvs command: " + sub);
        }
        return;
    }

    // ── ORI COMMANDS (/annieData{dev}/ori/...) ──────────────────────────────
    if (norm_adr.startsWith("/ori/") || norm_adr == "/ori") {
        String ori_rest = (norm_adr == "/ori") ? String("") : norm_adr.substring(4);
        Serial.println("  → ori command, sub=" + ori_rest);
        // Also extract original-case name from the address.
        String ori_rest_orig = "";
        if (address.length() > 4 && address.startsWith("/ori")) {
            ori_rest_orig = address.substring(4);
        }

        OriTracker& ot = ori_tracker();

        // /ori/register/{name}  — pre-register a named slot with color.
        //   Payload: "r,g,b"  (optional; defaults to auto-palette color)
        //   Creates the slot on the device so it appears in the Button B
        //   cycle immediately.  Button A captures the orientation later.
        if (ori_rest.startsWith("/register")) {
            String ori_name;
            if (ori_rest.length() > 9 && ori_rest.charAt(9) == '/') {
                ori_name = ori_rest_orig.length() > 10 ? ori_rest_orig.substring(10) : String("");
                ori_name.trim();
            }
            const char* typetags = osc_msg.getTypeTags();
            if (ori_name.length() == 0 && typetags && typetags[0] == 's') {
                ori_name = String(osc_msg.nextAsString());
                ori_name.trim();
            }
            if (ori_name.length() == 0) {
                status_reporter().warning("ori", "register: no name given");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/register", "ERROR: no name");
                return;
            }

            // Parse optional "r,g,b" color from payload.
            uint8_t cr = 255, cg = 255, cb = 255;
            bool have_color = false;
            if (typetags) {
                // Find the string arg for RGB (may be first or second arg).
                const char* tt = typetags;
                const char* rgb_str = nullptr;
                if (tt[0] == 's') {
                    // If name came from address, this is the color string.
                    // If name came from payload, there may be a second arg.
                    if (ori_rest.length() > 10) {
                        rgb_str = osc_msg.nextAsString();  // first arg = color
                    } else {
                        // name came from first string arg — peek second
                        osc_msg.nextAsString();  // skip name
                        if (tt[1] == 's') rgb_str = osc_msg.nextAsString();
                    }
                }
                if (rgb_str) {
                    String rs = String(rgb_str);
                    int c1 = rs.indexOf(',');
                    if (c1 > 0) {
                        int c2 = rs.indexOf(',', c1 + 1);
                        if (c2 > 0) {
                            cr = (uint8_t)rs.substring(0, c1).toInt();
                            cg = (uint8_t)rs.substring(c1 + 1, c2).toInt();
                            cb = (uint8_t)rs.substring(c2 + 1).toInt();
                            have_color = true;
                        }
                    }
                }
            }

            int idx;
            if (have_color) {
                idx = ot.register_ori(ori_name, cr, cg, cb);
            } else {
                // Auto-assign palette color.
                uint8_t ci = ot.next_color_index % ORI_PALETTE_SIZE;
                idx = ot.register_ori(ori_name,
                                      ORI_PALETTE[ci][0],
                                      ORI_PALETTE[ci][1],
                                      ORI_PALETTE[ci][2]);
                if (idx >= 0) ot.next_color_index++;
            }

            if (idx >= 0) {
                status_reporter().info("ori", "Registered ori '" + ori_name + "' (slot " + String(idx) + ")");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/register", "Registered: " + ori_name);
            } else {
                status_reporter().warning("ori", "Could not register ori '" + ori_name + "' (full?)");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/register", "ERROR: ori slots full");
            }
            return;
        }

        // /ori/save  or  /ori/save/{name}
        if (ori_rest.startsWith("/save")) {
            String ori_name;
            if (ori_rest.length() > 5 && ori_rest.charAt(5) == '/') {
                // Extract name from original-case address.
                int slash = ori_rest_orig.indexOf('/', 6);
                ori_name = (slash < 0) ? ori_rest_orig.substring(6)
                                       : ori_rest_orig.substring(6, slash);
                ori_name.trim();
            }
            // If payload provides a name, use that instead.
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 's') {
                ori_name = String(osc_msg.nextAsString());
                ori_name.trim();
            }

            int idx;
            if (ori_name.length() > 0) {
                idx = ot.save(ori_name, cur_qi, cur_qj, cur_qk, cur_qr);
            } else {
                idx = ot.save_auto(cur_qi, cur_qj, cur_qk, cur_qr);
                if (idx >= 0) ori_name = ot.oris[idx].name;
            }

            if (idx >= 0) {
                status_reporter().info("ori", "Saved ori '" + ori_name + "' (idx " + String(idx) + ")");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/save", "Saved: " + ori_name);
            } else {
                status_reporter().warning("ori", "Could not save ori (full?)");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/save", "ERROR: ori slots full");
            }
            return;
        }

        // /ori/delete/{name}
        if (ori_rest.startsWith("/delete")) {
            String ori_name;
            if (ori_rest.length() > 8 && ori_rest.charAt(7) == '/') {
                ori_name = ori_rest_orig.substring(8);
                ori_name.trim();
            }
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 's') {
                ori_name = String(osc_msg.nextAsString());
                ori_name.trim();
            }
            if (ori_name.length() == 0) {
                status_reporter().warning("ori", "delete: no name given");
                return;
            }
            if (ot.remove(ori_name)) {
                status_reporter().info("ori", "Deleted ori '" + ori_name + "'");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/delete", "Deleted: " + ori_name);
            } else {
                status_reporter().warning("ori", "Ori '" + ori_name + "' not found");
            }
            return;
        }

        // /ori/clear
        if (ori_rest == "/clear") {
            ot.clear();
            status_reporter().info("ori", "All oris cleared");
            osc_reply(sender_ip, sender_port, reply_adr + "/ori/clear", "All oris cleared");
            return;
        }

        // /ori/list
        if (ori_rest == "/list") {
            String listing = ot.list();
            status_reporter().info("ori", "Saved oris: " + listing);
            osc_reply(sender_ip, sender_port, reply_adr + "/ori/list", listing);
            return;
        }

        // /ori/threshold  (set motion gate threshold in rad/s)
        //   Accepts int ('i'), float ('f'), or quoted string ('s') argument.
        //   Handles both comma-prefixed (",f") and bare ("f") typetag formats.
        if (ori_rest == "/threshold" || ori_rest.startsWith("/threshold")) {
            const char* typetags = osc_msg.getTypeTags();
            bool have_val = false;
            float new_threshold = 0.0f;
            if (typetags && typetags[0] == ',' && (typetags[1] == 'i' || typetags[1] == 'f')) {
                new_threshold = osc_msg.nextAsFloat();
                have_val = true;
            } else if (typetags && (typetags[0] == 'f' || typetags[0] == 'i')) {
                new_threshold = osc_msg.nextAsFloat();
                have_val = true;
            }
            if (!have_val) {
                const char* raw = osc_msg.nextAsString();
                if (raw) {
                    String ts = String(raw);
                    ts.trim();
                    if (ts.length() > 0) {
                        new_threshold = ts.toFloat();
                        have_val = true;
                    }
                }
            }
            if (have_val) ot.motion_threshold = new_threshold;
            status_reporter().info("ori", "Motion threshold: " + String(ot.motion_threshold, 2) + " rad/s");
            osc_reply(sender_ip, sender_port, reply_adr + "/ori/threshold",
                      "threshold: " + String(ot.motion_threshold, 2));
            return;
        }

        // /ori/active  — query the current active ori
        if (ori_rest == "/active") {
            String info = (ot.active_ori_index >= 0)
                ? ot.active_ori_name
                : String("(none)");
            status_reporter().info("ori", "Active ori: " + info);
            osc_reply(sender_ip, sender_port, reply_adr + "/ori/active", info);
            return;
        }

        // /ori/reset/{name}  — reset a range ori back to a fresh single point
        if (ori_rest.startsWith("/reset")) {
            String ori_name;
            if (ori_rest.length() > 6 && ori_rest.charAt(6) == '/') {
                ori_name = ori_rest_orig.length() > 7 ? ori_rest_orig.substring(7) : String("");
                ori_name.trim();
            }
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 's') {
                ori_name = String(osc_msg.nextAsString());
                ori_name.trim();
            }
            if (ori_name.length() == 0) {
                status_reporter().warning("ori", "reset: no name given");
                return;
            }
            int idx = ot.reset(ori_name);
            if (idx >= 0) {
                status_reporter().info("ori", "Cleared samples for ori '" + ori_name + "' (ready to re-record)");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/reset", "Reset: " + ori_name);
            } else {
                status_reporter().warning("ori", "Ori '" + ori_name + "' not found");
            }
            return;
        }

        // /ori/info/{name}  — show ori details (center, half_width, samples)
        if (ori_rest.startsWith("/info")) {
            String ori_name;
            if (ori_rest.length() > 5 && ori_rest.charAt(5) == '/') {
                ori_name = ori_rest_orig.length() > 6 ? ori_rest_orig.substring(6) : String("");
                ori_name.trim();
            }
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 's') {
                ori_name = String(osc_msg.nextAsString());
                ori_name.trim();
            }
            if (ori_name.length() == 0) {
                status_reporter().warning("ori", "info: no name given");
                return;
            }
            String info_str = ot.info(ori_name);
            status_reporter().info("ori", info_str);
            IPAddress info_ip = sender_ip;
            unsigned int info_port = sender_port;
            if (status_reporter().configured && status_reporter().dest_port != 0) {
                info_ip = status_reporter().dest_ip;
                info_port = status_reporter().dest_port;
            }
            osc_reply(info_ip, info_port, reply_adr + "/ori/info", info_str);
            return;
        }

        // /ori/tolerance  — set/query the angular match tolerance (degrees)
        if (ori_rest == "/tolerance" || ori_rest.startsWith("/tolerance/")) {
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 'f') {
                ot.ori_tolerance = osc_msg.nextAsFloat();
            } else if (typetags && typetags[0] == 's') {
                ot.ori_tolerance = String(osc_msg.nextAsString()).toFloat();
            }
            status_reporter().info("ori", "Match tolerance: " + String(ot.ori_tolerance, 1) + " deg");
            osc_reply(sender_ip, sender_port, reply_adr + "/ori/tolerance",
                      "tolerance: " + String(ot.ori_tolerance, 1) + " deg");
            return;
        }

        // /ori/strict  — toggle strict matching mode (on/off)
        if (ori_rest == "/strict" || ori_rest.startsWith("/strict/")) {
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 's') {
                String val = String(osc_msg.nextAsString());
                val.trim(); val.toLowerCase();
                ot.strict_matching = (val == "on" || val == "true" || val == "1" || val == "yes");
            } else if (typetags && typetags[0] == 'f') {
                ot.strict_matching = (osc_msg.nextAsFloat() > 0.5f);
            } else {
                // No payload — toggle.
                ot.strict_matching = !ot.strict_matching;
            }
            String state = ot.strict_matching ? "ON" : "OFF";
            status_reporter().info("ori", "Strict matching: " + state);
            osc_reply(sender_ip, sender_port, reply_adr + "/ori/strict", "strict: " + state);
            return;
        }

        // /ori/color/{name}  — set the LED color of an ori ("r,g,b" payload)
        if (ori_rest.startsWith("/color")) {
            String ori_name;
            if (ori_rest.length() > 6 && ori_rest.charAt(6) == '/') {
                ori_name = ori_rest_orig.length() > 7 ? ori_rest_orig.substring(7) : String("");
                ori_name.trim();
            }
            const char* typetags = osc_msg.getTypeTags();
            if (ori_name.length() == 0 && typetags && typetags[0] == 's') {
                // Payload could be "name, r, g, b" or just "r,g,b" if name is in address.
                ori_name = String(osc_msg.nextAsString());
                ori_name.trim();
            }
            if (ori_name.length() == 0) {
                status_reporter().warning("ori", "color: no name given");
                return;
            }
            // Parse RGB from payload string: "r,g,b" or "r g b"
            String rgb_str;
            if (typetags) {
                const char* tt = osc_msg.getTypeTags();
                // Check if there's another string arg (the name was first arg, rgb is second)
                if (tt && strlen(tt) >= 2 && tt[1] == 's') {
                    rgb_str = String(osc_msg.nextAsString());
                } else {
                    // Name might contain the color: check if ori_name looks like "r,g,b"
                    // Actually, let's just use payload as the color string.
                    rgb_str = ori_name;
                    // Re-extract name from address
                    if (ori_rest.length() > 7) {
                        ori_name = ori_rest_orig.length() > 7 ? ori_rest_orig.substring(7) : String("");
                        ori_name.trim();
                    }
                }
            }
            // If rgb_str is empty, try the single-string payload
            if (rgb_str.length() == 0 && typetags && typetags[0] == 's') {
                rgb_str = ori_name;  // fallback
            }

            // Parse the "r,g,b" string
            uint8_t r = 255, g = 255, b = 255;  // default white
            if (rgb_str.length() > 0) {
                int c1 = rgb_str.indexOf(',');
                if (c1 > 0) {
                    int c2 = rgb_str.indexOf(',', c1 + 1);
                    if (c2 > 0) {
                        r = (uint8_t)rgb_str.substring(0, c1).toInt();
                        g = (uint8_t)rgb_str.substring(c1 + 1, c2).toInt();
                        b = (uint8_t)rgb_str.substring(c2 + 1).toInt();
                    }
                }
            }
            if (!ot.set_color(ori_name, r, g, b)) {
                // Ori not found — auto-register it so /ori/color can be used
                // as a shorthand for /ori/register + /ori/color in one step.
                ot.register_ori(ori_name, r, g, b);
                status_reporter().info("ori", "Auto-registered ori '" + ori_name + "' with color ("
                    + String(r) + "," + String(g) + "," + String(b) + ")");
            } else {
                status_reporter().info("ori", "Set color of '" + ori_name + "' to ("
                    + String(r) + "," + String(g) + "," + String(b) + ")");
            }
            osc_reply(sender_ip, sender_port, reply_adr + "/ori/color",
                      ori_name + ": " + String(r) + "," + String(g) + "," + String(b));
            return;
        }

        // /ori/select/{name}  — select an ori for button editing
        if (ori_rest.startsWith("/select")) {
            String ori_name;
            if (ori_rest.length() > 7 && ori_rest.charAt(7) == '/') {
                ori_name = ori_rest_orig.length() > 8 ? ori_rest_orig.substring(8) : String("");
                ori_name.trim();
            }
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 's') {
                ori_name = String(osc_msg.nextAsString());
                ori_name.trim();
            }
            if (ori_name.length() == 0) {
                status_reporter().warning("ori", "select: no name given");
                return;
            }
            int idx = ot.find(ori_name);
            if (idx >= 0) {
                ot.selected_ori_index = idx;
                status_reporter().info("ori", "Selected ori '" + ori_name + "' for button editing");
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/select", "Selected: " + ori_name);
            } else {
                status_reporter().warning("ori", "Ori '" + ori_name + "' not found");
            }
            return;
        }

        // /ori/record/start/{name}  — begin a timed recording session
        // /ori/record/stop          — finalize (auto-axis detect + farthest-first subsample)
        // /ori/record/cancel        — discard recording without saving
        // /ori/record/status        — query session state
        if (ori_rest.startsWith("/record")) {
            String rec_sub      = ori_rest.substring(7);       // e.g. "/start/myName"
            String rec_sub_orig = ori_rest_orig.length() > 7
                                  ? ori_rest_orig.substring(7)
                                  : String("");

            if (rec_sub == "/start" || rec_sub.startsWith("/start/")) {
                String ori_name;
                if (rec_sub.length() > 7) {
                    // Name embedded in address: /ori/record/start/{name}
                    ori_name = rec_sub_orig.length() > 7 ? rec_sub_orig.substring(7) : String("");
                    ori_name.trim();
                }
                if (ori_name.length() == 0) {
                    const char* typetags = osc_msg.getTypeTags();
                    if (typetags && typetags[0] == 's') {
                        ori_name = String(osc_msg.nextAsString());
                        ori_name.trim();
                    }
                }
                if (ori_name.length() == 0) {
                    status_reporter().warning("ori", "record/start: no name given");
                    osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/start",
                              "ERROR: no name given");
                    return;
                }
                if (ot.start_recording(ori_name)) {
                    status_reporter().info("ori", "Recording started for '" + ori_name + "'");
                    osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/start",
                              "Recording: " + ori_name);
                } else {
                    status_reporter().warning("ori", "Recording already active for '"
                                             + ot.session.name + "'");
                    osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/start",
                              "ERROR: already recording '" + ot.session.name + "'");
                }
                return;
            }

            if (rec_sub == "/stop") {
                if (!ot.session.active) {
                    osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/stop",
                              "ERROR: no active recording");
                    return;
                }
                String stopped_name = ot.session.name;
                int n_stored = ot.stop_recording();
                int idx = ot.find(stopped_name);
                String result = "Saved: " + stopped_name
                              + ", samples: " + String(n_stored);
                if (idx >= 0) {
                    const SavedOri& o = ot.oris[idx];
                    if (o.use_axis) {
                        result += String(", axis: (")
                               + String(o.axis_x, 2) + ","
                               + String(o.axis_y, 2) + ","
                               + String(o.axis_z, 2) + ")";
                    } else {
                        result += ", mode: fullQ";
                    }
                }
                status_reporter().info("ori", result);
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/stop", result);
                return;
            }

            if (rec_sub == "/cancel") {
                if (!ot.session.active) {
                    osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/cancel",
                              "No active recording");
                } else {
                    String cancelled = ot.session.name;
                    ot.cancel_recording();
                    status_reporter().info("ori", "Recording cancelled for '" + cancelled + "'");
                    osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/cancel",
                              "Cancelled: " + cancelled);
                }
                return;
            }

            if (rec_sub == "/status") {
                String resp;
                if (ot.session.active) {
                    resp = "active:true,name:" + ot.session.name
                         + ",count:" + String(ot.session.count)
                         + ",elapsed:" + String(ot.session.elapsed_ms());
                } else {
                    resp = "active:false";
                }
                osc_reply(sender_ip, sender_port, reply_adr + "/ori/record/status", resp);
                return;
            }

            status_reporter().warning("ori", "Unknown record sub-command: " + rec_sub);
            return;
        }

        status_reporter().warning("cmd", "Unknown ori command: " + ori_rest);
        return;
    }

    // ── FLUSH COMMAND (/annieData{dev}/flush) ────────────────────────────────
    //    Since commands are processed sequentially, /flush arriving means all
    //    preceding commands have been handled.  Reply immediately.
    if (norm_adr == "/flush") {
        osc_reply(sender_ip, sender_port, reply_adr + "/flush", "OK");
        return;
    }

    // ── SHOW COMMANDS (/annieData{dev}/show/...) ────────────────────────────
    //    show/save/{name}    — snapshot current RAM state as a named NVS show
    //    show/load/{name}    — two-step: set pending, wait for confirm
    //    show/load/confirm   — execute the pending load
    //    show/list           — reply CSV of show names
    //    show/delete/{name}  — delete a named show from NVS
    //    show/rename         — payload "oldName, newName"

    if (norm_adr.startsWith("/show")) {
        String show_rest      = norm_adr.length() > 5 ? norm_adr.substring(5) : String("");
        String show_rest_orig = address.length() > 5  ? address.substring(5)  : String("");

        // Expire pending show load after 30 seconds.
        if (pending_show_name.length() > 0
            && (millis() - pending_show_ms) > 30000UL) {
            status_reporter().info("show", "Pending load of '"
                                   + pending_show_name + "' expired");
            pending_show_name = "";
            pending_show_ms   = 0;
        }

        // show/save/{name}
        if (show_rest.startsWith("/save/") || show_rest == "/save") {
            String show_name;
            if (show_rest.length() > 6) {
                show_name = show_rest_orig.length() > 6
                            ? show_rest_orig.substring(6) : String("");
                show_name.trim();
            }
            if (show_name.length() == 0) {
                const char* typetags = osc_msg.getTypeTags();
                if (typetags && typetags[0] == 's') {
                    show_name = String(osc_msg.nextAsString());
                    show_name.trim();
                }
            }
            if (show_name.length() == 0) {
                status_reporter().warning("show", "save: no name given");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/save",
                          "ERROR: no name given");
                return;
            }
            bool ok = nvs_save_show(show_name);
            if (ok) {
                status_reporter().info("show", "Saved show '" + show_name + "'");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/save",
                          "Saved show: " + show_name);
            } else {
                status_reporter().warning("show", "Could not save show '" + show_name
                                         + "' (full or NVS error)");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/save",
                          "ERROR: could not save '" + show_name + "'");
            }
            return;
        }

        // show/load/confirm  (must be checked before show/load/{name})
        if (show_rest == "/load/confirm" || show_rest == "/loadconfirm") {
            if (pending_show_name.length() == 0) {
                osc_reply(sender_ip, sender_port, reply_adr + "/show/load/confirm",
                          "ERROR: no pending load (send /show/load/{name} first)");
                return;
            }
            String sname = pending_show_name;
            pending_show_name = "";
            pending_show_ms   = 0;
            int n = nvs_load_show(sname);
            if (n >= 0) {
                status_reporter().info("show", "Loaded show '" + sname
                                      + "' (" + String(n) + " objects)");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/load/confirm",
                          "Loaded show: " + sname + " (" + String(n) + " objects)");
            } else {
                status_reporter().warning("show", "Load failed for show '" + sname + "'");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/load/confirm",
                          "ERROR: show '" + sname + "' not found");
            }
            return;
        }

        // show/load/{name}  — stage the pending load, reply with confirmation prompt
        if (show_rest.startsWith("/load/") || show_rest == "/load") {
            String show_name;
            if (show_rest.length() > 6) {
                show_name = show_rest_orig.length() > 6
                            ? show_rest_orig.substring(6) : String("");
                show_name.trim();
            }
            if (show_name.length() == 0) {
                const char* typetags = osc_msg.getTypeTags();
                if (typetags && typetags[0] == 's') {
                    show_name = String(osc_msg.nextAsString());
                    show_name.trim();
                }
            }
            if (show_name.length() == 0) {
                status_reporter().warning("show", "load: no name given");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/load",
                          "ERROR: no name given");
                return;
            }
            pending_show_name = show_name;
            pending_show_ms   = millis();
            status_reporter().info("show", "Pending load of '" + show_name
                                  + "' — send /show/load/confirm to execute");
            osc_reply(sender_ip, sender_port, reply_adr + "/show/load",
                      "PENDING: load '" + show_name + "'? Send /show/load/confirm");
            return;
        }

        // show/list
        if (show_rest == "/list") {
            String listing = nvs_list_shows();
            osc_reply(sender_ip, sender_port, reply_adr + "/show/list",
                      listing.length() > 0 ? listing : "(none)");
            return;
        }

        // show/delete/{name}
        if (show_rest.startsWith("/delete/") || show_rest == "/delete") {
            String show_name;
            if (show_rest.length() > 8) {
                show_name = show_rest_orig.length() > 8
                            ? show_rest_orig.substring(8) : String("");
                show_name.trim();
            }
            if (show_name.length() == 0) {
                const char* typetags = osc_msg.getTypeTags();
                if (typetags && typetags[0] == 's') {
                    show_name = String(osc_msg.nextAsString());
                    show_name.trim();
                }
            }
            if (show_name.length() == 0) {
                status_reporter().warning("show", "delete: no name given");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/delete",
                          "ERROR: no name given");
                return;
            }
            if (nvs_delete_show(show_name)) {
                status_reporter().info("show", "Deleted show '" + show_name + "'");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/delete",
                          "Deleted: " + show_name);
            } else {
                status_reporter().warning("show", "Show '" + show_name + "' not found");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/delete",
                          "ERROR: '" + show_name + "' not found");
            }
            return;
        }

        // show/rename  — payload "oldName, newName"
        if (show_rest == "/rename") {
            String arg = String(osc_msg.nextAsString());
            arg.trim();
            int comma = arg.indexOf(',');
            if (comma < 0) {
                status_reporter().error("show", "rename requires \"oldName, newName\"");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/rename",
                          "ERROR: expected \"oldName, newName\"");
                return;
            }
            String old_name = osc_trim_copy(arg.substring(0, comma));
            String new_name = osc_trim_copy(arg.substring(comma + 1));
            if (old_name.length() == 0 || new_name.length() == 0) {
                status_reporter().error("show", "rename: both names must be non-empty");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/rename",
                          "ERROR: empty name");
                return;
            }
            if (nvs_rename_show(old_name, new_name)) {
                status_reporter().info("show", "Renamed show '" + old_name
                                      + "' → '" + new_name + "'");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/rename",
                          "Renamed: " + old_name + " → " + new_name);
            } else {
                status_reporter().warning("show", "Could not rename '" + old_name + "'");
                osc_reply(sender_ip, sender_port, reply_adr + "/show/rename",
                          "ERROR: '" + old_name + "' not found");
            }
            return;
        }

        status_reporter().warning("cmd", "Unknown show command: " + show_rest);
        return;
    }

    // ── CATEGORY DISPATCH: /msg or /scene ──────────────────────────────────

    bool is_msg   = norm_adr.startsWith("/msg");
    bool is_scene = norm_adr.startsWith("/scene");

    if (!is_msg && !is_scene) {
        status_reporter().warning("cmd", "Unknown category in: " + address);
        return;
    }

    // Strip category prefix to get /{name}/{command}.
    if (is_msg)   address = address.substring(4);   // strip "/msg"
    if (is_scene) address = address.substring(6);    // strip "/scene"

    // Parse name and command from the remaining /{name}/{command}.
    //   /name/command  →  name, command
    //   /name          →  name, "assign"
    //   /name/         →  name, "assign"
    int last_slash = address.lastIndexOf('/');
    String name_mp, command;

    if (last_slash <= 0) {
        name_mp = address.substring(1);
        command = "assign";
    } else if (last_slash == (int)address.length() - 1) {
        name_mp = address.substring(1, last_slash);
        command = "assign";
    } else {
        name_mp = address.substring(1, last_slash);
        command = address.substring(last_slash + 1);
    }

    if (name_mp.length() == 0) {
        status_reporter().warning("cmd", "Empty name in address");
        return;
    }

    command.toLowerCase();
    command = normalise_cmd(command);  // "addMsg" / "add_msg" / "addmsg" → "addmsg"
    Serial.println("  name=" + name_mp + "  cmd=" + command);

    // ════════════════════════════════════════════════════════════════════════
    // PATTERN MATCHING — OSC 1.0 wildcards in the {name} segment
    // ════════════════════════════════════════════════════════════════════════
    //
    // When the name contains pattern metacharacters (*, ?, [, {) the command
    // is applied to ALL matching entities.  The "assign" command is rejected
    // for patterns — you cannot create an entity named "*".

    if (osc_has_pattern(name_mp.c_str())) {

        // Reject create/update with pattern names.
        if (command == "assign") {
            status_reporter().warning("cmd", "Cannot create with pattern name '" + name_mp + "'");
            return;
        }

        reg.lock();

        if (is_scene) {
            OscScene* matches[MAX_OSC_SCENES];
            uint16_t n = reg.find_scenes_matching(name_mp.c_str(), matches, MAX_OSC_SCENES);
            if (n == 0) {
                status_reporter().warning("scene", "No scenes match pattern '" + name_mp + "'");
                reg.unlock();
                return;
            }

            // For delete: collect names first, then delete (avoids index invalidation).
            if (command == "delete" || command == "remove") {
                String names[MAX_OSC_SCENES];
                for (uint16_t i = 0; i < n; i++) names[i] = matches[i]->name;
                uint16_t deleted = 0;
                for (uint16_t i = 0; i < n; i++) {
                    if (reg.delete_scene(names[i])) deleted++;
                }
                status_reporter().info("scene", "Deleted " + String(deleted) + " scenes matching '" + name_mp + "'");
            }
            else if (command == "start" || command == "enable" || command == "go") {
                for (uint16_t i = 0; i < n; i++) start_scene(matches[i]);
                status_reporter().info("scene", "Started " + String(n) + " scenes matching '" + name_mp + "'");
            }
            else if (command == "stop" || command == "disable" || command == "mute") {
                for (uint16_t i = 0; i < n; i++) stop_scene(matches[i]);
                status_reporter().info("scene", "Stopped " + String(n) + " scenes matching '" + name_mp + "'");
            }
            else if (command == "info") {
                IPAddress info_ip = sender_ip;
                unsigned int info_port = sender_port;
                if (status_reporter().configured && status_reporter().dest_port != 0) {
                    info_ip = status_reporter().dest_ip;
                    info_port = status_reporter().dest_port;
                }
                for (uint16_t i = 0; i < n; i++) {
                    String info = matches[i]->to_info_string(true);
                    osc_reply(info_ip, info_port,
                              reply_adr + "/scene/" + matches[i]->name + "/info", info);
                }
            }
            else if (command == "period" || command == "rate") {
                int ms = 0;
                bool have_period = false;
                const char* typetags = osc_msg.getTypeTags();
                if (typetags && typetags[0] == ',' && (typetags[1] == 'i' || typetags[1] == 'f')) {
                    ms = (int)osc_msg.nextAsFloat(); have_period = true;
                }
                if (!have_period) {
                    const char* raw = osc_msg.nextAsString();
                    if (raw) { String s = String(raw); s.trim(); if (s.length() > 0) { ms = s.toInt(); have_period = true; } }
                }
                if (have_period && ms > 0) {
                    ms = clamp_scene_period_ms(ms);
                    for (uint16_t i = 0; i < n; i++) matches[i]->send_period_ms = ms;
                    status_reporter().info("scene", "Period set to " + String(ms) + " ms for " + String(n) + " scenes matching '" + name_mp + "'");
                } else {
                    status_reporter().warning("scene", "Period ignored (missing/invalid payload)");
                }
            }
            else if (command == "unsolo" || command == "unmute" || command == "enableall") {
                for (uint16_t i = 0; i < n; i++) {
                    OscScene* p = matches[i];
                    for (uint8_t j = 0; j < p->msg_count; j++) {
                        int mi = p->msg_indices[j];
                        if (mi >= 0 && mi < (int)reg.msg_count) reg.messages[mi].enabled = true;
                    }
                }
                status_reporter().info("scene", "Unsolo: enabled all msgs in " + String(n) + " scenes matching '" + name_mp + "'");
            }
            else {
                status_reporter().warning("cmd", "Pattern not supported for scene command: " + command);
            }

            reg.unlock();
            return;
        }

        if (is_msg) {
            OscMessage* matches[MAX_OSC_MESSAGES];
            uint16_t n = reg.find_msgs_matching(name_mp.c_str(), matches, MAX_OSC_MESSAGES);
            if (n == 0) {
                status_reporter().warning("msg", "No messages match pattern '" + name_mp + "'");
                reg.unlock();
                return;
            }

            if (command == "delete" || command == "remove") {
                String names[MAX_OSC_MESSAGES];
                for (uint16_t i = 0; i < n; i++) names[i] = matches[i]->name;
                uint16_t deleted = 0;
                for (uint16_t i = 0; i < n; i++) {
                    if (reg.delete_msg(names[i])) deleted++;
                }
                status_reporter().info("msg", "Deleted " + String(deleted) + " msgs matching '" + name_mp + "'");
            }
            else if (command == "enable" || command == "unmute") {
                for (uint16_t i = 0; i < n; i++) matches[i]->enabled = true;
                status_reporter().info("msg", "Enabled " + String(n) + " msgs matching '" + name_mp + "'");
            }
            else if (command == "disable" || command == "mute") {
                for (uint16_t i = 0; i < n; i++) matches[i]->enabled = false;
                status_reporter().info("msg", "Disabled " + String(n) + " msgs matching '" + name_mp + "'");
            }
            else if (command == "info") {
                IPAddress info_ip = sender_ip;
                unsigned int info_port = sender_port;
                if (status_reporter().configured && status_reporter().dest_port != 0) {
                    info_ip = status_reporter().dest_ip;
                    info_port = status_reporter().dest_port;
                }
                for (uint16_t i = 0; i < n; i++) {
                    osc_reply(info_ip, info_port,
                              reply_adr + "/msg/" + matches[i]->name + "/info",
                              matches[i]->to_info_string(true));
                }
            }
            else {
                status_reporter().warning("cmd", "Pattern not supported for msg command: " + command);
            }

            reg.unlock();
            return;
        }

        reg.unlock();
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    // SCENE COMMANDS
    // ════════════════════════════════════════════════════════════════════════

    if (is_scene) {
        reg.lock();

        // ── assign (create / update) ───────────────────────────────────────
        if (command == "assign") {
            OscScene* p = reg.get_or_create_scene(name_mp);
            if (!p) {
                status_reporter().error("scene", "Registry full");
                reg.unlock();
                return;
            }
            const char* arg = osc_msg.nextAsString();
            if (arg && strlen(arg) > 0) {
                OscMessage csv;
                String err;
                if (!csv.from_config_str(arg, &err)) {
                    status_reporter().warning("scene", "Parse warning: " + err);
                }
                if (csv.exist.ip)   { p->ip = csv.ip;                   p->exist.ip   = true; }
                if (csv.exist.port) { p->port = csv.port;               p->exist.port = true; }
                if (csv.exist.adr)  { p->osc_address = csv.osc_address; p->exist.adr  = true; }
                if (csv.exist.low)  { p->bounds[0] = csv.bounds[0];     p->exist.low  = true; }
                if (csv.exist.high) { p->bounds[1] = csv.bounds[1];     p->exist.high = true; }

                // Extract period from config string (from_config_str doesn't handle it).
                {
                    String cfg_lower = String(arg);
                    cfg_lower.toLowerCase();
                    int pi = cfg_lower.indexOf("period:");
                    if (pi < 0) pi = cfg_lower.indexOf("period-");
                    if (pi >= 0) {
                        int vstart = pi + 7;
                        int vend = cfg_lower.indexOf(',', vstart);
                        String pval = (vend < 0) ? String(arg).substring(vstart)
                                                 : String(arg).substring(vstart, vend);
                        pval.trim();
                        int pms = pval.toInt();
                        if (pms > 0) p->send_period_ms = clamp_scene_period_ms(pms);
                    }
                }
            }
            status_reporter().info("scene", "Scene '" + name_mp + "' updated");
        }

        // ── delete ─────────────────────────────────────────────────────────
        else if (command == "delete" || command == "remove") {
            if (reg.delete_scene(name_mp)) {
                status_reporter().info("scene", "Deleted scene '" + name_mp + "'");
            } else {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
            }
        }

        // ── start ──────────────────────────────────────────────────────────
        else if (command == "start" || command == "enable" || command == "go") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
            } else {
                start_scene(p);
            }
        }

        // ── stop ───────────────────────────────────────────────────────────
        else if (command == "stop" || command == "disable" || command == "mute") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
            } else {
                stop_scene(p);
            }
        }

        // ── addmsg ─────────────────────────────────────────────────────────
        //    Payload: comma-separated message names.
        else if (command == "addmsg" || command == "add") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String arg = osc_msg.nextAsString();
            // Split on commas.
            int start_pos = 0;
            while (start_pos < (int)arg.length()) {
                int comma = arg.indexOf(',', start_pos);
                String mname = (comma < 0) ? arg.substring(start_pos)
                                           : arg.substring(start_pos, comma);
                mname = osc_trim_copy(mname);
                start_pos = (comma < 0) ? arg.length() : comma + 1;
                if (mname.length() == 0) continue;

                OscMessage* m = reg.find_msg(mname);
                if (!m) {
                    status_reporter().warning("scene", "addmsg: msg '" + mname + "' not found");
                    continue;
                }
                int mi = reg.msg_index(m);
                p->add_msg(mi);
                m->add_scene(p);
                status_reporter().debug("scene", "Added msg '" + mname + "' to scene '" + name_mp + "'");
            }
            status_reporter().info("scene", "addmsg complete for '" + name_mp + "'");
        }

        // ── removemsg ──────────────────────────────────────────────────────
        else if (command == "removemsg" || command == "rmmsg") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String mname = osc_msg.nextAsString();
            mname = osc_trim_copy(mname);
            OscMessage* m = reg.find_msg(mname);
            if (!m) {
                status_reporter().warning("scene", "removemsg: msg '" + mname + "' not found");
            } else {
                int mi = reg.msg_index(m);
                p->remove_msg(mi);
                m->remove_scene(p);
                status_reporter().info("scene", "Removed msg '" + mname
                                       + "' from scene '" + name_mp + "'");
            }
        }

        // ── period ─────────────────────────────────────────────────────────
        //    Payload: a string or integer with the period in milliseconds.
        else if (command == "period" || command == "rate") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
            } else {
                // Accept either a numeric argument or a string like "50".
                int ms = 0;
                bool have_period = false;
                const char* typetags = osc_msg.getTypeTags();
                if (typetags && typetags[0] == ',' && (typetags[1] == 'i' || typetags[1] == 'f')) {
                    ms = (int)osc_msg.nextAsFloat();
                    have_period = true;
                }
                if (!have_period) {
                    const char* raw = osc_msg.nextAsString();
                    if (raw) {
                        String period_str = String(raw);
                        period_str.trim();
                        if (period_str.length() > 0) {
                            ms = period_str.toInt();
                            have_period = true;
                        }
                    }
                }

                if (!have_period || ms <= 0) {
                    status_reporter().warning("scene", "Period for '" + name_mp
                                              + "' ignored (missing/invalid payload)");
                } else {
                    p->send_period_ms = clamp_scene_period_ms(ms);
                    status_reporter().info("scene", "Period for '" + name_mp
                                           + "' set to " + String(p->send_period_ms) + " ms");
                }
            }
        }

        // ── override ───────────────────────────────────────────────────────
        //    Payload: comma-separated field names to toggle override on.
        //    Prefix with '-' to turn off.
        //    Examples:  "ip, port"        →  override ip and port
        //               "-ip"            →  stop overriding ip
        //               "low, high"      →  override output bounds (scale)
        //               "all"            →  override everything
        //               "none"           →  override nothing
        else if (command == "override") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String arg = osc_msg.nextAsString();
            arg.toLowerCase();
            arg.trim();

            if (arg == "all") {
                p->overrides.ip  = p->overrides.port = p->overrides.adr  = true;
                p->overrides.low = p->overrides.high = true;
            } else if (arg == "none" || arg == "clear") {
                p->overrides.ip  = p->overrides.port = p->overrides.adr  = false;
                p->overrides.low = p->overrides.high = false;
            } else {
                // Parse individual fields.
                int s = 0;
                while (s < (int)arg.length()) {
                    int c = arg.indexOf(',', s);
                    String f = (c < 0) ? arg.substring(s) : arg.substring(s, c);
                    f = osc_trim_copy(f);
                    s = (c < 0) ? arg.length() : c + 1;
                    if (f.length() == 0) continue;

                    bool enable = true;
                    if (f.startsWith("-")) { enable = false; f = f.substring(1); }
                    if (f.startsWith("+")) { f = f.substring(1); }

                    if (f == "ip")                                 p->overrides.ip   = enable;
                    else if (f == "port")                          p->overrides.port = enable;
                    else if (f == "adr" || f == "addr" || f == "address")
                                                                   p->overrides.adr  = enable;
                    else if (f == "low" || f == "min" || f == "lo")
                                                                   p->overrides.low  = enable;
                    else if (f == "high" || f == "max" || f == "hi")
                                                                   p->overrides.high = enable;
                    else if (f == "scale" || f == "bounds") {
                        p->overrides.low = enable;
                        p->overrides.high = enable;
                    }
                    else status_reporter().warning("scene", "Unknown override field: " + f);
                }
            }
            status_reporter().info("scene", "Override for '" + name_mp + "': ip="
                                   + String(p->overrides.ip ? "ON" : "OFF")
                                   + " port=" + String(p->overrides.port ? "ON" : "OFF")
                                   + " adr=" + String(p->overrides.adr ? "ON" : "OFF")
                                   + " low=" + String(p->overrides.low ? "ON" : "OFF")
                                   + " high=" + String(p->overrides.high ? "ON" : "OFF"));
        }

        // ── adrmode ────────────────────────────────────────────────────────
        //    Set how scene and message OSC addresses are combined:
        //      "fallback"  — message's address, scene as fallback (default)
        //      "override"  — scene address replaces message address
        //      "prepend"   — scene.adr + msg.adr  (e.g. /mixer + /fader1)
        //      "append"    — msg.adr + scene.adr  (e.g. /fader1 + /mixer)
        else if (command == "adrmode" || command == "addressmode") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
            } else {
                String mode_str = osc_msg.nextAsString();
                p->address_mode = address_mode_from_string(mode_str);
                status_reporter().info("scene", "Address mode for '" + name_mp
                                       + "' set to: " + address_mode_label(p->address_mode));
            }
        }

        // ── setall ─────────────────────────────────────────────────────────
        //    Apply a config string to every message in this scene.
        //    Example: setall "ip:192.168.1.100, port:9000"
        else if (command == "setall") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String cfg_str = osc_msg.nextAsString();
            int applied = 0;
            for (uint8_t i = 0; i < p->msg_count; i++) {
                int mi = p->msg_indices[i];
                if (mi < 0 || mi >= (int)reg.msg_count) continue;
                OscMessage& m = reg.messages[mi];
                OscMessage csv;
                String err;
                if (csv.from_config_str(cfg_str, &err)) {
                    // Merge: new config takes priority.
                    OscMessage merged = csv * m;
                    merged.name = m.name;
                    merged.exist.name = true;
                    m = merged;
                    applied++;
                }
            }
            status_reporter().info("scene", "setall on '" + name_mp + "': applied to "
                                   + String(applied) + " messages");
        }

        // ── solo ───────────────────────────────────────────────────────────
        //    Enable one message, disable all others in this scene.
        else if (command == "solo") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String solo_name = osc_msg.nextAsString();
            solo_name = osc_trim_copy(solo_name);
            OscMessage* solo_m = reg.find_msg(solo_name);
            if (!solo_m) {
                status_reporter().warning("scene", "solo: msg '" + solo_name + "' not found");
                reg.unlock();
                return;
            }
            int solo_idx = reg.msg_index(solo_m);
            for (uint8_t i = 0; i < p->msg_count; i++) {
                int mi = p->msg_indices[i];
                if (mi >= 0 && mi < (int)reg.msg_count) {
                    reg.messages[mi].enabled = (mi == solo_idx);
                }
            }
            status_reporter().info("scene", "Solo '" + solo_name + "' in scene '" + name_mp + "'");
        }

        // ── unsolo / enableall ──────────────────────────────────────────────
        //    Re-enable all messages in this scene.
        else if (command == "unsolo" || command == "unmute" || command == "enableall") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            for (uint8_t i = 0; i < p->msg_count; i++) {
                int mi = p->msg_indices[i];
                if (mi >= 0 && mi < (int)reg.msg_count) {
                    reg.messages[mi].enabled = true;
                }
            }
            status_reporter().info("scene", "Unsolo: all messages in '" + name_mp + "' enabled");
        }

        // ── info ───────────────────────────────────────────────────────────
        else if (command == "info") {
            OscScene* p = reg.find_scene(name_mp);
            if (!p) {
                status_reporter().warning("scene", "Scene '" + name_mp + "' not found");
            } else {
                String info = p->to_info_string(true) + "\n  Messages:";
                for (uint8_t i = 0; i < p->msg_count; i++) {
                    int mi = p->msg_indices[i];
                    if (mi >= 0 && mi < (int)reg.msg_count) {
                        info += "\n    " + reg.messages[mi].to_info_string(true);
                    }
                }
                IPAddress info_ip = sender_ip;
                unsigned int info_port = sender_port;
                if (status_reporter().configured && status_reporter().dest_port != 0) {
                    info_ip = status_reporter().dest_ip;
                    info_port = status_reporter().dest_port;
                }
                osc_reply(info_ip, info_port,
                          reply_adr + "/scene/" + name_mp + "/info", info);
            }
        }

        else {
            status_reporter().warning("cmd", "Unknown scene command: " + command);
        }

        reg.unlock();
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    // MESSAGE COMMANDS
    // ════════════════════════════════════════════════════════════════════════

    if (is_msg) {
        reg.lock();

        // ── assign (create / update) ───────────────────────────────────────
        if (command == "assign") {
            OscMessage* m = reg.get_or_create_msg(name_mp);
            if (!m) {
                status_reporter().error("msg", "Registry full");
                reg.unlock();
                return;
            }
            const char* arg = osc_msg.nextAsString();
            if (arg && strlen(arg) > 0) {
                OscMessage csv;
                String err;
                if (!csv.from_config_str(arg, &err)) {
                    status_reporter().warning("msg", "Parse warning: " + err);
                }
                // Merge: new config values take priority.
                *m = csv * (*m);
                m->name = name_mp;
                m->exist.name = true;

                // If scenes were specified, auto-add to those scenes.
                if (m->scene_count > 0) {
                    int mi = reg.msg_index(m);
                    for (uint8_t si = 0; si < m->scene_count; si++) {
                        if (m->scenes[si]) m->scenes[si]->add_msg(mi);
                    }
                }
            }
            status_reporter().info("msg", "Message '" + name_mp + "' updated");
        }

        // ── delete ─────────────────────────────────────────────────────────
        else if (command == "delete" || command == "remove") {
            if (reg.delete_msg(name_mp)) {
                status_reporter().info("msg", "Deleted msg '" + name_mp + "'");
            } else {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            }
        }

        // ── enable ─────────────────────────────────────────────────────────
        else if (command == "enable" || command == "unmute") {
            OscMessage* m = reg.find_msg(name_mp);
            if (!m) {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            } else {
                m->enabled = true;
                status_reporter().info("msg", "Enabled msg '" + name_mp + "'");
            }
        }

        // ── disable / mute ─────────────────────────────────────────────────
        else if (command == "disable" || command == "mute") {
            OscMessage* m = reg.find_msg(name_mp);
            if (!m) {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            } else {
                m->enabled = false;
                status_reporter().info("msg", "Disabled msg '" + name_mp + "'");
            }
        }

        // ── info ───────────────────────────────────────────────────────────
        else if (command == "info") {
            OscMessage* m = reg.find_msg(name_mp);
            if (!m) {
                status_reporter().warning("msg", "Msg '" + name_mp + "' not found");
            } else {
                IPAddress info_ip = sender_ip;
                unsigned int info_port = sender_port;
                if (status_reporter().configured && status_reporter().dest_port != 0) {
                    info_ip = status_reporter().dest_ip;
                    info_port = status_reporter().dest_port;
                }
                osc_reply(info_ip, info_port,
                          reply_adr + "/msg/" + name_mp + "/info",
                          m->to_info_string(true));
            }
        }

        else {
            status_reporter().warning("cmd", "Unknown msg command: " + command);
        }

        reg.unlock();
        return;
    }
}

#endif // OSC_COMMANDS_H
