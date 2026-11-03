// GENERATO DA h8gen.py - NON MODIFICARE A MANO.
// Interprete H8/300H derivato dal microcodice di MAME (src/devices/cpu/h8/h8.lst, h8.cpp).
// license:BSD-3-Clause   copyright-holders: Olivier Galibert e altri
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
class H8o {
public:
    enum { F_I = 0x80, F_UI = 0x40, F_H = 0x20, F_U = 0x10, F_N = 0x08, F_Z = 0x04, F_V = 0x02, F_C = 0x01 };
    enum { SUSPEND_REASON_CLOCK = 0 };
    enum { EXR_I = 7, EXR_NC = 0x78, EXR_T = 0x80 };   // solo H8S (non usati sull'H8/300H)
    H8Bus* bus = nullptr;
    uint16_t m_R[16] = {0};               // Rn (0-7), En (8-15)
    uint32_t m_PC = 0, m_NPC = 0, m_PPC = 0, m_TMP1 = 0, m_TMP2 = 0;
    uint16_t m_PIR = 0, m_IR[8] = {0};
    uint8_t  m_CCR = 0;
    uint32_t m_EXR = 0;
    bool m_has_hc = true, m_has_mac = false, m_mode_advanced = true, m_sleeping = false, m_standby_pending = false;
    int  m_irq_vector = 0, m_taken_irq_vector = 0;
    uint64_t m_cycles = 0;                // stati (cicli di clock) consumati
    uint64_t m_standby_time = 0;
    struct { void operator()(int) {} } m_standby_cb;
    uint8_t m_syscr = 0x09;
    void (*on_update_irq_filter)(void*) = nullptr; void* user = nullptr;

    // ---- accesso ai registri
    inline void r8_w(int reg, u8 val) { if(reg & 8) m_R[reg & 7] = (m_R[reg & 7] & 0xff00) | val; else m_R[reg & 7] = (m_R[reg & 7] & 0xff) | (val << 8); }
    inline u8  r8_r(int reg) { return (reg & 8) ? (u8)m_R[reg & 7] : (u8)(m_R[reg & 7] >> 8); }
    inline void r16_w(int reg, u16 val) { m_R[reg & 0xf] = val; }
    inline u16  r16_r(int reg) { return m_R[reg & 0xf]; }
    inline void r32_w(int reg, u32 val) { m_R[reg & 7] = val; m_R[(reg & 7) | 8] = val >> 16; }
    inline u32  r32_r(int reg) const { return m_R[reg & 7] | (m_R[(reg & 7) | 8] << 16); }

    // ---- bus
    inline u16 read16i(u32 adr) { m_cycles += 2; return bus->r16(adr & 0xfffffe); }
    inline u8  read8(u32 adr)   { m_cycles += 2; return bus->r8(adr & 0xffffff); }
    inline void write8(u32 adr, u8 d)   { m_cycles += 2; bus->w8(adr & 0xffffff, d); }
    inline u16 read16(u32 adr)  { m_cycles += 2; return bus->r16(adr & 0xfffffe); }
    inline void write16(u32 adr, u16 d) { m_cycles += 2; bus->w16(adr & 0xfffffe, d); }
    inline void internal(int c) { m_cycles += c + 1; }

    // ---- pipeline / controllo (stessa semantica di MAME)
    inline void prefetch_done()       { m_IR[0] = m_PIR; }
    inline void prefetch_done_noirq() { m_IR[0] = m_PIR; }
    inline void prefetch_done_notrace() { m_IR[0] = m_PIR; }
    inline void prefetch_switch(u32 pc, u16 ir) { m_NPC = pc & 0xffffff; m_PC = pc + 2; m_PIR = ir; }
    inline void update_irq_filter() { if(on_update_irq_filter) on_update_irq_filter(user); }
    inline void irq_setup() { if(m_syscr & 0x08) m_CCR |= F_I; else m_CCR |= F_I | F_UI; }
    void (*on_irq_taken)(void*, int) = nullptr;
    inline void interrupt_taken() { if(on_irq_taken) on_irq_taken(user, m_taken_irq_vector); }
    inline void debugger_exception_hook(int) {}
    inline bool exr_in_stack() const { return false; }
    inline int  trapa_setup() { if(m_syscr & 0x08) m_CCR |= F_I; else m_CCR |= F_I | F_UI; return 8; }
    inline int  trace_setup() { return 0; }
    inline uint64_t total_cycles() const { return m_cycles; }
    inline void notify_standby(int) {}
    inline void suspend(int, bool) {}
    inline void illegal() { m_illegal = true; }
    bool m_illegal = false;

