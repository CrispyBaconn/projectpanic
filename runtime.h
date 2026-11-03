// Runtime per il codice generato da mips2cpp.py (Panic Park / Namco System 23, R4650).
// Modello: registri a 32 bit (il codice usa solo daddiu/daddu come move/add su valori
// sign-extended), memoria big endian, RAM 16 MB a 0x00000000 (alias KSEG0/KSEG1).
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

struct CPU {
    uint32_t r[32];
    uint32_t hi, lo;
    uint32_t f[32];          // bit pattern single precision
    uint32_t fcsr;           // bit 23 = fcc0
    uint64_t steps;          // archi all'indietro eseguiti (nelle build di test: limite di passi)
    uint64_t steps_lim;      // prossimo controllo del limitatore di velocita'
    volatile bool irq_pending;   // scritto dal tick (segnale o altro thread): con -O2 non va tenuto in un registro
};

extern uint8_t* g_ram;       // 16 MB, byte in ordine big endian (come la memoria MIPS)
constexpr uint32_t RAM_SIZE = 0x01000000;

// --- hook forniti dalla piattaforma (o dall'harness di test) ---
uint32_t hw_read (uint32_t addr, int size);
void     hw_write(uint32_t addr, int size, uint32_t value);
uint32_t cp0_read (CPU* c, int reg, int sel);
void     cp0_write(CPU* c, int reg, int sel, uint32_t v);
void     cache_op (CPU* c, int op, uint32_t addr);
[[noreturn]] void guest_break  (CPU* c, uint32_t pc, uint32_t code);
[[noreturn]] void guest_unimpl (CPU* c, uint32_t pc, uint32_t word);
void     guest_eret(CPU* c);
void     call_guest(CPU* c, uint32_t addr);       // chiamata indiretta per indirizzo
void     backedge_hook(CPU* c);
void     backedge_throttle(CPU* c);               // limita la CPU ricompilata alla velocita' del R4650 nei cicli di attesa

#ifdef RECOMP_TEST
  #define BACKEDGE(c) backedge_hook(c)
#else
  // Il gioco misura alcuni timeout con cicli vuoti (es. attesa della sub-CPU): compilati con -O2 girerebbero
  // decine di volte piu' veloci del R4650 e scadrebbero subito. Ogni arco all'indietro vale almeno ~3 cicli a 133 MHz.
  #define BACKEDGE(c) do { if (++(c)->steps >= (c)->steps_lim) backedge_throttle(c); if ((c)->irq_pending) backedge_hook(c); } while (0)
#endif

// la RAM del guest e' big endian: sugli host little endian (PC) le parole vanno girate, su quelli big endian (Wii U) no
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline uint32_t be32h(uint32_t v) { return v; }
#else
static inline uint32_t be32h(uint32_t v) { return __builtin_bswap32(v); }
#endif
static inline bool is_ram(uint32_t a) { return (a & 0x1fffffffu) < RAM_SIZE; }

#ifdef RECOMP_TEST
void test_wlog(uint32_t a, int n);
#define WLOG(a, n) test_wlog((a), (n))
#else
#define WLOG(a, n) ((void)0)
#endif

static inline uint32_t mrd8 (uint32_t a) { return is_ram(a) ? g_ram[a & 0xffffff] : hw_read(a, 1); }
static inline uint32_t mrd16(uint32_t a) {
    if (!is_ram(a)) return hw_read(a, 2);
    return ((uint32_t)g_ram[a & 0xffffff] << 8) | g_ram[(a + 1) & 0xffffff];
}
static inline uint32_t mrd32(uint32_t a) {
    if (!is_ram(a)) return hw_read(a, 4);
    uint32_t v; memcpy(&v, g_ram + (a & 0xffffff), 4); return be32h(v);
}
static inline void mwr8 (uint32_t a, uint32_t v) { if (is_ram(a)) { WLOG(a, 1); g_ram[a & 0xffffff] = (uint8_t)v; } else hw_write(a, 1, v & 0xff); }
static inline void mwr16(uint32_t a, uint32_t v) {
    if (!is_ram(a)) { hw_write(a, 2, v & 0xffff); return; }
    WLOG(a, 2);
    g_ram[a & 0xffffff] = (uint8_t)(v >> 8); g_ram[(a + 1) & 0xffffff] = (uint8_t)v;
}
static inline void mwr32(uint32_t a, uint32_t v) {
    if (!is_ram(a)) { hw_write(a, 4, v); return; }
    WLOG(a, 4);
    v = be32h(v); memcpy(g_ram + (a & 0xffffff), &v, 4);
}

