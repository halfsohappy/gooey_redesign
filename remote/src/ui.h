#ifndef UI_H
#define UI_H

// ============================================================================
//  TheaterGWD Setup Remote — Menu System & UI State Machine
// ============================================================================
//
//  Hierarchical menu with a screen stack, plus overlay modes for text input,
//  number input, IP input, pick-lists, confirmations, and reply display.
//
//  Each MenuScreen carries its own item list and on_select callback.  The
//  overlay editors use global state and invoke a stored callback when the
//  user confirms a value.
//
// ============================================================================

#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "input.h"
#include "osc_remote.h"
#include "network.h"

// ── Forward declarations ────────────────────────────────────────────────────

static void _build_main_menu();
static void _build_msg_list();
static void _build_msg_form(bool is_edit);
static void _build_msg_detail(const char* name);
static void _build_patch_list();
static void _build_patch_form();
static void _build_patch_detail(const char* name);
static void _build_ori_menu();
static void _build_ori_detail(const char* name);
static void _build_monitor_menu();
static void _build_quick_menu();
static void _build_settings_menu();

// ── Types ───────────────────────────────────────────────────────────────────

typedef void (*SelectCb)(int index);
typedef void (*TextCb)(const char* text);
typedef void (*NumCb)(int value);
typedef void (*IpCb)(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
typedef void (*PickCb)(int index);
typedef void (*ConfirmCb)(bool yes);

struct MenuScreen {
    char title[MAX_ITEM_LEN];
    char items[MAX_MENU_ITEMS][MAX_ITEM_LEN];
    int count;
    int selected;
    int scroll;
    SelectCb on_select;
};

enum UIMode {
    MODE_MENU,
    MODE_TEXT_INPUT,
    MODE_NUM_INPUT,
    MODE_IP_INPUT,
    MODE_PICK,
    MODE_CONFIRM,
    MODE_REPLY,
    MODE_WAITING
};

// ── Global UI state ─────────────────────────────────────────────────────────

#define MAX_DEPTH 8
static MenuScreen _stack[MAX_DEPTH];
static int _depth = 0;
static UIMode _mode = MODE_MENU;

// Text input state
static char        _ti_buf[MAX_INPUT_LEN];
static int         _ti_pos;
static int         _ti_ci;          // charset index for current char
static const char* _ti_title;
static TextCb      _ti_cb;

// Number input state
static int         _ni_val;
static int         _ni_lo, _ni_hi;
static const char* _ni_title;
static NumCb       _ni_cb;

// IP input state
static uint8_t     _ip_oct[4];
static int         _ip_idx;
static const char* _ip_title;
static IpCb        _ip_cb;

// Pick list state
static const char* const* _pk_opts;
static int         _pk_count;
static int         _pk_sel;
static int         _pk_scroll;
static const char* _pk_title;
static PickCb      _pk_cb;

// Confirm state
static const char* _cf_text;
static bool        _cf_yes;
static ConfirmCb   _cf_cb;

// Reply / waiting state
static char        _rp_text[MAX_REPLY_LEN];
static int         _rp_scroll;
static int         _rp_lines;
static unsigned long _wait_start;
static bool        _waiting_reply;

// Form state — message creation / editing
static struct {
    char name[MAX_INPUT_LEN];
    char value[MAX_INPUT_LEN];
    uint8_t ip[4];
    char port[8];
    char address[MAX_INPUT_LEN];
    char low[12];
    char high[12];
    char patch[MAX_INPUT_LEN];
    char ori_only[MAX_INPUT_LEN];
    char ori_not[MAX_INPUT_LEN];
    bool editing;               // true = edit existing, false = new
} _mf;

// Form state — patch creation
static struct {
    char name[MAX_INPUT_LEN];
    uint8_t ip[4];
    char port[8];
    char address[MAX_INPUT_LEN];
    char period[8];
    char low[12];
    char high[12];
    char adr_mode[12];
} _pf;

// Selected item name (for detail screens)
static char _sel_name[MAX_INPUT_LEN];

// Dynamic item storage (populated from device replies)
static char _dyn_items[MAX_MENU_ITEMS][MAX_ITEM_LEN];
static int  _dyn_count;

// ============================================================================
//  Screen Stack
// ============================================================================

static MenuScreen& _cur() { return _stack[_depth]; }

static void _push(const char* title, SelectCb cb) {
    if (_depth < MAX_DEPTH - 1) _depth++;
    MenuScreen& s = _stack[_depth];
    strncpy(s.title, title, MAX_ITEM_LEN - 1);
    s.title[MAX_ITEM_LEN - 1] = 0;
    s.count = 0;
    s.selected = 0;
    s.scroll = 0;
    s.on_select = cb;
}

static void _add_item(const char* label) {
    MenuScreen& s = _stack[_depth];
    if (s.count >= MAX_MENU_ITEMS) return;
    strncpy(s.items[s.count], label, MAX_ITEM_LEN - 1);
    s.items[s.count][MAX_ITEM_LEN - 1] = 0;
    s.count++;
}

static void _pop() {
    if (_depth > 0) _depth--;
    _mode = MODE_MENU;
}

// ============================================================================
//  OSC Address Builder
// ============================================================================

static char _addr_buf[128];

// Build "/annieData/{target}/{rest}"
static const char* _osc_addr(const char* rest) {
    snprintf(_addr_buf, sizeof(_addr_buf), "/annieData/%s/%s",
             target_name, rest);
    return _addr_buf;
}

// Build "/annieData/{target}/{cat}/{name}/{cmd}"
static const char* _osc_addr3(const char* cat, const char* name,
                              const char* cmd) {
    if (cmd && cmd[0])
        snprintf(_addr_buf, sizeof(_addr_buf), "/annieData/%s/%s/%s/%s",
                 target_name, cat, name, cmd);
    else
        snprintf(_addr_buf, sizeof(_addr_buf), "/annieData/%s/%s/%s",
                 target_name, cat, name);
    return _addr_buf;
}

// ============================================================================
//  Overlay Starters
// ============================================================================

static void _start_text(const char* title, const char* init, TextCb cb) {
    _mode = MODE_TEXT_INPUT;
    _ti_title = title;
    _ti_cb = cb;
    strncpy(_ti_buf, init, MAX_INPUT_LEN - 1);
    _ti_buf[MAX_INPUT_LEN - 1] = 0;
    _ti_pos = strlen(_ti_buf);
    _ti_ci = 0;
}

static void _start_num(const char* title, int init, int lo, int hi, NumCb cb) {
    _mode = MODE_NUM_INPUT;
    _ni_title = title;
    _ni_cb = cb;
    _ni_val = init;
    _ni_lo = lo;
    _ni_hi = hi;
}

static void _start_ip(const char* title, const uint8_t init[4], IpCb cb) {
    _mode = MODE_IP_INPUT;
    _ip_title = title;
    _ip_cb = cb;
    memcpy(_ip_oct, init, 4);
    _ip_idx = 0;
}

static void _start_pick(const char* title, const char* const* opts, int cnt,
                        PickCb cb) {
    _mode = MODE_PICK;
    _pk_title = title;
    _pk_opts = opts;
    _pk_count = cnt;
    _pk_cb = cb;
    _pk_sel = 0;
    _pk_scroll = 0;
}

static void _start_confirm(const char* text, ConfirmCb cb) {
    _mode = MODE_CONFIRM;
    _cf_text = text;
    _cf_cb = cb;
    _cf_yes = false;
}

static void _show_reply(const char* text) {
    _mode = MODE_REPLY;
    strncpy(_rp_text, text, MAX_REPLY_LEN - 1);
    _rp_text[MAX_REPLY_LEN - 1] = 0;
    _rp_scroll = 0;
    _rp_lines = 0;
}

static void _send_and_wait(const char* addr, const char* payload = nullptr) {
    if (payload)
        osc_send_string(target_ip_addr(), target_port, addr, payload);
    else
        osc_send_empty(target_ip_addr(), target_port, addr);
    _mode = MODE_WAITING;
    _wait_start = millis();
    _waiting_reply = true;
}

// ============================================================================
//  Reply Parser — extract names from list replies
// ============================================================================

// Parse a list reply like "Messages (3): accelX, accelY, accelZ"
// into _dyn_items / _dyn_count.
static void _parse_list(const char* text) {
    _dyn_count = 0;
    // Find the ':' that precedes the list body
    const char* colon = strchr(text, ':');
    const char* p = colon ? colon + 1 : text;  // no colon → parse whole string
    while (*p && _dyn_count < MAX_MENU_ITEMS) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (*p == 0) break;
        // "none" means empty list
        if (strncmp(p, "none", 4) == 0) break;
        const char* start = p;
        while (*p && *p != ',' && *p != '\n') p++;
        int len = p - start;
        // trim trailing spaces
        while (len > 0 && start[len - 1] == ' ') len--;
        if (len > 0 && len < MAX_ITEM_LEN) {
            memcpy(_dyn_items[_dyn_count], start, len);
            _dyn_items[_dyn_count][len] = 0;
            _dyn_count++;
        }
        if (*p == ',') p++;
    }
}

