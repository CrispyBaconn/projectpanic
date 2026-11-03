// Namco ASCA-3 I/O board: H8/3334 (H8/300) running the original asca-3a.ic14 firmware.
// Map and wiring from bus/jvs/namcoio.cpp and cpu/h8/h83337.cpp (MAME). license:BSD-3-Clause (derived from MAME)
#pragma once
#include <cstdlib>
#include "h8cpu.h"
#include "h8cpu_o.h"
#include <deque>
#include <functional>
#include <string>

struct AscaInputs {                          // written by the frontend (another thread), read by the ASCA firmware
    volatile uint32_t p1 = 0;                // like MAME's JVS_PLAYER1 port (0x20 up, 0x10 down, 0x02 enter, 0x01 start1, 0x8000 start2 ...)
    volatile uint8_t  system = 0;            // bit7 = test/service
    volatile uint8_t  coin1 = 0, coin2 = 0;  // bit0 = coin slot, bit7 = counter disconnected
    volatile uint16_t analog[8] = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    volatile uint8_t  rotary[2] = {0, 0};
};

class AscaSys : public H8Bus {
public:
    static constexpr double CLOCK = 14745600.0;
    H8oCpu cpu;
    std::vector<uint8_t> rom;                // 256 KB, first 16 KB visible
    uint8_t ram[0x3f80] = {0};               // 0xC000-0xFF7F
    uint8_t reg[0x80] = {0};                 // 0xFF80-0xFFFF
    AscaInputs* in = nullptr;
    uint8_t dsw = 0x0f;
    bool sense_initialized = false;          // port 4 bit 2
    std::function<void(uint8_t)> on_tx;      // byte sent on SCI0 (towards the JVS bus)
    std::deque<uint8_t> rxq; int64_t rx_wait = 0;
    uint64_t unmapped_reads = 0, unmapped_writes = 0, tx_bytes = 0, rx_bytes = 0;

    struct T8 { uint8_t tcr = 0, tcsr = 0, tcora = 0xff, tcorb = 0xff, tcnt = 0; uint64_t acc = 0; };
    T8 t8[2];
    struct Sci { uint8_t smr = 0, brr = 0xff, scr = 0, tdr = 0xff, ssr = 0x84, rdr = 0; int64_t tx_left = 0; uint8_t tx_byte = 0; bool tdr_full = false; } sci;
    uint8_t adcsr = 0, adcr = 0; int64_t adc_left = 0; uint16_t addr[8] = {0};
    uint16_t frt = 0; uint64_t frt_acc = 0;     // 16-bit timer16 (FRT): free-running counter
    uint8_t pdr[10] = {0}, pddr[10] = {0};   // DR and DDR of ports 1..9

    AscaSys() { hook(); }
    void hook() {
        cpu.bus = this; cpu.user = this;
        cpu.on_update_irq_filter = [](void* u) { ((AscaSys*)u)->compute_irq(); };
        cpu.on_irq_taken = nullptr;
    }
    bool load_rom(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
        rom.resize(0x40000); size_t n = fread(rom.data(), 1, rom.size(), f); fclose(f); return n == rom.size();
    }
    void reset() {
        memset(ram, 0, sizeof ram); memset(reg, 0, sizeof reg); for (auto& t : t8) t = T8(); sci = Sci(); adcsr = adcr = 0; adc_left = 0;
        frt = 0; frt_acc = 0; memset(pdr, 0, sizeof pdr); memset(pddr, 0, sizeof pddr); rxq.clear(); rx_wait = 0; sense_initialized = false;
        cpu = H8oCpu(); hook(); cpu.do_reset();
    }
    // in SLEEP with no pending interrupts we advance in blocks of 64 cycles instead of 4 (the peripherals are
    // updated anyway: at most 64 cycles, ~4 us, of wake-up delay). S23_H8SLOW = as before.
    void run_to(uint64_t cycles) {
        static const bool slow = getenv("S23_H8SLOW") != nullptr; if (slow) cpu.rom_code = false;
        while (cpu.m_cycles < cycles) {
            if (cpu.m_sleeping && !cpu.m_irq_vector && !slow) { uint64_t d = cycles - cpu.m_cycles; if (d > 64) d = 64; cpu.m_cycles += d; pend_ += d; flush_tick(); continue; }
            uint64_t c0 = cpu.m_cycles; cpu.step(); pend_ += cpu.m_cycles - c0;
            if (pend_ >= TICK_Q || slow) flush_tick();
        }
        flush_tick();
    }
    static constexpr uint64_t TICK_Q = 32; uint64_t pend_ = 0;
    void flush_tick() { if (pend_) { uint64_t d = pend_; pend_ = 0; tick(d); } }
    void push_rx(const std::vector<uint8_t>& v) { for (uint8_t b : v) rxq.push_back(b); }

