// Namco System 23 sub-CPU: H8/3002 + bus + minimal on-chip peripherals (ITU, SCI, ADC, ports, intc).
// Bus map and wiring from namcos23.cpp (MAME); on-chip registers from h83002.cpp. license:BSD-3-Clause (derived from MAME)
#pragma once
#include <cstdlib>
#include "h8cpu.h"
#include "c352.h"
#include <functional>
#include <deque>
#include <map>
#include <string>
#include <algorithm>
#include <cstring>

class H8Sys : public H8Bus {
public:
    static constexpr double CLOCK = 16934400.0;                  // H8CLOCK
    H8Cpu cpu;
    std::vector<uint8_t> rom;                                    // 512 KB, big endian
    uint8_t* shared = nullptr;                                   // 64 KB shared with the main CPU (big endian)
    uint8_t  ram[0x200] = {0};                                   // RAM on-chip 0xFFFD10-0xFFFF0F
    uint8_t  reg[0x100] = {0};                                   // on-chip registers 0xFFFF00-0xFFFFFF
    C352 snd;                                                     // C352 audio chip (0x280000-0x287fff)
    // links to the outside
    std::function<void(uint16_t)> on_main_irq;                   // write to 0x300020
    std::function<void(int, uint8_t)> on_sci_tx;                 // byte transmitted by SCI n
    std::function<uint8_t()> p6_in, p8_in;                       // port inputs
    uint8_t porta_out = 0, portb_out = 0, portb_in = 0, p6_out = 0, p8_out = 0;
    // statistics and log
    std::map<uint32_t, uint64_t> onchip_acc, unmapped;
    uint64_t c352_writes = 0;
    bool log_acc = false; std::map<std::string, uint64_t> acc_log;
    void note_acc(char k, uint32_t a) { if (!log_acc) return; char b[40]; snprintf(b, sizeof b, "%c %06x", k, a & 0xfffffe); acc_log[b]++; }

    H8Sys() { hook(); init_state(); }
    void hook() { cpu.bus = this; cpu.user = this; cpu.on_update_irq_filter = [](void* u) { ((H8Sys*)u)->compute_irq(); };
        /* Edge-triggered IRQ: the ISR flag is cleared on acceptance */ cpu.on_irq_taken = [](void* u, int v) { H8Sys* h = (H8Sys*)u; if (v >= 12 && v <= 17 && (h->iscr & (1 << (v - 12)))) h->isr &= ~(1 << (v - 12)); };
    }

    bool load_rom(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
        rom.resize(0x80000); size_t n = fread(rom.data(), 1, rom.size(), f); fclose(f); return n == rom.size();
    }
    void reset() { snd.reset(); snd.last_cycle = 0; init_state(); cpu = H8Cpu(); hook(); cpu.do_reset(); }