// ============================================================================
//  Menu Builders
// ============================================================================

// ── Main Menu ───────────────────────────────────────────────────────────────

static void _on_main(int idx);

static void _build_main_menu() {
    _depth = 0;
    MenuScreen& s = _stack[0];
    strncpy(s.title, "Setup Remote", MAX_ITEM_LEN - 1);
    s.count = 0; s.selected = 0; s.scroll = 0;
    s.on_select = _on_main;
    auto add = [&](const char* l) {
        strncpy(s.items[s.count], l, MAX_ITEM_LEN - 1);
        s.items[s.count][MAX_ITEM_LEN - 1] = 0;
        s.count++;
    };
    add("Messages");
    add("Patches");
    add("Oris");
    add("Monitor");
    add("Quick Actions");
    add("Settings");
}

static void _on_main(int idx) {
    switch (idx) {
        case 0: _build_msg_list();     break;
        case 1: _build_patch_list();   break;
        case 2: _build_ori_menu();     break;
        case 3: _build_monitor_menu(); break;
        case 4: _build_quick_menu();   break;
        case 5: _build_settings_menu();break;
    }
}

// ── Messages List ───────────────────────────────────────────────────────────

static void _on_msg_list(int idx);

static void _build_msg_list() {
    _push("Messages", _on_msg_list);
    _add_item("< Refresh >");
    _add_item("< + New Message >");
    for (int i = 0; i < _dyn_count; i++) _add_item(_dyn_items[i]);
}

static void _on_msg_form_select(int idx);
static void _on_msg_detail(int idx);

static void _on_msg_list(int idx) {
    if (idx == 0) {
        // Refresh — query device
        _send_and_wait(_osc_addr("list/msgs"));
    } else if (idx == 1) {
        // New message — open form
        memset(&_mf, 0, sizeof(_mf));
        _mf.ip[0] = target_ip[0]; _mf.ip[1] = target_ip[1];
        _mf.ip[2] = target_ip[2]; _mf.ip[3] = target_ip[3];
        strncpy(_mf.port, "9000", sizeof(_mf.port));
        strncpy(_mf.low, "0", sizeof(_mf.low));
        strncpy(_mf.high, "1", sizeof(_mf.high));
        _mf.editing = false;
        _build_msg_form(false);
    } else {
        // Selected a message name
        int di = idx - 2;
        if (di >= 0 && di < _dyn_count) {
            strncpy(_sel_name, _dyn_items[di], MAX_INPUT_LEN - 1);
            _build_msg_detail(_sel_name);
        }
    }
}

// ── Message Form (New / Edit) ───────────────────────────────────────────────

static void _rebuild_msg_form();

static void _build_msg_form(bool is_edit) {
    _push(is_edit ? "Edit Message" : "New Message", _on_msg_form_select);
    _rebuild_msg_form();
}