    // ---------------------------------------------------------------- bus
    uint8_t r8(uint32_t a) override {
        a &= 0xffff;
        if (a < 0x4000) return rom.empty() ? 0 : rom[a];
        if (a >= 0xc000 && a < 0xff80) return ram[a - 0xc000];
        if (a >= 0xff80) return reg_r(a);
        if (a >= 0x6000 && a < 0x6004) return in_r(a - 0x6000);
        if (a == 0x6006 || a == 0x6007) { int i = a - 0x6006; uint8_t v = in ? in->rotary[i] : 0; return v; }
        unmapped_reads++; return 0;
    }
    uint16_t r16(uint32_t a) override {
        a &= 0xffff; if (!rom.empty() && a < 0x3fff) return (uint16_t)((rom[a] << 8) | rom[a + 1]);        // ROM: fast path
        if (a >= 0xc000 && a < 0xff7f) return (uint16_t)((ram[a - 0xc000] << 8) | ram[a - 0xc000 + 1]);
        return (r8(a) << 8) | r8(a + 1);
    }
    void w8(uint32_t a, uint8_t v) override {
        a &= 0xffff;
        if (a >= 0xc000 && a < 0xff80) { ram[a - 0xc000] = v; return; }
        if (a >= 0xff80) { reg_w(a, v); return; }
        if (a == 0x6004 || a == 0x6005) return;                         // outputs (lamps, coin counter)
        if (a < 0x4000) return;                                         // ROM
        unmapped_writes++;
    }
    void w16(uint32_t a, uint16_t v) override { w8(a, v >> 8); w8(a + 1, v & 0xff); }

    // ---------------------------------------------------------------- memory-mapped inputs (namco_asca_1_device::in_r)
    static unsigned bit(uint32_t v, int n) { return (v >> n) & 1; }
    uint8_t in_r(int offset) {
        uint32_t p1 = in ? in->p1 : 0; uint8_t sys = in ? in->system : 0, c1 = in ? in->coin1 : 0, c2 = in ? in->coin2 : 0;
        if (offset == 0) { p1 &= 0xe033; return (uint8_t)~((bit(p1, 13) << 7) | (bit(p1, 14) << 6) | (bit(p1, 15) << 5) | (bit(p1, 0) << 4) | (bit(p1, 1) << 3) | (bit(p1, 4) << 2) | (bit(p1, 5) << 1) | (bit(sys & 0x80, 7) << 0)); }
        if (offset == 1) { p1 &= 0xe00300; return (uint8_t)~((bit(p1, 21) << 4) | (bit(p1, 22) << 3) | (bit(p1, 23) << 2) | (bit(p1, 8) << 1) | (bit(p1, 9) << 0)); }
        if (offset == 2) { p1 &= 0x1c00; c1 &= 1; return (uint8_t)~((bit(c1, 0) << 3) | (bit(p1, 10) << 2) | (bit(p1, 11) << 1) | (bit(p1, 12) << 0)); }
        c1 &= 0x80; c2 &= 0x81;
        return (uint8_t)~((bit(c2, 0) << 3) | (bit(~c2, 7) << 1) | (bit(~c1, 7) << 0));
    }

