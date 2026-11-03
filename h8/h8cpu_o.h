// H8/300H execution on top of the generated core (h8core.h): decode, reset, interrupts, sleep.
// Semantics of the special micro-ops (reset, irq) taken from MAME h8.lst. license:BSD-3-Clause (derived from MAME)
#pragma once
#include "h8core_o.h"
#include <cstdio>

class H8oCpu : public H8o {
public:
    uint64_t illegal_count = 0, insn_count = 0;
    bool trace = false; unsigned trace_left = 0;
    uint32_t last_illegal_pc = 0;

    H8oCpu() { build_table(); m_has_hc = false; m_mode_advanced = false; m_syscr = 0x08; }

    void do_reset() {                                           // H8/300: 16-bit reset vector
        m_CCR |= (m_has_hc ? F_I : F_H);
        m_PC = read16i(0) & 0xffff;
        update_irq_filter();
        m_NPC = m_PC; m_PIR = read16i(m_PC); m_PC += 2; m_IR[0] = m_PIR;
        m_sleeping = false;
    }

    // one step: handles sleep, interrupts and one instruction
    void step() {
        if (m_sleeping) {
            if (!m_irq_vector) { m_cycles += 4; return; }
            m_sleeping = false;                                   // wake-up: continue from the instruction after SLEEP
            m_NPC = m_PC & 0xffffff; m_PIR = read16i(m_PC); m_PC += 2; m_IR[0] = m_PIR;
        }
        if (m_irq_vector) { take_irq(); return; }
        execute();
    }

private:
    std::vector<int> bucket_[256];
public:
    bool rom_code = true;                                         // false if the program can run from writable RAM below DC_LIMIT
private:
    static constexpr uint32_t DC_LIMIT = 0x4000;
    std::vector<const OpInfo*> dcache_ = std::vector<const OpInfo*>(DC_LIMIT / 2, nullptr);
    const OpInfo* T_ = nullptr; int n_ = 0;

    void build_table() {
        T_ = ops(n_);
        for (int i = 0; i < n_; i++) {
            const OpInfo& o = T_[i];
            const int hm = o.m[0] >> 8, hv = (o.w[0] >> 8) & hm;
            for (int b = 0; b < 256; b++) if ((b & hm) == hv) bucket_[b].push_back(i);
        }
    }

    void take_irq() {                                           // H8/300: 16-bit SP, 2-byte vectors
        m_taken_irq_vector = m_irq_vector;
        internal(1);
        m_TMP1 = r16_r(7) - 2; r16_w(7, m_TMP1); write16(m_TMP1, m_NPC);
        m_TMP1 = r16_r(7) - 2; r16_w(7, m_TMP1); write16(m_TMP1, (m_CCR << 8) | ((m_PC >> 16) & 0xff));
        m_PC = read16i(2 * m_taken_irq_vector);
        internal(1);
        irq_setup(); update_irq_filter(); interrupt_taken();
        m_NPC = m_PC & 0xffff; m_PIR = read16i(m_PC); m_PC += 2; m_IR[0] = m_PIR;
    }

    void execute() {
        const uint32_t A = m_NPC;
        const uint16_t w0 = m_IR[0];
        // decode cache by address: the code runs from ROM, which doesn't change (no extra reads
        // from the bus nor a linear search on every instruction). Outside the ROM it is decoded every time.
        const bool cacheable = A < DC_LIMIT && rom_code;
        const OpInfo* hit = cacheable ? dcache_[A >> 1] : nullptr;
        if (!hit) {
        uint16_t pk[6]; pk[0] = w0;
        for (int j = 1; j < 6; j++) pk[j] = bus->r16((A + 2 * j) & 0xfffe);                // read without cycle cost
        for (int idx : bucket_[w0 >> 8]) {
            const OpInfo& o = T_[idx];
            if ((w0 & o.m[0]) != o.w[0]) continue;
            bool ok = true;
            for (int k = 1; k < o.nw; k++) if ((pk[k + o.skip] & o.m[k]) != o.w[k]) { ok = false; break; }
            if (ok) { hit = &o; break; }
        }
        if (hit && cacheable) dcache_[A >> 1] = hit;
        }
        if (!hit) {
            illegal_count++; last_illegal_pc = A;
            if (illegal_count < 10) fprintf(stderr, "H8: invalid instruction @%06x (%04x)\n", A, w0);
            m_PC = A + 2; m_NPC = m_PC; m_PIR = read16i(m_PC); m_PC += 2; m_IR[0] = m_PIR; return;
        }
        const int total = hit->nw + hit->skip + hit->extra;
        for (int j = 1; j < total; j++) { m_IR[j] = read16i(m_PC); m_PC += 2; }
        if (trace && trace_left) { trace_left--; fprintf(stderr, "H8 %06x: %-28s R0=%04x R1=%04x R2=%04x R7=%08x CCR=%02x\n", A, hit->name, m_R[0], m_R[1], m_R[2], r32_r(7), m_CCR); }
        insn_count++;
        (this->*(hit->fn))();
    }
};