static void _rebuild_msg_form() {
    MenuScreen& s = _cur();
    int saved_sel = s.selected;
    s.count = 0;
    char buf[MAX_ITEM_LEN];

    snprintf(buf, MAX_ITEM_LEN, "name:  %s", _mf.name[0] ? _mf.name : "(set)");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "value: %s", _mf.value[0] ? _mf.value : "(set)");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "ip:    %d.%d.%d.%d", _mf.ip[0], _mf.ip[1], _mf.ip[2], _mf.ip[3]);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "port:  %s", _mf.port[0] ? _mf.port : "9000");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "adr:   %s", _mf.address[0] ? _mf.address : "(set)");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "low:   %s", _mf.low);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "high:  %s", _mf.high);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "patch: %s", _mf.patch[0] ? _mf.patch : "(none)");
    _add_item(buf);
    _add_item("ori_only / ori_not...");
    _add_item(_mf.editing ? ">> UPDATE <<" : ">> CREATE <<");

    // Restore cursor position, clamped to valid range
    if (saved_sel >= s.count) saved_sel = s.count - 1;
    if (saved_sel < 0) saved_sel = 0;
    s.selected = saved_sel;
    if (s.selected < s.scroll) s.scroll = s.selected;
    if (s.selected >= s.scroll + VISIBLE_ITEMS)
        s.scroll = s.selected - VISIBLE_ITEMS + 1;
}

// Callbacks for each form field
static void _mf_name_cb(const char* t) { strncpy(_mf.name, t, MAX_INPUT_LEN-1); _mode = MODE_MENU; _rebuild_msg_form(); }
static void _mf_value_cb(int i) { strncpy(_mf.value, SENSOR_NAMES[i], MAX_INPUT_LEN-1); _mode = MODE_MENU; _rebuild_msg_form(); }
static void _mf_ip_cb(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { _mf.ip[0]=a; _mf.ip[1]=b; _mf.ip[2]=c; _mf.ip[3]=d; _mode = MODE_MENU; _rebuild_msg_form(); }
static void _mf_port_cb(int v) { snprintf(_mf.port, sizeof(_mf.port), "%d", v); _mode = MODE_MENU; _rebuild_msg_form(); }
static void _mf_addr_cb(const char* t) { strncpy(_mf.address, t, MAX_INPUT_LEN-1); _mode = MODE_MENU; _rebuild_msg_form(); }
static void _mf_low_cb(const char* t) { strncpy(_mf.low, t, sizeof(_mf.low)-1); _mode = MODE_MENU; _rebuild_msg_form(); }
static void _mf_high_cb(const char* t) { strncpy(_mf.high, t, sizeof(_mf.high)-1); _mode = MODE_MENU; _rebuild_msg_form(); }
static void _mf_patch_cb(const char* t) { strncpy(_mf.patch, t, MAX_INPUT_LEN-1); _mode = MODE_MENU; _rebuild_msg_form(); }

static void _on_msg_ori_select(int idx);

static void _build_msg_ori_menu() {
    _push("Ori Conditions", _on_msg_ori_select);
    char buf[MAX_ITEM_LEN];
    snprintf(buf, MAX_ITEM_LEN, "ori_only: %s", _mf.ori_only[0] ? _mf.ori_only : "(none)");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "ori_not:  %s", _mf.ori_not[0] ? _mf.ori_not : "(none)");
    _add_item(buf);
}

static void _mf_ori_only_cb(const char* t) { strncpy(_mf.ori_only, t, MAX_INPUT_LEN-1); _mode = MODE_MENU; _pop(); _build_msg_ori_menu(); }
static void _mf_ori_not_cb(const char* t) { strncpy(_mf.ori_not, t, MAX_INPUT_LEN-1); _mode = MODE_MENU; _pop(); _build_msg_ori_menu(); }

static void _on_msg_ori_select(int idx) {
    if (idx == 0) _start_text("ori_only", _mf.ori_only, _mf_ori_only_cb);
    else          _start_text("ori_not",  _mf.ori_not,  _mf_ori_not_cb);
}

static void _on_msg_form_select(int idx) {
    switch (idx) {
        case 0: _start_text("Name", _mf.name, _mf_name_cb); break;
        case 1: _start_pick("Sensor Value", SENSOR_NAMES, SENSOR_COUNT, _mf_value_cb); break;
        case 2: _start_ip("IP Address", _mf.ip, _mf_ip_cb); break;
        case 3: _start_num("Port", atoi(_mf.port), 1, 65535, _mf_port_cb); break;
        case 4: _start_text("OSC Address", _mf.address, _mf_addr_cb); break;
        case 5: _start_text("Low bound", _mf.low, _mf_low_cb); break;
        case 6: _start_text("High bound", _mf.high, _mf_high_cb); break;
        case 7: _start_text("Patch name", _mf.patch, _mf_patch_cb); break;
        case 8: _build_msg_ori_menu(); break;
        case 9: {
            // CREATE / UPDATE — compose config string and send
            if (!_mf.name[0]) { _show_reply("Name is required"); return; }
            char cfg[256];
            int pos = 0;
            auto ap = [&](const char* k, const char* v) {
                if (!v[0]) return;
                if (pos > 0) pos += snprintf(cfg + pos, sizeof(cfg) - pos, ", ");
                pos += snprintf(cfg + pos, sizeof(cfg) - pos, "%s:%s", k, v);
            };
            cfg[0] = 0;
            ap("value", _mf.value);
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", _mf.ip[0], _mf.ip[1], _mf.ip[2], _mf.ip[3]);
            ap("ip", ip_str);
            ap("port", _mf.port);
            ap("adr", _mf.address);
            ap("low", _mf.low);
            ap("high", _mf.high);
            ap("patch", _mf.patch);
            ap("ori_only", _mf.ori_only);
            ap("ori_not", _mf.ori_not);
            _send_and_wait(_osc_addr3("msg", _mf.name, ""), cfg);
            break;
        }
    }
}

// ── Message Detail ──────────────────────────────────────────────────────────

