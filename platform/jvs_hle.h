// ASCA-3 I/O board replaced by native code: it answers the H8's JVS packets like the asca-3a.ic14 firmware.
// The replies were derived from the original emulated firmware (test/jvs_probe.cpp compares them byte by byte):
//   - identity "namco ltd.;ASCA-3;Ver2.02;JPN,Multipurpose + Rotary Encoder", revisions 1.1 / 2.0 / 1.0
//   - features: 1 player x 19 buttons, 2 coin slots, 8 analog, 2 encoders, 12 outputs, 1 analog output
//   - Namco 0x70 commands: 04 -> ff ff, 02 -> firmware date, 18 -> 01 only on the first request after power-on
//   - buttons: system = bit 7 (test); player 1 = p1 & 0x73, (p1 >> 8) & 0xff, (p1 >> 16) & 0xe0 (like MAME's in_r)
//   - coins: one per press of bit 0 of coin1/coin2; analogs 4-7 repeat 0-3 (value & 0xffc0)
// "sense" line: 1 = address not assigned, 2 = assigned (F1 command), back to 1 with the F0 reset.
#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <functional>
#include <vector>

struct JvsHle {
    AscaInputs* in = nullptr;                                   // same inputs as the emulated firmware
    int sense = 1;
    std::function<void(const std::vector<uint8_t>&)> on_tx;     // reply ready (already with E0 and escape characters)
    uint64_t rx_bytes = 0, tx_bytes = 0, packets = 0, bad = 0;

    void rx(uint8_t b) {                                        // a byte from the H8 (SCI0)
        rx_bytes++;
        if (b == 0xe0) { buf_.clear(); sync_ = true; esc_ = false; return; }
        if (!sync_) return;
        if (b == 0xd0) { esc_ = true; return; }
        if (esc_) { b++; esc_ = false; }
        buf_.push_back(b);
        if (buf_.size() >= 2 && buf_.size() == (size_t)buf_[1] + 2) { sync_ = false; packet(); }
    }
    // coins are counted here: call often (every video line is fine); dt in seconds, unused for now
    void poll_time(double) { coins(); }

private:
    std::vector<uint8_t> buf_, last_;
    bool sync_ = false, esc_ = false, first70_ = true;
    uint8_t addr_ = 0; uint16_t coin_[2] = {0, 0}; bool coin_prev_[2] = {false, false};

    void coins() {
        if (!in) return;
        const bool c[2] = {(in->coin1 & 1) != 0, (in->coin2 & 1) != 0};
        for (int i = 0; i < 2; i++) { if (c[i] && !coin_prev_[i] && sense == 2) coin_[i] = (uint16_t)((coin_[i] + 1) & 0x3fff); coin_prev_[i] = c[i]; }
    }
    void send(const std::vector<uint8_t>& body) {               // body = status + reports; adds node 00, length, checksum
        std::vector<uint8_t> raw = {0x00, (uint8_t)(body.size() + 1)}; raw.insert(raw.end(), body.begin(), body.end());
        uint8_t sum = 0; for (uint8_t x : raw) sum += x; raw.push_back(sum);
        std::vector<uint8_t> out = {0xe0};
        for (uint8_t x : raw) { if (x == 0xe0 || x == 0xd0) { out.push_back(0xd0); out.push_back((uint8_t)(x - 1)); } else out.push_back(x); }
        last_ = out; tx_bytes += out.size();
        if (on_tx) on_tx(out);
    }
    static void put16(std::vector<uint8_t>& r, uint16_t v) { r.push_back(v >> 8); r.push_back(v & 0xff); }