    // ---- funzioni aritmetiche e di flag (da MAME)
    u8 do_addx8(u8 v1, u8 v2)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	u16 res = v1 + v2 + c;
    	m_CCR &= ~(F_N|F_V|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xf) + (v2 & 0xf) + c) & 0x10)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(u8(res))
    		m_CCR &= ~F_Z;
    	if(s8(res) < 0)
    		m_CCR |= F_N;
    	if(~(v1^v2) & (v1^res) & 0x80)
    		m_CCR |= F_V;
    	if(res & 0x100)
    		m_CCR |= F_C;
    	return res;
    }
    u8 do_subx8(u8 v1, u8 v2)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	u16 res = v1 - v2 - c;
    	m_CCR &= ~(F_N|F_V|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xf) - (v2 & 0xf) - c) & 0x10)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(u8(res))
    		m_CCR &= ~F_Z;
    	if(s8(res) < 0)
    		m_CCR |= F_N;
    	if((v1^v2) & (v1^res) & 0x80)
    		m_CCR |= F_V;
    	if(res & 0x100)
    		m_CCR |= F_C;
    	return res;
    }
    u8 do_inc8(u8 v1, u8 v2)
    {
    	u8 res = v1 + v2;
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!res)
    		m_CCR |= F_Z;
    	else if(s8(res) < 0)
    		m_CCR |= F_N;
    	if(~(v1^v2) & (v1^res) & 0x80)
    		m_CCR |= F_V;
    	return res;
    }
    u16 do_inc16(u16 v1, u16 v2)
    {
    	u16 res = v1 + v2;
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!res)
    		m_CCR |= F_Z;
    	else if(s16(res) < 0)
    		m_CCR |= F_N;
    	if(~(v1^v2) & (v1^res) & 0x8000)
    		m_CCR |= F_V;
    	return res;
    }
    u32 do_inc32(u32 v1, u32 v2)
    {
    	u32 res = v1 + v2;
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!res)
    		m_CCR |= F_Z;
    	else if(s32(res) < 0)
    		m_CCR |= F_N;
    	if(~(v1^v2) & (v1^res) & 0x80000000)
    		m_CCR |= F_V;
    	return res;
    }
    u8 do_add8(u8 v1, u8 v2)
    {
    	u16 res = v1 + v2;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xf) + (v2 & 0xf)) & 0x10)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(!u8(res))
    		m_CCR |= F_Z;
    	else if(s8(res) < 0)
    		m_CCR |= F_N;
    	if(~(v1^v2) & (v1^res) & 0x80)
    		m_CCR |= F_V;
    	if(res & 0x100)
    		m_CCR |= F_C;
    	return res;
    }
    u16 do_add16(u16 v1, u16 v2)
    {
    	u32 res = v1 + v2;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xfff) + (v2 & 0xfff)) & 0x1000)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(!u16(res))
    		m_CCR |= F_Z;
    	else if(s16(res) < 0)
    		m_CCR |= F_N;
    	if(~(v1^v2) & (v1^res) & 0x8000)
    		m_CCR |= F_V;
    	if(res & 0x10000)
    		m_CCR |= F_C;
    	return res;
    }
    u32 do_add32(u32 v1, u32 v2)
    {
    	u64 res = u64(v1) + u64(v2);
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xfffffff) + (v2 & 0xfffffff)) & 0x10000000)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(!u32(res))
    		m_CCR |= F_Z;
    	else if(s32(res) < 0)
    		m_CCR |= F_N;
    	if(~(v1^v2) & (v1^res) & 0x80000000)
    		m_CCR |= F_V;
    	if(res & 0x100000000ULL)
    		m_CCR |= F_C;
    	return res;
    }
    u8 do_dec8(u8 v1, u8 v2)
    {
    	u8 res = v1 - v2;
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!res)
    		m_CCR |= F_Z;
    	else if(s8(res) < 0)
    		m_CCR |= F_N;
    	if((v1^v2) & (v1^res) & 0x80)
    		m_CCR |= F_V;
    	return res;
    }
    u16 do_dec16(u16 v1, u16 v2)
    {
    	u16 res = v1 - v2;
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!res)
    		m_CCR |= F_Z;
    	else if(s16(res) < 0)
    		m_CCR |= F_N;
    	if((v1^v2) & (v1^res) & 0x8000)
    		m_CCR |= F_V;
    	return res;
    }
    u32 do_dec32(u32 v1, u32 v2)
    {
    	u32 res = v1 - v2;
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!res)
    		m_CCR |= F_Z;
    	else if(s32(res) < 0)
    		m_CCR |= F_N;
    	if((v1^v2) & (v1^res) & 0x80000000)
    		m_CCR |= F_V;
    	return res;
    }
    u8 do_sub8(u8 v1, u8 v2)
    {
    	u16 res = v1 - v2;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xf) - (v2 & 0xf)) & 0x10)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(!u8(res))
    		m_CCR |= F_Z;
    	else if(s8(res) < 0)
    		m_CCR |= F_N;
    	if((v1^v2) & (v1^res) & 0x80)
    		m_CCR |= F_V;
    	if(res & 0x100)
    		m_CCR |= F_C;
    	return res;
    }
    u16 do_sub16(u16 v1, u16 v2)
    {
    	u32 res = v1 - v2;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xfff) - (v2 & 0xfff)) & 0x1000)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(!u16(res))
    		m_CCR |= F_Z;
    	else if(s16(res) < 0)
    		m_CCR |= F_N;
    	if((v1^v2) & (v1^res) & 0x8000)
    		m_CCR |= F_V;
    	if(res & 0x10000)
    		m_CCR |= F_C;
    	return res;
    }
    u32 do_sub32(u32 v1, u32 v2)
    {
    	u64 res = u64(v1) - u64(v2);
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(m_has_hc) {
    		if(((v1 & 0xfffffff) - (v2 & 0xfffffff)) & 0x10000000)
    			m_CCR |= F_H;
    		else
    			m_CCR &= ~F_H;
    	}
    	if(!u32(res))
    		m_CCR |= F_Z;
    	else if(s32(res) < 0)
    		m_CCR |= F_N;
    	if((v1^v2) & (v1^res) & 0x80000000)
    		m_CCR |= F_V;
    	if(res & 0x100000000ULL)
    		m_CCR |= F_C;
    	return res;
    }
    u8 do_shal8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80)
    		m_CCR |= F_C;
    	if((v & 0xc0) == 0x40 || (v & 0xc0) == 0x80)
    		m_CCR |= F_V;
    	v <<= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_shal16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x8000)
    		m_CCR |= F_C;
    	if((v & 0xc000) == 0x4000 || (v & 0xc000) == 0x8000)
    		m_CCR |= F_V;
    	v <<= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_shal32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80000000)
    		m_CCR |= F_C;
    	if((v & 0xc0000000) == 0x40000000 || (v & 0xc0000000) == 0x80000000)
    		m_CCR |= F_V;
    	v <<= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_shar8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 1)
    		m_CCR |= F_C;
    	v >>= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(v & 0x40) {
    		v |= 0x80;
    		m_CCR |= F_N;
    	}
    	return v;
    }
    u16 do_shar16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 1)
    		m_CCR |= F_C;
    	v >>= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(v & 0x4000) {
    		v |= 0x8000;
    		m_CCR |= F_N;
    	}
    	return v;
    }
    u32 do_shar32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 1)
    		m_CCR |= F_C;
    	v >>= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(v & 0x40000000) {
    		v |= 0x80000000;
    		m_CCR |= F_N;
    	}
    	return v;
    }
    u8 do_shll8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80)
    		m_CCR |= F_C;
    	v <<= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_shll16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x8000)
    		m_CCR |= F_C;
    	v <<= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_shll32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80000000)
    		m_CCR |= F_C;
    	v <<= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_shlr8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 1)
    		m_CCR |= F_C;
    	v >>= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	return v;
    }
    u16 do_shlr16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 1)
    		m_CCR |= F_C;
    	v >>= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	return v;
    }
    u32 do_shlr32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 1)
    		m_CCR |= F_C;
    	v >>= 1;
    	if(!v)
    		m_CCR |= F_Z;
    	return v;
    }
    u8 do_shal2_8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40)
    		m_CCR |= F_C;
    	if((v & 0xc0) == 0x40 || (v & 0xc0) == 0x80 ||
    		(v & 0x60) == 0x20 || (v & 0x60) == 0x40)
    		m_CCR |= F_V;
    	v <<= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_shal2_16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x4000)
    		m_CCR |= F_C;
    	if((v & 0xc000) == 0x4000 || (v & 0xc000) == 0x8000 ||
    		(v & 0x6000) == 0x2000 || (v & 0x6000) == 0x4000)
    		m_CCR |= F_V;
    	v <<= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_shal2_32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40000000)
    		m_CCR |= F_C;
    	if((v & 0xc0000000) == 0x40000000 || (v & 0xc0000000) == 0x80000000 ||
    		(v & 0x60000000) == 0x20000000 || (v & 0x60000000) == 0x40000000)
    		m_CCR |= F_V;
    	v <<= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_shar2_8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 2)
    		m_CCR |= F_C;
    	v >>= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(v & 0x20) {
    		v |= 0xc0;
    		m_CCR |= F_N;
    	}
    	return v;
    }
    u16 do_shar2_16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 2)
    		m_CCR |= F_C;
    	v >>= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(v & 0x2000) {
    		v |= 0xc000;
    		m_CCR |= F_N;
    	}
    	return v;
    }
    u32 do_shar2_32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 2)
    		m_CCR |= F_C;
    	v >>= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(v & 0x20000000) {
    		v |= 0xc0000000;
    		m_CCR |= F_N;
    	}
    	return v;
    }
    u8 do_shll2_8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40)
    		m_CCR |= F_C;
    	v <<= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_shll2_16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x4000)
    		m_CCR |= F_C;
    	v <<= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_shll2_32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40000000)
    		m_CCR |= F_C;
    	v <<= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_shlr2_8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 2)
    		m_CCR |= F_C;
    	v >>= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	return v;
    }
    u16 do_shlr2_16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 2)
    		m_CCR |= F_C;
    	v >>= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	return v;
    }
    u32 do_shlr2_32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 2)
    		m_CCR |= F_C;
    	v >>= 2;
    	if(!v)
    		m_CCR |= F_Z;
    	return v;
    }
    u8 do_rotl8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80)
    		m_CCR |= F_C;
    	v = (v << 1) | (v >> 7);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotl16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x8000)
    		m_CCR |= F_C;
    	v = (v << 1) | (v >> 15);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotl32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80000000)
    		m_CCR |= F_C;
    	v = (v << 1) | (v >> 31);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_rotr8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x01)
    		m_CCR |= F_C;
    	v = (v << 7) | (v >> 1);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotr16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x0001)
    		m_CCR |= F_C;
    	v = (v << 15) | (v >> 1);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotr32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x00000001)
    		m_CCR |= F_C;
    	v = (v << 31) | (v >> 1);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_rotxl8(u8 v)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80)
    		m_CCR |= F_C;
    	v = (v << 1) | c;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotxl16(u16 v)
    {
    	u16 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x8000)
    		m_CCR |= F_C;
    	v = (v << 1) | c;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotxl32(u32 v)
    {
    	u32 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x80000000)
    		m_CCR |= F_C;
    	v = (v << 1) | c;
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_rotxr8(u8 v)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x01)
    		m_CCR |= F_C;
    	v = (v >> 1) | (c << 7);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotxr16(u16 v)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x0001)
    		m_CCR |= F_C;
    	v = (v >> 1) | (c << 15);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotxr32(u32 v)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x00000001)
    		m_CCR |= F_C;
    	v = (v >> 1) | (c << 31);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_rotl2_8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40)
    		m_CCR |= F_C;
    	v = (v << 2) | (v >> 6);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotl2_16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x4000)
    		m_CCR |= F_C;
    	v = (v << 2) | (v >> 14);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotl2_32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40000000)
    		m_CCR |= F_C;
    	v = (v << 2) | (v >> 30);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_rotr2_8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x02)
    		m_CCR |= F_C;
    	v = (v << 6) | (v >> 2);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotr2_16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x0002)
    		m_CCR |= F_C;
    	v = (v << 14) | (v >> 2);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotr2_32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x00000002)
    		m_CCR |= F_C;
    	v = (v << 30) | (v >> 2);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_rotxl2_8(u8 v)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40)
    		m_CCR |= F_C;
    	v = (v << 2) | (c << 1) | (v >> 7);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotxl2_16(u16 v)
    {
    	u16 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x4000)
    		m_CCR |= F_C;
    	v = (v << 2) | (c << 1) | (v >> 15);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotxl2_32(u32 v)
    {
    	u32 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x40000000)
    		m_CCR |= F_C;
    	v = (v << 2) | (c << 1) | (v >> 31);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u8 do_rotxr2_8(u8 v)
    {
    	u8 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x02)
    		m_CCR |= F_C;
    	v = (v >> 2) | (c << 6) | (v << 7);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u16 do_rotxr2_16(u16 v)
    {
    	u16 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x0002)
    		m_CCR |= F_C;
    	v = (v >> 2) | (c << 14) | (v << 15);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    u32 do_rotxr2_32(u32 v)
    {
    	u32 c = m_CCR & F_C ? 1 : 0;
    	m_CCR &= ~(F_N|F_V|F_Z|F_C);
    	if(v & 0x00000002)
    		m_CCR |= F_C;
    	v = (v >> 2) | (c << 30) | (v << 31);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    	return v;
    }
    void set_nzv8(u8 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s8(v) < 0)
    		m_CCR |= F_N;
    }
    void set_nzv16(u16 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    }
    void set_nzv32(u32 v)
    {
    	m_CCR &= ~(F_N|F_V|F_Z);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    }
    void set_nz16(u16 v)
    {
    	m_CCR &= ~(F_N|F_Z);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s16(v) < 0)
    		m_CCR |= F_N;
    }
    void set_nz32(u32 v)
    {
    	m_CCR &= ~(F_N|F_Z);
    	if(!v)
    		m_CCR |= F_Z;
    	else if(s32(v) < 0)
    		m_CCR |= F_N;
    }

    // ---- istruzioni (corpi del microcodice)
	// nop - -   [0000/ffff skip=0 extra=0]
	void op_0() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// sleep - -   [0180/ffff skip=0 extra=0]
	void op_1() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	if(m_standby_pending) {
		m_standby_time = total_cycles();
		notify_standby(1);
		suspend(SUSPEND_REASON_CLOCK, true);
		m_standby_cb(1);
	} else {
		if(!m_irq_vector) { m_sleeping = true; return; }
		prefetch_done();
	}
	}
	// stc ccr r8l   [0200/fff0 skip=0 extra=0]
	void op_2() {
	r8_w(m_IR[0], m_CCR);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// ldc r8l ccr   [0300/fff0 skip=0 extra=0]
	void op_3() {
	m_CCR = r8_r(m_IR[0]);
	update_irq_filter();
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done_noirq();
	}
	// orc imm8 ccr   [0400/ff00 skip=0 extra=0]
	void op_4() {
	m_CCR |= m_IR[0];
	update_irq_filter();
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done_noirq();
	}
	// xorc imm8 ccr   [0500/ff00 skip=0 extra=0]
	void op_5() {
	m_CCR ^= m_IR[0];
	update_irq_filter();
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done_noirq();
	}
	// andc imm8 ccr   [0600/ff00 skip=0 extra=0]
	void op_6() {
	m_CCR &= m_IR[0];
	update_irq_filter();
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done_noirq();
	}
	// ldc imm8 ccr   [0700/ff00 skip=0 extra=0]
	void op_7() {
	m_CCR = m_IR[0];
	update_irq_filter();
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done_noirq();
	}
	// add.b r8h r8l   [0800/ff00 skip=0 extra=0]
	void op_8() {
	r8_w(m_IR[0], do_add8(r8_r(m_IR[0]), r8_r(m_IR[0] >> 4)));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// add.w r16h r16l   [0900/ff00 skip=0 extra=0]
	void op_9() {
	r16_w(m_IR[0], do_add16(r16_r(m_IR[0]), r16_r(m_IR[0] >> 4)));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// inc.b one r8l   [0a00/fff0 skip=0 extra=0]
	void op_10() {
	r8_w(m_IR[0], do_inc8(r8_r(m_IR[0]), 1));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// adds.l one r16l   [0b00/fff8 skip=0 extra=0]
	void op_11() {
	r16_w(m_IR[0], r16_r(m_IR[0])+1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// adds.l two r16l   [0b80/fff8 skip=0 extra=0]
	void op_12() {
	r16_w(m_IR[0], r16_r(m_IR[0])+2);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// mov.b r8h r8l   [0c00/ff00 skip=0 extra=0]
	void op_13() {
	m_TMP1 = r8_r(m_IR[0] >> 4);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// mov.w r16h r16l   [0d00/ff00 skip=0 extra=0]
	void op_14() {
	m_TMP1 = r16_r(m_IR[0] >> 4);
	set_nzv16(m_TMP1);
	r16_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// addx.b r8h r8l   [0e00/ff00 skip=0 extra=0]
	void op_15() {
	r8_w(m_IR[0], do_addx8(r8_r(m_IR[0]), r8_r(m_IR[0] >> 4)));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// daa.b r8l -   [0f00/fff0 skip=0 extra=0]
	void op_16() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP2 = 0;
	if(m_CCR & F_C) {
		if(m_has_hc && (m_CCR & F_H)) {
			if((m_TMP1 & 0xf0) <= 0x30 && (m_TMP1 & 0x0f) <= 3)
				m_TMP2 = 0x66;
		} else {
			if((m_TMP1 & 0xf0) <= 0x20)
				m_TMP2 = (m_TMP1 & 0x0f) <= 9 ? 0x60 : 0x66;
		}
	} else {
		if(m_has_hc && (m_CCR & F_H)) {
			if((m_TMP1 & 0x0f) <= 3)
				m_TMP2 = (m_TMP1 & 0xf0) <= 0x90 ? 0x06 : 0x66;
		} else {
			if((m_TMP1 & 0x0f) <= 9)
				m_TMP2 = (m_TMP1 & 0xf0) <= 0x90 ? 0x00 : 0x60;
			else
				m_TMP2 = (m_TMP1 & 0xf0) <= 0x80 ? 0x06 : 0x66;
		}
	}
	r8_w(m_IR[0], do_add8(m_TMP1, m_TMP2));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// shll.b r8l -   [1000/fff0 skip=0 extra=0]
	void op_17() {
	r8_w(m_IR[0], do_shll8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// shal.b r8l -   [1080/fff0 skip=0 extra=0]
	void op_18() {
	r8_w(m_IR[0], do_shal8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// shlr.b r8l -   [1100/fff0 skip=0 extra=0]
	void op_19() {
	r8_w(m_IR[0], do_shlr8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// shar.b r8l -   [1180/fff0 skip=0 extra=0]
	void op_20() {
	r8_w(m_IR[0], do_shar8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// rotxl.b r8l -   [1200/fff0 skip=0 extra=0]
	void op_21() {
	r8_w(m_IR[0], do_rotxl8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// rotl.b r8l -   [1280/fff0 skip=0 extra=0]
	void op_22() {
	r8_w(m_IR[0], do_rotl8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// rotxr.b r8l -   [1300/fff0 skip=0 extra=0]
	void op_23() {
	r8_w(m_IR[0], do_rotxr8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// rotr.b r8l -   [1380/fff0 skip=0 extra=0]
	void op_24() {
	r8_w(m_IR[0], do_rotr8(r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// or.b r8h r8l   [1400/ff00 skip=0 extra=0]
	void op_25() {
	m_TMP1 = r8_r(m_IR[0] >> 4) | r8_r(m_IR[0]);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// xor.b r8h r8l   [1500/ff00 skip=0 extra=0]
	void op_26() {
	m_TMP1 = r8_r(m_IR[0] >> 4) ^ r8_r(m_IR[0]);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// and.b r8h r8l   [1600/ff00 skip=0 extra=0]
	void op_27() {
	m_TMP1 = r8_r(m_IR[0] >> 4) & r8_r(m_IR[0]);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// not.b r8l -   [1700/fff0 skip=0 extra=0]
	void op_28() {
	m_TMP1 = ~r8_r(m_IR[0]);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// neg.b r8l -   [1780/fff0 skip=0 extra=0]
	void op_29() {
	r8_w(m_IR[0], do_sub8(0, r8_r(m_IR[0])));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// sub.b r8h r8l   [1800/ff00 skip=0 extra=0]
	void op_30() {
	r8_w(m_IR[0], do_sub8(r8_r(m_IR[0]), r8_r(m_IR[0] >> 4)));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// sub.w r16h r16l   [1900/ff00 skip=0 extra=0]
	void op_31() {
	r16_w(m_IR[0], do_sub16(r16_r(m_IR[0]), r16_r(m_IR[0] >> 4)));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// dec.b one r8l   [1a00/fff0 skip=0 extra=0]
	void op_32() {
	r8_w(m_IR[0], do_dec8(r8_r(m_IR[0]), 1));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// subs.l one r16l   [1b00/fff8 skip=0 extra=0]
	void op_33() {
	r16_w(m_IR[0], r16_r(m_IR[0])-1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// subs.l two r16l   [1b80/fff8 skip=0 extra=0]
	void op_34() {
	r16_w(m_IR[0], r16_r(m_IR[0])-2);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// cmp.b r8h r8l   [1c00/ff00 skip=0 extra=0]
	void op_35() {
	do_sub8(r8_r(m_IR[0]), r8_r(m_IR[0] >> 4));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// cmp.w r16h r16l   [1d00/ff00 skip=0 extra=0]
	void op_36() {
	do_sub16(r16_r(m_IR[0]), r16_r(m_IR[0] >> 4));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// subx.b r8h r8l   [1e00/ff00 skip=0 extra=0]
	void op_37() {
	r8_w(m_IR[0], do_subx8(r8_r(m_IR[0]), r8_r(m_IR[0] >> 4)));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// das.b r8l -   [1f00/fff0 skip=0 extra=0]
	void op_38() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP2 = 0;
	if(m_CCR & F_C) {
		if(m_has_hc && (m_CCR & F_H)) {
			if((m_TMP1 & 0xf0) >= 0x60 && (m_TMP1 & 0x0f) >= 6)
				m_TMP2 = 0x9a;
		} else {
			if((m_TMP1 & 0xf0) >= 0x70 && (m_TMP1 & 0x0f) <= 9)
				m_TMP2 = 0xa0;
		}
	} else {
		if(m_has_hc && (m_CCR & F_H)) {
			if((m_TMP1 & 0xf0) <= 0x80 && (m_TMP1 & 0x0f) >= 6)
				m_TMP2 = 0xfa;
		}
	}
	r8_w(m_IR[0], do_add8(m_TMP1, m_TMP2));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// mov.b abs8 r8u   [2000/f000 skip=0 extra=0]
	void op_39() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = read8(0xffffff00 | m_IR[0]);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0] >> 8, m_TMP1);
	prefetch_done();
	}
	// mov.b r8u abs8   [3000/f000 skip=0 extra=0]
	void op_40() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r8_r(m_IR[0] >> 8);
	set_nzv8(m_TMP1);
	write8(0xffffff00 | m_IR[0], m_TMP1);
	prefetch_done();
	}
	// bt rel8 -   [4000/ff00 skip=0 extra=0]
	void op_41() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(true)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bf rel8 -   [4100/ff00 skip=0 extra=0]
	void op_42() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(false)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bhi rel8 -   [4200/ff00 skip=0 extra=0]
	void op_43() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(!(m_CCR & (F_C|F_Z)))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bls rel8 -   [4300/ff00 skip=0 extra=0]
	void op_44() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(m_CCR & (F_C|F_Z))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bcc rel8 -   [4400/ff00 skip=0 extra=0]
	void op_45() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(!(m_CCR & F_C))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bcs rel8 -   [4500/ff00 skip=0 extra=0]
	void op_46() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(m_CCR & F_C)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bne rel8 -   [4600/ff00 skip=0 extra=0]
	void op_47() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(!(m_CCR & F_Z))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// beq rel8 -   [4700/ff00 skip=0 extra=0]
	void op_48() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(m_CCR & F_Z)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bvc rel8 -   [4800/ff00 skip=0 extra=0]
	void op_49() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(!(m_CCR & F_V))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bvs rel8 -   [4900/ff00 skip=0 extra=0]
	void op_50() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(m_CCR & F_V)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bpl rel8 -   [4a00/ff00 skip=0 extra=0]
	void op_51() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(!(m_CCR & F_N))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bmi rel8 -   [4b00/ff00 skip=0 extra=0]
	void op_52() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(m_CCR & F_N)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bge rel8 -   [4c00/ff00 skip=0 extra=0]
	void op_53() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(!((m_CCR & (F_N|F_V)) == F_N || (m_CCR & (F_N|F_V)) == F_V))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// blt rel8 -   [4d00/ff00 skip=0 extra=0]
	void op_54() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if((m_CCR & (F_N|F_V)) == F_N || (m_CCR & (F_N|F_V)) == F_V)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bgt rel8 -   [4e00/ff00 skip=0 extra=0]
	void op_55() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if(!((m_CCR & F_Z) || (m_CCR & (F_N|F_V)) == F_N || (m_CCR & (F_N|F_V)) == F_V))
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// ble rel8 -   [4f00/ff00 skip=0 extra=0]
	void op_56() {
	m_TMP1 = m_PC + s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP2 = read16i(m_TMP1);
	if((m_CCR & F_Z) || (m_CCR & (F_N|F_V)) == F_N || (m_CCR & (F_N|F_V)) == F_V)
		prefetch_switch(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// mulxu.b r8h r16l   [5000/ff00 skip=0 extra=0]
	void op_57() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	r16_w(m_IR[0], u8(r16_r(m_IR[0])) * r8_r(m_IR[0] >> 4));
	internal(m_has_mac ? 2 : 11);
	prefetch_done();
	}
	// divxu.b r8h r16l   [5100/ff00 skip=0 extra=0]
	void op_58() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	internal(11);
	m_TMP1 = r16_r(m_IR[0]);
	m_TMP2 = r8_r(m_IR[0] >> 4);
	m_CCR &= ~(F_Z|F_N);
	if(m_TMP2 & 0x80)
		m_CCR |= F_N;
	if(!m_TMP2) {
		m_CCR |= F_Z;
	} else {
		int q = m_TMP1 / m_TMP2;
		int r = m_TMP1 % m_TMP2;
		r16_w(m_IR[0], (q & 0xff) | ((r & 0xff) << 8));
	}
	prefetch_done();
	}
	// rts - -   [5470/ffff skip=0 extra=0]
	void op_59() {
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(7);
	m_TMP2 = read16(m_TMP1);
	r16_w(7, m_TMP1+2);
	internal(1);
	m_PC = m_TMP2;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bsr rel8 -   [5500/ff00 skip=0 extra=0]
	void op_60() {
	m_PIR = read16i(m_PC);
	m_TMP2 = m_PC;
	m_PC += s8(m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(7) - 2;
	r16_w(7, m_TMP1);
	write16(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// rte - -   [5670/ffff skip=0 extra=0]
	void op_61() {
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(7);
	m_CCR = read16(m_TMP1) >> 8;
	m_TMP2 = read16(m_TMP1+2);
	r16_w(7, m_TMP1+4);
	internal(1);
	m_PC = m_TMP2;
	update_irq_filter();
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done_notrace();
	}
	// jmp r16h -   [5900/ff8f skip=0 extra=0]
	void op_62() {
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_PC = r16_r(m_IR[0] >> 4);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// jmp abs16e -   [5a00/ffff skip=0 extra=1]
	void op_63() {
	internal(1);
	m_PC = m_IR[1];
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// jmp abs8i -   [5b00/ff00 skip=0 extra=0]
	void op_64() {
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_PC = read16(m_IR[0] & 0xff);
	internal(1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// jsr r16h -   [5d00/ff8f skip=0 extra=0]
	void op_65() {
	m_PIR = read16i(m_PC);
	m_TMP2 = m_PC;
	m_PC = r16_r(m_IR[0] >> 4);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(7) - 2;
	r16_w(7, m_TMP1);
	write16(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// jsr abs16e -   [5e00/ffff skip=0 extra=1]
	void op_66() {
	internal(1);
	m_TMP2 = m_PC;
	m_PC = m_IR[1];
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(7) - 2;
	r16_w(7, m_TMP1);
	write16(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// jsr abs8i -   [5f00/ff00 skip=0 extra=0]
	void op_67() {
	m_PIR = read16i(m_PC);
	m_TMP2 = m_PC;
	m_PC = read16(m_IR[0] & 0xff);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(7) - 2;
	r16_w(7, m_TMP1);
	write16(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bset r8h r8l   [6000/ff00 skip=0 extra=0]
	void op_68() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP1 |= 1 << ((r8_r(m_IR[0] >> 4)) & 7);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bnot r8h r8l   [6100/ff00 skip=0 extra=0]
	void op_69() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP1 ^= 1 << ((r8_r(m_IR[0] >> 4)) & 7);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bclr r8h r8l   [6200/ff00 skip=0 extra=0]
	void op_70() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP1 &= ~(1 << ((r8_r(m_IR[0] >> 4)) & 7));
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// btst r8h r8l   [6300/ff00 skip=0 extra=0]
	void op_71() {
	m_TMP1 = r8_r(m_IR[0]);
	if(m_TMP1 & (1 << ((r8_r(m_IR[0] >> 4)) & 7)))
		m_CCR &= ~F_Z;
	else
		m_CCR |= F_Z;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bst imm3 r8l   [6700/ff80 skip=0 extra=0]
	void op_72() {
	m_TMP1 = r8_r(m_IR[0]);
	if(m_CCR & F_C)
		m_TMP1 |= 1 << ((m_IR[0] >> 4) & 7);
	else
		m_TMP1 &= ~(1 << ((m_IR[0] >> 4) & 7));
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bist imm3 r8l   [6780/ff80 skip=0 extra=0]
	void op_73() {
	m_TMP1 = r8_r(m_IR[0]);
	if(!(m_CCR & F_C))
		m_TMP1 |= 1 << ((m_IR[0] >> 4) & 7);
	else
		m_TMP1 &= ~(1 << ((m_IR[0] >> 4) & 7));
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// mov.b r16ih r8l   [6800/ff80 skip=0 extra=0]
	void op_74() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = read8(r16_r(m_IR[0] >> 4));
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	prefetch_done();
	}
	// mov.b r8l r16ih   [6880/ff80 skip=0 extra=0]
	void op_75() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r8_r(m_IR[0]);
	set_nzv8(m_TMP1);
	write8(r16_r((m_IR[0] >> 4) & 7), m_TMP1);
	prefetch_done();
	}
	// mov.w r16ih r16l   [6900/ff88 skip=0 extra=0]
	void op_76() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = read16(r16_r(m_IR[0] >> 4));
	set_nzv16(m_TMP1);
	r16_w(m_IR[0], m_TMP1);
	prefetch_done();
	}
	// mov.w r16l r16ih   [6980/ff88 skip=0 extra=0]
	void op_77() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(m_IR[0]);
	set_nzv16(m_TMP1);
	write16(r16_r((m_IR[0] >> 4) & 7), m_TMP1);
	prefetch_done();
	}
	// mov.b abs16 r8l   [6a00/fff0 skip=0 extra=1]
	void op_78() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = read8(s16(m_IR[1]));
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	prefetch_done();
	}
	// movfpe abs16 r8l   [6a40/fff0 skip=0 extra=1]
	void op_79() {
	// TODO: timing
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = read8(s16(m_IR[1]));
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	prefetch_done();
	}
	// mov.b r8l abs16   [6a80/fff0 skip=0 extra=1]
	void op_80() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r8_r(m_IR[0]);
	set_nzv8(m_TMP1);
	write8(s16(m_IR[1]), m_TMP1);
	prefetch_done();
	}
	// movtpe r8l abs16   [6ac0/fff0 skip=0 extra=1]
	void op_81() {
	// TODO: timing
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r8_r(m_IR[0]);
	set_nzv8(m_TMP1);
	write8(s16(m_IR[1]), m_TMP1);
	prefetch_done();
	}
	// mov.w abs16 r16l   [6b00/fff0 skip=0 extra=1]
	void op_82() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = read16(s16(m_IR[1]));
	set_nzv16(m_TMP1);
	r16_w(m_IR[0], m_TMP1);
	prefetch_done();
	}
	// mov.w r16l abs16   [6b80/fff0 skip=0 extra=1]
	void op_83() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = r16_r(m_IR[0]);
	set_nzv16(m_TMP1);
	write16(s16(m_IR[1]), m_TMP1);
	prefetch_done();
	}
	// mov.b r16ph r8l   [6c00/ff80 skip=0 extra=0]
	void op_84() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	internal(1);
	m_TMP1 = read8(m_TMP2);
	m_TMP2 += 1;
	r16_w(m_IR[0] >> 4, m_TMP2);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0], m_TMP1);
	prefetch_done();
	}
	// mov.b r8l pr16h   [6c80/ff80 skip=0 extra=0]
	void op_85() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP2 = r16_r((m_IR[0] >> 4) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	internal(1);
	m_TMP2 -= 1;
	r16_w((m_IR[0] >> 4) & 7, m_TMP2);
	set_nzv8(m_TMP1);
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// mov.w r16ph r16l   [6d00/ff88 skip=0 extra=0]
	void op_86() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	internal(1);
	m_TMP1 = read16(m_TMP2);
	m_TMP2 += 2;
	r16_w(m_IR[0] >> 4, m_TMP2);
	set_nzv16(m_TMP1);
	r16_w(m_IR[0], m_TMP1);
	prefetch_done();
	}
	// mov.w r16l pr16h   [6d80/ff88 skip=0 extra=0]
	void op_87() {
	m_TMP1 = r16_r(m_IR[0]);
	m_TMP2 = r16_r((m_IR[0] >> 4) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	internal(1);
	m_TMP2 -= 2;
	r16_w((m_IR[0] >> 4) & 7, m_TMP2);
	set_nzv16(m_TMP1);
	write16(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// mov.b r16d16h r8l   [6e00/ff80 skip=0 extra=1]
	void op_88() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = u16(r16_r(m_IR[0] >> 4) + s16(m_IR[1]));
	m_TMP2 = read8(m_TMP1);
	set_nzv8(m_TMP2);
	r8_w(m_IR[0], m_TMP2);
	prefetch_done();
	}
	// mov.b r8l r16d16h   [6e80/ff80 skip=0 extra=1]
	void op_89() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = u16(r16_r((m_IR[0] >> 4) & 7) + s16(m_IR[1]));
	m_TMP2 = r8_r(m_IR[0]);
	set_nzv8(m_TMP2);
	write8(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// mov.w r16d16h r16l   [6f00/ff80 skip=0 extra=1]
	void op_90() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = u16(r16_r(m_IR[0] >> 4) + s16(m_IR[1]));
	m_TMP2 = read16(m_TMP1);
	set_nzv16(m_TMP2);
	r16_w(m_IR[0], m_TMP2);
	prefetch_done();
	}
	// mov.w r16l r16d16h   [6f80/ff80 skip=0 extra=1]
	void op_91() {
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 = u16(r16_r((m_IR[0] >> 4) & 7) + s16(m_IR[1]));
	m_TMP2 = r16_r(m_IR[0]);
	set_nzv16(m_TMP2);
	write16(m_TMP1, m_TMP2);
	prefetch_done();
	}
	// bset imm3 r8l   [7000/ff80 skip=0 extra=0]
	void op_92() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP1 |= 1 << ((m_IR[0] >> 4) & 7);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bnot imm3 r8l   [7100/ff80 skip=0 extra=0]
	void op_93() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP1 ^= 1 << ((m_IR[0] >> 4) & 7);
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bclr imm3 r8l   [7200/ff80 skip=0 extra=0]
	void op_94() {
	m_TMP1 = r8_r(m_IR[0]);
	m_TMP1 &= ~(1 << ((m_IR[0] >> 4) & 7));
	r8_w(m_IR[0], m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// btst imm3 r8l   [7300/ff80 skip=0 extra=0]
	void op_95() {
	m_TMP1 = r8_r(m_IR[0]);
	if(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7)))
		m_CCR &= ~F_Z;
	else
		m_CCR |= F_Z;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bor imm3 r8l   [7400/ff80 skip=0 extra=0]
	void op_96() {
	m_TMP1 = r8_r(m_IR[0]);
	if(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7)))
		m_CCR |= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bior imm3 r8l   [7480/ff80 skip=0 extra=0]
	void op_97() {
	m_TMP1 = r8_r(m_IR[0]);
	if(!(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7))))
		m_CCR |= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bxor imm3 r8l   [7500/ff80 skip=0 extra=0]
	void op_98() {
	m_TMP1 = r8_r(m_IR[0]);
	if(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7)))
		m_CCR ^= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bixor imm3 r8l   [7580/ff80 skip=0 extra=0]
	void op_99() {
	m_TMP1 = r8_r(m_IR[0]);
	if(!(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7))))
		m_CCR ^= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// band imm3 r8l   [7600/ff80 skip=0 extra=0]
	void op_100() {
	m_TMP1 = r8_r(m_IR[0]);
	if(!(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7))))
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// biand imm3 r8l   [7680/ff80 skip=0 extra=0]
	void op_101() {
	m_TMP1 = r8_r(m_IR[0]);
	if(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7)))
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bld imm3 r8l   [7700/ff80 skip=0 extra=0]
	void op_102() {
	m_TMP1 = r8_r(m_IR[0]);
	if(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7)))
		m_CCR |= F_C;
	else
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bild imm3 r8l   [7780/ff80 skip=0 extra=0]
	void op_103() {
	m_TMP1 = r8_r(m_IR[0]);
	if(!(m_TMP1 & (1 << ((m_IR[0] >> 4) & 7))))
		m_CCR |= F_C;
	else
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// mov.w imm16 r16l   [7900/fff0 skip=0 extra=1]
	void op_104() {
	set_nzv16(m_IR[1]);
	r16_w(m_IR[0], m_IR[1]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// eepmov.b - -   [7b5c598f/ffffffff skip=0 extra=0]
	void op_105() {
	while(r8_r(4+8)) {
		m_TMP1 = read8(r16_r(5));
		write8(r16_r(6), m_TMP1);
		r16_w(5, r16_r(5)+1);
		r16_w(6, r16_r(6)+1);
		r8_w(4+8, r8_r(4+8)-1);
	}
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// btst r8h r16ihh   [7c006300/ff8fff0f skip=0 extra=0]
	void op_106() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((r8_r(m_IR[1] >> 4)) & 7)))
		m_CCR &= ~F_Z;
	else
		m_CCR |= F_Z;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// btst imm3 r16ihh   [7c007300/ff8fff8f skip=0 extra=0]
	void op_107() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR &= ~F_Z;
	else
		m_CCR |= F_Z;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bor imm3 r16ihh   [7c007400/ff8fff8f skip=0 extra=0]
	void op_108() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR |= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bior imm3 r16ihh   [7c007480/ff8fff8f skip=0 extra=0]
	void op_109() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR |= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bxor imm3 r16ihh   [7c007500/ff8fff8f skip=0 extra=0]
	void op_110() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR ^= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bixor imm3 r16ihh   [7c007580/ff8fff8f skip=0 extra=0]
	void op_111() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR ^= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// band imm3 r16ihh   [7c007600/ff8fff8f skip=0 extra=0]
	void op_112() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// biand imm3 r16ihh   [7c007680/ff8fff8f skip=0 extra=0]
	void op_113() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bld imm3 r16ihh   [7c007700/ff8fff8f skip=0 extra=0]
	void op_114() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR |= F_C;
	else
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bild imm3 r16ihh   [7c007780/ff8fff8f skip=0 extra=0]
	void op_115() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR |= F_C;
	else
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bset r8h r16ihh   [7d006000/ff8fff0f skip=0 extra=0]
	void op_116() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	m_TMP1 |= 1 << ((r8_r(m_IR[1] >> 4)) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bnot r8h r16ihh   [7d006100/ff8fff0f skip=0 extra=0]
	void op_117() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	m_TMP1 ^= 1 << ((r8_r(m_IR[1] >> 4)) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bclr r8h r16ihh   [7d006200/ff8fff0f skip=0 extra=0]
	void op_118() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	m_TMP1 &= ~(1 << ((r8_r(m_IR[1] >> 4)) & 7));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bst imm3 r16ihh   [7d006700/ff8fff8f skip=0 extra=0]
	void op_119() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(m_CCR & F_C)
		m_TMP1 |= 1 << ((m_IR[1] >> 4) & 7);
	else
		m_TMP1 &= ~(1 << ((m_IR[1] >> 4) & 7));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bist imm3 r16ihh   [7d006780/ff8fff8f skip=0 extra=0]
	void op_120() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	if(!(m_CCR & F_C))
		m_TMP1 |= 1 << ((m_IR[1] >> 4) & 7);
	else
		m_TMP1 &= ~(1 << ((m_IR[1] >> 4) & 7));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bset imm3 r16ihh   [7d007000/ff8fff8f skip=0 extra=0]
	void op_121() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	m_TMP1 |= 1 << ((m_IR[1] >> 4) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bnot imm3 r16ihh   [7d007100/ff8fff8f skip=0 extra=0]
	void op_122() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	m_TMP1 ^= 1 << ((m_IR[1] >> 4) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bclr imm3 r16ihh   [7d007200/ff8fff8f skip=0 extra=0]
	void op_123() {
	m_TMP2 = r16_r(m_IR[0] >> 4);
	m_TMP1 = read8(m_TMP2);
	m_TMP1 &= ~(1 << ((m_IR[1] >> 4) & 7));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// btst r8h abs8   [7e006300/ff00ff0f skip=0 extra=0]
	void op_124() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((r8_r(m_IR[1] >> 4)) & 7)))
		m_CCR &= ~F_Z;
	else
		m_CCR |= F_Z;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// btst imm3 abs8   [7e007300/ff00ff8f skip=0 extra=0]
	void op_125() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR &= ~F_Z;
	else
		m_CCR |= F_Z;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bor imm3 abs8   [7e007400/ff00ff8f skip=0 extra=0]
	void op_126() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR |= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bior imm3 abs8   [7e007480/ff00ff8f skip=0 extra=0]
	void op_127() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR |= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bxor imm3 abs8   [7e007500/ff00ff8f skip=0 extra=0]
	void op_128() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR ^= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bixor imm3 abs8   [7e007580/ff00ff8f skip=0 extra=0]
	void op_129() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR ^= F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// band imm3 abs8   [7e007600/ff00ff8f skip=0 extra=0]
	void op_130() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// biand imm3 abs8   [7e007680/ff00ff8f skip=0 extra=0]
	void op_131() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bld imm3 abs8   [7e007700/ff00ff8f skip=0 extra=0]
	void op_132() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7)))
		m_CCR |= F_C;
	else
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bild imm3 abs8   [7e007780/ff00ff8f skip=0 extra=0]
	void op_133() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	if(!(m_TMP1 & (1 << ((m_IR[1] >> 4) & 7))))
		m_CCR |= F_C;
	else
		m_CCR &= ~F_C;
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// bset r8h abs8   [7f006000/ff00ff0f skip=0 extra=0]
	void op_134() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_TMP1 |= 1 << ((r8_r(m_IR[1] >> 4)) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bnot r8h abs8   [7f006100/ff00ff0f skip=0 extra=0]
	void op_135() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_TMP1 ^= 1 << ((r8_r(m_IR[1] >> 4)) & 7);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bclr r8h abs8   [7f006200/ff00ff0f skip=0 extra=0]
	void op_136() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_TMP1 &= ~(1 << ((r8_r(m_IR[1] >> 4)) & 7));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bst imm3 abs8   [7f006700/ff00ff8f skip=0 extra=0]
	void op_137() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	if(m_CCR & F_C)
		m_TMP1 |= 1 << ((m_IR[1] >> 4) & 7);
	else
		m_TMP1 &= ~(1 << ((m_IR[1] >> 4) & 7));
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bist imm3 abs8   [7f006780/ff00ff8f skip=0 extra=0]
	void op_138() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	if(!(m_CCR & F_C))
		m_TMP1 |= 1 << ((m_IR[1] >> 4) & 7);
	else
		m_TMP1 &= ~(1 << ((m_IR[1] >> 4) & 7));
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bset imm3 abs8   [7f007000/ff00ff8f skip=0 extra=0]
	void op_139() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 |= 1 << ((m_IR[1] >> 4) & 7);
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bnot imm3 abs8   [7f007100/ff00ff8f skip=0 extra=0]
	void op_140() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 ^= 1 << ((m_IR[1] >> 4) & 7);
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// bclr imm3 abs8   [7f007200/ff00ff8f skip=0 extra=0]
	void op_141() {
	m_TMP2 = 0xffffff00 | m_IR[0];
	m_TMP1 = read8(m_TMP2);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	m_TMP1 &= ~(1 << ((m_IR[1] >> 4) & 7));
	write8(m_TMP2, m_TMP1);
	prefetch_done();
	}
	// add.b imm8 r8u   [8000/f000 skip=0 extra=0]
	void op_142() {
	r8_w(m_IR[0] >> 8, do_add8(r8_r(m_IR[0] >> 8), m_IR[0]));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// addx.b imm8 r8u   [9000/f000 skip=0 extra=0]
	void op_143() {
	r8_w(m_IR[0] >> 8, do_addx8(r8_r(m_IR[0] >> 8), m_IR[0]));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// cmp.b imm8 r8u   [a000/f000 skip=0 extra=0]
	void op_144() {
	do_sub8(r8_r(m_IR[0] >> 8), m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// subx.b imm8 r8u   [b000/f000 skip=0 extra=0]
	void op_145() {
	r8_w(m_IR[0] >> 8, do_subx8(r8_r(m_IR[0] >> 8), m_IR[0]));
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// or.b imm8 r8u   [c000/f000 skip=0 extra=0]
	void op_146() {
	m_TMP1 = m_IR[0] | r8_r(m_IR[0] >> 8);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0] >> 8, m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// xor.b imm8 r8u   [d000/f000 skip=0 extra=0]
	void op_147() {
	m_TMP1 = m_IR[0] ^ r8_r(m_IR[0] >> 8);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0] >> 8, m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// and.b imm8 r8u   [e000/f000 skip=0 extra=0]
	void op_148() {
	m_TMP1 = m_IR[0] & r8_r(m_IR[0] >> 8);
	set_nzv8(m_TMP1);
	r8_w(m_IR[0] >> 8, m_TMP1);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}
	// mov.b imm8 r8u   [f000/f000 skip=0 extra=0]
	void op_149() {
	set_nzv8(m_IR[0]);
	r8_w(m_IR[0] >> 8, m_IR[0]);
	m_NPC = m_PC & 0xffffff;
	m_PIR = read16i(m_PC);
	m_PC += 2;
	prefetch_done();
	}

    typedef void (H8o::*OpFn)();
    struct OpInfo { uint16_t w[3], m[3]; uint8_t nw, skip, extra; OpFn fn; const char* name; };
    static const OpInfo* ops(int& n) {
        static const OpInfo T[] = {
            { {0x0000,0x0000,0x0000}, {0xffff,0x0000,0x0000}, 1, 0, 0, &H8o::op_0, "nop - -" },
            { {0x0180,0x0000,0x0000}, {0xffff,0x0000,0x0000}, 1, 0, 0, &H8o::op_1, "sleep - -" },
            { {0x0200,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_2, "stc ccr r8l" },
            { {0x0300,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_3, "ldc r8l ccr" },
            { {0x0400,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_4, "orc imm8 ccr" },
            { {0x0500,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_5, "xorc imm8 ccr" },
            { {0x0600,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_6, "andc imm8 ccr" },
            { {0x0700,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_7, "ldc imm8 ccr" },
            { {0x0800,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_8, "add.b r8h r8l" },
            { {0x0900,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_9, "add.w r16h r16l" },
            { {0x0a00,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_10, "inc.b one r8l" },
            { {0x0b00,0x0000,0x0000}, {0xfff8,0x0000,0x0000}, 1, 0, 0, &H8o::op_11, "adds.l one r16l" },
            { {0x0b80,0x0000,0x0000}, {0xfff8,0x0000,0x0000}, 1, 0, 0, &H8o::op_12, "adds.l two r16l" },
            { {0x0c00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_13, "mov.b r8h r8l" },
            { {0x0d00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_14, "mov.w r16h r16l" },
            { {0x0e00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_15, "addx.b r8h r8l" },
            { {0x0f00,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_16, "daa.b r8l -" },
            { {0x1000,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_17, "shll.b r8l -" },
            { {0x1080,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_18, "shal.b r8l -" },
            { {0x1100,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_19, "shlr.b r8l -" },
            { {0x1180,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_20, "shar.b r8l -" },
            { {0x1200,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_21, "rotxl.b r8l -" },
            { {0x1280,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_22, "rotl.b r8l -" },
            { {0x1300,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_23, "rotxr.b r8l -" },
            { {0x1380,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_24, "rotr.b r8l -" },
            { {0x1400,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_25, "or.b r8h r8l" },
            { {0x1500,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_26, "xor.b r8h r8l" },
            { {0x1600,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_27, "and.b r8h r8l" },
            { {0x1700,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_28, "not.b r8l -" },
            { {0x1780,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_29, "neg.b r8l -" },
            { {0x1800,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_30, "sub.b r8h r8l" },
            { {0x1900,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_31, "sub.w r16h r16l" },
            { {0x1a00,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_32, "dec.b one r8l" },
            { {0x1b00,0x0000,0x0000}, {0xfff8,0x0000,0x0000}, 1, 0, 0, &H8o::op_33, "subs.l one r16l" },
            { {0x1b80,0x0000,0x0000}, {0xfff8,0x0000,0x0000}, 1, 0, 0, &H8o::op_34, "subs.l two r16l" },
            { {0x1c00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_35, "cmp.b r8h r8l" },
            { {0x1d00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_36, "cmp.w r16h r16l" },
            { {0x1e00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_37, "subx.b r8h r8l" },
            { {0x1f00,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 0, &H8o::op_38, "das.b r8l -" },
            { {0x2000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_39, "mov.b abs8 r8u" },
            { {0x3000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_40, "mov.b r8u abs8" },
            { {0x4000,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_41, "bt rel8 -" },
            { {0x4100,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_42, "bf rel8 -" },
            { {0x4200,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_43, "bhi rel8 -" },
            { {0x4300,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_44, "bls rel8 -" },
            { {0x4400,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_45, "bcc rel8 -" },
            { {0x4500,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_46, "bcs rel8 -" },
            { {0x4600,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_47, "bne rel8 -" },
            { {0x4700,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_48, "beq rel8 -" },
            { {0x4800,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_49, "bvc rel8 -" },
            { {0x4900,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_50, "bvs rel8 -" },
            { {0x4a00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_51, "bpl rel8 -" },
            { {0x4b00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_52, "bmi rel8 -" },
            { {0x4c00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_53, "bge rel8 -" },
            { {0x4d00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_54, "blt rel8 -" },
            { {0x4e00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_55, "bgt rel8 -" },
            { {0x4f00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_56, "ble rel8 -" },
            { {0x5000,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_57, "mulxu.b r8h r16l" },
            { {0x5100,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_58, "divxu.b r8h r16l" },
            { {0x5470,0x0000,0x0000}, {0xffff,0x0000,0x0000}, 1, 0, 0, &H8o::op_59, "rts - -" },
            { {0x5500,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_60, "bsr rel8 -" },
            { {0x5670,0x0000,0x0000}, {0xffff,0x0000,0x0000}, 1, 0, 0, &H8o::op_61, "rte - -" },
            { {0x5900,0x0000,0x0000}, {0xff8f,0x0000,0x0000}, 1, 0, 0, &H8o::op_62, "jmp r16h -" },
            { {0x5a00,0x0000,0x0000}, {0xffff,0x0000,0x0000}, 1, 0, 1, &H8o::op_63, "jmp abs16e -" },
            { {0x5b00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_64, "jmp abs8i -" },
            { {0x5d00,0x0000,0x0000}, {0xff8f,0x0000,0x0000}, 1, 0, 0, &H8o::op_65, "jsr r16h -" },
            { {0x5e00,0x0000,0x0000}, {0xffff,0x0000,0x0000}, 1, 0, 1, &H8o::op_66, "jsr abs16e -" },
            { {0x5f00,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_67, "jsr abs8i -" },
            { {0x6000,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_68, "bset r8h r8l" },
            { {0x6100,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_69, "bnot r8h r8l" },
            { {0x6200,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_70, "bclr r8h r8l" },
            { {0x6300,0x0000,0x0000}, {0xff00,0x0000,0x0000}, 1, 0, 0, &H8o::op_71, "btst r8h r8l" },
            { {0x6700,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_72, "bst imm3 r8l" },
            { {0x6780,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_73, "bist imm3 r8l" },
            { {0x6800,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_74, "mov.b r16ih r8l" },
            { {0x6880,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_75, "mov.b r8l r16ih" },
            { {0x6900,0x0000,0x0000}, {0xff88,0x0000,0x0000}, 1, 0, 0, &H8o::op_76, "mov.w r16ih r16l" },
            { {0x6980,0x0000,0x0000}, {0xff88,0x0000,0x0000}, 1, 0, 0, &H8o::op_77, "mov.w r16l r16ih" },
            { {0x6a00,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 1, &H8o::op_78, "mov.b abs16 r8l" },
            { {0x6a40,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 1, &H8o::op_79, "movfpe abs16 r8l" },
            { {0x6a80,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 1, &H8o::op_80, "mov.b r8l abs16" },
            { {0x6ac0,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 1, &H8o::op_81, "movtpe r8l abs16" },
            { {0x6b00,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 1, &H8o::op_82, "mov.w abs16 r16l" },
            { {0x6b80,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 1, &H8o::op_83, "mov.w r16l abs16" },
            { {0x6c00,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_84, "mov.b r16ph r8l" },
            { {0x6c80,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_85, "mov.b r8l pr16h" },
            { {0x6d00,0x0000,0x0000}, {0xff88,0x0000,0x0000}, 1, 0, 0, &H8o::op_86, "mov.w r16ph r16l" },
            { {0x6d80,0x0000,0x0000}, {0xff88,0x0000,0x0000}, 1, 0, 0, &H8o::op_87, "mov.w r16l pr16h" },
            { {0x6e00,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 1, &H8o::op_88, "mov.b r16d16h r8l" },
            { {0x6e80,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 1, &H8o::op_89, "mov.b r8l r16d16h" },
            { {0x6f00,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 1, &H8o::op_90, "mov.w r16d16h r16l" },
            { {0x6f80,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 1, &H8o::op_91, "mov.w r16l r16d16h" },
            { {0x7000,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_92, "bset imm3 r8l" },
            { {0x7100,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_93, "bnot imm3 r8l" },
            { {0x7200,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_94, "bclr imm3 r8l" },
            { {0x7300,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_95, "btst imm3 r8l" },
            { {0x7400,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_96, "bor imm3 r8l" },
            { {0x7480,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_97, "bior imm3 r8l" },
            { {0x7500,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_98, "bxor imm3 r8l" },
            { {0x7580,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_99, "bixor imm3 r8l" },
            { {0x7600,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_100, "band imm3 r8l" },
            { {0x7680,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_101, "biand imm3 r8l" },
            { {0x7700,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_102, "bld imm3 r8l" },
            { {0x7780,0x0000,0x0000}, {0xff80,0x0000,0x0000}, 1, 0, 0, &H8o::op_103, "bild imm3 r8l" },
            { {0x7900,0x0000,0x0000}, {0xfff0,0x0000,0x0000}, 1, 0, 1, &H8o::op_104, "mov.w imm16 r16l" },
            { {0x7b5c,0x598f,0x0000}, {0xffff,0xffff,0x0000}, 2, 0, 0, &H8o::op_105, "eepmov.b - -" },
            { {0x7c00,0x6300,0x0000}, {0xff8f,0xff0f,0x0000}, 2, 0, 0, &H8o::op_106, "btst r8h r16ihh" },
            { {0x7c00,0x7300,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_107, "btst imm3 r16ihh" },
            { {0x7c00,0x7400,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_108, "bor imm3 r16ihh" },
            { {0x7c00,0x7480,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_109, "bior imm3 r16ihh" },
            { {0x7c00,0x7500,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_110, "bxor imm3 r16ihh" },
            { {0x7c00,0x7580,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_111, "bixor imm3 r16ihh" },
            { {0x7c00,0x7600,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_112, "band imm3 r16ihh" },
            { {0x7c00,0x7680,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_113, "biand imm3 r16ihh" },
            { {0x7c00,0x7700,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_114, "bld imm3 r16ihh" },
            { {0x7c00,0x7780,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_115, "bild imm3 r16ihh" },
            { {0x7d00,0x6000,0x0000}, {0xff8f,0xff0f,0x0000}, 2, 0, 0, &H8o::op_116, "bset r8h r16ihh" },
            { {0x7d00,0x6100,0x0000}, {0xff8f,0xff0f,0x0000}, 2, 0, 0, &H8o::op_117, "bnot r8h r16ihh" },
            { {0x7d00,0x6200,0x0000}, {0xff8f,0xff0f,0x0000}, 2, 0, 0, &H8o::op_118, "bclr r8h r16ihh" },
            { {0x7d00,0x6700,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_119, "bst imm3 r16ihh" },
            { {0x7d00,0x6780,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_120, "bist imm3 r16ihh" },
            { {0x7d00,0x7000,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_121, "bset imm3 r16ihh" },
            { {0x7d00,0x7100,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_122, "bnot imm3 r16ihh" },
            { {0x7d00,0x7200,0x0000}, {0xff8f,0xff8f,0x0000}, 2, 0, 0, &H8o::op_123, "bclr imm3 r16ihh" },
            { {0x7e00,0x6300,0x0000}, {0xff00,0xff0f,0x0000}, 2, 0, 0, &H8o::op_124, "btst r8h abs8" },
            { {0x7e00,0x7300,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_125, "btst imm3 abs8" },
            { {0x7e00,0x7400,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_126, "bor imm3 abs8" },
            { {0x7e00,0x7480,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_127, "bior imm3 abs8" },
            { {0x7e00,0x7500,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_128, "bxor imm3 abs8" },
            { {0x7e00,0x7580,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_129, "bixor imm3 abs8" },
            { {0x7e00,0x7600,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_130, "band imm3 abs8" },
            { {0x7e00,0x7680,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_131, "biand imm3 abs8" },
            { {0x7e00,0x7700,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_132, "bld imm3 abs8" },
            { {0x7e00,0x7780,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_133, "bild imm3 abs8" },
            { {0x7f00,0x6000,0x0000}, {0xff00,0xff0f,0x0000}, 2, 0, 0, &H8o::op_134, "bset r8h abs8" },
            { {0x7f00,0x6100,0x0000}, {0xff00,0xff0f,0x0000}, 2, 0, 0, &H8o::op_135, "bnot r8h abs8" },
            { {0x7f00,0x6200,0x0000}, {0xff00,0xff0f,0x0000}, 2, 0, 0, &H8o::op_136, "bclr r8h abs8" },
            { {0x7f00,0x6700,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_137, "bst imm3 abs8" },
            { {0x7f00,0x6780,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_138, "bist imm3 abs8" },
            { {0x7f00,0x7000,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_139, "bset imm3 abs8" },
            { {0x7f00,0x7100,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_140, "bnot imm3 abs8" },
            { {0x7f00,0x7200,0x0000}, {0xff00,0xff8f,0x0000}, 2, 0, 0, &H8o::op_141, "bclr imm3 abs8" },
            { {0x8000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_142, "add.b imm8 r8u" },
            { {0x9000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_143, "addx.b imm8 r8u" },
            { {0xa000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_144, "cmp.b imm8 r8u" },
            { {0xb000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_145, "subx.b imm8 r8u" },
            { {0xc000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_146, "or.b imm8 r8u" },
            { {0xd000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_147, "xor.b imm8 r8u" },
            { {0xe000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_148, "and.b imm8 r8u" },
            { {0xf000,0x0000,0x0000}, {0xf000,0x0000,0x0000}, 1, 0, 0, &H8o::op_149, "mov.b imm8 r8u" },
        };
        n = sizeof(T) / sizeof(T[0]); return T;
    }
};