    // ---------------------------------------------------------------- ports
    uint8_t port_in(int p) {
        uint32_t p1 = in ? in->p1 : 0;
        switch (p) {
            case 4: return (uint8_t)((1 << 7) | ((dsw & 3) << 5) | (0 << 4) | (1 << 3) | (1 << 2) | (1 << 1) | 1);
            case 5: return (31 << 3) | (1 << 2) | (1 << 1) | 1;
            case 6: return (uint8_t)((1 << 7) | (3 << 5) | (bit(~p1 & 0x40, 6) << 4) | (3 << 2) | (bit(dsw, 2) << 1) | 1);
            case 8: return (1 << 7) | (3 << 5) | 31;
            case 9: return (uint8_t)((3 << 6) | (31 << 1) | bit(dsw, 3));
        }
        return 0xff;
    }
    int sense_state() { return sense_ext; }                           // 0 = none, 1 = not initialised, 2 = initialised (read from the board itself)
    int sense_ext = 1;                                                // state of the sense line seen by the host (1 = not initialised, 2 = initialised)
    uint8_t port_read(int p) { uint8_t ddr = pddr[p], dr = pdr[p]; return (ddr & dr) | (~ddr & port_in(p)); }
    void port_write(int p, uint8_t v) {
        pdr[p] = v;
        if (p == 4) { sense_initialized = (v >> 2) & 1; sense_ext = sense_initialized ? 2 : 1; }
    }
    // port number for the DR/DDR address
    static int dr_port(uint32_t a) { switch (a) { case 0xffb2: return 1; case 0xffb3: return 2; case 0xffb6: return 3; case 0xffb7: return 4; case 0xffba: return 5; case 0xffbb: return 6; case 0xffbe: return 7; case 0xffbf: return 8; case 0xffc1: return 9; } return 0; }
    static int ddr_port(uint32_t a) { switch (a) { case 0xffb0: return 1; case 0xffb1: return 2; case 0xffb4: return 3; case 0xffb5: return 4; case 0xffb8: return 5; case 0xffb9: return 6; case 0xffbd: return 8; case 0xffc0: return 9; } return 0; }

    // ---------------------------------------------------------------- on-chip registers
    uint8_t reg_r(uint32_t a) {
        flush_tick();
        if (int p = dr_port(a)) return port_read(p);
        if (ddr_port(a)) return 0xff;
        switch (a) {
            case 0xffc8: return t8[0].tcr; case 0xffc9: return t8[0].tcsr; case 0xffca: return t8[0].tcora; case 0xffcb: return t8[0].tcorb; case 0xffcc: return t8[0].tcnt;
            case 0xffd0: return t8[1].tcr; case 0xffd1: return t8[1].tcsr; case 0xffd2: return t8[1].tcora; case 0xffd3: return t8[1].tcorb; case 0xffd4: return t8[1].tcnt;
            case 0xffd8: return sci.smr; case 0xffd9: return sci.brr; case 0xffda: return sci.scr; case 0xffdb: return sci.tdr; case 0xffdc: return sci.ssr; case 0xffdd: return sci.rdr;
            case 0xff92: return frt >> 8; case 0xff93: return frt & 0xff;
            case 0xffe8: return adcsr; case 0xffe9: return adcr;
        }
        if (a >= 0xffe0 && a <= 0xffe7) { uint16_t v = addr[(a - 0xffe0) >> 1]; return (a & 1) ? (v & 0xff) : (v >> 8); }
        return reg[a - 0xff80];
    }
    void reg_w(uint32_t a, uint8_t v) {
        flush_tick();
        reg[a - 0xff80] = v;
        if (int p = dr_port(a)) { port_write(p, v); return; }
        if (int p = ddr_port(a)) { pddr[p] = v; return; }
        switch (a) {
            case 0xffc8: t8[0].tcr = v; break; case 0xffc9: t8[0].tcsr = (t8[0].tcsr & v & 0xe0) | (v & 0x1f); break; case 0xffca: t8[0].tcora = v; break; case 0xffcb: t8[0].tcorb = v; break; case 0xffcc: t8[0].tcnt = v; break;
            case 0xffd0: t8[1].tcr = v; break; case 0xffd1: t8[1].tcsr = (t8[1].tcsr & v & 0xe0) | (v & 0x1f); break; case 0xffd2: t8[1].tcora = v; break; case 0xffd3: t8[1].tcorb = v; break; case 0xffd4: t8[1].tcnt = v; break;
            case 0xffd8: sci.smr = v; break; case 0xffd9: sci.brr = v; break;
            case 0xffda: sci.scr = v; if (!(v & 0x20)) { sci.ssr |= 0x84; sci.tx_left = 0; sci.tdr_full = false; } break;
            case 0xffdb: sci.tdr = v; break;
            case 0xffdc: sci_ssr_w(v); break;
            case 0xff92: frt = (frt & 0xff) | (v << 8); break; case 0xff93: frt = (frt & 0xff00) | v; break;
            case 0xffe8: adcsr = (adcsr & 0x80 & v) | (v & 0x7f); if (v & 0x20) adc_left = 134; break;
            case 0xffe9: adcr = v; break;
        }
        compute_irq();
    }
    int64_t bytetime() const { return (int64_t)(10.0 * 32.0 * (1 << (2 * (sci.smr & 3))) * (sci.brr + 1)); }   // cycles per 10 bits at B = phi/(32 * 4^n * (N+1))
    void sci_ssr_w(uint8_t v) {
        uint8_t old = sci.ssr; sci.ssr = (old & v & 0xf8) | (old & 0x07);
        if (!(sci.scr & 0x20)) sci.ssr |= 0x84;
        else if ((old & 0x80) && !(v & 0x80)) {
            if (sci.tx_left == 0) { sci.tx_byte = sci.tdr; sci.ssr = (sci.ssr | 0x80) & ~0x04; sci.tx_left = bytetime(); }
            else { sci.tdr_full = true; sci.ssr &= ~0x80; }
        }
    }