    // runs instructions until the requested number of cycles is reached
    // in SLEEP with no pending interrupts we advance in blocks of 64 cycles instead of 4 (the peripherals are
    // updated anyway: at most 64 cycles, ~4 us, of wake-up delay). S23_H8SLOW = as before.
    void run_to(uint64_t cycles) {
        static const bool slow = getenv("S23_H8SLOW") != nullptr; if (slow) cpu.rom_code = false;
        static const bool noidle = slow || getenv("S23_H8NOIDLE") != nullptr;
        while (cpu.m_cycles < cycles) {
            if (cpu.m_sleeping && !cpu.m_irq_vector && !slow) { uint64_t d = cycles - cpu.m_cycles; if (d > 64) d = 64; cpu.m_cycles += d; pend_ += d; flush_tick(); continue; }
            const uint32_t A = cpu.m_NPC;
            if (!noidle && A == 0x2c0c && timeout_poll(cycles)) continue;
            if (!noidle && A + 4 <= rom.size() && rom[A] == 0x1a && (rom[A + 1] & 0xf0) == 0 && rom[A + 2] == 0x46 && rom[A + 3] == 0xfc && !cpu.m_irq_vector) { delay_loop(cycles); continue; }
            uint64_t c0 = cpu.m_cycles; if (pcprof_) pcprof_[A & 0x7ffff]++; cpu.step(); pend_ += cpu.m_cycles - c0;
            if (pend_ >= TICK_Q || slow) flush_tick();      // peripherals updated in blocks of ~32 cycles
            if (!noidle && cpu.m_NPC < A && A - cpu.m_NPC <= 24) idle_check(A, cycles);
        }
        flush_tick();
    }
    // ---- H8 idle waits (about 75% of its instructions): waiting for the main CPU to change the shared RAM,
    // waiting for the serial port (TDRE), delay loops. A short backward branch that comes back with registers, flags and memory
    // identical is a pure wait: the outcome can only depend on something external, so time is advanced to the
    // next peripheral event (timers, serial, ADC) or the end of the block (the main CPU and the video lines
    // are updated between one block and the next, every ~32 us). S23_H8NOIDLE=1 = as before.
    uint64_t wr_ = 0;                                            // bus writes (to recognise a loop that doesn't write)
    uint32_t idle_br_ = ~0u, idle_head_ = 0; uint16_t idle_r_[16]; uint8_t idle_ccr_ = 0; uint64_t idle_wr_ = 0, idle_c_ = 0;
    uint64_t idle_skipped = 0, delay_skipped = 0;
    void idle_check(uint32_t br, uint64_t limit) {
        if (br == idle_br_ && cpu.m_NPC == idle_head_ && wr_ == idle_wr_ && cpu.m_CCR == idle_ccr_ && !memcmp(idle_r_, cpu.m_R, sizeof idle_r_) && !cpu.m_irq_vector) {
            flush_tick();
            uint64_t to = limit, ev = next_event(); if (ev < to - cpu.m_cycles) to = cpu.m_cycles + ev;
            const uint64_t it = cpu.m_cycles - idle_c_;          // length of one iteration
            if (to > cpu.m_cycles + it) { uint64_t d = to - cpu.m_cycles; cpu.m_cycles += d; pend_ += d; idle_skipped += d; flush_tick(); }
        }
        idle_br_ = br; idle_head_ = cpu.m_NPC; idle_wr_ = wr_; idle_ccr_ = cpu.m_CCR; memcpy(idle_r_, cpu.m_R, sizeof idle_r_); idle_c_ = cpu.m_cycles;
    }
    // "dec.b rX / bne -4": the loop is really run only to measure its length, then we jump to the second-to-last iteration
    void delay_loop(uint64_t limit) {
        const uint32_t A = cpu.m_NPC; const int reg = rom[A + 1] & 0x0f;
        uint64_t c0 = cpu.m_cycles; cpu.step(); cpu.step(); pend_ += cpu.m_cycles - c0;   // a full iteration (dec + bne)
        if (cpu.m_NPC != A || cpu.m_irq_vector) { if (pend_ >= TICK_Q) flush_tick(); return; }
        const unsigned v = cpu.r8_r(reg); if (v < 2) { if (pend_ >= TICK_Q) flush_tick(); return; }
        flush_tick();
        const uint64_t it = cpu.m_cycles - c0; uint64_t n = v - 1;          // skippable iterations: the last one is kept (correct flags)
        uint64_t room = limit > cpu.m_cycles ? limit - cpu.m_cycles : 0; const uint64_t ev = next_event(); if (ev < room) room = ev;
        if (it && n * it > room) n = room / it;
        if (!n) return;
        cpu.r8_w(reg, (uint8_t)(v - n)); cpu.m_cycles += n * it; pend_ += n * it; delay_skipped += n * it; flush_tick();
    }
    // pnp1vera 0x2c0c: wait with timeout "while shared[0x4de1] == 0 and TCNT3 < E0" (main CPU reply).
    // The register read (TCNT3) changes on every iteration, so idle_check doesn't recognise it: we advance to the timeout,
    // to the next event or the end of the block. The code bytes are checked, so with another ROM it does nothing.
    uint64_t timeout_skipped = 0;
    bool timeout_poll(uint64_t limit) {
        static const uint8_t pat[16] = {0x6a, 0x28, 0x00, 0x08, 0x4d, 0xe1, 0x46, 0x1a, 0x6b, 0x00, 0xff, 0x86, 0x1d, 0x80, 0x45, 0xf0};
        static int ok = -1; if (ok < 0) ok = rom.size() > 0x2c20 && !memcmp(&rom[0x2c0c], pat, sizeof pat);
        if (!ok || cpu.m_irq_vector || !shared || shared[0x4de1]) return false;
        flush_tick();
        const Itu& t = itu[3]; const int d = ipsc(t.tcr); const uint16_t lim = cpu.m_R[8];
        if (!(tstr & 8) || !d || t.tcnt >= lim) return false;
        uint64_t to = limit; const uint64_t tl = (uint64_t)(lim - t.tcnt) * d; const uint64_t tt = tl > t.acc ? tl - t.acc : 0;
        const uint64_t ev = next_event(); uint64_t room = std::min(tt, ev);
        if (to - cpu.m_cycles < room) room = to > cpu.m_cycles ? to - cpu.m_cycles : 0;
        if (room < 64) return false;                              // little to gain: run normally
        cpu.m_cycles += room; pend_ += room; timeout_skipped += room; flush_tick();
        return true;
    }
    // cycles to the next peripheral event that can change a register or raise an interrupt
    uint64_t next_event() {
        uint64_t m = ~0ull;
        for (int i = 0; i < 2; i++) {
            if (sci[i].tx_left > 0) m = std::min<uint64_t>(m, (uint64_t)sci[i].tx_left);
            if (!rxq[i].empty() && (sci[i].scr & 0x10)) m = std::min<uint64_t>(m, rx_wait[i] > 0 ? (uint64_t)rx_wait[i] : 1);
        }
        if (adc_left > 0) m = std::min<uint64_t>(m, (uint64_t)adc_left);
        for (int c = 0; c < 5; c++) {
            if (!(tstr & (1 << c))) continue; const Itu& t = itu[c]; const int d = ipsc(t.tcr); if (!d) continue;
            uint32_t to_a = (uint16_t)(t.gra - t.tcnt), to_b = (uint16_t)(t.grb - t.tcnt), ev = 0x10000 - t.tcnt;
            if (to_a && to_a < ev) ev = to_a; if (to_b && to_b < ev) ev = to_b;
            const uint64_t cy = (uint64_t)ev * d; m = std::min<uint64_t>(m, cy > t.acc ? cy - t.acc : 1);
        }
        return m;
    }
    // the peripherals (timers, serial, ADC) advance in blocks; before every register access the delay is caught up
#if defined(__3DS__) || defined(S23_LIGHT_H8)
    static constexpr uint64_t TICK_Q = 128; uint64_t pend_ = 0;  // 3DS: fewer peripheral updates (7.5 us), the H8 must fit in core 1
#else
    static constexpr uint64_t TICK_Q = 32; uint64_t pend_ = 0;
#endif
    uint32_t* pcprof_ = getenv("S23_H8PROF") ? new uint32_t[0x80000]() : nullptr; uint64_t pccyc_[2] = {0, 0};   // instruction count per address (analysis)
    void dump_prof(const char* path) { if (!pcprof_) return; FILE* f = fopen(path, "w"); if (!f) return; for (uint32_t a = 0; a < 0x80000; a++) if (pcprof_[a]) fprintf(f, "%06x %u\n", a, pcprof_[a]); fclose(f); }
    void flush_tick() { if (pend_) { uint64_t d = pend_; pend_ = 0; tick(d); } }

