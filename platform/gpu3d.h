// Pipeline 3D di Namco System 23 (C435 / C417 / C412 / C404): decodifica dei comandi, trasformazione, proiezione e
// rasterizzazione software con texture. Porta in C++ puro (senza MAME) della logica di namcos23.cpp (BSD-3-Clause,
// copyright Olivier Galibert, R. Belmont, Angelo Salese, hap, Ariane Fugmann e altri) per l'uso con la ricompilazione statica.
#pragma once
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <atomic>
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace g3d {

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;

static inline s32 s24(u32 v) { return (v & 0x800000) ? (s32)(v | 0xff000000u) : (s32)(v & 0xffffff); }
static inline s32 s10(u32 v) { return (s32)(v << 22) >> 22; }
static float f24(u32 v) {                                 // 8 bit di esponente + 16 di mantissa (come f24_to_f32 di MAME)
    if (!(v & 0x0000ffff)) return 0;
    u32 r = v & 0x8000 ? 0x80000000u : 0;
    u16 m = (u16)(r ? -(s32)v : v);
    u8 e = (u8)((v >> 16) + 0x60);
    while (!(m & 0x8000)) { m <<= 1; e--; }
    r = r | ((u32)e << 23) | ((u32)(m & 0x7fff) << 8);
    float f; memcpy(&f, &r, 4); return f;
}

enum { MODEL = 0, DIRECT = 1, IMMEDIATE = 2 };

struct Entry {
    int type = 0; int seg = 0;
    u16 absolute_priority = 0, tx = 0, ty = 0, model_blend_factor = 0, light_power = 0, light_ambient = 0;
    bool mirror_x = false;
    u8 pfr = 0xff, pfg = 0xff, pfb = 0xff, alpha_color = 0, alpha_pen = 0, alpha = 0, sfr = 0, sfg = 0, sfb = 0, sfactor = 0, fade_flags = 0;
    s16 vp_sx = 320, vp_sy = 240, vp_ox = 0, vp_oy = 0; float fov = 0;
    bool slant = false; float cpl[2][3] = {{0, 0, 0}, {0, 0, 0}};   // piani di clip sinistro/destro inclinati (finestra a trapezio)
    float clip[24] = {0};                                             // dati di clip grezzi (solo per il dump F10)
    struct { u16 model, model2; s16 m[9]; s32 v[3]; float scaling; bool transpose; s32 lv[3]; } model;
    u16 d[28];
    struct { u32 type, h, pal, zbias, i[4], u[4], v[4], x[4], y[4], z[4]; } imm;
    Entry() { memset(&model, 0, sizeof model); memset(d, 0, sizeof d); memset(&imm, 0, sizeof imm); }
};

struct PV { float x, y, p[4]; };
struct RD {
    float intensity_base = 0; u32 pens = 0; u32 tbase = 0; u8 cmode = 0; u32 h = 0, type = 0;
    bool stencil = false, shade = true, pfade = false, blend = false, alpha_enabled = false;
    s32 pcr = 0, pcg = 0, pcb = 0, fadefactor = 0xff, fadefactor_inv = 1, fcr = 0, fcg = 0, fcb = 0, alpha = 0xff, alpha_inv = 1;
    u8 alpha_pen = 0; s16 vp_sx = 320, vp_sy = 240, vp_ox = 0, vp_oy = 0;
    bool slant = false; float cpl[2][3] = {{0, 0, 0}, {0, 0, 0}}, cfov = 0;
};
struct Poly { int nv = 0; int zkey = 0; int index = 0; int seg = 0; RD rd; PV pv[16]; };
// ---------------------------------------------------------------- dati di sola lettura (ROM) e memorie video
struct Rom {
    const u32* ptrom = nullptr; size_t ptrom_n = 0;       // point ROM: parole a 32 bit (asset pt.bin)
    const u8* texrom = nullptr; size_t tex_n = 0;         // textile (CG): 32 MB (asset tex.bin)
    const u32* tm_decoded = nullptr; const u8* tm_attr = nullptr; size_t tm_n = 0;   // indice di tile -> offset nella texrom (tm.bin)
    u32 tileid_mask = 0, tile_mask = 0;
    bool ok = false;
};
static Rom R;

static bool read_file(const std::string& p, std::vector<u8>& d) { return assets::read_all(p, d); }

// carica le ROM grafiche (dalla cache degli asset, o convertendole la prima volta).
// Restituisce false (e il 3D resta disattivato) se manca qualcosa.
static bool load_roms(const std::string& dir) {
    size_t n = 0;
    const u8* pt = assets::get("pt.bin", n, [&](std::vector<u8>& out) {
        std::vector<u8> a, b; out.assign(0x400000 * 4, 0xff); u32* w = (u32*)out.data();
        const char* f[2][2] = { {"pnp1pt0h.7a", "pnp1pt0l.7c"}, {"pnp1pt1h.5a", "pnp1pt1l.5c"} };
        for (int k = 0; k < 2; k++) {
            if (!read_file(dir + "/" + f[k][0], a) || !read_file(dir + "/" + f[k][1], b)) { printf("3D: ROM %s/%s non trovate\n", f[k][0], f[k][1]); return false; }
            size_t base = k ? 0x200000 : 0, m = std::min(std::min(a.size(), b.size()) / 2, (size_t)0x200000);
            for (size_t i = 0; i < m; i++) {                                       // ROM_LOAD32_WORD_SWAP
                u32 hi = (a[2 * i + 1] << 8) | a[2 * i], lo = (b[2 * i + 1] << 8) | b[2 * i];
                w[base + i] = (hi << 16) | lo;
            }
        }
        return true; });
    if (!pt) return false;
    R.ptrom = (const u32*)pt; R.ptrom_n = n / 4;
    R.texrom = assets::get("tex.bin", R.tex_n, [&](std::vector<u8>& out) {
        std::vector<u8> a; out.assign(0x2000000, 0);
        const char* cg[4] = { "pnp1cgll.4m", "pnp1cglm.5k", "pnp1cgum.4j", "pnp1cguu.5f" };
        for (int k = 0; k < 4; k++) {
            if (!read_file(dir + "/" + cg[k], a)) { printf("3D: ROM %s non trovata\n", cg[k]); return false; }
            memcpy(&out[(size_t)k * 0x800000], a.data(), std::min<size_t>(a.size(), 0x800000));
        }
        return true; });
    if (!R.texrom) return false;
    R.tile_mask = (u32)(R.tex_n / 256 - 1);
    const u8* tm = assets::get("tm.bin", n, [&](std::vector<u8>& out) {         // intestazione: cnt, tileid_mask; poi cnt u32 e cnt u8
        std::vector<u8> b, c;
        if (!read_file(dir + "/pnp1ccrl.7f", b) || !read_file(dir + "/pnp1ccrh.7e", c)) { printf("3D: ROM tilemap non trovate\n"); return false; }
        u32 tidm = (u32)((b.size() / 2 - 1) & ~0xffu); size_t cnt = (size_t)(tidm | 0xff) + 1;
        out.assign(8 + cnt * 5, 0); u32* h = (u32*)out.data(); h[0] = (u32)cnt; h[1] = tidm;
        u32* dec = (u32*)(out.data() + 8); u8* at = out.data() + 8 + cnt * 4;
        for (size_t t = 0; t < cnt; t++) {
            u8 attr = c[(t >> 1) % c.size()]; attr = (t & 1) ? (attr & 15) : (attr >> 4);
            u16 l = (u16)(b[(2 * t) % b.size()] | (b[(2 * t + 1) % b.size()] << 8));
            dec[t] = (((u32)l | ((u32)(attr & 1) << 16)) & R.tile_mask) << 8;
            at[t] = attr >> 1;
        }
        return true; });
    if (!tm) return false;
    R.tm_n = ((const u32*)tm)[0]; R.tileid_mask = ((const u32*)tm)[1];
    R.tm_decoded = (const u32*)(tm + 8); R.tm_attr = tm + 8 + R.tm_n * 4;
    R.ok = true; return true;
}

// ---------------------------------------------------------------- C412 / C421 / C417 (registri e memorie accessibili dalla CPU)
struct Chips {
    std::vector<u16> c412_a, c412_b; u16 c412_sram[0x20000]; u16 c412_pcz[0x200]; u32 c412_adr = 0; u16 c412_status = 0;
    std::vector<u16> c421_a, c421_b; u16 c421_sram[0x8000]; u32 c421_adr = 0;
    u16 c417_ram[0x10000]; u16 c417_adr = 0; u32 c417_prom_adr = 0; bool c417_test = false;
    Chips() { c412_a.assign(0x100000, 0); c412_b.assign(0x100000, 0); c421_a.assign(0x40000, 0); c421_b.assign(0x40000, 0);
              memset(c412_sram, 0, sizeof c412_sram); memset(c412_pcz, 0, sizeof c412_pcz); memset(c421_sram, 0, sizeof c421_sram); memset(c417_ram, 0, sizeof c417_ram); }
};
static Chips C;

// ---------------------------------------------------------------- stato del processore C435
struct State {
    int seg = 0;                      // segmento della lista: si chiude a ogni comando di fine lista
    s16 matrices[256][9]; s32 vectors[256][3]; s32 light_vector[3]; s32 spv[3]; s16 spm[9];
    u16 absolute_priority = 0, tx = 0, ty = 0, scaling = 0x4000, model_blend_factor = 0, light_power = 0, light_ambient = 0;
    float clip_data[24]; u8 clip_line = 0; s16 vp_sx = 320, vp_sy = 240, vp_ox = 0, vp_oy = 0; bool slant = false;
    u16 buffer[256]; int buffer_pos = 0;
    u16 direct_buf[28]; int direct_pos = 0; bool direct_nonempty = false, direct_open = false;
    State() { memset(matrices, 0, sizeof matrices); memset(vectors, 0, sizeof vectors); memset(light_vector, 0, sizeof light_vector);
              memset(spv, 0, sizeof spv); memset(spm, 0, sizeof spm); memset(clip_data, 0, sizeof clip_data); memset(buffer, 0, sizeof buffer); memset(direct_buf, 0, sizeof direct_buf); }
};
static State S;
static std::vector<Entry> cur_list, ready_list, disp_list;      // lista in costruzione / completata al vblank precedente / in visualizzazione
#if defined(__WIIU__) || defined(__SWITCH__) || defined(__3DS__)
// console: spinlock al posto di std::mutex (la sezione critica e' solo la copia della lista)
struct SpinMutex { std::atomic_flag f = ATOMIC_FLAG_INIT; void lock() { while (f.test_and_set(std::memory_order_acquire)) {} } void unlock() { f.clear(std::memory_order_release); } };
static SpinMutex mtx;
#else
static std::mutex mtx;
#endif
struct MtxGuard { decltype(mtx)& m; explicit MtxGuard(decltype(mtx)& x) : m(x) { m.lock(); } ~MtxGuard() { m.unlock(); } };
static bool disp_new = false;
static unsigned stat_models = 0, stat_direct = 0, stat_imm = 0, stat_polys = 0;
static bool tl = getenv("S23_TL") != nullptr;
static const bool imlog = getenv("S23_IMLOG") != nullptr;
static unsigned n_eol = 0, n_vbl = 0, ev1 = 0, ev0 = 0, evr = 0;
static void (*irq_cb)(bool) = nullptr;                          // interrupt del C435 (IRQ2)

// valori del C404 (mixer) letti dalla memoria hardware della piattaforma: puntatore ai 0x800 byte di registri
static const u8* c404 = nullptr;
static inline u16 c404r(int idx) { return c404 ? (u16)((c404[2 * idx] << 8) | c404[2 * idx + 1]) : 0; }

static void snap(Entry& e) {                                   // stato del C404 al momento del comando (come in MAME)
    e.pfr = (u8)c404r(0x00); e.pfg = (u8)c404r(0x01); e.pfb = (u8)c404r(0x02);
    e.alpha_color = (u8)c404r(0x0f); e.alpha_pen = (u8)c404r(0x10); e.alpha = (u8)c404r(0x11);
    e.sfr = (u8)c404r(0x16); e.sfg = (u8)c404r(0x17); e.sfb = (u8)c404r(0x18); e.sfactor = (u8)c404r(0x19); e.fade_flags = (u8)c404r(0x1a);
}
static void common(Entry& e, bool viewport) {
    snap(e); e.seg = S.seg; e.absolute_priority = S.absolute_priority; e.light_power = S.light_power; e.light_ambient = S.light_ambient;
    if (viewport) { e.vp_sx = S.vp_sx; e.vp_sy = S.vp_sy; e.vp_ox = S.vp_ox; e.vp_oy = S.vp_oy; e.fov = S.clip_data[23];
        memcpy(e.clip, S.clip_data, sizeof e.clip); e.slant = S.slant; if (S.slant) for (int k = 0; k < 2; k++) for (int j = 0; j < 3; j++) e.cpl[k][j] = S.clip_data[4 * k + j]; }
}
static unsigned n_fb = 0;
static int seg_pend_k = -1; static bool seg_pend_used = false;    // finestra in attesa (vedi seg_win_commit)
static void push(const Entry& e) { if (seg_pend_k == e.seg && e.type == MODEL && e.model.model != 0x06ef) seg_pend_used = true; if (cur_list.size() < 6000) cur_list.push_back(e); }

static s32* getv(u16 id) {
    if (id == 0x8000) return S.light_vector;
    if (id >= 0x100) { memset(S.spv, 0, sizeof S.spv); return S.spv; }
    return S.vectors[id];
}
static s16* getm(u16 id) {
    if (id >= 0x100) { memset(S.spm, 0, sizeof S.spm); return S.spm; }
    return S.matrices[id];
}
static void transpose(s16* m1) {
    s16 m2[9]; memcpy(m2, m1, sizeof m2);
    m1[0] = m2[0]; m1[1] = m2[3]; m1[2] = m2[6]; m1[3] = m2[1]; m1[4] = m2[4]; m1[5] = m2[7]; m1[6] = m2[2]; m1[7] = m2[5]; m1[8] = m2[8];
}

static void matrix_matrix_mul() {
    const u16* b = S.buffer; if ((b[0] & 0xf) != 4) return;
    bool tr = (b[0] >> 10) & 1, identity = (b[0] >> 8) & 1;
    s16* t = getm(b[1]); s16 m1[9], m2[9];
    memcpy(m2, getm(b[2]), sizeof m2); if (tr) transpose(m2);
    if (identity) { memset(m1, 0, sizeof m1); m1[0] = m1[4] = m1[8] = 0x4000; } else memcpy(m1, getm(b[4]), sizeof m1);
    t[0] = (s16)((m1[0] * m2[0] + m1[1] * m2[1] + m1[2] * m2[2]) >> 14); t[1] = (s16)((m1[0] * m2[3] + m1[1] * m2[4] + m1[2] * m2[5]) >> 14); t[2] = (s16)((m1[0] * m2[6] + m1[1] * m2[7] + m1[2] * m2[8]) >> 14);
    t[3] = (s16)((m1[3] * m2[0] + m1[4] * m2[1] + m1[5] * m2[2]) >> 14); t[4] = (s16)((m1[3] * m2[3] + m1[4] * m2[4] + m1[5] * m2[5]) >> 14); t[5] = (s16)((m1[3] * m2[6] + m1[4] * m2[7] + m1[5] * m2[8]) >> 14);
    t[6] = (s16)((m1[6] * m2[0] + m1[7] * m2[1] + m1[8] * m2[2]) >> 14); t[7] = (s16)((m1[6] * m2[3] + m1[7] * m2[4] + m1[8] * m2[5]) >> 14); t[8] = (s16)((m1[6] * m2[6] + m1[7] * m2[7] + m1[8] * m2[8]) >> 14);
}
static void mat_vec(s32* t, s16* m0, const s32* v, const s32* vt, bool sub, bool tr) {
    s16 m[9]; memcpy(m, m0, sizeof m); if (tr) transpose(m);
    s32 r0 = (s32)((m[0] * (s64)v[0] + m[1] * (s64)v[1] + m[2] * (s64)v[2]) >> 14);
    s32 r1 = (s32)((m[3] * (s64)v[0] + m[4] * (s64)v[1] + m[5] * (s64)v[2]) >> 14);
    s32 r2 = (s32)((m[6] * (s64)v[0] + m[7] * (s64)v[1] + m[8] * (s64)v[2]) >> 14);
    if (vt) { if (sub) { r0 -= vt[0]; r1 -= vt[1]; r2 -= vt[2]; } else { r0 += vt[0]; r1 += vt[1]; r2 += vt[2]; } }
    t[0] = r0; t[1] = r1; t[2] = r2;
}
static void matrix_vector_mul() {
    const u16* b = S.buffer; if ((b[0] & 0xf) != 4) return;
    s32* t = getv(b[1]); s32 v[3], vt[3]; memcpy(v, getv(b[4]), sizeof v);
    s16 m[9]; memcpy(m, getm(b[2]), sizeof m);
    if (b[3] != 0xffff) { memcpy(vt, getv(b[3]), sizeof vt); mat_vec(t, m, v, vt, (b[0] >> 12) & 1, (b[0] >> 10) & 1); }
    else mat_vec(t, m, v, nullptr, false, (b[0] >> 10) & 1);
}
static void matrix_matrix_immed_mul() {
    const u16* b = S.buffer; if ((b[0] & 0xf) != 12) return;
    bool tr = (b[0] >> 10) & 1, identity = (b[0] >> 8) & 1;
    s16* t = getm(b[1]); s16 m1[9], m2[9];
    memcpy(m1, &b[4], sizeof m1);
    if (identity) { memset(m2, 0, sizeof m2); m2[0] = m2[4] = m2[8] = 0x4000; } else memcpy(m2, getm(b[2]), sizeof m2);
    if (tr) transpose(m2);
    t[0] = (s16)((m1[0] * m2[0] + m1[1] * m2[3] + m1[2] * m2[6]) >> 14); t[1] = (s16)((m1[0] * m2[1] + m1[1] * m2[4] + m1[2] * m2[7]) >> 14); t[2] = (s16)((m1[0] * m2[2] + m1[1] * m2[5] + m1[2] * m2[8]) >> 14);
    t[3] = (s16)((m1[3] * m2[0] + m1[4] * m2[3] + m1[5] * m2[6]) >> 14); t[4] = (s16)((m1[3] * m2[1] + m1[4] * m2[4] + m1[5] * m2[7]) >> 14); t[5] = (s16)((m1[3] * m2[2] + m1[4] * m2[5] + m1[5] * m2[8]) >> 14);
    t[6] = (s16)((m1[6] * m2[0] + m1[7] * m2[3] + m1[8] * m2[6]) >> 14); t[7] = (s16)((m1[6] * m2[1] + m1[7] * m2[4] + m1[8] * m2[7]) >> 14); t[8] = (s16)((m1[6] * m2[2] + m1[7] * m2[5] + m1[8] * m2[8]) >> 14);
}
static void matrix_vector_immed_mul() {
    const u16* b = S.buffer; if ((b[0] & 0xf) != 9) return;
    s32* t = getv(b[1]); s16 m[9]; memcpy(m, getm(b[2]), sizeof m);
    s32 v[3] = { (s32)(((u32)b[4] << 16) | b[5]), (s32)(((u32)b[6] << 16) | b[7]), (s32)(((u32)b[8] << 16) | b[9]) };
    if (b[3] != 0xffff) { s32 vt[3]; memcpy(vt, getv(b[3]), sizeof vt); mat_vec(t, m, v, vt, (b[0] >> 12) & 1, (b[0] >> 10) & 1); }
    else mat_vec(t, m, v, nullptr, false, (b[0] >> 10) & 1);
}
static void matrix_set() {
    const u16* b = S.buffer; if ((b[0] & 0xf) != 10) return;
    s16* t = getm(b[1]); for (int i = 0; i < 9; i++) t[i] = (s16)b[i + 2];
}
static void vector_set() {
    const u16* b = S.buffer; if ((b[0] & 0xf) != 7) return;
    s32* t = getv(b[1]); for (int i = 0; i < 3; i++) t[i] = s24(((u32)b[2 * i + 2] << 16) | b[2 * i + 3]);
}

static int state_entry_size(u16 type) {
    static const int T[0x100] = {
        43, 1, 53, -1, -1, -1, -1, -1, -1, 19, 47, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 41, -1, -1, -1, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 33, -1, -1, -1, -1, -1, 13, -1, 17, -1, -1, -1, -1, -1, -1, -1 };
    return T[type & 0xff];
}

// Finestra (clip + viewport) per segmento della lista: il gioco la manda una volta sola per i segmenti che non
// cambiano (es. lo schermino del monitor in NEEDED/RULE di alcuni livelli), mentre il segmento 0 la rimanda a ogni
// fotogramma. Sul cabinato evidentemente resta associata al segmento; con un unico stato globale lo schermino
// prendeva la finestra a tutto schermo del segmento 0 e la demo/le icone uscivano fuori.
struct SegWin { bool has = false; float clip[24]; s16 sx, sy, ox, oy; bool slant; };
static SegWin seg_win[32]; static int seg_win_prev_max = 0;      // numero di segmenti del fotogramma precedente
static const bool seg_win_off = getenv("S23_NOSEGWIN") != nullptr;
// la finestra inviata in un segmento viene ricordata solo se nel segmento segue almeno un modello (il gioco a fine
// fotogramma imposta una finestra a tutto schermo in un segmento vuoto: non va ricordata)
static SegWin seg_pend;
static SegWin win_snap, win_snap_pend; static bool win_snap_ok = false; static int win_snap_k = -1; static bool win_snap_used = false;   // stato prima di una finestra (vedi 0x0046)
static void seg_win_commit() { if (seg_pend_k >= 0 && seg_pend_used && seg_pend_k < 32) seg_win[seg_pend_k] = seg_pend; seg_pend_k = -1; seg_pend_used = false; }
static void seg_win_save2();
// finestra a tutto schermo del fotogramma (la prima mandata nel segmento 0): vedi full_window_model
static SegWin g_full;
static void seg_win_save() { if (S.seg == 0 && !S.slant) { g_full.has = true; memcpy(g_full.clip, S.clip_data, sizeof g_full.clip); g_full.sx = S.vp_sx; g_full.sy = S.vp_sy; g_full.ox = S.vp_ox; g_full.oy = S.vp_oy; g_full.slant = false; }
    seg_win_save2(); }
static void seg_win_save2() { if (seg_win_off || S.seg < 0 || S.seg >= 32) return; SegWin& w = seg_pend; seg_pend_k = S.seg; seg_pend_used = false; w.has = true; memcpy(w.clip, S.clip_data, sizeof w.clip); w.sx = S.vp_sx; w.sy = S.vp_sy; w.ox = S.vp_ox; w.oy = S.vp_oy; w.slant = S.slant; }
// Una finestra ricordata vale finche' il suo segmento esiste in ogni fotogramma: se un fotogramma ha meno segmenti
// (cambio di schermata) quelle oltre vengono dimenticate, cosi' non torna una finestra vecchia di un'altra schermata.
// All'inizio del fotogramma (segmento 0) resta lo stato corrente, come prima.
// Finestra dello schermino dei tutorial (RULE/NEEDED/LIFE) a telecamera ferma. Il gioco la ricalcola e la manda
// solo nei fotogrammi in cui la telecamera si sposta verso il monitor, e (con i tempi di questa versione, ma anche in
// MAME) spesso solo al primo fotogramma della panoramica: la finestra ricordata restava quella di meta' percorso e il
// contenuto finiva fuori dallo schermino (o non si vedeva). Quando si riusa una finestra inclinata (trapezio) ricordata
// si usa questa, che e' quella che il gioco manda a fine panoramica (identica in tutte le prove).
static void state_set(u16 type, const u16* P);
static const bool canon_off = getenv("S23_NOCANON") != nullptr;
static const SegWin& win_canon() {
    static SegWin w; static bool done = false; if (done) return w; done = true;
    static const u16 l1[17] = { 0x0000, 0x0027, 0x6800, 0x0025, 0xb000, 0x0027, 0xb6e6, 0x0000, 0x0000, 0x0027, 0x9800, 0x0025, 0xae00, 0x0027, 0xb6b5, 0x0000, 0x0000 };
    static const u16 l2[17] = { 0x0000, 0x0000, 0x0000, 0x0028, 0x6b40, 0x0027, 0xaf2c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0028, 0xa900, 0x0027, 0xbe6f, 0x0000, 0x0000 };
    static const u16 l3[17] = { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x001f, 0x8000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0028, 0x4500 };
    static const u16 vp[13] = { 0x0000, 0x0084, 0x5000, 0x0027, 0x0000, 0x0018, 0x4020, 0x00fc, 0xa000, 0x007f, 0xf800, 0x007f, 0xf800 };
    // si fanno passare i comandi dal decodificatore normale, salvando e rimettendo tutto lo stato che toccano
    float cd[24]; memcpy(cd, S.clip_data, sizeof cd); const u8 cl = S.clip_line; const s16 sx = S.vp_sx, sy = S.vp_sy, ox = S.vp_ox, oy = S.vp_oy; const bool sl = S.slant;
    const SegWin pend = seg_pend, snap = win_snap, snap_p = win_snap_pend; const int pk = seg_pend_k, sk = win_snap_k; const bool pu = seg_pend_used, so = win_snap_ok, su = win_snap_used;
    S.clip_line = 0; state_set(0x00c8, l1); state_set(0x00c8, l2); state_set(0x00c8, l3); state_set(0x0046, vp);
    w.has = true; memcpy(w.clip, S.clip_data, sizeof w.clip); w.sx = S.vp_sx; w.sy = S.vp_sy; w.ox = S.vp_ox; w.oy = S.vp_oy; w.slant = S.slant;
    memcpy(S.clip_data, cd, sizeof cd); S.clip_line = cl; S.vp_sx = sx; S.vp_sy = sy; S.vp_ox = ox; S.vp_oy = oy; S.slant = sl;
    seg_pend = pend; win_snap = snap; win_snap_pend = snap_p; seg_pend_k = pk; win_snap_k = sk; seg_pend_used = pu; win_snap_ok = so; win_snap_used = su;
    return w;
}
static void seg_win_load() { if (seg_win_off ||  S.seg < 0 || S.seg >= 32 || !seg_win[S.seg].has) return; const SegWin& w = seg_win[S.seg].slant && !canon_off ? win_canon() : seg_win[S.seg]; memcpy(S.clip_data, w.clip, sizeof w.clip); S.vp_sx = w.sx; S.vp_sy = w.sy; S.vp_ox = w.ox; S.vp_oy = w.oy; S.slant = w.slant; }
static void state_set(u16 type, const u16* P) {
    { static const char* sle = getenv("S23_STLOG"); static const unsigned sla = sle ? atoi(sle) : 0, slb = sle && strchr(sle, ',') ? atoi(strchr(sle, ',') + 1) : ~0u; const bool sl = sle && n_vbl >= sla && n_vbl <= slb; if (sl && type != 0x0000 && type != 0x0002) { printf("ST vbl=%u seg=%d type=%04x:", n_vbl, S.seg, type); int n = state_entry_size(type); for (int i = 0; i < n && i < 20; i++) printf(" %04x", P[i]); printf("\n"); } }
    switch (type) {
    case 0x0000: {
        Entry e; e.type = IMMEDIATE; common(e, true); e.model_blend_factor = 0;
        auto L = [&](int i) { return ((u32)P[i] << 16) | P[i + 1]; };
        e.imm.type = P[0]; e.imm.h = L(1); e.imm.pal = L(3); e.imm.zbias = L(5);
        if (S.buffer[0] == 0x4f38) {
            for (int k = 0; k < 4; k++) e.imm.i[k] = L(7 + 2 * k);
            for (int k = 0; k < 4; k++) { e.imm.u[k] = L(15 + 4 * k); e.imm.v[k] = L(17 + 4 * k); }
            for (int k = 0; k < 4; k++) { e.imm.x[k] = L(31 + 6 * k); e.imm.y[k] = L(33 + 6 * k); e.imm.z[k] = L(35 + 6 * k); }
        } else {
            for (int k = 0; k < 3; k++) e.imm.i[k] = L(7 + 2 * k);
            for (int k = 0; k < 3; k++) { e.imm.u[k] = L(13 + 4 * k); e.imm.v[k] = L(15 + 4 * k); }
            for (int k = 0; k < 3; k++) { e.imm.x[k] = L(25 + 6 * k); e.imm.y[k] = L(27 + 6 * k); e.imm.z[k] = L(29 + 6 * k); }
        }
        if (imlog) printf("IMQ fb=%u t=%04x src=%04x pal=%08x x=%08x y=%08x z=%08x\n", n_fb, type, S.buffer[0], e.imm.pal, e.imm.x[0], e.imm.y[0], e.imm.z[0]); if (imlog) { for (int k = 0; k < 4; k++) printf(" (%d,%d,%d)", s24(e.imm.x[k]), s24(e.imm.y[k]), s24(e.imm.z[k])); printf("\n"); } push(e); stat_imm++; break; }
    case 0x0002: {
        Entry e; e.type = IMMEDIATE; common(e, true);
        auto L = [&](int i) { return ((u32)P[i] << 16) | P[i + 1]; };
        e.imm.type = P[0]; e.imm.h = L(1); e.imm.pal = L(3); e.imm.zbias = 0;
        for (int k = 0; k < 4; k++) e.imm.i[k] = L(5 + 2 * k);
        for (int k = 0; k < 4; k++) { e.imm.u[k] = L(13 + 4 * k); e.imm.v[k] = L(15 + 4 * k); }
        for (int k = 0; k < 4; k++) { e.imm.x[k] = L(29 + 6 * k); e.imm.y[k] = L(31 + 6 * k); e.imm.z[k] = L(33 + 6 * k); }
        if (imlog) printf("IMQ fb=%u t=%04x src=%04x pal=%08x x=%08x y=%08x z=%08x\n", n_fb, type, S.buffer[0], e.imm.pal, e.imm.x[0], e.imm.y[0], e.imm.z[0]); if (imlog) { for (int k = 0; k < 4; k++) printf(" (%d,%d,%d)", s24(e.imm.x[k]), s24(e.imm.y[k]), s24(e.imm.z[k])); printf("\n"); } push(e); stat_imm++; break; }
    case 0x000a: {
        Entry e; e.type = IMMEDIATE; common(e, true);
        auto L = [&](int i) { return ((u32)P[i] << 16) | P[i + 1]; };
        e.imm.type = P[0]; e.imm.h = L(1); e.imm.pal = L(3); e.imm.zbias = L(5);
        for (int k = 0; k < 4; k++) { e.imm.u[k] = L(7 + 4 * k); e.imm.v[k] = L(9 + 4 * k); }
        for (int k = 0; k < 4; k++) { e.imm.x[k] = L(23 + 6 * k); e.imm.y[k] = L(25 + 6 * k); e.imm.z[k] = L(27 + 6 * k); }
        if (imlog) printf("IMQ fb=%u t=%04x src=%04x pal=%08x x=%08x y=%08x z=%08x\n", n_fb, type, S.buffer[0], e.imm.pal, e.imm.x[0], e.imm.y[0], e.imm.z[0]); if (imlog) { for (int k = 0; k < 4; k++) printf(" (%d,%d,%d)", s24(e.imm.x[k]), s24(e.imm.y[k]), s24(e.imm.z[k])); printf("\n"); } push(e); stat_imm++; break; }
    case 0x0001: seg_win_commit(); S.seg++; seg_win_load(); n_eol++; if (P[0] & 1) ev1++; else ev0++; if (irq_cb) irq_cb(P[0] & 1); break;
    case 0x0046: {                                                      // viewport
        // P[3] con i 3 bit bassi a 0 (0x20): finestra a tutto schermo che il gioco manda, con i nostri tempi, subito dopo il
        // monitor dei tutorial mentre scende la saracinesca: applicandola NEEDED/LIFE uscivano a tutto schermo. Si ignora
        // solo se la finestra in uso e' quella a trapezio del monitor; negli altri casi (es. attract "INTERACTIVE
        // CONTROLS": personaggi a tutto schermo davanti alla scena in una finestra piccola) serve e si applica.
        { static const bool all = getenv("S23_WINALL") != nullptr;
          const bool was_slant = win_snap_ok ? win_snap.slant : S.slant;
          if (!all && (P[3] & 7) == 0 && was_slant) { if (win_snap_ok) { memcpy(S.clip_data, win_snap.clip, sizeof win_snap.clip); S.vp_sx = win_snap.sx; S.vp_sy = win_snap.sy; S.vp_ox = win_snap.ox; S.vp_oy = win_snap.oy; S.slant = win_snap.slant; seg_pend = win_snap_pend; seg_pend_k = win_snap_k; seg_pend_used = win_snap_used; } win_snap_ok = false; break; } }
        win_snap_ok = false;
        u32 raw = ((u32)P[7] << 16) | P[8];
        u16 ox = (u16)(raw & 0x0fff), oy = (u16)((raw >> 12) & 0x0fff);
        S.vp_ox = (s16)((s16)(ox << 4) >> 4); S.vp_oy = (s16) - (((s16)(oy << 4)) >> 4); seg_win_save(); break; }
    case 0x00c8: {                                                      // riga dei dati di clip
        if (S.clip_line == 0) { win_snap_ok = true; memcpy(win_snap.clip, S.clip_data, sizeof win_snap.clip); win_snap.sx = S.vp_sx; win_snap.sy = S.vp_sy; win_snap.ox = S.vp_ox; win_snap.oy = S.vp_oy; win_snap.slant = S.slant; win_snap_pend = seg_pend; win_snap_k = seg_pend_k; win_snap_used = seg_pend_used; }
        for (int i = 0; i < 8; i++) S.clip_data[S.clip_line * 8 + i] = f24(((u32)P[2 * i + 1] << 16) | P[2 * i + 2]);
        S.clip_line = (S.clip_line + 1) % 3;
        if (S.clip_line == 0 && S.clip_data[10] != 0.f) {
            // piani del tronco di vista (a, b, c, d): 0/1 = sinistra/destra, 2/3 = alto/basso, [23] = fov.
            // MAME assume a = 1 e b = 1 (piani normalizzati e simmetrici). La finestra dei tutorial (lo schermo del
            // monitor in RULE/NEEDED) ha piani non normalizzati e inclinati (un trapezio): con la formula di MAME la
            // finestra usciva enorme (int16 traboccato) e la demo finiva fuori dallo schermino.
            const float fv = std::fabs(S.clip_data[23]);
            const float a0 = std::fabs(S.clip_data[0]) > 1e-6f ? std::fabs(S.clip_data[0]) : 1.f, b2 = std::fabs(S.clip_data[9]) > 1e-6f ? std::fabs(S.clip_data[9]) : 1.f;
            const float a1 = std::fabs(S.clip_data[4]) > 1e-6f ? std::fabs(S.clip_data[4]) : 1.f, b3 = std::fabs(S.clip_data[13]) > 1e-6f ? std::fabs(S.clip_data[13]) : 1.f;
            const float sx = std::max(std::fabs(S.clip_data[2]) / a0, std::fabs(S.clip_data[6]) / a1) * fv;
            const float sy = std::max(std::fabs(S.clip_data[10]) / b2, std::fabs(S.clip_data[14]) / b3) * fv;
            S.vp_sy = (s16)std::min(2048.f, std::roundf(sy));
            S.vp_sx = (s16)std::min(2048.f, std::roundf(sx));
            S.slant = std::fabs(S.clip_data[1]) > 1e-3f * a0 || std::fabs(S.clip_data[5]) > 1e-3f * a1;
            static const bool noslant = getenv("S23_NOSLANT") != nullptr; if (noslant) S.slant = false;
        }
        if (S.clip_line == 0) seg_win_save();
        if (S.clip_line == 0) { static const bool cl = getenv("S23_CLIPLOG") != nullptr; if (cl) { static std::string last; char b[512]; int k = 0; for (int i = 0; i < 24; i++) k += snprintf(b + k, sizeof b - k, "%.4g ", S.clip_data[i]); if (last != b) { last = b; printf("CLIP vbl=%u vp=%d,%d,%d,%d : %s\n", n_vbl, S.vp_sx, S.vp_sy, S.vp_ox, S.vp_oy, b); } } }
        break; }
    default: break;
    }
}

// Saracinesca "PANIC PARK" che chiude il monitor dei tutorial (modelli 02c8/02c9): sul cabinato sta fuori dallo
// schermino e davanti a tutto. Il gioco (con i nostri tempi, ma anche in MAME) non le manda la sua finestra a tutto
// schermo prima del modello, quindi finiva ritagliata nello schermino e dietro alle scritte. S23_FULLWIN=lista|none.
static bool full_window_model(u32 m) {
    static std::vector<u32> L; static bool init = false;
    if (!init) { init = true; std::string t = getenv("S23_FULLWIN") ? getenv("S23_FULLWIN") : "2c8,2c9";
        if (t != "none") { size_t i = 0; while (i < t.size()) { L.push_back((u32)strtoul(t.c_str() + i, nullptr, 16)); size_t j = t.find(',', i); if (j == std::string::npos) break; i = j + 1; } } }
    for (u32 x : L) if (x == m) return true; return false;
}
static void do_render() {                                               // comando 8: disegna un modello
    const u16* b = S.buffer; int size = b[0] & 0xf;
    { static const char* sle = getenv("S23_STLOG"); static const unsigned sla = sle ? atoi(sle) : 0, slb = sle && strchr(sle, ',') ? atoi(strchr(sle, ',') + 1) : ~0u; if (sle && n_vbl >= sla && n_vbl <= slb) printf("R vbl=%u seg=%d model=%04x\n", n_vbl, S.seg, b[1]); }
    if (b[1] == 0) { seg_win_commit(); S.seg++; seg_win_load(); n_eol++; evr++; if (irq_cb) irq_cb(true); return; }
    bool mirror_x = (b[0] >> 13) & 1, scroll = (b[0] >> 9) & 1, use_scaling = (b[0] >> 7) & 1, tr = (b[0] >> 6) & 1;
    if (size < 3 || size > 4) return;
    const s16* m = getm(b[size - 1]); const s32* v = getv(b[size]);
    Entry e; e.type = MODEL; common(e, true);
    e.model.model = b[1]; e.model.model2 = size == 4 ? b[2] : 0;
    e.model.scaling = use_scaling ? S.scaling / 16384.0f : 1.0f; e.model.transpose = tr;
    e.model_blend_factor = S.model_blend_factor; e.mirror_x = mirror_x; e.tx = scroll ? S.tx : 0; e.ty = scroll ? S.ty : 0;
    memcpy(e.model.lv, S.light_vector, sizeof e.model.lv); memcpy(e.model.m, m, sizeof e.model.m); memcpy(e.model.v, v, sizeof e.model.v);
    if (g_full.has && full_window_model(e.model.model)) {            // saracinesca dei tutorial: finestra a tutto schermo
        e.vp_sx = g_full.sx; e.vp_sy = g_full.sy; e.vp_ox = g_full.ox; e.vp_oy = g_full.oy; e.fov = g_full.clip[23]; memcpy(e.clip, g_full.clip, sizeof e.clip); e.slant = false;
    }
    push(e); stat_models++;
}

static void frame_begin();
// PIO/DMA del C435 (la coda dei comandi usata dal gioco)
static void pio(u16 data) {
    u16* b = S.buffer;
    if (S.buffer_pos < 255) b[S.buffer_pos++] = data;
    u16 h = b[0];
    int psize = ((h & 0x4000) == 0x4000) ? (h & 0xff) : (h & 0xf);
    if (S.buffer_pos < psize + 1) return;
    { static bool pk = getenv("S23_PKTLOG") != nullptr; if (pk && (h == 0x4f2a || h == 0x4f14 || (h & 0xff00) == 0x4000 || h == 0x4601)) { printf("PKT fb=%u", n_fb); for (int i = 0; i < S.buffer_pos; i++) printf(" %04x", b[i]); printf("\n"); } }
    { static unsigned cl = getenv("S23_CMDLOG") ? (unsigned)atoi(getenv("S23_CMDLOG")) : ~0u; if (n_fb >= cl && n_fb < cl + 3000) printf("CMD fb=%u %04x %04x\n", n_fb, h, b[1]); }
    if (tl && n_vbl >= 640 && n_vbl <= 642) { printf("CMD v%u %04x", n_vbl, h); if ((h & 0xc000) == 0x4000 && (h & 0x3f00) == 0x0f00) printf(" sub=%04x", b[1]); if ((h & 0xc000) == 0x8000) printf(" model=%04x", b[1]); printf("\n"); }
    switch (h & 0xc000) {
    case 0x0000:
        switch (h & 0xf0) {
        case 0x00: matrix_matrix_mul(); break; case 0x10: matrix_vector_mul(); break;
        case 0x20: matrix_matrix_immed_mul(); break; case 0x30: matrix_vector_immed_mul(); break;
        case 0x40: matrix_set(); break; case 0x50: vector_set(); break;
        }
        break;
    case 0x4000:
        switch (h & 0x3f00) {
        case 0x0000: if (h == 0x4001) frame_begin(); break;
        case 0x0100: S.absolute_priority = b[1]; break; case 0x0200: S.tx = b[1]; break; case 0x0300: S.ty = b[1]; break;
        case 0x0400: if ((h & 0xff) == 1) S.scaling = b[1]; break; case 0x0500: S.model_blend_factor = b[1]; break;
        case 0x0700: S.light_power = b[1]; break; case 0x0800: S.light_ambient = b[1]; break;
        case 0x0f00: {
            if ((h & 0xff) == 0) break;
            int size = state_entry_size(b[1]);
            if (size != (int)(h & 0xff) - 1 && h != 0x4f38) { static int nd = 0; if (getenv("S23_DROPLOG") && nd < 200) { nd++; printf("DROP v%u h=%04x sub=%04x size=%d\n", n_vbl, h, b[1], size); } break; }
            // 4f38 con sotto-tipo 0x0001: e' comunque un quad immediato completo (55 parole). Il gioco lo usa per
            // l'ultimo spicchio della stella esplosa; MAME lo tratta solo come fine lista e lo perde.
            if (h == 0x4f38 && b[1] == 0x0001) state_set(0x0000, b + 2);
            state_set(b[1], b + 2); break; }
        }
        break;
    case 0x8000: do_render(); break;
    case 0xc000: break;
    }
    S.buffer_pos = 0;
}
static void state_reset() { S.buffer_pos = 0; }
static void state_pio(u16 data) {                                        // 0x0c800016
    { static unsigned cl = getenv("S23_CMDLOG") ? (unsigned)atoi(getenv("S23_CMDLOG")) : ~0u; if (n_fb >= cl && n_fb < cl + 3000) printf("SPIO fb=%u %04x pos=%d\n", n_fb, data, S.buffer_pos); }
    u16* b = S.buffer; if (S.buffer_pos < 255) b[S.buffer_pos++] = data;
    int psize = state_entry_size(b[0]);
    if (S.buffer_pos < psize + 1) return;
    state_set(b[0], b + 1); S.buffer_pos = 0;
}

// poligoni "diretti" (usati per sfondi/effetti)
static void direct_start(u16 data) { S.direct_open = data != 0; S.direct_pos = 0; }
static void direct_buf_w(u16 data) {
    if (!S.direct_open) return;
    if (S.direct_pos == 0) { S.direct_pos++; return; }
    S.direct_buf[S.direct_pos - 1] = data; S.direct_pos++; S.direct_nonempty = true;
    if (S.direct_pos >= 29) {
        Entry e; e.type = DIRECT; snap(e); e.absolute_priority = S.absolute_priority;
        e.vp_sx = 320; e.vp_sy = 240; e.vp_ox = 0; e.vp_oy = 0; e.fov = 320.f;
        memcpy(e.d, S.direct_buf, sizeof e.d); push(e); stat_direct++;
        S.direct_nonempty = false; S.direct_pos = 0;
    }
}
static void ctl_direct_w(u16 data) {                                      // 0x0d00000c
    if (S.direct_pos < 28) S.direct_buf[S.direct_pos++] = data;
    if (data) S.direct_nonempty = true;
    if (S.direct_pos >= 28) {
        if (S.direct_nonempty && !(S.direct_buf[0] & 0x8000)) {
            Entry e; e.type = DIRECT; snap(e); e.absolute_priority = S.absolute_priority;
            memcpy(e.d, S.direct_buf, sizeof e.d); push(e); stat_direct++;
        }
        S.direct_nonempty = false; S.direct_pos = 0;
    }
}

// fine frame: la lista completata viene resa visibile con un frame di ritardo, come in MAME (doppio buffer)
static bool have_latest = false;
static size_t cut = (size_t)-1;                                   // dimensione della lista corrente al momento del vblank reale
static inline void mark_vbl() { cut = cur_list.size(); }         // chiamata dal timer: nessun lock

static std::vector<Entry> latest;                                 // ultima lista completa (tra due 0x4001)
static void frame_begin() {                                       // il gioco apre ogni fotogramma con il comando 0x4001: la lista precedente e' completa
    n_fb++; seg_win_commit(); for (int k = S.seg + 1; k < 32; k++) seg_win[k].has = false; seg_win_prev_max = S.seg; S.seg = 0; MtxGuard lk(mtx);
    if (!cur_list.empty()) { latest.swap(cur_list); cur_list.clear(); have_latest = true; }
}
static void vblank() {
    MtxGuard lk(mtx);
    n_vbl++;
    if (have_latest) { disp_list = latest; disp_new = true; }
    else { disp_list.swap(ready_list); ready_list = cur_list; disp_new = true; }
}

// ---------------------------------------------------------------- registri C412/C421/C417
static bool c412_r(u32 p, u32& v) {
    switch (p) {
    case 0x0c000006: v = 2; return true;
    case 0x0c000010: v = C.c412_adr & 0xffff; return true;
    case 0x0c000012: v = C.c412_adr >> 16; return true;
    case 0x0c000014:
        if (C.c412_adr < 0x100000) v = C.c412_a[C.c412_adr & 0xfffff];
        else if (C.c412_adr < 0x200000) v = C.c412_b[C.c412_adr & 0xfffff];
        else if (C.c412_adr < 0x220000) v = C.c412_sram[C.c412_adr & 0x1ffff];
        else if (C.c412_adr < 0x220200) v = C.c412_pcz[C.c412_adr & 0x1ff]; else v = 0xffff;
        return true;
    case 0x0c000018: C.c412_status ^= 1; v = C.c412_status; return true;
    }
    return false;
}
static bool c412_w(u32 p, u32 v) {
    switch (p) {
    case 0x0c000004: return true;
    case 0x0c000010: C.c412_adr = (C.c412_adr & 0xffff0000u) | (v & 0xffff); return true;
    case 0x0c000012: C.c412_adr = (C.c412_adr & 0xffffu) | ((v & 0xffff) << 16); return true;
    case 0x0c000014:
        if (C.c412_adr < 0x100000) C.c412_a[C.c412_adr & 0xfffff] = (u16)v;
        else if (C.c412_adr < 0x200000) C.c412_b[C.c412_adr & 0xfffff] = (u16)v;
        else if (C.c412_adr < 0x220000) C.c412_sram[C.c412_adr & 0x1ffff] = (u16)v;
        else if (C.c412_adr < 0x220200) C.c412_pcz[C.c412_adr & 0x1ff] = (u16)v;
        C.c412_adr++; return true;
    }
    return false;
}
static bool c421_r(u32 p, u32& v) {
    u32 off = C.c421_adr & 0xfffff;
    switch (p) {
    case 0x0c400000:
        if (off < 0x40000) v = C.c421_a[off]; else if (off < 0x80000) v = C.c421_b[off & 0x3ffff]; else if (off < 0x88000) v = C.c421_sram[off & 0x7fff]; else v = 0xffff;
        return true;
    case 0x0c400004: v = C.c421_adr >> 16; return true;
    case 0x0c400006: v = C.c421_adr & 0xffff; return true;
    }
    return false;
}
static bool c421_w(u32 p, u32 v) {
    switch (p) {
    case 0x0c400000: {
        u32 off = C.c421_adr & 0xfffff;
        if (off < 0x40000) C.c421_a[off] = (u16)v; else if (off < 0x80000) C.c421_b[off & 0x3ffff] = (u16)v; else if (off < 0x88000) C.c421_sram[off & 0x7fff] = (u16)v;
        C.c421_adr += 2; return true; }
    case 0x0c400004: C.c421_adr = (C.c421_adr & 0xffffu) | ((v & 0xffff) << 16); return true;
    case 0x0c400006: C.c421_adr = (C.c421_adr & 0xffff0000u) | (v & 0xffff); return true;
    }
    return false;
}
static u32 ptrom_word() { return R.ok && C.c417_prom_adr < R.ptrom_n ? R.ptrom[C.c417_prom_adr] : 0xffffffffu; }

// ---------------------------------------------------------------- generazione dei poligoni (modelli, diretti, immediati)
struct Frame {                                                            // parametri di rendering del fotogramma
    std::vector<Poly> polys; std::vector<u32> pal;                          // pal[i] = 0xRRGGBB
};

static int zclip(int n, const PV* v, PV* out, float clipval) {
    bool prev = v[n - 1].p[0] < clipval; const PV* pv = &v[n - 1]; PV* o = out;
    for (int i = 0; i < n; i++) {
        bool cl = v[i].p[0] < clipval;
        if (cl != prev) {
            float f = (clipval - pv->p[0]) / (v[i].p[0] - pv->p[0]);
            o->x = pv->x + f * (v[i].x - pv->x); o->y = pv->y + f * (v[i].y - pv->y);
            for (int k = 0; k < 4; k++) o->p[k] = pv->p[k] + f * (v[i].p[k] - pv->p[k]);
            o++;
        }
        if (!cl) *o++ = v[i];
        prev = cl; pv = &v[i];
    }
    return (int)(o - out);
}

static void apply_transform(s32 xi, s32 yi, s32 zi, const Entry& re, float& x, float& y, float& z) {
    s16 m[9]; memcpy(m, re.model.m, sizeof m); if (re.model.transpose) transpose(m);
    x = (((m[0] * (s64)xi + m[1] * (s64)yi + m[2] * (s64)zi) >> 14) * re.model.scaling + re.model.v[0]) / 16384.f;
    y = (((m[3] * (s64)xi + m[4] * (s64)yi + m[5] * (s64)zi) >> 14) * re.model.scaling + re.model.v[1]) / 16384.f;
    z = (((m[6] * (s64)xi + m[7] * (s64)yi + m[8] * (s64)zi) >> 14) * re.model.scaling + re.model.v[2]) / 16384.f;
}
// rotazione di normali e luce: stessa matrice dei vertici (trasposta se il modello lo chiede). Per la luce del
// C435 (prodotto scalare) non cambia nulla, ma la normale e la luce in spazio camera servono a riflesso e ombre.
static void apply_matrot(s32 xi, s32 yi, s32 zi, const Entry& re, float& x, float& y, float& z) {
    s16 m[9]; memcpy(m, re.model.m, sizeof m); if (re.model.transpose) transpose(m);
    x = (m[0] * (float)xi + m[1] * (float)yi + m[2] * (float)zi) / 4194304.f;
    y = (m[3] * (float)xi + m[4] * (float)yi + m[5] * (float)zi) / 4194304.f;
    z = (m[6] * (float)xi + m[7] * (float)yi + m[8] * (float)zi) / 4194304.f;
}
static void project(PV& pv, s16 sx, s16 sy, float fov) { pv.x = (float)sx + fov * pv.x; pv.y = (float)sy - fov * pv.y; pv.p[0] = 1.0f / pv.p[0]; }

static void fill_fades(RD& rd, const Entry& re, u32 alpha_pal) {
    rd.fadefactor = 0xff;
    if (re.fade_flags & 1) { rd.fadefactor = 0xff - re.sfactor; rd.fadefactor_inv = 0x100 - rd.fadefactor; rd.fcr = re.sfr; rd.fcg = re.sfg; rd.fcb = re.sfb; }
    rd.pfade = re.pfr != 0xff || re.pfg != 0xff || re.pfb != 0xff; rd.pcr = re.pfr; rd.pcg = re.pfg; rd.pcb = re.pfb;
    rd.alpha = 0xff - re.alpha; rd.alpha_inv = 0x100 - rd.alpha; rd.alpha_pen = re.alpha_pen; (void)alpha_pal;
}

static bool dump = false;
static bool no_bias = getenv("S23_NOBIAS") != nullptr;
// Modelli che sul cabinato stanno davanti a tutto il 3D ma che con l'ordinamento ricostruito (MAME) finiscono dietro.
// 02db = pulsante START nella schermata "PANIC KING" (spostamento di profondita' +20000: dietro alle statuine).
// S23_FRONT=lista esadecimale separata da virgole per sostituirla, S23_FRONT=none per disattivarla.
static bool front_model(u32 m) {
    static std::vector<u32> L; static bool init = false;
    if (!init) { init = true; std::string t = getenv("S23_FRONT") ? getenv("S23_FRONT") : "2db,2c8,2c9";
        if (t != "none") { size_t i = 0; while (i < t.size()) { L.push_back((u32)strtoul(t.c_str() + i, nullptr, 16)); size_t j = t.find(',', i); if (j == std::string::npos) break; i = j + 1; } } }
    for (u32 x : L) if (x == m) return true; return false;
}
static bool zalt = getenv("S23_ZALT") != nullptr;
static bool neg_bias = getenv("S23_NEGBIAS") != nullptr;      // test: ignora lo spostamento di profondita' dei modelli
// MAME aggiunge 0,5 alle coordinate di texture perche' campiona all'angolo del pixel; il nostro rasterizzatore campiona
// al centro (x + 0,5), quindi con lo 0,5 in piu' ogni pixel prendeva il texel a destra/sotto e l'ultima colonna/riga di
// un riquadro prendeva quello del riquadro vicino (bordi colorati). S23_UVHALF=1 = come MAME.
static float uv_bias(bool stencil) { static const bool half = getenv("S23_UVHALF") != nullptr; return (half && !stencil) ? 0.5f : 0.f; }
// massimo della luce calcolata (lmode 3): 64 = colore della texture. MAME lascia arrivare a 255 (fino a 4x, colori sbiaditi);
// il confronto col cabinato (D&L, ruota della difficolta') indica che la luce non supera il colore originale.
static float light_max = getenv("S23_LMAX") ? (float)atof(getenv("S23_LMAX")) : 64.f;
static int g_lm3 = 0, g_lmo = 0, g_lmc[4];
static void make_model(const Entry& re, Frame& F) {
    g_lm3 = g_lmo = 0; memset(g_lmc, 0, sizeof g_lmc);
    bool model_blend = re.model.model2 != 0;
    if (re.model.model >= R.ptrom_n || re.model.model2 >= R.ptrom_n) return;
    u32 adr = R.ptrom[re.model.model], adr2 = R.ptrom[re.model.model2], limit = (u32)R.ptrom_n;
    if (adr >= limit || adr2 >= limit) return;
    u32 offs = 0; const u32* data = &R.ptrom[adr]; const u32* data2 = &R.ptrom[adr2];
    while (adr + offs + 16 < limit && adr2 + offs + 16 < limit) {
        PV pv[16];
        u32 type = data[offs++], h = data[offs++];
        u32 cmode = (type & 0x70000000) >> 28, tbase = (((type & 0x0f000000) >> 24) << 12) | (((type >> 31) & 1) << 16);
        u8 color = (h >> 24) & 0x7f; int lmode = (type >> 19) & 3, ne = (type >> 8) & 15; bool stencil = (h >> 11) & 1;
        u32 polyshift = 0; if (type & 0x00001000) polyshift = data[offs++];
        u32 light = 0, extptr = 0;
        if (lmode == 3) { extptr = offs; offs += ne; } else light = data[offs++];
        for (int i = 0; i < ne && i < 16; i++) {
            u32 v1 = data[offs + 0], v2 = data[offs + 1], v3 = data[offs + 2];
            float x, y, z; apply_transform(s24(v1), s24(v2), s24(v3), re, x, y, z);
            float fb = re.model_blend_factor / 16384.f, fa = 1.f - fb;
            if (model_blend) {
                float x2, y2, z2; apply_transform(s24(data2[offs]), s24(data2[offs + 1]), s24(data2[offs + 2]), re, x2, y2, z2);
                x = x * fa + x2 * fb; y = y * fa + y2 * fb; z = z * fa + z2 * fb;
            }
            offs += 3;
            pv[i].x = x; pv[i].y = y; pv[i].p[0] = z;
            pv[i].p[1] = (float)(((v1 >> 20) & 0xf00) | ((v2 >> 24) & 0xff)) + uv_bias(stencil) + re.tx;
            pv[i].p[2] = (float)(((v1 >> 16) & 0xf00) | ((v3 >> 24) & 0xff)) + uv_bias(stencil) + re.ty;
            pv[i].p[3] = 64;
            static const u8 LS[4] = { 24, 16, 8, 0 };
            switch (lmode) {
            case 0: case 1: pv[i].p[3] = (float)((light >> LS[i & 3]) & 0xff); break;
            case 2: pv[i].p[3] = 64.f; break;
            case 3: {
                u32 norm = data[extptr]; s32 nx = s10((norm >> 20) & 0x3ff), ny = s10((norm >> 10) & 0x3ff), nz = s10(norm & 0x3ff);
                if (model_blend) {
                    u32 n2 = data2[extptr]; s32 nx2 = s10((n2 >> 20) & 0x3ff), ny2 = s10((n2 >> 10) & 0x3ff), nz2 = s10(n2 & 0x3ff);
                    nx = (s32)(nx * fa + nx2 * fb); ny = (s32)(ny * fa + ny2 * fb); nz = (s32)(nz * fa + nz2 * fb);
                }
                extptr++;
                float nrx, nry, nrz; apply_matrot(nx, ny, nz, re, nrx, nry, nrz);
                float len = sqrtf(nrx * nrx + nry * nry + nrz * nrz); if (len != 0.f) { nrx /= len; nry /= len; nrz /= len; }
                float cx, cy, cz; apply_matrot(re.model.lv[0], re.model.lv[1], re.model.lv[2], re, cx, cy, cz);
                len = sqrtf(cx * cx + cy * cy + cz * cz); if (len != 0.f) { cx /= len; cy /= len; cz /= len; }
                float lsi = nrx * cx + nry * cy + nrz * cz; if (lsi < 0) lsi = 0;
                pv[i].p[3] = std::clamp(re.light_ambient + re.light_power * lsi, 0.f, light_max);
                { static const int dm = getenv("S23_LDBG") ? (int)strtol(getenv("S23_LDBG"), 0, 16) : -1; if (re.model.model == dm && dump) printf("LDBG v%d n=%.2f,%.2f,%.2f raw=%d,%d,%d L=%.2f,%.2f,%.2f lsi=%.2f pos=%.3f,%.3f,%.3f\n", i, nrx, nry, nrz, nx, ny, nz, cx, cy, cz, lsi, x, y, z); }
            } break;
            }
        }
        bool last = (type & 0x00010000) != 0;
        if (ne >= 3) {
            if ((h >> 5) & 1) {                                                   // back-face culling
                float z0 = pv[0].p[0], z1 = pv[1].p[0], z2 = pv[2].p[0], z3 = pv[3].p[0];
                float c1 = (pv[2].x * (z0 * pv[1].y - pv[0].y * z1)) + (pv[2].y * (pv[0].x * z1 - z0 * pv[1].x)) + (z2 * (pv[0].y * pv[1].x - pv[0].x * pv[1].y));
                float c2 = (pv[0].x * (z2 * pv[3].y - pv[2].y * z3)) + (pv[0].y * (pv[2].x * z3 - z2 * pv[3].x)) + (z0 * (pv[2].y * pv[3].x - pv[2].x * pv[3].y));
                if (c1 >= 0.f && c2 >= 0.f) { if (last) break; continue; }
            }
            if (re.mirror_x) for (int k = 0; k < 4 && k < ne; k++) pv[k].x *= -1.0f;
        } else { if (last) break; continue; }
        (lmode == 3 ? g_lm3 : g_lmo)++; g_lmc[lmode]++;
        Poly P; P.index = (int)F.polys.size();
        P.nv = zclip(ne, pv, P.pv, 0.0001f);
        if (P.nv >= 3) {
            P.rd.intensity_base = P.pv[0].p[3]; float minz = FLT_MAX, maxz = FLT_MIN;
            for (int i = 0; i < P.nv; i++) {
                float z = P.pv[i].p[0] != 0 ? P.pv[i].p[0] : 1.f;
                P.pv[i].x /= z; P.pv[i].y /= z; z *= 16384.f; if (z > maxz) maxz = z; if (z < minz) minz = z;
                project(P.pv[i], re.vp_sx, re.vp_sy, re.fov);
                float w = P.pv[i].p[0]; P.pv[i].p[1] *= w; P.pv[i].p[2] *= w; P.pv[i].p[3] = (P.pv[i].p[3] - P.rd.intensity_base) * w;
            }
            if (maxz < 0) { if (last) break; continue; }

            int zsort = 0;
            switch (h & 0x300) { case 0x000: zsort = (int)(minz + 0.5f); break; case 0x100: zsort = (int)(maxz + 0.5f); break; case 0x200: if (zalt) { zsort = (int)(maxz + 0.5f); break; } default: zsort = (int)(0.5f * (minz + maxz) + 0.5f); }
            if (zsort > 0x1fffff) zsort = 0x1fffff;
            int prio = re.absolute_priority & 7;
            { static const Entry* le = nullptr; if (dump && &re != le) { le = &re; printf("PS idx=%d model=%x tz=%d type=%08x h=%08x ps=%08x minz=%.0f maxz=%.0f abs=%d\n", P.index, (unsigned)re.model.model, (int)re.model.v[2], type, h, polyshift, minz, maxz, re.absolute_priority); } }
            { static const bool bl = getenv("S23_BIASLOG") != nullptr; static const Entry* lb = nullptr; if (bl && &re != lb && !((polyshift >> 21) & 1)) { int bz = ((polyshift >> 17) & 1) ? (int)(polyshift | 0xfffc0000u) : (int)(polyshift & 0x0001ffff); if (bz >= 8000 || bz <= -8000) { lb = &re; printf("BIAS model=%04x seg=%d bias=%d tz=%d vbl=%u\n", re.model.model, re.seg, bz, (int)re.model.v[2], n_vbl); } } }
            if (no_bias) {}
            else if ((polyshift >> 21) & 1) zsort = polyshift & 0x1fffff;
            else { int bz = ((polyshift >> 17) & 1) ? (int)(polyshift | 0xfffc0000u) : (int)(polyshift & 0x0001ffff); zsort += neg_bias ? bz / 4 : bz; prio += (polyshift & 0x1c0000) >> 18; }
            if (front_model(re.model.model)) zsort = 0;                        // eccezione: sempre davanti (vedi front_model)
            zsort = std::clamp(zsort, 0, 0x1fffff); zsort |= (prio << 21);
            P.zkey = zsort;
            P.rd.stencil = stencil; P.rd.pens = (u32)color << 8; P.rd.h = h; P.rd.type = type; P.rd.tbase = tbase; P.rd.cmode = (u8)cmode;
            P.rd.vp_sx = re.vp_sx; P.rd.vp_sy = re.vp_sy; P.rd.vp_ox = re.vp_ox; P.rd.vp_oy = re.vp_oy; P.rd.slant = re.slant; memcpy(P.rd.cpl, re.cpl, sizeof re.cpl); P.rd.cfov = re.fov; P.rd.shade = true;
            fill_fades(P.rd, re, color);
            P.rd.alpha_enabled = ((color & 0x7f) != re.alpha_color) || ((type >> 21) & 1);
            P.rd.blend = (h >> 10) & 1;
            F.polys.push_back(P);
        }
        if (last) break;
    }
}

static void make_direct(const Entry& re, Frame& F) {
    u32 polyshift = (((u32)re.d[1] & 0x1ff) << 12) | (re.d[0] & 0xfff);
    u32 cztype = re.d[3] & 3; (void)cztype;
    static const int idx[2][3] = { {0, 1, 2}, {0, 2, 3} };
    for (int i = 0; i < 2; i++) {
        Poly P; P.nv = 3; P.index = (int)F.polys.size();
        P.rd.intensity_base = (float)(re.d[8 + idx[i][0] * 6] >> 8);
        for (int j = 0; j < 3; j++) {
            const u16* src = &re.d[4 + idx[i][j] * 6];
            int mant = src[5], expo = src[4] & 0x3f;
            if (mant) { P.pv[j].p[0] = (float)mant; while (expo < 0x2e) { P.pv[j].p[0] /= 2.0f; expo++; } } else P.pv[j].p[0] = 1.f;
            P.pv[j].p[1] = (float)(((src[0] >> 4) + uv_bias(false)) * P.pv[j].p[0]);
            P.pv[j].p[2] = (float)(((src[1] >> 4) + uv_bias(false)) * P.pv[j].p[0]);
            P.pv[j].p[3] = (float)(((src[4] >> 8) - P.rd.intensity_base) * P.pv[j].p[0]);
            P.pv[j].x = (float)((s16)src[2] + 320); P.pv[j].y = (float)((s16)src[3] + 240);
        }
        int zsort = 0; int prio = re.absolute_priority & 7;
        if ((polyshift >> 21) & 1) zsort = polyshift & 0x1fffff;
        else { zsort += ((polyshift >> 17) & 1) ? (int)(polyshift | 0xfffc0000u) : (int)(polyshift & 0x0001ffff); prio += (polyshift & 0x1c0000) >> 18; }
        zsort = std::clamp(zsort, 0, 0x1fffff); zsort |= (prio << 21);
        P.zkey = zsort;
        P.rd.pens = re.d[2] & 0x7f00; P.rd.tbase = ((re.d[2] & 0x000fu) << 12) | (((re.d[2] >> 7) & 1u) << 16); P.rd.cmode = (re.d[2] & 0x0070) >> 4;
        P.rd.shade = true; P.rd.vp_sx = re.vp_sx; P.rd.vp_sy = re.vp_sy; P.rd.vp_ox = re.vp_ox; P.rd.vp_oy = re.vp_oy; P.rd.slant = re.slant; memcpy(P.rd.cpl, re.cpl, sizeof re.cpl); P.rd.cfov = re.fov;
        fill_fades(P.rd, re, 0);
        P.rd.alpha_enabled = ((re.d[2] >> 8) & 0x7f) != re.alpha_color; P.rd.blend = false;
        F.polys.push_back(P);
    }
}

static void make_immediate(const Entry& re, Frame& F) {
    u32 type = re.imm.type, h = re.imm.h, polyshift = re.imm.zbias; int ne = (type >> 8) & 0xf; bool stencil = (h >> 11) & 1;
    if (ne > 4) ne = 4;
    PV pv[16];
    for (int i = 0; i < ne; i++) {
        pv[i].x = s24(re.imm.x[i]) / 16384.f; pv[i].y = s24(re.imm.y[i]) / 16384.f; pv[i].p[0] = s24(re.imm.z[i]) / 16384.f;
        pv[i].p[1] = (float)(s32)re.imm.u[i]; pv[i].p[2] = (float)(s32)re.imm.v[i]; pv[i].p[3] = (float)(s32)re.imm.i[i];
    }
    if (ne < 3) return;
    Poly P; P.index = (int)F.polys.size();
    P.nv = zclip(ne, pv, P.pv, 0.0001f);
    if (P.nv < 3) return;
    P.rd.intensity_base = P.pv[0].p[3]; float minz = FLT_MAX, maxz = FLT_MIN;
    for (int i = 0; i < P.nv; i++) {
        float z = P.pv[i].p[0] != 0 ? P.pv[i].p[0] : 1.f; P.pv[i].x /= z; P.pv[i].y /= z; z *= 16384.f;
        if (z > maxz) maxz = z; if (z < minz) minz = z;
        project(P.pv[i], re.vp_sx, re.vp_sy, re.fov);
        float w = P.pv[i].p[0]; P.pv[i].p[1] *= w; P.pv[i].p[2] *= w; P.pv[i].p[3] = (P.pv[i].p[3] - P.rd.intensity_base) * w;
    }
    int zsort = (int)(0.5f * (minz + maxz) + 0.5f); if (zsort > 0x1fffff) zsort = 0x1fffff;
    int prio = re.absolute_priority & 7;
    if (dump) printf("IM idx=%d type=%08x h=%08x zb=%08x pal=%08x minz=%.0f maxz=%.0f abs=%d z0=%08x\n", P.index, type, h, polyshift, re.imm.pal, minz, maxz, re.absolute_priority, re.imm.z[0]);
    if ((polyshift >> 21) & 1) zsort = polyshift & 0x1fffff;
    else { zsort += ((polyshift >> 16) & 1) ? (int)(polyshift | 0xfffe0000u) : (int)(polyshift & 0x0000ffff); prio += (polyshift & 0x1c0000) >> 18; }
    // Euristica (non in MAME): un quad immediato piatto (tutti i vertici alla stessa z) e' un fondale in
    // spazio schermo, es. la stella esplosa dietro le scritte 3D. Sul cabinato originale sta dietro a
    // tutto il resto della stessa priorita'; con la z reale (molto vicina) finirebbe davanti. Lo mandiamo
    // in fondo; i pareggi restano in ordine di invio, quindi gli strati della stella restano corretti.
    { static bool off = getenv("S23_FLATBG") == nullptr; if (!off && !((polyshift >> 21) & 1) && maxz - minz < 1.f) zsort = 0x1fffff; }
    P.zkey = zsort | (prio << 21);
    P.rd.stencil = stencil; P.rd.pens = re.imm.pal & 0x7f00; P.rd.shade = true; P.rd.h = h; P.rd.type = type; P.rd.tbase = 0; P.rd.cmode = 0;
    P.rd.vp_sx = re.vp_sx; P.rd.vp_sy = re.vp_sy; P.rd.vp_ox = re.vp_ox; P.rd.vp_oy = re.vp_oy; P.rd.slant = re.slant; memcpy(P.rd.cpl, re.cpl, sizeof re.cpl); P.rd.cfov = re.fov;
    fill_fades(P.rd, re, 0);
    P.rd.alpha_enabled = ((re.imm.pal >> 8) & 0x7f) != re.alpha_color; P.rd.blend = (h >> 10) & 1;
    F.polys.push_back(P);
}

// ---------------------------------------------------------------- rasterizzazione
static inline bool stencil_lookup(u32 x, u32 y) {
    u32 bit = (x & 15) ^ 15, offs = ((y << 6) | (x >> 4)) & 0x1ffff; return !((C.c412_sram[offs] >> bit) & 1);
}
static inline u32 tex_lookup(const u32* pal, u32 pens, int penshift, int penmask, u32 u, u32 v, u8& pen) {
    u32 tileid = ((u >> 4) & 0xff) | ((v << 4) & R.tileid_mask);
    u32 tile = R.tm_decoded[tileid]; u32 attr = R.tm_attr[tileid];
    if (attr & 1) v = ~v; if (attr & 2) u = ~u; if (attr & 4) std::swap(u, v);
    pen = R.texrom[(tile | ((v << 4) & 0xf0) | (u & 0x0f)) & 0x1ffffff];
    return pal[(pens + ((pen >> penshift) & penmask)) & 0x7fff];
}

struct Target { u32* fb; int w = 640, h = 480; double s = 1.0; int xoff = 0; u32 amark = 0xff000000u; int by0 = 0, by1 = 1 << 30; };   // by0..by1: fascia di righe (disegno in parallelo)   // xoff: margine laterale del widescreen (pixel)   // s: scala rispetto a 640x480

// correzione prospettica ogni N pixel nella via veloce: 1 = esatta per ogni pixel. Su console 8 (meno divisioni).
static int& pspan() {
#if defined(__WIIU__) || defined(__SWITCH__)
    static int v = getenv("S23_PSPAN") ? std::max(1, atoi(getenv("S23_PSPAN"))) : 8;
#else
    static int v = getenv("S23_PSPAN") ? std::max(1, atoi(getenv("S23_PSPAN"))) : 1;
#endif
    return v;
}
static int dbg_px = -1, dbg_py = -1;                              // debug: pixel da descrivere (probe_owner)
static bool force_slow_rast = getenv("S23_SLOWRAST") != nullptr;                            // test: confronto con il percorso per pixel
static const int NA = 4;                                         // attributi interpolati: 1/z, u, v, intensita'
static void scanline(const RD& rd, const u32* pal, Target& T, int y, int x0, int x1, const double* a0, const double* da) {
    double z = a0[0], u = a0[1], v = a0[2], i = a0[3], dz = da[0], du = da[1], dv = da[2], di = da[3];
    const double sc = T.s;
    s32 clip_left = (s32)std::lround(((320 - rd.vp_sx) + rd.vp_ox) * sc), clip_right = (s32)std::lround(((320 + rd.vp_sx) + rd.vp_ox) * sc);
    s32 clip_top = (s32)std::lround(((240 - rd.vp_sy) - rd.vp_oy) * sc), clip_bottom = (s32)std::lround(((240 + rd.vp_sy) - rd.vp_oy) * sc);
    if (T.xoff) {                                                 // widescreen: tutto spostato al centro; le viewport a tutta larghezza si allargano ai lati
        bool full = rd.vp_sx >= 320 && rd.vp_ox == 0;
        clip_left += T.xoff; clip_right += T.xoff;
        if (full) { clip_left = 0; clip_right = T.w; }
    }
    const s32 xorg = (s32)std::lround(((320 - rd.vp_sx) + rd.vp_ox) * sc) + T.xoff;   // pixel 0 della viewport originale
    s32 oy = y + clip_top; if (oy < 0 || oy >= T.h || oy < clip_top || oy >= clip_bottom) return;
    if (rd.slant) {                                               // finestra a trapezio: limiti di x per questa riga
        const double syw = (240 - rd.vp_oy) - (oy + 0.5) / sc;     // y sullo schermo rispetto al centro di proiezione (in su)
        double lo = -1e9, hi = 1e9;
        for (int k = 0; k < 2; k++) {
            const double a = rd.cpl[k][0], b = rd.cpl[k][1], c = rd.cpl[k][2]; if (std::fabs(a) < 1e-9) continue;
            const double bound = -(b * syw + c * rd.cfov) / a;     // a*sx + b*syw + c*fov = 0
            if ((a > 0) == (c > 0)) lo = std::max(lo, bound); else hi = std::min(hi, bound);   // il centro (0,0) sta dentro
        }
        const double cx = 320 + rd.vp_ox;
        clip_left = std::max(clip_left, (s32)std::lround((cx + lo) * sc) + T.xoff); clip_right = std::min(clip_right, (s32)std::lround((cx + hi) * sc) + T.xoff);
        if (clip_left >= clip_right) return;
    }
    u32* dest = T.fb + (size_t)oy * T.w;
    u32 pens = rd.pens; int penmask = 0xff, penshift = 0;
    if (rd.cmode & 4) { pens += 0xec + ((rd.cmode & 8) << 1); penmask = 3; penshift = 2 * (~rd.cmode & 3); }
    else if (rd.cmode & 2) { pens += 0xe0 + ((rd.cmode & 8) << 1); penmask = 0xf; penshift = 4 * (~rd.cmode & 1); }
    bool poly_alpha = rd.alpha != 0xff, color_fade = rd.fadefactor != 0xff;
    if (!force_slow_rast) {
        // via veloce: intervallo di x gia' ritagliato, correzione prospettica esatta ogni pspan pixel
        // e interpolazione lineare in mezzo (come Quake). pspan = 1 -> esatto come prima.
        int xs = std::max<int>(x0, std::max<int>(clip_left, 0) - xorg), xe = std::min<int>(x1, std::min<int>(clip_right, T.w) - xorg);
        if (xs >= xe) return;
        const double o = xs - x0; z += dz * o; u += du * o; v += dv * o; i += di * o;
        const int span = pspan();
        double ooz = 1.0 / z; float uL = (float)(u * ooz), vL = (float)(v * ooz), iL = (float)(i * ooz);
        const float ib = rd.intensity_base; const bool dbgrow = oy == dbg_py;
        u32* d = dest + xorg;
        for (int x = xs; x < xe;) {
            const int n = std::min(span, xe - x);
            const double z2 = z + dz * n, u2 = u + du * n, v2 = v + dv * n, i2 = i + di * n, ooz2 = 1.0 / z2;
            const float uR = (float)(u2 * ooz2), vR = (float)(v2 * ooz2), iR = (float)(i2 * ooz2), inv = 1.f / n;
            const float su = (uR - uL) * inv, sv = (vR - vL) * inv, si = (iR - iL) * inv;
#if defined(__powerpc__) || defined(__PPC__)
            // coordinate in virgola fissa (12 bit di frazione) dentro il tratto: niente conversioni float->int per
            // pixel, che sui PowerPC (Wii U) passano dalla memoria e costano decine di cicli
            // (bit di frazione ridotti per coordinate molto grandi, cosi' non si esce dai 32 bit)
            const float um = std::max(std::max(std::fabs(uL), std::fabs(vL)), std::max(std::fabs(uR), std::fabs(vR)));
            const int FB = um < 4.0e5f ? 12 : um < 6.0e6f ? 8 : 4; const float FS = (float)(1 << FB);
            s32 fu = (s32)(uL * FS), fv = (s32)(vL * FS); const s32 fsu = (s32)(su * FS), fsv = (s32)(sv * FS);
            s32 fi = (s32)((ib + iL) * 4096.f); const s32 fsi = (s32)(si * 4096.f);
            for (int k = 0; k < n; k++, fu += fsu, fv += fsv, fi += fsi) {
                u32 tx = (u32)(fu >= 0 ? fu >> FB : -((-fu) >> FB)), ty = (u32)(fv >= 0 ? fv >> FB : -((-fv) >> FB)); u8 pen = 0;
#else
            float uu = uL, vv = vL; s32 fi = (s32)((ib + iL) * 4096.f); const s32 fsi = (s32)(si * 4096.f);   // x86/ARM: conversioni float->int economiche
            for (int k = 0; k < n; k++, uu += su, vv += sv, fi += fsi) {
                u32 tx = (u32)(s32)uu, ty = (u32)(s32)vv; u8 pen = 0;
#endif
                if (rd.stencil && stencil_lookup(tx, ty)) continue;
                u32 rgb = tex_lookup(pal, pens, penshift, penmask, tx, ty + rd.tbase, pen);
                s32 r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
                if (dbgrow && xorg + x + k == dbg_px) printf("SWPX tx=%u ty=%u sh=%d pen=%u rgb=%06x\n", tx, ty, (int)(fi >= 0 ? fi >> 12 : -((-fi) >> 12)), pen, rgb);
                if (rd.shade) { s32 sh = fi >= 0 ? fi >> 12 : 0; sh = sh < 0 ? 0 : sh > 255 ? 255 : sh; r = std::min<s32>((r * sh) >> 6, 255); g = std::min<s32>((g * sh) >> 6, 255); b = std::min<s32>((b * sh) >> 6, 255); }
                if (rd.pfade) { r = (r * rd.pcr) >> 8; g = (g * rd.pcg) >> 8; b = (b * rd.pcb) >> 8; }
                if (color_fade) { r = ((r * rd.fadefactor) + (rd.fcr * rd.fadefactor_inv)) >> 8; g = ((g * rd.fadefactor) + (rd.fcg * rd.fadefactor_inv)) >> 8; b = ((b * rd.fadefactor) + (rd.fcb * rd.fadefactor_inv)) >> 8; }
                const bool use_alpha = poly_alpha && (rd.alpha_enabled || pen == rd.alpha_pen);
                u32& px = d[x + k];
                if (rd.blend || use_alpha) {
                    u32 dd = px; s32 dr = (dd >> 16) & 0xff, dg = (dd >> 8) & 0xff, db = dd & 0xff;
                    if (use_alpha) { r = ((r * rd.alpha) + (dr * rd.alpha_inv)) >> 8; g = ((g * rd.alpha) + (dg * rd.alpha_inv)) >> 8; b = ((b * rd.alpha) + (db * rd.alpha_inv)) >> 8; }
                    else { r = (r + dr) >> 1; g = (g + dg) >> 1; b = (b + db) >> 1; }
                }
                px = T.amark | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
            }
            x += n; z = z2; u = u2; v = v2; i = i2; uL = uR; vL = vR; iL = iR;
        }
        return;
    }
    for (int x = x0; x < x1; x++, z += dz, u += du, v += dv, i += di) {   // via lenta (S23_SLOWRAST): per pixel, per i confronti
        double ooz = 1.0 / z; u32 tx = (u32)(s32)(u * ooz), ty = (u32)(s32)(v * ooz); u8 pen = 0;
        s32 vx = x + xorg; if (vx < clip_left || vx >= clip_right || vx < 0 || vx >= T.w) continue;
        if (rd.stencil && stencil_lookup(tx, ty)) continue;
        ty += rd.tbase;
        u32 rgb = tex_lookup(pal, pens, penshift, penmask, tx, ty, pen);
        s32 r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
        if (rd.shade) { s32 sh = std::clamp<s32>((s32)((double)rd.intensity_base + i * ooz), 0, 255); r = std::min<s32>((r * sh) >> 6, 255); g = std::min<s32>((g * sh) >> 6, 255); b = std::min<s32>((b * sh) >> 6, 255); }
        if (rd.pfade) { r = (r * rd.pcr) >> 8; g = (g * rd.pcg) >> 8; b = (b * rd.pcb) >> 8; }
        if (color_fade) { r = ((r * rd.fadefactor) + (rd.fcr * rd.fadefactor_inv)) >> 8; g = ((g * rd.fadefactor) + (rd.fcg * rd.fadefactor_inv)) >> 8; b = ((b * rd.fadefactor) + (rd.fcb * rd.fadefactor_inv)) >> 8; }
        bool use_alpha = poly_alpha && (rd.alpha_enabled || pen == rd.alpha_pen);
        if (rd.blend || use_alpha) {
            u32 d = dest[vx]; s32 dr = (d >> 16) & 0xff, dg = (d >> 8) & 0xff, db = d & 0xff;
            if (use_alpha) { r = ((r * rd.alpha) + (dr * rd.alpha_inv)) >> 8; g = ((g * rd.alpha) + (dg * rd.alpha_inv)) >> 8; b = ((b * rd.alpha) + (db * rd.alpha_inv)) >> 8; }
            else { r = ((r * 0x80) + (dr * 0x80)) >> 8; g = ((g * 0x80) + (dg * 0x80)) >> 8; b = ((b * 0x80) + (db * 0x80)) >> 8; }
        }
        dest[vx] = T.amark | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
}

static void tri(const RD& rd, const u32* pal, Target& T, const PV& fa, const PV& fb, const PV& fc) {
    struct V { double x, y, p[NA]; };
    const double sc = T.s;
    auto cv = [sc](const PV& q) { V r; r.x = q.x * sc; r.y = q.y * sc; for (int k = 0; k < 4; k++) r.p[k] = q.p[k]; return r; };
    V a = cv(fa), b = cv(fb), c = cv(fc);
    double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (!(std::fabs(area) > 1e-9)) return;
    double dpx[NA], dpy[NA]; const int na = NA;
    for (int k = 0; k < na; k++) {
        dpx[k] = ((b.p[k] - a.p[k]) * (c.y - a.y) - (c.p[k] - a.p[k]) * (b.y - a.y)) / area;
        dpy[k] = ((c.p[k] - a.p[k]) * (b.x - a.x) - (b.p[k] - a.p[k]) * (c.x - a.x)) / area;
    }
    double ymin = std::min({a.y, b.y, c.y}), ymax = std::max({a.y, b.y, c.y});
    const int HH = (int)std::lround(480 * sc), WW = (int)std::lround(640 * sc);
    int ys = (int)std::ceil(std::max(ymin, -1.0) - 0.5), ye = (int)std::ceil(std::min(ymax, HH + 1.0) - 0.5);
    ys = std::max(ys, 0); ye = std::min(ye, HH);
    if (T.by0 > 0 || T.by1 < (1 << 30)) {                         // solo le righe della propria fascia (stesso clip_top di scanline)
        const int ct = (int)std::lround(((240 - rd.vp_sy) - rd.vp_oy) * sc);
        ys = std::max(ys, T.by0 - ct); ye = std::min(ye, T.by1 - ct);
        if (ys >= ye) return;
    }
    const V* e[3][2] = { {&a, &b}, {&b, &c}, {&c, &a} };
    for (int y = ys; y < ye; y++) {
        double yc = y + 0.5, xl = 1e300, xr = -1e300;
        for (int k = 0; k < 3; k++) {
            const V *p = e[k][0], *q = e[k][1];
            if (p->y == q->y) continue;
            double lo = std::min(p->y, q->y), hi = std::max(p->y, q->y);
            if (yc < lo || yc >= hi) continue;
            double x = p->x + (yc - p->y) * (q->x - p->x) / (q->y - p->y);
            xl = std::min(xl, x); xr = std::max(xr, x);
        }
        if (xl > xr) continue;
        const int XL = -T.xoff, XR = WW + T.xoff;                 // in widescreen si disegna anche fuori dal 4:3
        int sx = (int)std::ceil(std::max(xl, XL - 1.0) - 0.5), ex = (int)std::ceil(std::min(xr, XR + 1.0) - 0.5);
        sx = std::max(sx, XL); ex = std::min(ex, XR);
        if (sx >= ex) continue;
        double dx = (sx + 0.5) - a.x, dy = yc - a.y;
        double a0[NA]; for (int k = 0; k < NA; k++) a0[k] = k < na ? a.p[k] + dpx[k] * dx + dpy[k] * dy : 0;
        scanline(rd, pal, T, y, sx, ex, a0, dpx);
    }
}


// un poligono: ventaglio di triangoli
static void draw_poly(const Poly& p, const u32* pal, Target& T) {
    if (p.nv == 4) {
        // quad concavo (es. le punte della stella): se v1 e v3 stanno dallo stesso lato della diagonale
        // 0-2, il ventaglio da v0 lascerebbe un buco; si usa allora la diagonale 1-3.
        auto side = [&](const PV& a, const PV& b, const PV& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
        double s1 = side(p.pv[0], p.pv[2], p.pv[1]), s3 = side(p.pv[0], p.pv[2], p.pv[3]);
        if (s1 * s3 > 0) { tri(p.rd, pal, T, p.pv[1], p.pv[2], p.pv[3]); tri(p.rd, pal, T, p.pv[1], p.pv[3], p.pv[0]); }
        else { tri(p.rd, pal, T, p.pv[0], p.pv[1], p.pv[2]); tri(p.rd, pal, T, p.pv[0], p.pv[2], p.pv[3]); }
    } else
        for (int k = 1; k + 1 < p.nv; k++) tri(p.rd, pal, T, p.pv[0], p.pv[k], p.pv[k + 1]);
}
// disegno in parallelo: la piattaforma fornisce par_run (esegue fn(ctx, 0..n-1) su piu' thread e aspetta la fine)
static void (*par_run)(int n, void (*fn)(void*, int), void* ctx) = nullptr;
static int par_bands = 1;

// mappe d'ombra: una per segmento che contiene modelli illuminati
static int seg_mode = getenv("S23_SEGMODE") ? atoi(getenv("S23_SEGMODE")) : (getenv("S23_NOSEG") ? 0 : 1);   // 0 MAME, 1 segmento 1 sopra, 2 segmenti dall'ultimo al primo
static FILE* segdump = nullptr;           // se non nullo, draw_list scrive la struttura della lista
// costruisce i poligoni del fotogramma (modelli trasformati, illuminati, proiettati) e il loro ordine di disegno.
// Usata dal disegno software (draw_list) e da quello su GPU (platform/gl3d.h).
static void prepare(const std::vector<Entry>& list, Frame& F, std::vector<int>& order) {
    F.polys.reserve(4096);
    if (segdump) fprintf(segdump, "lista: %zu voci, modo segmenti %d\n", list.size(), seg_mode);
    for (const Entry& re : list) {
        size_t first = F.polys.size();
        { static std::vector<int> hide; static bool hi = false; if (!hi) { hi = true; if (const char* h = getenv("S23_HIDEMODEL")) { std::string t = h; size_t i = 0; while (i < t.size()) { hide.push_back((int)strtol(t.c_str() + i, nullptr, 16)); size_t j = t.find(',', i); if (j == std::string::npos) break; i = j + 1; } } }
          if (re.type == MODEL && std::find(hide.begin(), hide.end(), (int)re.model.model) != hide.end()) continue; }
        switch (re.type) { case MODEL: make_model(re, F); break; case DIRECT: make_direct(re, F); break; case IMMEDIATE: { static bool ni = getenv("S23_NOIMM"); if (!ni) make_immediate(re, F); } break; }
        for (size_t k = first; k < F.polys.size(); k++) F.polys[k].seg = re.seg;
        { static const bool ls = getenv("S23_LMSTAT") != nullptr; if (ls && re.type == MODEL) { const s16* m = re.model.m; const s32* l = re.model.lv; double a[3], b[3];
              for (int k = 0; k < 3; k++) { a[k] = (m[3*k] * (double)l[0] + m[3*k+1] * (double)l[1] + m[3*k+2] * (double)l[2]); b[k] = (m[k] * (double)l[0] + m[3+k] * (double)l[1] + m[6+k] * (double)l[2]); }
              double na = std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]), nb = std::sqrt(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);
              printf("LM seg=%d model=%04x lm=%d/%d/%d/%d amb=%d pow=%d tr=%d M*l=%.2f,%.2f,%.2f Mt*l=%.2f,%.2f,%.2f\n", re.seg, re.model.model, g_lmc[0], g_lmc[1], g_lmc[2], g_lmc[3], re.light_ambient, re.light_power, re.model.transpose, a[0]/na, a[1]/na, a[2]/na, b[0]/nb, b[1]/nb, b[2]/nb); } }
        if (segdump) { static const Entry* lastc = nullptr; if (re.type == MODEL && (!lastc || memcmp(lastc->clip, re.clip, sizeof re.clip) != 0 || &re == &list.front())) { lastc = &re; fprintf(segdump, "  CLIP slant=%d:", re.slant); for (int k = 0; k < 24; k++) fprintf(segdump, " %.4g", re.clip[k]); fprintf(segdump, "\n"); } }
        if (segdump) { int zmin = 1 << 30, zmax = -1; for (size_t k = first; k < F.polys.size(); k++) { int z = F.polys[k].zkey & 0x1fffff; zmin = std::min(zmin, z); zmax = std::max(zmax, z); }
            fprintf(segdump, "seg=%d tipo=%d modello=%04x pri=%d poligoni=%zu z=%d..%d tz=%d vp=%d,%d,%d,%d fov=%g tv=%d,%d\n", re.seg, re.type, re.type == MODEL ? re.model.model : 0, F.polys.size() > first ? F.polys[first].zkey >> 21 : -1, F.polys.size() - first, zmin, zmax, re.type == MODEL ? (int)re.model.v[2] : 0, re.vp_sx, re.vp_sy, re.vp_ox, re.vp_oy, re.fov, re.type == MODEL ? (int)re.model.v[0] : 0, re.type == MODEL ? (int)re.model.v[1] : 0); }
        if (F.polys.size() > 40000) break;
    }
    stat_polys += (unsigned)F.polys.size();
    static const bool imstat = getenv("S23_IMSTAT") != nullptr;
    if (imstat) { int ni = 0, nm = 0; for (const Entry& re : list) { ni += re.type == IMMEDIATE; nm += re.type == MODEL; } static int fc = 0; fc++; if (ni) printf("IMS f=%d im=%d models=%d\n", fc, ni, nm); }
    order.resize(F.polys.size()); for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { const Poly &p = F.polys[a], &q = F.polys[b]; // Ogni comando di fine lista chiude un segmento; il cabinato disegna i segmenti dall'ultimo al primo
        // (es. deserto: scenario, poi scritte, poi personaggi; stella: stella, poi scritte sopra). MAME li fonde.
        if (seg_mode == 1 && (p.seg == 1) != (q.seg == 1)) return q.seg == 1;
        if (seg_mode == 3) { auto rk = [](int sg) { return sg == 1 ? 2 : sg == 0 ? 0 : 1; }; int rp = rk(p.seg), rq = rk(q.seg); if (rp != rq) return rp < rq; }
        if (seg_mode == 3 && p.seg != q.seg) return p.seg < q.seg;                 // test: segmenti successivi sopra
        if (seg_mode == 2) { int pp = p.zkey >> 21, qp = q.zkey >> 21; if (pp != qp) return pp > qp; if (p.seg != q.seg) return p.seg > q.seg; }
        return p.zkey != q.zkey ? p.zkey > q.zkey : p.index < q.index; });
}
static std::atomic<uint32_t> t_prep_us{0}; static std::atomic<uint32_t> n_polys{0};   // statistiche per l'indicatore (geometria, poligoni)
static void draw_list(const std::vector<Entry>& list, const u32* pal, u32* fb, bool layer3d, int W = 640, int H = 480, double scale = 1.0, int xoff = 0) {
    if (!R.ok || !layer3d) return;
    struct Tm { uint64_t t0; static uint64_t ns() { return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
        Tm() : t0(ns()) {} ~Tm() { static const bool on = getenv("S23_DRAWTIME") != nullptr; static double acc = 0; static int n = 0; if (!on) return; acc += (ns() - t0) / 1e6; if (++n == 60) { printf("DRAWTIME %.2f ms/fotogramma\n", acc / n); acc = 0; n = 0; } } } tm_;
    Frame F; std::vector<int> order; { const uint64_t p0 = Tm::ns(); prepare(list, F, order); t_prep_us += (uint32_t)((Tm::ns() - p0) / 1000); n_polys = (uint32_t)F.polys.size(); }
    if (dump) { printf("--- DUMP %zu entries %zu polys pal[601]=%06x pal[1501]=%06x pal[6b01]=%06x pal[2b01]=%06x", list.size(), F.polys.size(), pal[0x601], pal[0x1501], pal[0x6b01], pal[0x2b01]); if (!list.empty()) printf(" e0: pf=%02x%02x%02x sf=%02x%02x%02x fac=%02x fl=%02x alpha=%02x pen=%02x vp=%d,%d,%d,%d fov=%g", list[0].pfr, list[0].pfg, list[0].pfb, list[0].sfr, list[0].sfg, list[0].sfb, list[0].sfactor, list[0].fade_flags, list[0].alpha, list[0].alpha_pen, list[0].vp_sx, list[0].vp_sy, list[0].vp_ox, list[0].vp_oy, list[0].fov); printf("\n");
        for (int oi : order) { const Poly& p = F.polys[oi]; float x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; for (int k = 0; k < p.nv; k++) { x0 = std::min(x0, p.pv[k].x); x1 = std::max(x1, p.pv[k].x); y0 = std::min(y0, p.pv[k].y); y1 = std::max(y1, p.pv[k].y); }
            printf("idx=%d z=%06x pri=%d nv=%d box=%.0f,%.0f-%.0f,%.0f h=%08x type=%08x pens=%x a=%d bl=%d\n", p.index, p.zkey & 0x1fffff, p.zkey >> 21, p.nv, x0, y0, x1, y1, p.rd.h, p.rd.type, p.rd.pens, p.rd.alpha_enabled, p.rd.blend);
            if (p.rd.type == 0x34e0 && p.zkey == (0x1fffff | (6 << 21))) { printf("  V"); for (int k = 0; k < p.nv; k++) printf(" (%.0f,%.0f)", p.pv[k].x, p.pv[k].y); printf("\n"); } } }
    Target T; T.fb = fb; T.w = W; T.h = H; T.s = scale; T.xoff = xoff;
    if (par_run && !dump && par_bands > 1) {                       // in parallelo: ogni fascia di righe disegna tutti i poligoni in ordine
        struct Ctx { const Frame* F; const std::vector<int>* order; const u32* pal; Target T; int nb; } cx{&F, &order, pal, T, par_bands};
        par_run(par_bands, [](void* c, int b) {
            Ctx& x = *(Ctx*)c; Target t = x.T; t.by0 = x.T.h * b / x.nb; t.by1 = x.T.h * (b + 1) / x.nb;
            for (int oi : *x.order) {
                const Poly& p = x.F->polys[oi];
                { static int om = getenv("S23_OVER") ? (int)strtol(getenv("S23_OVER"), 0, 0) : 0; int pr = p.zkey >> 21; t.amark = (pr < 16 && (om >> pr & 1)) ? 0xfe000000u : 0xff000000u; }
                draw_poly(p, x.pal, t);
            }
        }, &cx);
        return;
    }
    int dn = 0;
    for (int oi : order) {
        const Poly& p = F.polys[oi];
        { static int om = getenv("S23_OVER") ? (int)strtol(getenv("S23_OVER"), 0, 0) : 0; int pr = p.zkey >> 21; T.amark = (pr < 16 && (om >> pr & 1)) ? 0xfe000000u : 0xff000000u; }
        std::vector<u32> before; if (dump && dn < 60) before.assign(fb, fb + (size_t)W * H);
        { static int px = -1, py = -1; static bool init = false; if (!init) { init = true; if (const char* e = getenv("S23_PROBE")) sscanf(e, "%d,%d", &px, &py); }
          u32 before1 = (px >= 0 && dump) ? fb[(size_t)py * W + px] : 0;
          draw_poly(p, pal, T);
          if (px >= 0 && dump && fb[(size_t)py * W + px] != before1) printf("PROBE idx=%d seg=%d zkey=%06x pri=%d h=%08x type=%08x col=%06x\n", p.index, p.seg, p.zkey & 0x1fffff, p.zkey >> 21, p.rd.h, p.rd.type, fb[(size_t)py * W + px] & 0xffffff); }
        if (dump && dn < 60) { size_t ch = 0; u64 sr = 0, sg = 0, sb = 0; for (int q = 0; q < W * H; q++) if (before[q] != fb[q]) { ch++; sr += (fb[q] >> 16) & 255; sg += (fb[q] >> 8) & 255; sb += fb[q] & 255; } printf("DRAW#%d idx=%d changed=%zu avg=%llu,%llu,%llu h=%08x cmode=%d tbase=%x pens=%x type=%08x z=%x\n", dn, p.index, ch, ch ? sr / ch : 0, ch ? sg / ch : 0, ch ? sb / ch : 0, p.rd.h, p.rd.cmode, p.rd.tbase, p.rd.pens, p.rd.type, p.zkey); dn++; }
    }
}

// debug: indice dell'ultimo poligono (p.index) che scrive il pixel (px, py) nel disegno software; -1 = nessuno
static int probe_owner(const std::vector<Entry>& list, const u32* pal, int W, int H, double scale, int xoff, int px, int py, u32* color = nullptr) {
    Frame F; std::vector<int> order; prepare(list, F, order); dbg_px = px; dbg_py = py;
    std::vector<u32> fb((size_t)W * H, 0); Target T; T.fb = fb.data(); T.w = W; T.h = H; T.s = scale; T.xoff = xoff;
    int owner = -1; const size_t o = (size_t)py * W + px;
    for (int oi : order) { const u32 keep = fb[o]; fb[o] = 0x12345678u; draw_poly(F.polys[oi], pal, T); if (fb[o] != 0x12345678u) { owner = F.polys[oi].index; } else fb[o] = keep; }
    if (color) *color = fb[o]; dbg_px = dbg_py = -1;
    return owner;
}
// ritorna una copia della lista da visualizzare (l'ultima completata)
static void get_display(std::vector<Entry>& out) { MtxGuard lk(mtx); out = disp_list; disp_new = false; }

} // namespace g3d

