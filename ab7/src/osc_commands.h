// =============================================================================
// osc_commands.h — Incoming OSC command dispatcher
// =============================================================================
//
// This file contains the main handler that is called every time an OSC
// message arrives.  It parses the address, identifies the target (message,
// patch, or system command), and dispatches to the appropriate action.
//
// CASE HANDLING:
//   All command segments accept camelCase, snake_case, and lowercase.
//   For example, "addMsg", "add_msg", and "addmsg" are all equivalent.
//   User-defined names (message and patch names) preserve their original case.
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
//   {category}    — "msg", "patch", "list", "clone", "rename", "save",
//                    "load", "nvs", or a top-level command
//   {name}        — the name of the target message or patch
//   {command}     — the action to perform (defaults to "assign" if omitted)
//
// COMMAND REFERENCE  (see user_guide.md for full documentation):
// ─────────────────────────────────────────────────────────────────────────────
//
// ── MESSAGE COMMANDS (/annieData{dev}/msg/{name}/...) ──────────────────────
//   (no command / assign)  — create or update a message from config string
//   delete                 — remove message from registry
//   enable                 — enable message for sending
//   disable / mute         — disable message (skip during send)
//   info                   — reply with message details
//
// ── PATCH COMMANDS (/annieData{dev}/patch/{name}/...) ──────────────────────
//   (no command / assign)  — create or update a patch from config string
//   delete                 — remove patch and its task
//   start                  — create FreeRTOS task and begin sending
//   stop                   — stop the send task
//   enable                 — re-enable a stopped patch (same as start)
//   disable / mute         — stop sending without deleting the task
//   addMsg                 — add message(s) to this patch (CSV payload)
//   removeMsg              — remove a message from this patch
//   period                 — set the send period in milliseconds
//   override               — set which fields the patch forces on its messages
//   adrMode                — set address composition mode
//   setAll                 — set a property on all messages in this patch
//   solo                   — enable one message, disable all others in patch
//   unsolo                 — re-enable all messages in this patch
//   enableAll              — enable all messages in this patch
//   info                   — reply with patch details
//
// ── CLONE COMMANDS (/annieData{dev}/clone/...) ─────────────────────────────
//   msg     "src, dest"    — duplicate a message under a new name
//   patch   "src, dest"    — duplicate a patch (and optionally its messages)
//
// ── RENAME COMMANDS (/annieData{dev}/rename/...) ───────────────────────────
//   msg     "old, new"     — rename a message
//   patch   "old, new"     — rename a patch
//
// ── MOVE COMMAND (/annieData{dev}/move) ────────────────────────────────────
//   "msgName, patchName"   — move a message into a different patch
//
// ── LIST COMMANDS (/annieData{dev}/list/...) ───────────────────────────────
//   msgs     [verbose?]    — reply with all message names (+ params if verbose)
//   patches  [verbose?]    — reply with all patch names (+ params if verbose)
//   all      [verbose?]    — reply with everything
//
// ── GLOBAL COMMANDS (/annieData{dev}/...) ──────────────────────────────────
//   blackout               — stop ALL patch tasks immediately
//   restore                — restart all patches that have messages
//
// ── DIRECT COMMAND (/annieData{dev}/direct/{name}) ────────────────────────
//   (config string payload) — one-step: create msg + patch, add, and start
//
// ── STATUS COMMANDS (/annieData{dev}/status/...) ───────────────────────────
//   config   [config_str]  — set status destination (ip, port, address)
//   level    [level_str]   — set minimum importance level
//
// ── SAVE / LOAD COMMANDS (/annieData{dev}/...) ─────────────────────────────
//   save                   — save all patches and messages to NVS
//   save/all               — same as save
//   save/msg    "name"     — save one message to NVS
//   save/patch  "name"     — save one patch to NVS
//   load                   — load all patches and messages from NVS
//   load/all               — same as load
//   nvs/clear              — erase all saved OSC data from NVS
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

    // ── DIRECT COMMAND ────────────────────────────────────────────────────
    //    One-step: create a message + patch, add the message, and start
    //    sending — all from a single OSC command.
    //
    //    Address:  /annieData{dev}/direct/{name}
    //    Payload:  config string with value, ip, port, adr, and optionally
    //              period, low, high.
    //
    //    Creates a message named "{name}" and a patch named "{name}" (or
    //    updates them if they exist).  The message gets the sensor value and
    //    destination fields.  The patch gets the destination and period.
    //    The message is added to the patch, and the patch is started.
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
                if (pms > 0) period_ms = clamp_patch_period_ms(pms);
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

        // Create or update the patch.
        OscPatch* p = reg.get_or_create_patch(dname);
        if (!p) {
            status_reporter().error("direct", "Registry full (patches)");
            reg.unlock();
            return;
        }
        if (parsed.exist.ip)   { p->ip = parsed.ip;                   p->exist.ip   = true; }
        if (parsed.exist.port) { p->port = parsed.port;               p->exist.port = true; }
        if (parsed.exist.adr)  { p->osc_address = parsed.osc_address; p->exist.adr  = true; }
        if (parsed.exist.low)  { p->bounds[0] = parsed.bounds[0];     p->exist.low  = true; }
        if (parsed.exist.high) { p->bounds[1] = parsed.bounds[1];     p->exist.high = true; }
        p->send_period_ms = period_ms;

        // Add the message to the patch (no-op if already there).
        int mi = reg.msg_index(m);
        p->add_msg(mi);
        m->patch = p;
        m->exist.patch = true;

        // Start the patch (stop first if already running to pick up changes).
        if (p->task_handle) {
            stop_patch(p);
        }
        start_patch(p);

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
        String sub_label = (sub.length() == 0) ? String("(all)") : sub;
        Serial.println("  → list sub='" + sub_label + "' verbose="
                       + String(verbose ? "true" : "false")
                       + " sender=" + sender_ip.toString() + ":" + String(sender_port)
                       + " dest=" + reply_ip.toString() + ":" + String(reply_port));
        if (reply_port == 0) {
            status_reporter().warning("list", "No reply port available for list response");
            return;
        }

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
        } else if (sub == "/patches") {
            String result = "Patches (" + String(reg.patch_count) + "):";
            if (reg.patch_count == 0) {
                result += " none\n";
            } else {
                result += "\n";
            }
            for (uint16_t i = 0; i < reg.patch_count; i++) {
                result += "  " + reg.patches[i].to_info_string(verbose) + "\n";
            }
            osc_reply(reply_ip, reply_port, reply_adr + "/list/patches", result);
        } else if (sub == "/all" || sub == "") {
            String result = "Patches (" + String(reg.patch_count) + "):";
            if (reg.patch_count == 0) {
                result += " none\n";
            } else {
                result += "\n";
            }
            for (uint16_t i = 0; i < reg.patch_count; i++) {
                result += "  " + reg.patches[i].to_info_string(verbose) + "\n";
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
        } else if (sub == "/patch") {
            OscPatch* src = reg.find_patch(src_name);
            if (!src) {
                status_reporter().error("cmd", "clone/patch: source '" + src_name + "' not found");
                reg.unlock();
                return;
            }
            OscPatch* dest = reg.get_or_create_patch(dest_name);
            if (!dest) {
                status_reporter().error("cmd", "clone/patch: registry full");
                reg.unlock();
                return;
            }
            // Copy config but not task state.
            dest->ip             = src->ip;
            dest->port           = src->port;
            dest->osc_address    = src->osc_address;
            dest->bounds[0]      = src->bounds[0];
            dest->bounds[1]      = src->bounds[1];
            dest->send_period_ms = clamp_patch_period_ms(src->send_period_ms);
            dest->address_mode   = src->address_mode;
            dest->overrides      = src->overrides;
            dest->exist          = src->exist;
            dest->exist.name     = true;
            dest->name           = dest_name;
            // Copy message list.
            dest->msg_count = src->msg_count;
            memcpy(dest->msg_indices, src->msg_indices,
                   src->msg_count * sizeof(int));
            status_reporter().info("cmd", "Cloned patch '" + src_name + "' → '" + dest_name + "'");
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
        } else if (sub == "/patch") {
            OscPatch* p = reg.find_patch(old_name);
            if (!p) {
                status_reporter().error("cmd", "rename/patch: '" + old_name + "' not found");
            } else {
                p->name = new_name;
                status_reporter().info("cmd", "Renamed patch '" + old_name + "' → '" + new_name + "'");
            }
        } else {
            status_reporter().warning("cmd", "Unknown rename target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── MOVE COMMAND ───────────────────────────────────────────────────────
    //    Payload: single CSV string "msgName, patchName"

    if (norm_adr == "/move") {
        String arg = osc_msg.nextAsString();
        arg.trim();
        int comma = arg.indexOf(',');
        if (comma < 0) {
            status_reporter().error("cmd", "move requires a CSV string: \"msgName, patchName\"");
            return;
        }
        String msg_name   = osc_trim_copy(arg.substring(0, comma));
        String patch_name = osc_trim_copy(arg.substring(comma + 1));
        if (msg_name.length() == 0 || patch_name.length() == 0) {
            status_reporter().error("cmd", "move requires two non-empty names: \"msgName, patchName\"");
            return;
        }

        reg.lock();

        OscMessage* m = reg.find_msg(msg_name);
        OscPatch*   p = reg.find_patch(patch_name);
        if (!m) {
            status_reporter().error("cmd", "move: msg '" + msg_name + "' not found");
            reg.unlock();
            return;
        }
        if (!p) {
            status_reporter().error("cmd", "move: patch '" + patch_name + "' not found");
            reg.unlock();
            return;
        }

        int mi = reg.msg_index(m);

        // Remove from current patch (if any).
        if (m->patch) {
            m->patch->remove_msg(mi);
        }

        // Add to new patch.
        p->add_msg(mi);
        m->patch = p;
        m->exist.patch = true;

        status_reporter().info("cmd", "Moved msg '" + msg_name
                               + "' → patch '" + patch_name + "'");
        reg.unlock();
        return;
    }

    // ── SAVE COMMANDS ──────────────────────────────────────────────────────
    //    /save          — save all patches and messages to NVS
    //    /save/all      — same as /save
    //    /save/msg      — save one message (payload: name)
    //    /save/patch    — save one patch (payload: name)

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
        } else if (sub == "/patch") {
            String name = osc_trim_copy(osc_msg.nextAsString());
            if (name.length() == 0) {
                status_reporter().error("nvs", "save/patch requires a patch name");
            } else if (nvs_save_patch(name)) {
                status_reporter().info("nvs", "Saved patch '" + name + "' to NVS");
            } else {
                status_reporter().error("nvs", "save/patch: '" + name + "' not found");
            }
        } else {
            status_reporter().warning("cmd", "Unknown save target: " + sub);
        }

        reg.unlock();
        return;
    }

    // ── LOAD COMMANDS ──────────────────────────────────────────────────────
    //    /load          — load all patches and messages from NVS
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

    // ── ORI COMMANDS (/annieData{dev}/ori/...) ─────────────────────────────
    //
    // Orientation save/recall/matching system.  See ori_tracker.h for details.

    if (norm_adr.startsWith("/ori/") || norm_adr == "/ori") {
        String ori_rest = (norm_adr == "/ori") ? String("") : norm_adr.substring(4);
        Serial.println("  → ori command, sub=" + ori_rest);
        // Also extract original-case name from the address.
        String ori_rest_orig = "";
        if (address.length() > 4 && address.startsWith("/ori")) {
            ori_rest_orig = address.substring(4);
        }

        OriTracker& ot = ori_tracker();

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
        if (ori_rest == "/threshold" || ori_rest.startsWith("/threshold")) {
            const char* typetags = osc_msg.getTypeTags();
            if (typetags && typetags[0] == 'f') {
                ot.motion_threshold = osc_msg.nextAsFloat();
            } else if (typetags && typetags[0] == 's') {
                ot.motion_threshold = String(osc_msg.nextAsString()).toFloat();
            }
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

        status_reporter().warning("cmd", "Unknown ori command: " + ori_rest);
        return;
    }

    // ── CATEGORY DISPATCH: /msg or /patch ──────────────────────────────────

    bool is_msg   = norm_adr.startsWith("/msg");
    bool is_patch = norm_adr.startsWith("/patch");

    if (!is_msg && !is_patch) {
        status_reporter().warning("cmd", "Unknown category in: " + address);
        return;
    }

    // Strip category prefix to get /{name}/{command}.
    if (is_msg)   address = address.substring(4);   // strip "/msg"
    if (is_patch) address = address.substring(6);    // strip "/patch"

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
    // PATCH COMMANDS
    // ════════════════════════════════════════════════════════════════════════

    if (is_patch) {
        reg.lock();

        // ── assign (create / update) ───────────────────────────────────────
        if (command == "assign") {
            OscPatch* p = reg.get_or_create_patch(name_mp);
            if (!p) {
                status_reporter().error("patch", "Registry full");
                reg.unlock();
                return;
            }
            const char* arg = osc_msg.nextAsString();
            if (arg && strlen(arg) > 0) {
                OscMessage csv;
                String err;
                if (!csv.from_config_str(arg, &err)) {
                    status_reporter().warning("patch", "Parse warning: " + err);
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
                        if (pms > 0) p->send_period_ms = clamp_patch_period_ms(pms);
                    }
                }
            }
            status_reporter().info("patch", "Patch '" + name_mp + "' updated");
        }

        // ── delete ─────────────────────────────────────────────────────────
        else if (command == "delete" || command == "remove") {
            if (reg.delete_patch(name_mp)) {
                status_reporter().info("patch", "Deleted patch '" + name_mp + "'");
            } else {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            }
        }

        // ── start ──────────────────────────────────────────────────────────
        else if (command == "start" || command == "enable" || command == "go") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                start_patch(p);
            }
        }

        // ── stop ───────────────────────────────────────────────────────────
        else if (command == "stop" || command == "disable" || command == "mute") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                stop_patch(p);
            }
        }

        // ── addmsg ─────────────────────────────────────────────────────────
        //    Payload: comma-separated message names.
        else if (command == "addmsg" || command == "add") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
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
                    status_reporter().warning("patch", "addmsg: msg '" + mname + "' not found");
                    continue;
                }
                int mi = reg.msg_index(m);
                p->add_msg(mi);
                m->patch = p;
                m->exist.patch = true;
                status_reporter().debug("patch", "Added msg '" + mname + "' to patch '" + name_mp + "'");
            }
            status_reporter().info("patch", "addmsg complete for '" + name_mp + "'");
        }

        // ── removemsg ──────────────────────────────────────────────────────
        else if (command == "removemsg" || command == "rmmsg") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String mname = osc_msg.nextAsString();
            mname = osc_trim_copy(mname);
            OscMessage* m = reg.find_msg(mname);
            if (!m) {
                status_reporter().warning("patch", "removemsg: msg '" + mname + "' not found");
            } else {
                int mi = reg.msg_index(m);
                p->remove_msg(mi);
                if (m->patch == p) {
                    m->patch = nullptr;
                    m->exist.patch = false;
                }
                status_reporter().info("patch", "Removed msg '" + mname
                                       + "' from patch '" + name_mp + "'");
            }
        }

        // ── period ─────────────────────────────────────────────────────────
        //    Payload: a string or integer with the period in milliseconds.
        else if (command == "period" || command == "rate") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
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
                    status_reporter().warning("patch", "Period for '" + name_mp
                                              + "' ignored (missing/invalid payload)");
                } else {
                    p->send_period_ms = clamp_patch_period_ms(ms);
                    status_reporter().info("patch", "Period for '" + name_mp
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
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
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
                    else status_reporter().warning("patch", "Unknown override field: " + f);
                }
            }
            status_reporter().info("patch", "Override for '" + name_mp + "': ip="
                                   + String(p->overrides.ip ? "ON" : "OFF")
                                   + " port=" + String(p->overrides.port ? "ON" : "OFF")
                                   + " adr=" + String(p->overrides.adr ? "ON" : "OFF")
                                   + " low=" + String(p->overrides.low ? "ON" : "OFF")
                                   + " high=" + String(p->overrides.high ? "ON" : "OFF"));
        }

        // ── adrmode ────────────────────────────────────────────────────────
        //    Set how patch and message OSC addresses are combined:
        //      "fallback"  — message's address, patch as fallback (default)
        //      "override"  — patch address replaces message address
        //      "prepend"   — patch.adr + msg.adr  (e.g. /mixer + /fader1)
        //      "append"    — msg.adr + patch.adr  (e.g. /fader1 + /mixer)
        else if (command == "adrmode" || command == "addressmode") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                String mode_str = osc_msg.nextAsString();
                p->address_mode = address_mode_from_string(mode_str);
                status_reporter().info("patch", "Address mode for '" + name_mp
                                       + "' set to: " + address_mode_label(p->address_mode));
            }
        }

        // ── setall ─────────────────────────────────────────────────────────
        //    Apply a config string to every message in this patch.
        //    Example: setall "ip:192.168.1.100, port:9000"
        else if (command == "setall") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
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
            status_reporter().info("patch", "setall on '" + name_mp + "': applied to "
                                   + String(applied) + " messages");
        }

        // ── solo ───────────────────────────────────────────────────────────
        //    Enable one message, disable all others in this patch.
        else if (command == "solo") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            String solo_name = osc_msg.nextAsString();
            solo_name = osc_trim_copy(solo_name);
            OscMessage* solo_m = reg.find_msg(solo_name);
            if (!solo_m) {
                status_reporter().warning("patch", "solo: msg '" + solo_name + "' not found");
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
            status_reporter().info("patch", "Solo '" + solo_name + "' in patch '" + name_mp + "'");
        }

        // ── unsolo / enableall ──────────────────────────────────────────────
        //    Re-enable all messages in this patch.
        else if (command == "unsolo" || command == "unmute" || command == "enableall") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
                reg.unlock();
                return;
            }
            for (uint8_t i = 0; i < p->msg_count; i++) {
                int mi = p->msg_indices[i];
                if (mi >= 0 && mi < (int)reg.msg_count) {
                    reg.messages[mi].enabled = true;
                }
            }
            status_reporter().info("patch", "Unsolo: all messages in '" + name_mp + "' enabled");
        }

        // ── info ───────────────────────────────────────────────────────────
        else if (command == "info") {
            OscPatch* p = reg.find_patch(name_mp);
            if (!p) {
                status_reporter().warning("patch", "Patch '" + name_mp + "' not found");
            } else {
                String info = p->to_info_string(true) + "\n  Messages:";
                for (uint8_t i = 0; i < p->msg_count; i++) {
                    int mi = p->msg_indices[i];
                    if (mi >= 0 && mi < (int)reg.msg_count) {
                        info += "\n    " + reg.messages[mi].to_info_string(true);
                    }
                }
                osc_reply(sender_ip, sender_port,
                          reply_adr + "/patch/" + name_mp + "/info", info);
            }
        }

        else {
            status_reporter().warning("cmd", "Unknown patch command: " + command);
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

                // If a patch was specified, auto-add to that patch.
                if (m->exist.patch && m->patch) {
                    int mi = reg.msg_index(m);
                    m->patch->add_msg(mi);
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
                osc_reply(sender_ip, sender_port,
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