    // ---------------------------------------------------------------- time and interrupts
    void tick(uint64_t dc) {
        static const int div_tab[2][3] = {{8, 64, 1024}, {8, 64, 1024}};
        bool changed = false;
        { static const int fd[4] = {2, 8, 32, 0}; int d = fd[reg[0xff96 - 0xff80] & 3]; if (d) { frt_acc += dc; frt += (uint16_t)(frt_acc / d); frt_acc %= d; } }
        for (int c = 0; c < 2; c++) {
            T8& t = t8[c]; int cks = t.tcr & 7;
            if (cks < 1 || cks > 3) continue;
            t.acc += dc; int d = div_tab[c][cks - 1];
            while (t.acc >= (uint64_t)d) {
                t.acc -= d; t.tcnt++;
                if (t.tcnt == t.tcora) { t.tcsr |= 0x40; changed = true; if (((t.tcr >> 3) & 3) == 1) t.tcnt = 0; }
                if (t.tcnt == t.tcorb) { t.tcsr |= 0x80; changed = true; if (((t.tcr >> 3) & 3) == 2) t.tcnt = 0; }
                if (t.tcnt == 0 && ((t.tcr >> 3) & 3) == 0) { t.tcsr |= 0x20; changed = true; }
            }
        }
        if (sci.tx_left > 0) {
            sci.tx_left -= (int64_t)dc;
            if (sci.tx_left <= 0) {
                sci.tx_left = 0; tx_bytes++; if (on_tx) on_tx(sci.tx_byte);
                if (sci.tdr_full) { sci.tdr_full = false; sci.tx_byte = sci.tdr; sci.ssr |= 0x80; sci.ssr &= ~0x04; sci.tx_left = bytetime(); } else sci.ssr |= 0x04;
                changed = true;
            }
        }
        if (rx_wait > 0) rx_wait -= (int64_t)dc;
        if (rx_wait <= 0 && !rxq.empty() && (sci.scr & 0x10)) {
            uint8_t b = rxq.front(); rxq.pop_front(); rx_wait = bytetime(); rx_bytes++;
            if (sci.ssr & 0x40) sci.ssr |= 0x20; else { sci.rdr = b; sci.ssr |= 0x40; }
            changed = true;
        }
        if (adc_left > 0) {
            adc_left -= (int64_t)dc;
            if (adc_left <= 0) {
                adc_left = 0; int ch = adcsr & 7; bool scan = adcsr & 0x10;
                int lo = scan ? 0 : ch, hi = ch;
                for (int i = lo; i <= hi; i++) addr[i] = (uint16_t)((in ? in->analog[i] : 0x8000) & 0xffc0);
                adcsr |= 0x80; if (scan) adc_left = 134 * (hi - lo + 1); else adcsr &= ~0x20;
                changed = true;
            }
        }
        if (changed) compute_irq();
    }
    int pending_vector() {
        for (int c = 0; c < 2; c++) {
            const T8& t = t8[c]; int base = 19 + 3 * c;
            if ((t.tcsr & 0x40) && (t.tcr & 0x40)) return base;
            if ((t.tcsr & 0x80) && (t.tcr & 0x80)) return base + 1;
            if ((t.tcsr & 0x20) && (t.tcr & 0x20)) return base + 2;
        }
        if ((sci.ssr & 0x38) && (sci.scr & 0x40) && (sci.scr & 0x10)) return 27;
        if ((sci.ssr & 0x40) && (sci.scr & 0x40) && (sci.scr & 0x10)) return 28;
        if ((sci.ssr & 0x80) && (sci.scr & 0x80) && (sci.scr & 0x20)) return 29;
        if ((sci.ssr & 0x04) && (sci.scr & 0x04) && (sci.scr & 0x20)) return 30;
        return 0;
    }
    void compute_irq() { int v = pending_vector(); cpu.m_irq_vector = (v && !(cpu.m_CCR & H8o::F_I)) ? v : 0; }
};