static void _build_msg_detail(const char* name) {
    strncpy(_sel_name, name, MAX_INPUT_LEN - 1);
    _push(name, _on_msg_detail);
    _add_item("Info");
    _add_item("Enable");
    _add_item("Disable");
    _add_item("Delete");
}

static void _msg_del_cb(bool yes) {
    _mode = MODE_MENU;
    if (yes) _send_and_wait(_osc_addr3("msg", _sel_name, "delete"));
    else _pop();
}

static void _on_msg_detail(int idx) {
    switch (idx) {
        case 0: _send_and_wait(_osc_addr3("msg", _sel_name, "info")); break;
        case 1:
            osc_send_empty(target_ip_addr(), target_port,
                           _osc_addr3("msg", _sel_name, "enable"));
            _show_reply("Sent enable");
            break;
        case 2:
            osc_send_empty(target_ip_addr(), target_port,
                           _osc_addr3("msg", _sel_name, "disable"));
            _show_reply("Sent disable");
            break;
        case 3: _start_confirm("Delete message?", _msg_del_cb); break;
    }
}

// ── Patches List ────────────────────────────────────────────────────────────

static void _on_patch_list(int idx);

static void _build_patch_list() {
    _push("Patches", _on_patch_list);
    _add_item("< Refresh >");
    _add_item("< + New Patch >");
    for (int i = 0; i < _dyn_count; i++) _add_item(_dyn_items[i]);
}

static void _on_patch_form_select(int idx);
static void _on_patch_detail(int idx);

static void _on_patch_list(int idx) {
    if (idx == 0) {
        _send_and_wait(_osc_addr("list/patches"));
    } else if (idx == 1) {
        memset(&_pf, 0, sizeof(_pf));
        _pf.ip[0] = target_ip[0]; _pf.ip[1] = target_ip[1];
        _pf.ip[2] = target_ip[2]; _pf.ip[3] = target_ip[3];
        strncpy(_pf.port, "9000", sizeof(_pf.port));
        strncpy(_pf.period, "50", sizeof(_pf.period));
        strncpy(_pf.low, "0", sizeof(_pf.low));
        strncpy(_pf.high, "1", sizeof(_pf.high));
        strncpy(_pf.adr_mode, "fallback", sizeof(_pf.adr_mode));
        _build_patch_form();
    } else {
        int di = idx - 2;
        if (di >= 0 && di < _dyn_count) {
            strncpy(_sel_name, _dyn_items[di], MAX_INPUT_LEN - 1);
            _build_patch_detail(_sel_name);
        }
    }
}

// ── Patch Form (New) ────────────────────────────────────────────────────────

static void _rebuild_patch_form();

static void _build_patch_form() {
    _push("New Patch", _on_patch_form_select);
    _rebuild_patch_form();
}

static void _rebuild_patch_form() {
    MenuScreen& s = _cur();
    int saved_sel = s.selected;
    s.count = 0;
    char buf[MAX_ITEM_LEN];
    snprintf(buf, MAX_ITEM_LEN, "name:    %s", _pf.name[0] ? _pf.name : "(set)");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "ip:      %d.%d.%d.%d", _pf.ip[0], _pf.ip[1], _pf.ip[2], _pf.ip[3]);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "port:    %s", _pf.port[0] ? _pf.port : "9000");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "address: %s", _pf.address[0] ? _pf.address : "(set)");
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "period:  %s ms", _pf.period);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "low:     %s", _pf.low);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "high:    %s", _pf.high);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "adrMode: %s", _pf.adr_mode);
    _add_item(buf);
    _add_item(">> CREATE <<");

    // Restore cursor position, clamped to valid range
    if (saved_sel >= s.count) saved_sel = s.count - 1;
    if (saved_sel < 0) saved_sel = 0;
    s.selected = saved_sel;
    if (s.selected < s.scroll) s.scroll = s.selected;
    if (s.selected >= s.scroll + VISIBLE_ITEMS)
        s.scroll = s.selected - VISIBLE_ITEMS + 1;
}