    // ---------------------------------------------------------------- bus
    uint8_t r8(uint32_t a) override {
        a &= 0xffffff; if (log_acc && a >= 0x80000) note_acc('R', a);
        if (a < 0x80000) return rom.empty() ? 0 : rom[a];
        if (a >= 0x80000 && a < 0x90000) return shared ? shared[a - 0x80000] : 0;
        if (a >= 0x280000 && a < 0x288000) { snd.run_to(cpu.m_cycles); uint16_t v = snd.read((a - 0x280000) >> 1); return (a & 1) ? (v & 0xff) : (v >> 8); }
        if (a >= 0xfffd10 && a <= 0xffff0f) return ram[a - 0xfffd10];
        if (a >= 0xffff00) return onchip_r(a & 0xff);
        if (a >= 0x300000 && a < 0x300040) return 0;
        if (log_acc) unmapped[a & ~0xffu]++; return 0;
    }
    uint16_t r16(uint32_t a) override {
        a &= 0xffffff; if (a < 0x7ffff && !rom.empty()) return (uint16_t)((rom[a] << 8) | rom[a + 1]);   // ROM: fast path
        if (a >= 0xfffd10 && a < 0xffff0f) return (uint16_t)((ram[a - 0xfffd10] << 8) | ram[a - 0xfffd0f]);   // internal RAM
        return (r8(a) << 8) | r8(a + 1);
    }
    void w8(uint32_t a, uint8_t v) override {
        a &= 0xffffff; wr_++; if (log_acc && a >= 0x80000) note_acc('W', a);
        if (a >= 0x80000 && a < 0x90000) { if (shared) shared[a - 0x80000] = v; return; }
        if (a >= 0x280000 && a < 0x288000) { snd.run_to(cpu.m_cycles); snd.write((a - 0x280000) >> 1, (a & 1) ? v : (uint16_t)(v << 8), (a & 1) ? 0x00ff : 0xff00); c352_writes++; return; }
        if (a >= 0xfffd10 && a <= 0xffff0f) { ram[a - 0xfffd10] = v; return; }
        if (a >= 0xffff00) { onchip_w(a & 0xff, v); return; }
        if (a >= 0x300000 && a < 0x300040) return;
        if (log_acc) unmapped[a & ~0xffu]++;
    }
    void w16(uint32_t a, uint16_t v) override {
        a &= 0xffffff; wr_++;
        if (a == 0x300020) { if (on_main_irq) on_main_irq(v); return; }
        if (a >= 0x280000 && a < 0x288000 && !(a & 1)) { snd.run_to(cpu.m_cycles); snd.write((a - 0x280000) >> 1, v, 0xffff); c352_writes++; return; }
        w8(a, v >> 8); w8(a + 1, v & 0xff);
    }

