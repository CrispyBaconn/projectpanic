// ASCA-3 I/O board probe: sends JVS packets to the original emulated firmware (asca-3a.ic14) and prints the replies.
// Used to derive (and then verify) the native reply of platform/jvs_hle.h.
//   g++ -std=c++17 -O1 -I. test/jvs_probe.cpp -o /tmp/jvs_probe && /tmp/jvs_probe panicprkrom/asca-3a.ic14 [hle]
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include "h8/ascasys.h"
#include "platform/jvs_hle.h"

static std::vector<uint8_t> pkt(uint8_t node, std::vector<uint8_t> data) {
    std::vector<uint8_t> raw = {node, (uint8_t)(data.size() + 1)}; raw.insert(raw.end(), data.begin(), data.end());
    uint8_t sum = 0; for (uint8_t b : raw) sum += b; raw.push_back(sum);
    std::vector<uint8_t> out = {0xe0};
    for (uint8_t b : raw) { if (b == 0xe0 || b == 0xd0) { out.push_back(0xd0); out.push_back(b - 1); } else out.push_back(b); }
    return out;
}
static std::vector<uint8_t> unesc(const std::vector<uint8_t>& v) {
    std::vector<uint8_t> o; bool e = false; for (size_t i = 1; i < v.size(); i++) { uint8_t b = v[i]; if (b == 0xd0) { e = true; continue; } if (e) { b++; e = false; } o.push_back(b); } return o;
}

struct Board {
    virtual ~Board() {}
    virtual std::vector<uint8_t> xfer(const std::vector<uint8_t>& p) = 0;     // packet -> raw reply (with E0)
    virtual int sense() = 0;
};
struct Real : Board {
    AscaSys a; std::vector<uint8_t> tx;
    Real(const char* rom, AscaInputs* in) { a.load_rom(rom); a.in = in; a.reset(); a.on_tx = [this](uint8_t b) { tx.push_back(b); }; a.run_to(AscaSys::CLOCK * 0.2); }
    std::vector<uint8_t> xfer(const std::vector<uint8_t>& p) override {
        tx.clear(); a.push_rx(p); uint64_t c = a.cpu.m_cycles; a.run_to(c + (uint64_t)(AscaSys::CLOCK * 0.03)); return tx;
    }
    int sense() override { return a.sense_ext; }
};
struct Hle : Board {
    JvsHle j; std::vector<uint8_t> tx;
    Hle(AscaInputs* in) { j.in = in; j.on_tx = [this](const std::vector<uint8_t>& v) { tx.insert(tx.end(), v.begin(), v.end()); }; }
    std::vector<uint8_t> xfer(const std::vector<uint8_t>& p) override { tx.clear(); for (uint8_t b : p) j.rx(b); j.poll_time(0.03); return tx; }
    int sense() override { return j.sense; }
};

static std::string hex(const std::vector<uint8_t>& v) { std::string s; char b[4]; for (uint8_t x : v) { snprintf(b, sizeof b, "%02x ", x); s += b; } return s; }

int main(int argc, char** argv) {
    static AscaInputs in;
    const bool hle = argc > 2 && !strcmp(argv[2], "hle");
    Board* B = hle ? (Board*)new Hle(&in) : (Board*)new Real(argv[1], &in);
    auto X = [&](const char* tag, std::vector<uint8_t> data, uint8_t node = 1) { auto r = B->xfer(pkt(node, data)); printf("%-10s %s| sense=%d\n", tag, hex(r.empty() ? r : unesc(r)).c_str(), B->sense()); };
    const std::vector<uint8_t> poll = {0x20, 0x01, 0x03, 0x21, 0x02, 0x22, 0x08, 0x23, 0x02, 0x32, 0x02, 0x00, 0x00, 0x33, 0x01, 0x00, 0x00};
    printf("initial sense=%d\n", B->sense());
    X("reset", {0xf0, 0xd9}, 0xff); X("reset", {0xf0, 0xd9}, 0xff);
    X("addr", {0xf1, 0x01}, 0xff);
    X("rev", {0x11, 0x12, 0x13, 0x14}); X("id", {0x10}); X("70 04/02", {0x70, 0x04, 0x70, 0x02}); X("70 18", {0x70, 0x18, 0x50, 0x4c, 0x01, 0x04});
    X("poll", poll);
    for (int b = 0; b < 32; b++) { in.p1 = 1u << b; char t[16]; snprintf(t, sizeof t, "p1 bit%d", b); X(t, poll); } in.p1 = 0;
    for (int b = 0; b < 8; b++) { in.system = 1u << b; char t[16]; snprintf(t, sizeof t, "sys bit%d", b); X(t, poll); } in.system = 0;
    for (int b = 0; b < 8; b++) { in.coin1 = 1u << b; char t[16]; snprintf(t, sizeof t, "c1 bit%d", b); X(t, poll); in.coin1 = 0; X("c1 off", poll); }
    for (int b = 0; b < 8; b++) { in.coin2 = 1u << b; char t[16]; snprintf(t, sizeof t, "c2 bit%d", b); X(t, poll); in.coin2 = 0; X("c2 off", poll); }
    for (int k = 0; k < 8; k++) { in.analog[k] = (uint16_t)(0x1234 + k * 0x1111); } X("analog", poll); for (auto& a : in.analog) a = 0x8000;
    in.rotary[0] = 0x12; in.rotary[1] = 0xfe; X("rotary", poll); in.rotary[0] = in.rotary[1] = 0;
    X("out 80", {0x20, 0x01, 0x03, 0x21, 0x02, 0x22, 0x08, 0x23, 0x02, 0x32, 0x02, 0x80, 0x00, 0x33, 0x01, 0x00, 0x00});
    X("70 18 bis", {0x70, 0x18, 0x50, 0x4c, 0x01, 0x04});
    X("main id", {0x15, 'N', 'A', 'M', 'C', 'O', 0});
    X("reset", {0xf0, 0xd9}, 0xff);
    X("poll noaddr", poll);
    X("addr", {0xf1, 0x01}, 0xff);
    X("70 18 ter", {0x70, 0x18, 0x50, 0x4c, 0x01, 0x04});
    X("unknown 99", {0x99});
    X("coin dec", {0x30, 0x01, 0x00, 0x01});
    X("poll", poll);
    return 0;
}