static void _pf_name_cb(const char* t) { strncpy(_pf.name, t, MAX_INPUT_LEN-1); _mode = MODE_MENU; _rebuild_patch_form(); }
static void _pf_ip_cb(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { _pf.ip[0]=a; _pf.ip[1]=b; _pf.ip[2]=c; _pf.ip[3]=d; _mode = MODE_MENU; _rebuild_patch_form(); }
static void _pf_port_cb(int v) { snprintf(_pf.port, sizeof(_pf.port), "%d", v); _mode = MODE_MENU; _rebuild_patch_form(); }
static void _pf_addr_cb(const char* t) { strncpy(_pf.address, t, MAX_INPUT_LEN-1); _mode = MODE_MENU; _rebuild_patch_form(); }
static void _pf_period_cb(int v) { snprintf(_pf.period, sizeof(_pf.period), "%d", v); _mode = MODE_MENU; _rebuild_patch_form(); }
static void _pf_low_cb(const char* t) { strncpy(_pf.low, t, sizeof(_pf.low)-1); _mode = MODE_MENU; _rebuild_patch_form(); }
static void _pf_high_cb(const char* t) { strncpy(_pf.high, t, sizeof(_pf.high)-1); _mode = MODE_MENU; _rebuild_patch_form(); }
static void _pf_adr_mode_cb(int i) { strncpy(_pf.adr_mode, ADR_MODES[i], sizeof(_pf.adr_mode)-1); _mode = MODE_MENU; _rebuild_patch_form(); }

static void _on_patch_form_select(int idx) {
    switch (idx) {
        case 0: _start_text("Patch Name", _pf.name, _pf_name_cb); break;
        case 1: _start_ip("IP Address", _pf.ip, _pf_ip_cb); break;
        case 2: _start_num("Port", atoi(_pf.port), 1, 65535, _pf_port_cb); break;
        case 3: _start_text("OSC Address", _pf.address, _pf_addr_cb); break;
        case 4: _start_num("Period (ms)", atoi(_pf.period), 20, 60000, _pf_period_cb); break;
        case 5: _start_text("Low bound", _pf.low, _pf_low_cb); break;
        case 6: _start_text("High bound", _pf.high, _pf_high_cb); break;
        case 7: _start_pick("Address Mode", ADR_MODES, ADR_MODE_COUNT, _pf_adr_mode_cb); break;
        case 8: {
            if (!_pf.name[0]) { _show_reply("Name is required"); return; }
            char cfg[256];
            int pos = 0;
            auto ap = [&](const char* k, const char* v) {
                if (!v[0]) return;
                if (pos > 0) pos += snprintf(cfg + pos, sizeof(cfg) - pos, ", ");
                pos += snprintf(cfg + pos, sizeof(cfg) - pos, "%s:%s", k, v);
            };
            cfg[0] = 0;
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", _pf.ip[0], _pf.ip[1], _pf.ip[2], _pf.ip[3]);
            ap("ip", ip_str);
            ap("port", _pf.port);
            ap("adr", _pf.address);
            ap("period", _pf.period);
            ap("low", _pf.low);
            ap("high", _pf.high);
            _send_and_wait(_osc_addr3("patch", _pf.name, ""), cfg);
            break;
        }
    }
}

// ── Patch Detail ────────────────────────────────────────────────────────────

static void _build_patch_detail(const char* name) {
    strncpy(_sel_name, name, MAX_INPUT_LEN - 1);
    _push(name, _on_patch_detail);
    _add_item("Info");
    _add_item("Start");
    _add_item("Stop");
    _add_item("Set Period...");
    _add_item("Add Message...");
    _add_item("Remove Message...");
    _add_item("Set All...");
    _add_item("Delete");
}

static void _pd_period_done(int v) {
    char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
    _send_and_wait(_osc_addr3("patch", _sel_name, "period"), buf);
}
static void _pd_add_msg_done(const char* t) {
    _send_and_wait(_osc_addr3("patch", _sel_name, "addMsg"), t);
}
static void _pd_rm_msg_done(const char* t) {
    _send_and_wait(_osc_addr3("patch", _sel_name, "removeMsg"), t);
}
static void _pd_setall_done(const char* t) {
    _send_and_wait(_osc_addr3("patch", _sel_name, "setAll"), t);
}
static void _patch_del_cb(bool yes) {
    _mode = MODE_MENU;
    if (yes) _send_and_wait(_osc_addr3("patch", _sel_name, "delete"));
    else _pop();
}

static void _on_patch_detail(int idx) {
    switch (idx) {
        case 0: _send_and_wait(_osc_addr3("patch", _sel_name, "info")); break;
        case 1:
            osc_send_empty(target_ip_addr(), target_port,
                           _osc_addr3("patch", _sel_name, "start"));
            _show_reply("Sent start");
            break;
        case 2:
            osc_send_empty(target_ip_addr(), target_port,
                           _osc_addr3("patch", _sel_name, "stop"));
            _show_reply("Sent stop");
            break;
        case 3: _start_num("Period (ms)", 50, 20, 60000, _pd_period_done); break;
        case 4: _start_text("Msg name(s)", "", _pd_add_msg_done); break;
        case 5: _start_text("Msg to remove", "", _pd_rm_msg_done); break;
        case 6: _start_text("Config (k:v,...)", "", _pd_setall_done); break;
        case 7: _start_confirm("Delete patch?", _patch_del_cb); break;
    }
}

// ── Oris Menu ───────────────────────────────────────────────────────────────

static void _on_ori_menu(int idx);

static void _build_ori_menu() {
    _push("Oris", _on_ori_menu);
    _add_item("< Refresh >");
    _add_item("< Save New Ori >");
    _add_item("Threshold...");
    _add_item("Tolerance...");
    _add_item("Strict Toggle");
    _add_item("Clear All Oris");
    for (int i = 0; i < _dyn_count; i++) _add_item(_dyn_items[i]);
}

static void _on_ori_detail(int idx);
static void _ori_save_cb(const char* t);
static void _ori_thresh_cb(int v);
static void _ori_tol_cb(int v);
static void _ori_clear_cb(bool yes);

static void _on_ori_menu(int idx) {
    switch (idx) {
        case 0: _send_and_wait(_osc_addr("ori/list")); break;
        case 1: _start_text("Ori Name", "", _ori_save_cb); break;
        case 2: _start_num("Threshold (x10)", 15, 1, 100, _ori_thresh_cb); break;
        case 3: _start_num("Tolerance (deg)", 10, 1, 90, _ori_tol_cb); break;
        case 4:
            osc_send_empty(target_ip_addr(), target_port,
                           _osc_addr("ori/strict"));
            _show_reply("Toggled strict");
            break;
        case 5: _start_confirm("Clear ALL oris?", _ori_clear_cb); break;
        default: {
            int di = idx - 6;
            if (di >= 0 && di < _dyn_count) {
                strncpy(_sel_name, _dyn_items[di], MAX_INPUT_LEN - 1);
                _build_ori_detail(_sel_name);
            }
            break;
        }
    }
}

static void _ori_save_cb(const char* t) {
    if (!t[0]) { _mode = MODE_MENU; return; }
    char addr[128];
    snprintf(addr, sizeof(addr), "ori/save/%s", t);
    osc_send_empty(target_ip_addr(), target_port, _osc_addr(addr));
    _show_reply("Saved ori");
}

static void _ori_thresh_cb(int v) {
    char buf[8]; snprintf(buf, sizeof(buf), "%.1f", v / 10.0f);
    osc_send_string(target_ip_addr(), target_port,
                    _osc_addr("ori/threshold"), buf);
    _mode = MODE_MENU;
    _show_reply("Threshold set");
}

static void _ori_tol_cb(int v) {
    char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
    osc_send_string(target_ip_addr(), target_port,
                    _osc_addr("ori/tolerance"), buf);
    _mode = MODE_MENU;
    _show_reply("Tolerance set");
}

static void _ori_clear_cb(bool yes) {
    _mode = MODE_MENU;
    if (yes) {
        osc_send_empty(target_ip_addr(), target_port, _osc_addr("ori/clear"));
        _show_reply("Cleared all oris");
    }
}

// ── Ori Detail ──────────────────────────────────────────────────────────────

static void _build_ori_detail(const char* name) {
    strncpy(_sel_name, name, MAX_INPUT_LEN - 1);
    _push(name, _on_ori_detail);
    _add_item("Info");
    _add_item("Extend to Range");
    _add_item("Reset");
    _add_item("Delete");
}

static void _ori_del_cb(bool yes) {
    _mode = MODE_MENU;
    if (yes) {
        char addr[128];
        snprintf(addr, sizeof(addr), "ori/delete/%s", _sel_name);
        osc_send_empty(target_ip_addr(), target_port, _osc_addr(addr));
        _show_reply("Deleted ori");
    }
}

static void _on_ori_detail(int idx) {
    char addr[128];
    switch (idx) {
        case 0:
            snprintf(addr, sizeof(addr), "ori/info/%s", _sel_name);
            _send_and_wait(_osc_addr(addr));
            break;
        case 1:
            // Save the same name again to extend to range
            snprintf(addr, sizeof(addr), "ori/save/%s", _sel_name);
            osc_send_empty(target_ip_addr(), target_port, _osc_addr(addr));
            _show_reply("Extended to range");
            break;
        case 2:
            snprintf(addr, sizeof(addr), "ori/reset/%s", _sel_name);
            osc_send_empty(target_ip_addr(), target_port, _osc_addr(addr));
            _show_reply("Reset ori");
            break;
        case 3: _start_confirm("Delete ori?", _ori_del_cb); break;
    }
}

// ── Monitor ─────────────────────────────────────────────────────────────────

static void _on_monitor(int idx);

static void _build_monitor_menu() {
    _push("Monitor", _on_monitor);
    _add_item("List All (verbose)");
    _add_item("List Messages");
    _add_item("List Patches");
    _add_item("List Oris");
    _add_item("Device Status");
}

static void _on_monitor(int idx) {
    switch (idx) {
        case 0: _send_and_wait(_osc_addr("list/all"), "verbose"); break;
        case 1: _send_and_wait(_osc_addr("list/msgs"), "verbose"); break;
        case 2: _send_and_wait(_osc_addr("list/patches"), "verbose"); break;
        case 3: _send_and_wait(_osc_addr("ori/list")); break;
        case 4: _send_and_wait(_osc_addr("status/config")); break;
    }
}

// ── Quick Actions ───────────────────────────────────────────────────────────

static void _on_quick(int idx);
static void _quick_clear_cb(bool yes);

static void _build_quick_menu() {
    _push("Quick Actions", _on_quick);
    _add_item("Blackout");
    _add_item("Restore");
    _add_item("Save All");
    _add_item("Load All");
    _add_item("Clear NVS");
    _add_item("Dedup On");
    _add_item("Dedup Off");
    _add_item("Tare Euler");
    _add_item("Tare Reset");
}

static void _quick_clear_cb(bool yes) {
    _mode = MODE_MENU;
    if (yes) {
        osc_send_empty(target_ip_addr(), target_port, _osc_addr("nvs/clear"));
        _show_reply("NVS cleared");
    }
}

static void _on_quick(int idx) {
    switch (idx) {
        case 0:
            osc_send_empty(target_ip_addr(), target_port, _osc_addr("blackout"));
            _show_reply("Blackout sent");
            break;
        case 1:
            osc_send_empty(target_ip_addr(), target_port, _osc_addr("restore"));
            _show_reply("Restore sent");
            break;
        case 2:
            _send_and_wait(_osc_addr("save"));
            break;
        case 3:
            _send_and_wait(_osc_addr("load"));
            break;
        case 4: _start_confirm("Clear device NVS?", _quick_clear_cb); break;
        case 5:
            osc_send_string(target_ip_addr(), target_port,
                            _osc_addr("dedup"), "on");
            _show_reply("Dedup on");
            break;
        case 6:
            osc_send_string(target_ip_addr(), target_port,
                            _osc_addr("dedup"), "off");
            _show_reply("Dedup off");
            break;
        case 7:
            osc_send_empty(target_ip_addr(), target_port, _osc_addr("tare"));
            _show_reply("Tare set");
            break;
        case 8:
            osc_send_empty(target_ip_addr(), target_port, _osc_addr("tare/reset"));
            _show_reply("Tare reset");
            break;
    }
}

// ── Settings ────────────────────────────────────────────────────────────────

static void _on_settings(int idx);

static void _build_settings_menu() {
    _push("Settings", _on_settings);
    char buf[MAX_ITEM_LEN];
    snprintf(buf, MAX_ITEM_LEN, "SSID: %s", net_ssid[0] ? net_ssid : "(none)");
    _add_item(buf);
    _add_item("WiFi Password...");
    snprintf(buf, MAX_ITEM_LEN, "Device: %s", target_name);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "Target IP: %d.%d.%d.%d",
             target_ip[0], target_ip[1], target_ip[2], target_ip[3]);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "Target Port: %d", target_port);
    _add_item(buf);
    snprintf(buf, MAX_ITEM_LEN, "Listen Port: %d", listen_port);
    _add_item(buf);
    _add_item(">> Save & Reconnect <<");
}