    // ---------------------------------------------------------------- peripherals
    struct Itu { uint8_t tcr = 0, tior = 0, tier = 0xf8, tsr = 0xf8; uint16_t tcnt = 0, gra = 0xffff, grb = 0xffff; uint64_t acc = 0; };   // acc: cycles not yet counted (< prescaler)
    Itu itu[5]; uint8_t tstr = 0;
    static constexpr uint8_t ITU_BASE[5] = {0x64, 0x6e, 0x78, 0x82, 0x92};

    struct Sci { uint8_t smr = 0, brr = 0xff, scr = 0, tdr = 0xff, ssr = 0x84, rdr = 0; int64_t tx_left = 0; uint8_t tx_byte = 0; bool tdr_full = false; };
    Sci sci[2];
    bool adclog = getenv("S23_ADCLOG") != nullptr; int adclog_n = 0; uint8_t adcsr = 0, adcr = 0x7e; int64_t adc_left = 0;
    uint8_t iscr = 0, ier = 0, isr = 0, syscr = 0x09; uint16_t icr = 0;

    bool adtrg = false;                                           // ADTRG pin (wired to vblank): falling edge with TRGE=1 starts the conversion
    void adtrg_w(bool st) { if (st == adtrg) return; adtrg = st; if (!st && (adcr & 0x80) && !(adcsr & 0x20)) { adcsr |= 0x20; adc_left = 134; } }
    void init_state() { memset(reg, 0, sizeof reg); memset(ram, 0, sizeof ram); for (auto& i : itu) i = Itu(); tstr = 0; sci[0] = Sci(); sci[1] = Sci(); adcsr = 0; adc_left = 0; iscr = ier = isr = 0; syscr = 0x09; icr = 0; }