// --- accessi non allineati (big endian) ---
static inline uint32_t lwl_(uint32_t old, uint32_t a) {
    uint32_t w = mrd32(a & ~3u); unsigned s = (a & 3u) * 8;
    return (w << s) | (old & ((1u << s) - 1u));
}
static inline uint32_t lwr_(uint32_t old, uint32_t a) {
    uint32_t w = mrd32(a & ~3u); unsigned s = (3u - (a & 3u)) * 8;
    return (w >> s) | (old & ~(0xffffffffu >> s));
}
static inline void swl_(uint32_t a, uint32_t v) {
    uint32_t m = mrd32(a & ~3u); unsigned s = (a & 3u) * 8;
    mwr32(a & ~3u, (m & ~(0xffffffffu >> s)) | (v >> s));
}
static inline void swr_(uint32_t a, uint32_t v) {
    uint32_t m = mrd32(a & ~3u); unsigned s = (3u - (a & 3u)) * 8;
    mwr32(a & ~3u, (m & ~(0xffffffffu << s)) | (v << s));
}

// --- HI/LO ---
static inline void do_mult (CPU* c, uint32_t a, uint32_t b) { int64_t p = (int64_t)(int32_t)a * (int32_t)b; c->lo = (uint32_t)p; c->hi = (uint32_t)((uint64_t)p >> 32); }
static inline void do_multu(CPU* c, uint32_t a, uint32_t b) { uint64_t p = (uint64_t)a * b; c->lo = (uint32_t)p; c->hi = (uint32_t)(p >> 32); }
static inline void do_madd (CPU* c, uint32_t a, uint32_t b) {
    uint64_t acc = ((uint64_t)c->hi << 32) | c->lo;
    acc += (uint64_t)((int64_t)(int32_t)a * (int32_t)b);
    c->lo = (uint32_t)acc; c->hi = (uint32_t)(acc >> 32);
}
static inline void do_div (CPU* c, uint32_t a, uint32_t b) {
    int32_t x = (int32_t)a, y = (int32_t)b;
    if (y == 0) { c->lo = (uint32_t)x; c->hi = 0; return; }       // indefinito su HW; il gioco protegge con break
    if (x == INT32_MIN && y == -1) { c->lo = (uint32_t)x; c->hi = 0; return; }
    c->lo = (uint32_t)(x / y); c->hi = (uint32_t)(x % y);
}
static inline void do_divu(CPU* c, uint32_t a, uint32_t b) {
    if (b == 0) { c->lo = a; c->hi = 0; return; }
    c->lo = a / b; c->hi = a % b;
}

// --- FPU (solo precisione singola) ---
static inline float    fl  (uint32_t b) { float x; memcpy(&x, &b, 4); return x; }
static inline uint32_t fbits(float x)   { uint32_t b; memcpy(&b, &x, 4); return b; }
static inline void set_fcc(CPU* c, bool v) { c->fcsr = (c->fcsr & ~(1u << 23)) | ((uint32_t)v << 23); }
static inline uint32_t trunc_w_s(float x) {
    if (x != x) return 0x7fffffffu;
    if (x >= 2147483648.0f || x < -2147483648.0f) return 0x7fffffffu;
    return (uint32_t)(int32_t)x;
}
static inline uint32_t cfc1_(CPU* c, int n) { return n == 31 ? c->fcsr : (n == 0 ? 0x00000000u : 0u); }
static inline void     ctc1_(CPU* c, int n, uint32_t v) { if (n == 31) c->fcsr = v; }
