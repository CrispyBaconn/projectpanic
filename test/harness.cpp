// Harness di test: esegue le funzioni generate su stati iniziali letti da file e scrive i risultati.
#include "../runtime.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

uint8_t* g_ram;
static int g_status = 0;
struct StepLimit {}; struct Trap {};

uint32_t hw_read(uint32_t, int)            { g_status = 1; return 0; }
void     hw_write(uint32_t, int, uint32_t) { g_status = 1; }
uint32_t cp0_read(CPU*, int, int)          { g_status = 4; return 0; }
void     cp0_write(CPU*, int, int, uint32_t) { g_status = 4; }
void     cache_op(CPU*, int, uint32_t)     {}
void     guest_break(CPU*, uint32_t, uint32_t) { g_status = 3; throw Trap(); }
void     guest_unimpl(CPU*, uint32_t, uint32_t) { g_status = 5; throw Trap(); }
void     guest_eret(CPU*)                  {}
void     call_guest(CPU*, uint32_t)        { g_status = 6; throw Trap(); }
void     backedge_hook(CPU* c)             { if (++c->steps > 200000) throw StepLimit(); }

extern void (*g_tests[])(CPU*);
extern const int g_ntests;

constexpr uint32_t SCR = 0x00300000, SCR_LEN = 4096;
struct Case   { uint32_t idx, r[32], hi, lo, f[32], fcsr; uint8_t mem[SCR_LEN]; };
struct Result { uint32_t status, r[32], hi, lo, f[32], fcsr; uint8_t mem[SCR_LEN]; };

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    g_ram = (uint8_t*)calloc(RAM_SIZE, 1);
    FILE* in = fopen(argv[1], "rb"); FILE* out = fopen(argv[2], "wb");
    uint32_t n; fread(&n, 4, 1, in);
    Case cs; 
    for (uint32_t i = 0; i < n; i++) {
        if (fread(&cs, sizeof cs, 1, in) != 1) return 3;
        CPU c; memset(&c, 0, sizeof c);
        memcpy(c.r, cs.r, sizeof c.r); c.r[0] = 0; c.hi = cs.hi; c.lo = cs.lo;
        memcpy(c.f, cs.f, sizeof c.f); c.fcsr = cs.fcsr;
        memcpy(g_ram + SCR, cs.mem, SCR_LEN);
        g_status = 0;
        try { g_tests[cs.idx](&c); } catch (StepLimit&) { g_status = 2; } catch (Trap&) {}
        Result rs; memset(&rs, 0, sizeof rs);
        rs.status = g_status; memcpy(rs.r, c.r, sizeof rs.r); rs.hi = c.hi; rs.lo = c.lo;
        memcpy(rs.f, c.f, sizeof rs.f); rs.fcsr = c.fcsr; memcpy(rs.mem, g_ram + SCR, SCR_LEN);
        fwrite(&rs, sizeof rs, 1, out);
    }
    fclose(out); return 0;
}