// Helper: rebuild settings menu while preserving cursor position.
static void _rebuild_settings(int sel) {
    _pop();
    _build_settings_menu();
    MenuScreen& s = _cur();
    if (sel >= s.count) sel = s.count - 1;
    s.selected = sel;
    if (s.selected >= s.scroll + VISIBLE_ITEMS)
        s.scroll = s.selected - VISIBLE_ITEMS + 1;
}

static void _set_ssid_cb(const char* t) { strncpy(net_ssid, t, sizeof(net_ssid)-1); _mode = MODE_MENU; _rebuild_settings(0); }
static void _set_pass_cb(const char* t) { strncpy(net_pass, t, sizeof(net_pass)-1); _mode = MODE_MENU; _rebuild_settings(1); }
static void _set_dev_cb(const char* t)  { strncpy(target_name, t, sizeof(target_name)-1); _mode = MODE_MENU; _rebuild_settings(2); }
static void _set_tip_cb(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    target_ip[0]=a; target_ip[1]=b; target_ip[2]=c; target_ip[3]=d;
    _mode = MODE_MENU; _rebuild_settings(3);
}
static void _set_tport_cb(int v) { target_port = v; _mode = MODE_MENU; _rebuild_settings(4); }
static void _set_lport_cb(int v) { listen_port = v; _mode = MODE_MENU; _rebuild_settings(5); }