    void packet() {
        const uint8_t node = buf_[0], len = buf_[1];
        uint8_t sum = 0; for (size_t i = 0; i + 1 < buf_.size(); i++) sum += buf_[i];
        if (sum != buf_.back()) { bad++; return; }
        packets++;
        if (node != 0xff && (addr_ == 0 || node != addr_)) return;  // not for us (or address not yet assigned)
        const uint8_t* d = buf_.data() + 2; const int n = len - 1;
        coins();
        std::vector<uint8_t> r = {0x01};                           // status: 1 = normal
        int i = 0; bool reply = true;
        auto need = [&](int k) { return i + k <= n; };
        while (i < n) {
            const uint8_t c = d[i++];
            switch (c) {
            case 0xf0: if (!need(1)) goto bad_len; i++; sense = 1; addr_ = 0; coin_[0] = coin_[1] = 0; reply = false; break;   // reset: no reply
            case 0xf1: if (!need(1)) goto bad_len;
                       if (sense == 2) { reply = false; i++; break; }                   // already addressed: the command is for the next board
                       addr_ = d[i++]; sense = 2; r.push_back(1); break;
            case 0x10: { static const char id[] = "namco ltd.;ASCA-3;Ver2.02;JPN,Multipurpose + Rotary Encoder"; r.push_back(1); r.insert(r.end(), id, id + sizeof id); break; }
            case 0x11: r.push_back(1); r.push_back(0x11); break;
            case 0x12: r.push_back(1); r.push_back(0x20); break;
            case 0x13: r.push_back(1); r.push_back(0x10); break;
            case 0x14: { static const uint8_t f[] = {0x01, 0x01, 0x13, 0x00, 0x02, 0x02, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x12, 0x0c, 0x00, 0x00, 0x13, 0x01, 0x00, 0x00, 0x00};
                         r.push_back(1); r.insert(r.end(), f, f + sizeof f); break; }
            case 0x15: { while (i < n && d[i]) i++; if (i < n) i++; r.push_back(1); break; }   // host identity: ignored
            case 0x20: { if (!need(2)) goto bad_len; const int pl = d[i], nb = d[i + 1]; i += 2; r.push_back(1);
                         const uint32_t p1 = in ? in->p1 : 0; r.push_back(in ? (uint8_t)(in->system & 0x80) : 0);
                         for (int p = 0; p < pl; p++) for (int b = 0; b < nb; b++) {
                             uint8_t v = 0; if (p == 0) { if (b == 0) v = p1 & 0x73; else if (b == 1) v = (p1 >> 8) & 0xff; else if (b == 2) v = (p1 >> 16) & 0xe0; }
                             r.push_back(v); }
                         break; }
            case 0x21: { if (!need(1)) goto bad_len; const int k = d[i++]; r.push_back(1); for (int s = 0; s < k; s++) put16(r, s < 2 ? coin_[s] : 0); break; }
            case 0x22: { if (!need(1)) goto bad_len; const int k = d[i++]; r.push_back(1); for (int s = 0; s < k; s++) put16(r, in ? (uint16_t)(in->analog[s & 3] & 0xffc0) : 0x8000); break; }
            case 0x23: { if (!need(1)) goto bad_len; const int k = d[i++]; r.push_back(1); for (int s = 0; s < k; s++) put16(r, (s < 2 && in) ? (uint16_t)(int16_t)(int8_t)in->rotary[s] : 0); break; }
            case 0x2f: if (!last_.empty() && on_tx) { on_tx(last_); } return;                      // retransmission
            case 0x30: case 0x35: { if (!need(3)) goto bad_len; const int s = d[i] - 1; const uint16_t a = (uint16_t)((d[i + 1] << 8) | d[i + 2]); i += 3;
                         if (s >= 0 && s < 2) { coin_[s] = (uint16_t)(c == 0x30 ? (coin_[s] > a ? coin_[s] - a : 0) : std::min(0x3fff, coin_[s] + a)); }
                         r.push_back(1); break; }
            case 0x32: { if (!need(1)) goto bad_len; const int k = d[i++]; if (!need(k)) goto bad_len; i += k; r.push_back(1); break; }          // lamps / coin counter
            case 0x33: { if (!need(1)) goto bad_len; const int k = d[i++]; if (!need(2 * k)) goto bad_len; i += 2 * k; r.push_back(1); break; }  // analog output
            case 0x70: { if (!need(1)) goto bad_len; const uint8_t s = d[i++];
                         if (s == 0x04) { r.push_back(1); r.push_back(0xff); r.push_back(0xff); }
                         else if (s == 0x02) { static const uint8_t v[] = {0x19, 0x97, 0x07, 0x29, 0x02, 0x00, 0x25, 0x34}; r.push_back(1); r.insert(r.end(), v, v + sizeof v); }
                         else if (s == 0x18) { if (!need(4)) goto bad_len; i += 4; r.push_back(1); r.push_back(first70_ ? 1 : 0); first70_ = false; }
                         else { r[0] = 2; i = n; }
                         break; }
            default: r[0] = 2; i = n; break;                                                        // unknown command
            }
        }
        if (reply) send(r);
        return;
    bad_len:
        r[0] = 2; send(r);
    }
};
