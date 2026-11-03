// Harness livello 2: funzioni reali del gioco. RAM inizializzata da ram_init.bin; log delle scritture.
#include "../runtime.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

uint8_t* g_ram;
static int g_status = 0;
static std::vector<uint32_t> g_wlog;
struct StepLimit {}; struct Trap {};

void     test_wlog(uint32_t a, int n)      { for (int i = 0; i < n; i++) g_wlog.push_back((a + i) & 0xffffff); }
uint32_t hw_read(uint32_t, int)            { g_status = 1; return 0; }
void     hw_write(uint32_t, int, uint32_t) { g_status = 1; }
uint32_t cp0_read(CPU*, int, int)          { g_status = 4; throw Trap(); }
void     cp0_write(CPU*, int, int, uint32_t) { g_status = 4; throw Trap(); }
void     cache_op(CPU*, int, uint32_t)     {}
void     guest_break(CPU*, uint32_t, uint32_t) { g_status = 3; throw Trap(); }
void     guest_unimpl(CPU*, uint32_t, uint32_t) { g_status = 5; throw Trap(); }
void     guest_eret(CPU*)                  {}
void     call_guest(CPU*, uint32_t)        { g_status = 6; throw Trap(); }
void     backedge_hook(CPU* c)             { if (++c->steps > 300000) throw StepLimit(); }

extern void (*g_tests[])(CPU*);
extern const int g_ntests;
struct Case { uint32_t idx, r[32], hi, lo, f[32], fcsr; };

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    g_ram = (uint8_t*)calloc(RAM_SIZE, 1);
    FILE* im = fopen(argv[1], "rb"); size_t isz = fread(g_ram, 1, RAM_SIZE, im); fclose(im);
    std::vector<uint8_t> init(g_ram, g_ram + RAM_SIZE); (void)isz;
    FILE* in = fopen(argv[2], "rb"); FILE* out = fopen(argv[3], "wb");
    uint32_t n; if (fread(&n, 4, 1, in) != 1) return 3;
    for (uint32_t i = 0; i < n; i++) {
        Case cs; if (fread(&cs, sizeof cs, 1, in) != 1) return 3;
        CPU c; memset(&c, 0, sizeof c);
        memcpy(c.r, cs.r, sizeof c.r); c.r[0] = 0; c.hi = cs.hi; c.lo = cs.lo;
        memcpy(c.f, cs.f, sizeof c.f); c.fcsr = cs.fcsr;
        g_status = 0; g_wlog.clear();
        try { g_tests[cs.idx](&c); } catch (StepLimit&) { g_status = 2; } catch (Trap&) {}
        std::sort(g_wlog.begin(), g_wlog.end()); g_wlog.erase(std::unique(g_wlog.begin(), g_wlog.end()), g_wlog.end());
        bool too_big = g_wlog.size() > 200000;            // es. azzeramento di una regione enorme: caso non confrontabile
        if (too_big && g_status == 0) g_status = 7;
        uint32_t head[1 + 32 + 2 + 32 + 1 + 1];
        int k = 0; head[k++] = g_status; for (int j = 0; j < 32; j++) head[k++] = c.r[j];
        head[k++] = c.hi; head[k++] = c.lo; for (int j = 0; j < 32; j++) head[k++] = c.f[j];
        head[k++] = c.fcsr; head[k++] = too_big ? 0u : (uint32_t)g_wlog.size();
        fwrite(head, sizeof head, 1, out);
        for (uint32_t a : g_wlog) { if (!too_big) { uint32_t rec[2] = { a, g_ram[a] }; fwrite(rec, 8, 1, out); } g_ram[a] = init[a]; }
    }
    fclose(out); return 0;
}