static void _on_settings(int idx) {
    switch (idx) {
        case 0: _start_text("WiFi SSID", net_ssid, _set_ssid_cb); break;
        case 1: _start_text("WiFi Password", "", _set_pass_cb); break;
        case 2: _start_text("Device Name", target_name, _set_dev_cb); break;
        case 3: _start_ip("Target IP", target_ip, _set_tip_cb); break;
        case 4: _start_num("Target Port", target_port, 1, 65535, _set_tport_cb); break;
        case 5: _start_num("Listen Port", listen_port, 1, 65535, _set_lport_cb); break;
        case 6:
            net_save();
            net_disconnect();
            net_connect();
            osc_init(listen_port);
            _show_reply("Saved. Reconnecting...");
            break;
    }
}

// ============================================================================
//  Overlay Input Handlers
// ============================================================================

static void _handle_text(InputEvent ev) {
    switch (ev) {
        case EVT_ENC_CW:
            _ti_ci = (_ti_ci + 1) % CHARSET_LEN;
            break;
        case EVT_ENC_CCW:
            _ti_ci = (_ti_ci - 1 + CHARSET_LEN) % CHARSET_LEN;
            break;
        case EVT_SELECT:
        case EVT_RIGHT:
        case EVT_ENC_PRESS:
            // Add current character
            if (_ti_pos < MAX_INPUT_LEN - 1) {
                _ti_buf[_ti_pos] = CHARSET[_ti_ci];
                _ti_pos++;
                _ti_buf[_ti_pos] = 0;
            }
            break;
        case EVT_LEFT:
            // Backspace
            if (_ti_pos > 0) { _ti_pos--; _ti_buf[_ti_pos] = 0; }
            break;
        case EVT_DOWN:
            // Confirm
            if (_ti_cb) _ti_cb(_ti_buf);
            break;
        case EVT_UP:
            // Cancel
            _mode = MODE_MENU;
            break;
        default: break;
    }
}

static void _handle_num(InputEvent ev) {
    switch (ev) {
        case EVT_ENC_CW:
            if (_ni_val < _ni_hi) _ni_val++;
            break;
        case EVT_ENC_CCW:
            if (_ni_val > _ni_lo) _ni_val--;
            break;
        case EVT_RIGHT:
            _ni_val += 10;
            if (_ni_val > _ni_hi) _ni_val = _ni_hi;
            break;
        case EVT_LEFT:
            _ni_val -= 10;
            if (_ni_val < _ni_lo) _ni_val = _ni_lo;
            break;
        case EVT_DOWN:
        case EVT_SELECT:
        case EVT_ENC_PRESS:
            if (_ni_cb) _ni_cb(_ni_val);
            break;
        case EVT_UP:
            _mode = MODE_MENU;
            break;
        default: break;
    }
}

static void _handle_ip(InputEvent ev) {
    switch (ev) {
        case EVT_ENC_CW:
            if (_ip_oct[_ip_idx] < 255) _ip_oct[_ip_idx]++;
            break;
        case EVT_ENC_CCW:
            if (_ip_oct[_ip_idx] > 0) _ip_oct[_ip_idx]--;
            break;
        case EVT_RIGHT:
            if (_ip_idx < 3) _ip_idx++;
            break;
        case EVT_LEFT:
            if (_ip_idx > 0) _ip_idx--;
            break;
        case EVT_DOWN:
        case EVT_SELECT:
        case EVT_ENC_PRESS:
            if (_ip_cb) _ip_cb(_ip_oct[0], _ip_oct[1], _ip_oct[2], _ip_oct[3]);
            break;
        case EVT_UP:
            _mode = MODE_MENU;
            break;
        default: break;
    }
}

static void _handle_pick(InputEvent ev) {
    switch (ev) {
        case EVT_ENC_CW:
        case EVT_DOWN:
            if (_pk_sel < _pk_count - 1) {
                _pk_sel++;
                if (_pk_sel >= _pk_scroll + VISIBLE_ITEMS)
                    _pk_scroll = _pk_sel - VISIBLE_ITEMS + 1;
            }
            break;
        case EVT_ENC_CCW:
        case EVT_UP:
            if (_pk_sel > 0) {
                _pk_sel--;
                if (_pk_sel < _pk_scroll) _pk_scroll = _pk_sel;
            }
            break;
        case EVT_SELECT:
        case EVT_RIGHT:
        case EVT_ENC_PRESS:
            if (_pk_cb) _pk_cb(_pk_sel);
            break;
        case EVT_LEFT:
            _mode = MODE_MENU;
            break;
        default: break;
    }
}

static void _handle_confirm(InputEvent ev) {
    switch (ev) {
        case EVT_LEFT:
        case EVT_ENC_CCW:
            _cf_yes = true;
            break;
        case EVT_RIGHT:
        case EVT_ENC_CW:
            _cf_yes = false;
            break;
        case EVT_SELECT:
        case EVT_ENC_PRESS:
        case EVT_DOWN:
            if (_cf_cb) _cf_cb(_cf_yes);
            break;
        case EVT_UP:
            _mode = MODE_MENU;
            break;
        default: break;
    }
}