    // RTC4543 (synchronous SCI1): 56 bits, LSB first, BCD registers seconds..year; CE = PB5 & PA0
    uint8_t rtc_regs[7] = {0}; int rtc_bit = 0; bool rtc_ce_prev = false;
    bool rtc_ce() const { return (portb_out & 0x20) && (porta_out & 1); }
    std::function<void()> on_rtc_latch;                          // at the start of every read: updates the time (from the PC)
    void rtc_ce_update() { bool ce = rtc_ce(); if (ce && !rtc_ce_prev) { rtc_bit = 0; if (on_rtc_latch) on_rtc_latch(); } rtc_ce_prev = ce; }
    uint8_t rtc_shift8() {                                       // 8 clock pulses: the RTC outputs 8 bits (if selected)
        uint8_t b = 0;
        // RTC4543 stream (LSB first): seconds 8, minutes 8, hours 8, weekday 4 bits, day 8, month 8, year 8 = 52 bits
        static const int W[7] = {8, 8, 8, 4, 8, 8, 8};
        auto bitat = [&](int n) { for (int f = 0; f < 7; f++) { if (n < W[f]) return (rtc_regs[f] >> n) & 1; n -= W[f]; } return 0; };
        if (rtc_ce()) for (int i = 0; i < 8; i++) { if (rtc_bit < 52) { b |= bitat(rtc_bit) << i; rtc_bit++; } }
        { static const bool lg = getenv("S23_RTCLOG") != nullptr; if (lg) printf("RTC shift ce=%d bit=%d -> %02x (pa=%02x pb=%02x smr=%02x scr=%02x)\n", rtc_ce(), rtc_bit, b, porta_out, portb_out, sci[1].smr, sci[1].scr); }
        return b;
    }
    std::deque<uint8_t> rxq[2]; int64_t rx_wait[2] = {0, 0};      // byte arriving on the SCI (at line speed)
    void sci_push(int n, const std::vector<uint8_t>& v) { for (uint8_t b : v) rxq[n].push_back(b); }
    void sci_rx(int n, uint8_t b) {                              // a byte arrives on SCI n
        Sci& s = sci[n];
        if (s.ssr & 0x40) { s.ssr |= 0x20; } else { s.rdr = b; s.ssr |= 0x40; }
        compute_irq();
    }

    static int ipsc(uint8_t tcr) { int p = tcr & 7; return p < 4 ? (1 << p) : 0; }

