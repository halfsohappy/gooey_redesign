#ifndef OSC_REMOTE_H
#define OSC_REMOTE_H

// ============================================================================
//  TheaterGWD Setup Remote — Minimal OSC over WiFiUDP
// ============================================================================
//
//  Provides lightweight OSC message packing and parsing so the remote can
//  send commands to TheaterGWD devices and read their replies.  No external
//  OSC library is needed — the protocol is simple enough to handle inline.
//
//  Public API
//  ----------
//  osc_init(listen_port)               bind UDP socket
//  osc_send_string(ip, port, addr, s)  send OSC message with one string arg
//  osc_send_empty(ip, port, addr)      send OSC message with no args
//  osc_send_float(ip, port, addr, f)   send OSC message with one float arg
//  osc_poll(reply)                     non-blocking receive; true if got msg
//
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"

// ── Reply structure ─────────────────────────────────────────────────────────

struct OscReply {
    char address[128];
    char payload[MAX_REPLY_LEN];
    bool is_string;       // true = string arg, false = float formatted as text
};

// ── Internal state ──────────────────────────────────────────────────────────

static WiFiUDP _udp;
static uint8_t _osc_buf[OSC_BUF_SIZE];

// ── Helpers ─────────────────────────────────────────────────────────────────

// Pad length up to next 4-byte boundary.
static inline int _osc_pad(int len) {
    return (len + 3) & ~3;
}

// Write a null-terminated string into buf with OSC 4-byte padding.
// Returns number of bytes written.
static int _osc_write_str(uint8_t* buf, const char* str) {
    int raw = strlen(str) + 1;          // include null terminator
    int padded = _osc_pad(raw);
    memcpy(buf, str, raw);
    if (padded > raw) memset(buf + raw, 0, padded - raw);
    return padded;
}

// Write a big-endian IEEE-754 float.
static void _osc_write_f32(uint8_t* buf, float val) {
    union { float f; uint32_t u; } v;
    v.f = val;
    buf[0] = (v.u >> 24) & 0xFF;
    buf[1] = (v.u >> 16) & 0xFF;
    buf[2] = (v.u >>  8) & 0xFF;
    buf[3] =  v.u        & 0xFF;
}

// Read a big-endian IEEE-754 float.
static float _osc_read_f32(const uint8_t* buf) {
    union { float f; uint32_t u; } v;
    v.u = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
          ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
    return v.f;
}

// ── Public API ──────────────────────────────────────────────────────────────

static void osc_init(uint16_t listen_port) {
    _udp.begin(listen_port);
}

// Send an OSC message carrying a single string argument.
static void osc_send_string(IPAddress ip, uint16_t port,
                            const char* address, const char* arg) {
    int pos = 0;
    pos += _osc_write_str(_osc_buf + pos, address);
    pos += _osc_write_str(_osc_buf + pos, ",s");
    pos += _osc_write_str(_osc_buf + pos, arg);
    _udp.beginPacket(ip, port);
    _udp.write(_osc_buf, pos);
    _udp.endPacket();
}

// Send an OSC message with no arguments.
static void osc_send_empty(IPAddress ip, uint16_t port,
                           const char* address) {
    int pos = 0;
    pos += _osc_write_str(_osc_buf + pos, address);
    pos += _osc_write_str(_osc_buf + pos, ",");
    _udp.beginPacket(ip, port);
    _udp.write(_osc_buf, pos);
    _udp.endPacket();
}

// Send an OSC message carrying a single float argument.
static void osc_send_float(IPAddress ip, uint16_t port,
                           const char* address, float val) {
    int pos = 0;
    pos += _osc_write_str(_osc_buf + pos, address);
    pos += _osc_write_str(_osc_buf + pos, ",f");
    _osc_write_f32(_osc_buf + pos, val);
    pos += 4;
    _udp.beginPacket(ip, port);
    _udp.write(_osc_buf, pos);
    _udp.endPacket();
}

// Non-blocking poll for an incoming OSC message.
// Returns true and fills `reply` when a message is available.
static bool osc_poll(OscReply& reply) {
    int pkt = _udp.parsePacket();
    if (pkt <= 0) return false;

    int len = _udp.read(_osc_buf, OSC_BUF_SIZE);
    if (len < 4 || _osc_buf[0] != '/') return false;

    // ── read address ────────────────────────────────────────────────────
    int end = 0;
    while (end < len && _osc_buf[end] != 0) end++;
    int addr_len = end;
    if (addr_len >= (int)sizeof(reply.address))
        addr_len = sizeof(reply.address) - 1;
    memcpy(reply.address, _osc_buf, addr_len);
    reply.address[addr_len] = 0;
    int pos = _osc_pad(end + 1);

    // ── read type tag ───────────────────────────────────────────────────
    reply.payload[0] = 0;
    reply.is_string  = true;
    if (pos >= len || _osc_buf[pos] != ',') return true;

    char type = (pos + 1 < len) ? _osc_buf[pos + 1] : 0;
    int tag_end = pos;
    while (tag_end < len && _osc_buf[tag_end] != 0) tag_end++;
    if (tag_end >= len) return true;   // malformed — no null terminator
    pos = _osc_pad(tag_end + 1);
    if (pos >= len) return true;       // no argument data follows

    // ── read argument ───────────────────────────────────────────────────
    if (type == 's' && pos < len) {
        int s = pos;
        while (s < len && _osc_buf[s] != 0) s++;
        int slen = s - pos;
        if (slen >= MAX_REPLY_LEN) slen = MAX_REPLY_LEN - 1;
        memcpy(reply.payload, _osc_buf + pos, slen);
        reply.payload[slen] = 0;
        reply.is_string = true;
    } else if (type == 'f' && pos + 4 <= len) {
        float f = _osc_read_f32(_osc_buf + pos);
        snprintf(reply.payload, MAX_REPLY_LEN, "%.4f", f);
        reply.is_string = false;
    }
    return true;
}

#endif // OSC_REMOTE_H