static void _handle_reply(InputEvent ev) {
    switch (ev) {
        case EVT_ENC_CW:
        case EVT_DOWN:
            if (_rp_scroll < _rp_lines - CONTENT_ROWS) _rp_scroll++;
            break;
        case EVT_ENC_CCW:
        case EVT_UP:
            if (_rp_scroll > 0) _rp_scroll--;
            break;
        case EVT_LEFT:
        case EVT_SELECT:
        case EVT_ENC_PRESS:
            _mode = MODE_MENU;
            break;
        default: break;
    }
}

static void _handle_menu(InputEvent ev) {
    MenuScreen& s = _cur();
    switch (ev) {
        case EVT_ENC_CW:
        case EVT_DOWN:
            if (s.selected < s.count - 1) {
                s.selected++;
                if (s.selected >= s.scroll + VISIBLE_ITEMS)
                    s.scroll = s.selected - VISIBLE_ITEMS + 1;
            }
            break;
        case EVT_ENC_CCW:
        case EVT_UP:
            if (s.selected > 0) {
                s.selected--;
                if (s.selected < s.scroll) s.scroll = s.selected;
            }
            break;
        case EVT_SELECT:
        case EVT_RIGHT:
        case EVT_ENC_PRESS:
            if (s.on_select) s.on_select(s.selected);
            break;
        case EVT_LEFT:
            _pop();
            break;
        default: break;
    }
}

// ============================================================================
//  Drawing
// ============================================================================

static void _draw() {
    disp_clear();

    switch (_mode) {
        case MODE_MENU: {
            MenuScreen& s = _cur();
            disp_title(s.title);
            disp_menu(s.items, s.count, s.selected, s.scroll);
            // Status bar: WiFi + target
            char st[22];
            if (net_state() == NET_CONNECTED)
                snprintf(st, sizeof(st), "\x10 %s %d.%d.%d.%d",
                         target_name, target_ip[0], target_ip[1],
                         target_ip[2], target_ip[3]);
            else
                snprintf(st, sizeof(st), "WiFi: %s",
                         net_state() == NET_CONNECTING ? "..." : "off");
            disp_status(st);
            break;
        }
        case MODE_TEXT_INPUT:
            disp_text_input(_ti_title, _ti_buf, _ti_pos, CHARSET[_ti_ci]);
            break;
        case MODE_NUM_INPUT:
            disp_num_input(_ni_title, _ni_val, _ni_lo, _ni_hi);
            break;
        case MODE_IP_INPUT:
            disp_ip_input(_ip_title, _ip_oct, _ip_idx);
            break;
        case MODE_PICK:
            disp_pick(_pk_title, _pk_opts, _pk_count, _pk_sel, _pk_scroll);
            break;
        case MODE_CONFIRM:
            disp_title("Confirm");
            disp_confirm(_cf_text, _cf_yes);
            break;
        case MODE_REPLY:
            disp_title("Reply");
            _rp_lines = disp_wrapped(_rp_text, _rp_scroll);
            disp_status("\x11""back  \x12""scroll");
            break;
        case MODE_WAITING: {
            disp_title("Waiting...");
            int dot = ((millis() - _wait_start) / 300) % 4;
            char dots[5] = "    ";
            for (int i = 0; i < dot; i++) dots[i] = '.';
            disp_message("Waiting for reply", dots);
            break;
        }
    }
    disp_show();
}

// ============================================================================
//  Public API
// ============================================================================

static void ui_init() {
    _build_main_menu();
    _mode = MODE_MENU;
}

// Called when an OSC reply arrives (from main loop).
static void ui_handle_reply(const OscReply& reply) {
    if (_mode == MODE_WAITING) {
        _waiting_reply = false;

        // Check if this was a list reply — populate dynamic items
        const char* addr = reply.address;
        bool is_list = (strstr(addr, "/list/") != nullptr) ||
                       (strstr(addr, "/ori/list") != nullptr);
        if (is_list && reply.payload[0]) {
            _parse_list(reply.payload);
        }

        // Show the reply text
        if (reply.payload[0])
            _show_reply(reply.payload);
        else
            _show_reply("(empty reply)");

        // If this was a list query that populated dyn items, rebuild the
        // parent menu so the user sees the results when they dismiss.
    }
}

// Main update — call once per loop().
static void ui_update() {
    // ── Check for waiting timeout ───────────────────────────────────────
    if (_mode == MODE_WAITING) {
        if (millis() - _wait_start > REPLY_TIMEOUT_MS) {
            _show_reply("No reply (timeout)");
        }
        // Spin NeoPixels while waiting
        int pos = ((millis() - _wait_start) / 100) % SS_NEOPIX_NUM;
        led_spin(80, 80, 0, pos);
    }

    // ── Poll OSC replies ────────────────────────────────────────────────
    OscReply reply;
    if (osc_poll(reply)) {
        ui_handle_reply(reply);
    }

    // ── Poll input ──────────────────────────────────────────────────────
    InputEvent ev = input_read();
    if (ev != EVT_NONE) {
        switch (_mode) {
            case MODE_MENU:       _handle_menu(ev);    break;
            case MODE_TEXT_INPUT: _handle_text(ev);    break;
            case MODE_NUM_INPUT:  _handle_num(ev);     break;
            case MODE_IP_INPUT:   _handle_ip(ev);     break;
            case MODE_PICK:       _handle_pick(ev);    break;
            case MODE_CONFIRM:    _handle_confirm(ev); break;
            case MODE_REPLY:      _handle_reply(ev);   break;
            case MODE_WAITING:
                // Allow cancel while waiting
                if (ev == EVT_LEFT || ev == EVT_UP) {
                    _waiting_reply = false;
                    _mode = MODE_MENU;
                }
                break;
        }
    }

    // ── NeoPixel status (when not in waiting mode) ──────────────────────
    if (_mode != MODE_WAITING) {
        if (net_state() == NET_CONNECTED)
            led_set(0, 15, 0);       // dim green
        else if (net_state() == NET_CONNECTING)
            led_spin(0, 0, 60, (millis() / 150) % SS_NEOPIX_NUM);
        else
            led_set(15, 0, 0);       // dim red
    }

    // ── Redraw ──────────────────────────────────────────────────────────
    _draw();
}

#endif // UI_H