    void tick(uint64_t dc) {
        bool changed = false;
        for (int c = 0; c < 5; c++) {
            if (!(tstr & (1 << c))) continue; Itu& t = itu[c]; int d = ipsc(t.tcr); if (!d) continue;
            int sh = t.tcr & 3; t.acc += dc; uint32_t n = (uint32_t)(t.acc >> sh); t.acc &= (uint64_t)(d - 1);   // d = 1 << sh (integer, no divisions)
            while (n) {
                uint32_t to_a = (uint16_t)(t.gra - t.tcnt), to_b = (uint16_t)(t.grb - t.tcnt), to_o = 0x10000 - t.tcnt;
                uint32_t ev = n;
                if (to_a >= 1 && to_a < ev) ev = to_a;
                if (to_b >= 1 && to_b < ev) ev = to_b;
                if (to_o < ev) ev = to_o;
                if (ev == 0) ev = 1;
                t.tcnt = (uint16_t)(t.tcnt + ev); n -= ev;
                if (t.tcnt == t.gra) { t.tsr |= 1; changed = true; if (((t.tcr >> 3) & 3) == 1) t.tcnt = 0; }
                if (t.tcnt == t.grb) { t.tsr |= 2; changed = true; if (((t.tcr >> 3) & 3) == 2) t.tcnt = 0; }
                if (ev == to_o) { t.tsr |= 4; changed = true; }
            }
        }
        for (int i = 0; i < 2; i++) {
            Sci& s = sci[i];
            if (s.tx_left > 0) { s.tx_left -= (int64_t)dc; if (s.tx_left <= 0) { s.tx_left = 0; if (on_sci_tx) on_sci_tx(i, s.tx_byte); if (s.tdr_full) { s.tdr_full = false; s.tx_byte = s.tdr; s.ssr |= 0x80; s.ssr &= ~0x04; s.tx_left = bytetime(i); } else s.ssr |= 0x04; if (i == 1 && (s.smr & 0x80) && (s.scr & 0x10)) sci_rx(1, rtc_shift8()); changed = true; } }
        }
        for (int i = 0; i < 2; i++) {
            if (rx_wait[i] > 0) rx_wait[i] -= (int64_t)dc;
            if (rx_wait[i] <= 0 && !rxq[i].empty() && (sci[i].scr & 0x10) && !(sci[i].ssr & 0x40)) { sci_rx(i, rxq[i].front()); rxq[i].pop_front(); rx_wait[i] = bytetime(i); changed = true; }
        }
        if (adc_left > 0) { adc_left -= (int64_t)dc; if (adc_left <= 0) { adc_left = 0; adcsr |= 0x80; if (!(adcsr & 0x10)) adcsr &= ~0x20; changed = true; } }
        if (changed) compute_irq();
    }

    // vector of the highest-priority interrupt (lowest number) among the active sources
    uint8_t irq_pins = 0xff;                                    // level of the IRQ0-5 pins (1 = high, active low)
    void set_irq_pin(int n, bool asserted) {
        uint8_t bit = 1 << n; bool was_low = !(irq_pins & bit);
        if (asserted) irq_pins &= ~bit; else irq_pins |= bit;
        if (asserted && !was_low && (iscr & bit)) isr |= bit;     // falling edge
        compute_irq();
    }
    int pending_vector() {
        for (int n = 0; n < 6; n++) {
            uint8_t bit = 1 << n;
            if (!(iscr & bit) && !(irq_pins & bit)) isr |= bit;     // level-sensitive
            if ((isr & bit) && (ier & bit)) return 12 + n;
        }
        for (int c = 0; c < 5; c++) {
            const Itu& t = itu[c]; int base = 24 + 4 * c;
            if ((t.tsr & 1) && (t.tier & 1)) return base;
            if ((t.tsr & 2) && (t.tier & 2)) return base + 1;
            if ((t.tsr & 4) && (t.tier & 4)) return base + 2;
        }
        for (int i = 0; i < 2; i++) {
            const Sci& s = sci[i]; int base = 52 + 4 * i;
            if ((s.ssr & 0x38) && (s.scr & 0x40) && (s.scr & 0x10)) return base;                  // ERI
            if ((s.ssr & 0x40) && (s.scr & 0x40) && (s.scr & 0x10)) return base + 1;              // RXI
            if ((s.ssr & 0x80) && (s.scr & 0x80) && (s.scr & 0x20)) return base + 2;              // TXI
            if ((s.ssr & 0x04) && (s.scr & 0x04) && (s.scr & 0x20)) return base + 3;              // TEI
        }
        if ((adcsr & 0x80) && (adcsr & 0x40)) return 60;
        return 0;
    }
    void compute_irq() { int v = pending_vector(); cpu.m_irq_vector = (v && !(cpu.m_CCR & H8::F_I)) ? v : 0; }

    // ---------------------------------------------------------------- on-chip registers
    uint8_t port_read(uint8_t ddr, uint8_t dr, uint8_t in) { return (ddr & dr) | (~ddr & in); }
    uint8_t onchip_r(uint32_t o) {
        if (log_acc) onchip_acc[0xffff00 + o]++;
        flush_tick();
        if (o >= 0x64 && o <= 0x9f) { for (int c = 0; c < 5; c++) { uint8_t b = ITU_BASE[c]; if (o >= b && o < b + 10) return itu_r(c, o - b); } }
        switch (o) {
            case 0x60: return tstr;
            case 0xb0: return sci[0].smr; case 0xb1: return sci[0].brr; case 0xb2: return sci[0].scr; case 0xb3: return sci[0].tdr; case 0xb4: return sci[0].ssr; case 0xb5: { Sci& s = sci[0]; return s.rdr; }
            case 0xb8: return sci[1].smr; case 0xb9: return sci[1].brr; case 0xba: return sci[1].scr; case 0xbb: return sci[1].tdr; case 0xbc: return sci[1].ssr; case 0xbd: return sci[1].rdr;
            case 0xc5: case 0xc9: case 0xcd: case 0xd0: case 0xd1: case 0xd4: return 0xff;        // DDR (write-only)
            case 0xc7: return 0xff;
            case 0xcb: return port_read(reg[0xc9], p6_out, p6_in ? p6_in() : 0xff);
            case 0xce: return 0;                                                                   // port 7: constant 0
            case 0xcf: return port_read(reg[0xcd], p8_out, p8_in ? p8_in() : 0xff);
            case 0xd2: return 0xff;
            case 0xd3: return port_read(reg[0xd1], porta_out, 0xff);
            case 0xd6: return port_read(reg[0xd4], portb_out, portb_in);
            case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: case 0xe6: case 0xe7: return 0;   // ADC: inputs at 0
            case 0xe8: return adcsr; case 0xe9: return adcr;
            case 0xf2: return syscr; case 0xf4: return iscr; case 0xf5: return ier; case 0xf6: return isr;
            case 0xf8: return icr >> 8; case 0xf9: return icr & 0xff;
        }
        return reg[o];
    }
    void onchip_w(uint32_t o, uint8_t v) {
        if (log_acc) onchip_acc[0xffff00 + o]++;
        flush_tick();
        if (o >= 0x64 && o <= 0x9f) { for (int c = 0; c < 5; c++) { uint8_t b = ITU_BASE[c]; if (o >= b && o < b + 10) { itu_w(c, o - b, v); compute_irq(); return; } } }
        reg[o] = v;
        switch (o) {
            case 0x60: tstr = v; break;
            case 0xb2: sci[0].scr = v; if (!(v & 0x20)) { sci[0].ssr |= 0x84; sci[0].tx_left = 0; } compute_irq(); break;
            case 0xba: sci[1].scr = v; if (!(v & 0x20)) { sci[1].ssr |= 0x84; if ((sci[1].smr & 0x80) && (v & 0x10) && !sci[1].tx_left) { sci[1].tx_left = bytetime(1); sci[1].ssr &= ~0x04; } else if (!(sci[1].smr & 0x80) || !(v & 0x10)) sci[1].tx_left = 0; } compute_irq(); break;
            case 0xb0: sci[0].smr = v; break; case 0xb8: sci[1].smr = v; break;
            case 0xb1: sci[0].brr = v; break; case 0xb9: sci[1].brr = v; break;
            case 0xb3: sci[0].tdr = v; break; case 0xbb: sci[1].tdr = v; break;
            case 0xb4: sci_ssr_w(0, v); break; case 0xbc: sci_ssr_w(1, v); break;
            case 0xc9: case 0xcd: case 0xd1: case 0xd4: break;                                     // DDR
            case 0xcb: p6_out = v; break; case 0xcf: p8_out = v; break;
            case 0xd3: porta_out = v; rtc_ce_update(); break; case 0xd6: portb_out = v; rtc_ce_update(); break;
            case 0xe8: if (adclog && adclog_n++ < 400) fprintf(stderr, "ADCSR<=%02x (was %02x) pc=%06x cyc=%llu\n", v, adcsr, cpu.m_NPC, (unsigned long long)cpu.m_cycles); adcsr = (adcsr & 0x80 & v) | (v & 0x7f); if (v & 0x20) adc_left = 134; compute_irq(); break;
            case 0xe9: if (adclog && adclog_n++ < 400) fprintf(stderr, "ADCR<=%02x pc=%06x\n", v, cpu.m_NPC); adcr = v; break;
            case 0xf2: syscr = v; cpu.m_syscr = v; break;
            case 0xf4: iscr = v; break; case 0xf5: ier = v; break; case 0xf6: isr &= v; break;
            case 0xf8: icr = (icr & 0xff) | (v << 8); break; case 0xf9: icr = (icr & 0xff00) | v; break;
        }
    }
    void sci_ssr_w(int n, uint8_t v) {
        Sci& s = sci[n]; uint8_t old = s.ssr;
        s.ssr = (old & v & 0xf8) | (old & 0x07);                 // flags 7..3 are cleared by writing 0
        if (!(s.scr & 0x20)) s.ssr |= 0x84;                      // with TE=0, TDRE and TEND stay at 1
        else if ((old & 0x80) && !(v & 0x80)) {                  // TDRE cleared with TE=1: TDR goes to the shift register
            if (s.tx_left == 0) { s.tx_byte = s.tdr; s.ssr = (s.ssr | 0x80) & ~0x04; s.tx_left = bytetime(n); }
            else { s.tdr_full = true; s.ssr &= ~0x80; }           // shift register busy: the byte waits in TDR
        }
        // synchronous receive-only (RTC): clearing RDRF starts receiving the next byte (continuous receive)
        if ((old & 0x40) && !(v & 0x40) && (s.smr & 0x80) && (s.scr & 0x10) && !(s.scr & 0x20) && s.tx_left == 0) { s.tx_left = bytetime(n); s.ssr &= ~0x04; }
        compute_irq();
    }
    int64_t bytetime(int n) {                                    // cycles per 10 bits; external clock (JVS) = 115200 baud
        const Sci& s = sci[n];
        double baud = 115200.0;
        if (s.smr & 0x80) { int k = s.smr & 3; return (int64_t)(8.0 * (4.0 * (1 << (2 * k)) * (s.brr + 1))); }   // synchronous: 8 clock pulses
        if (!(s.scr & 0x02)) { int k = s.smr & 3; baud = CLOCK / (64.0 * (1 << (2 * k)) * (s.brr + 1)); }
        return (int64_t)(10.0 * CLOCK / baud);
    }
    uint8_t itu_r(int c, int o) {
        Itu& t = itu[c];
        switch (o) { case 0: return t.tcr; case 1: return t.tior; case 2: return t.tier; case 3: return t.tsr;
            case 4: return t.tcnt >> 8; case 5: return t.tcnt & 0xff; case 6: return t.gra >> 8; case 7: return t.gra & 0xff;
            case 8: return t.grb >> 8; case 9: return t.grb & 0xff; } return 0;
    }
    void itu_w(int c, int o, uint8_t v) {
        Itu& t = itu[c];
        switch (o) { case 0: t.tcr = v; break; case 1: t.tior = v; break; case 2: t.tier = v | 0xf8; break;
            case 3: t.tsr = (t.tsr & v) | 0xf8; break;                                              // flags cleared by writing 0
            case 4: t.tcnt = (t.tcnt & 0xff) | (v << 8); break; case 5: t.tcnt = (t.tcnt & 0xff00) | v; break;
            case 6: t.gra = (t.gra & 0xff) | (v << 8); break; case 7: t.gra = (t.gra & 0xff00) | v; break;
            case 8: t.grb = (t.grb & 0xff) | (v << 8); break; case 9: t.grb = (t.grb & 0xff00) | v; break; }
    }
};
