// 3D drawing for a fixed-function GPU (Nintendo 3DS PICA200): no pixel shaders, so the
// System 23 8-bit palettised textures can't be read directly like on PC/Switch (gl3d.h).
// It works like this:
//  * the texture space (4096 x N texels, remapped 16x16 tiles, per-polygon palette) is split into cells
//    of CELL x CELL texels; for each cell used by a polygon, with its palette, a texture is prepared (once)
//    RGBA in a cache of "slots" on the GPU;
//  * each polygon is clipped on the CPU against the cells (in linear eye space, so perspective
//    stays exact), against the viewport window (the trapezoid one too) and against the screen;
//  * lighting, per-polygon fade and transparency become vertex colour and two combiner stages:
//      stage 0: rgb = texture * vertex_colour * 4 (light 0..255/64 and polygon fade in the colour)
//                a   = interpolate(vertex.a, constant.a, texture.a)   (texel "alpha pen" -> polygon alpha)
//      stage 1: rgb = interpolate(previous, constant.rgb, constant.a) (fade towards a colour)
//    consecutive polygons with the same state end up in a single draw call.
// This file is platform-independent: the backend (citro3d on 3DS, fixed-function OpenGL for the
// PC test) loads the slots and runs the batches.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace pica {
using g3d::u8; using g3d::u32;

static const int CELL = 128;                       // side of a cell / slot (texels)

struct V { float x, y, z, w; float s, t; uint8_t r, g, b, a; };      // x,y,z already multiplied by w (pixels)

struct Batch {
    int slot = -1;              // GPU texture containing the slot (Backend::tex_of); -1 = none
    int first = 0, count = 0;   // vertices (triangles)
    uint8_t ca = 255;           // constant alpha (stage 0)
    uint8_t mode = 0;           // 0 opaque, 1 alpha, 2 50/50 average with the background
    uint8_t sten = 0;           // 1: texels cut out by the C412 (alpha 0 = discarded)
    uint32_t fade = 0xff000000u;// stage 1: rgb = colour, alpha = weight of the previous (255 = no fade)
    bool same(const Batch& o) const { return slot == o.slot && ca == o.ca && mode == o.mode && sten == o.sten && fade == o.fade; }
};

struct Key {
    int32_t cu, cv; uint32_t pens, tbase; uint8_t cmode, apen, sten, lvl;     // lvl: the cell covers CELL<<lvl texels, one every 1<<lvl
    bool operator==(const Key& o) const { return cu == o.cu && cv == o.cv && pens == o.pens && tbase == o.tbase && cmode == o.cmode && apen == o.apen && sten == o.sten && lvl == o.lvl; }
};
struct KeyHash { size_t operator()(const Key& k) const {
    uint64_t h = 1469598103934665603ull; auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix((uint32_t)k.cu); mix((uint32_t)k.cv); mix(k.pens); mix(k.tbase); mix(k.cmode | (k.apen << 8) | (k.sten << 16) | ((uint32_t)k.lvl << 24)); return (size_t)h; } };

// backend: loads the contents of a slot (CELL x CELL, RGBA5551 like the PICA200 texture format: bits 15-11 red,
// 10-6 green, 5-1 blue, 0 alpha; row-major, row 0 = lowest texel v)
// Slots can live in a few big textures (atlases): this way polygons close together in drawing order that use
// different slots of the same atlas end up in the same draw call.
static inline uint16_t rgba5551(uint32_t rgba) { return (uint16_t)((((rgba >> 27) & 31) << 11) | (((rgba >> 19) & 31) << 6) | (((rgba >> 11) & 31) << 1) | ((rgba & 0xff) >= 128 ? 1 : 0)); }
struct SlotMap { float s0, ds, t0, dt; };          // final coordinates = (s0 + ls * ds, t0 + lt * dt), ls/lt in [0,1] within the cell
struct Backend {
    virtual ~Backend() {}
    virtual void upload_slot(int slot, const uint16_t* rgba5551) = 0;
    virtual bool flip_t() const = 0;               // true: t = 1 at the bottom (PICA200), false: OpenGL
    virtual int tex_of(int slot) const { return slot; }          // GPU texture (atlas) of the slot
    virtual int slots_per_tex() const { return 1; }              // slots per atlas (the slots of an atlas are consecutive)
    virtual SlotMap slot_map(int) const { return flip_t() ? SlotMap{0, 1, 1, -1} : SlotMap{0, 1, 0, 1}; }
};

struct Stats { unsigned polys = 0, tris = 0, batches = 0, new_slots = 0, cells = 0, dropped = 0, reloads = 0, evicted = 0; double ms_tex = 0; };

class Renderer {
public:
    int nslots = 0; int max_lvl = 4; float lod_bias = 0.f;
    uint64_t (*clock_ns)() = nullptr;               // optional: time spent preparing textures (Stats::ms_tex)
    Backend* be = nullptr;
    std::vector<V> verts; std::vector<Batch> batches; Stats st;

    void init(Backend* b, int slots) {
        be = b; nslots = slots; slot_key.assign(slots, Key{}); slot_valid.assign(slots, 0); slot_used.assign(slots, 0); slot_born.assign(slots, 0);
        pal_prev.assign(0x8000, 0xffffffffu); pal_stamp.assign(128, 0); map.clear(); frame = 1; sten_stamp = 0;
    }

    // W x H: target area (pixels), sc: scale relative to 640x480, xoff/yoff: offset (e.g. 40,0 for 320x240 inside 400x240)
    void build(const g3d::Frame& F, const std::vector<int>& order, const uint32_t* pal3d, float sc, float xoff, float yoff, float W, float H) {
        verts.clear(); batches.clear(); st = Stats{}; frame++;
        // changed palettes (in blocks of 256 colours, like the polygons' pens)
        for (int b = 0; b < 128; b++) if (memcmp(&pal_prev[b * 256], &pal3d[b * 256], 256 * 4)) { memcpy(&pal_prev[b * 256], &pal3d[b * 256], 256 * 4); pal_stamp[b] = frame; }
        pal = pal3d;
        bool sten_checked = false; int wk[4] = {-99999, 0, 0, 0}; float wv[6] = {0, 0, 0, 0, 0, 0};
        const int HH = (int)std::lround(480 * sc), WW = (int)std::lround(640 * sc);
        for (int oi : order) {
            const g3d::Poly& p = F.polys[oi]; const g3d::RD& rd = p.rd;
            if (rd.stencil && !sten_checked) { sten_checked = true; check_stencil(); }
            // window (like gl3d::export_polys / gpu3d scanline)
            if (rd.vp_sx != wk[0] || rd.vp_sy != wk[1] || rd.vp_ox != wk[2] || rd.vp_oy != wk[3]) {   // same window as the previous polygon: usual
                wk[0] = rd.vp_sx; wk[1] = rd.vp_sy; wk[2] = rd.vp_ox; wk[3] = rd.vp_oy;
                const float cl = std::lround(((320 - rd.vp_sx) + rd.vp_ox) * sc) + xoff, cr = std::lround(((320 + rd.vp_sx) + rd.vp_ox) * sc) + xoff;
                const float ct = std::lround(((240 - rd.vp_sy) - rd.vp_oy) * sc) + yoff, cb = std::lround(((240 + rd.vp_sy) - rd.vp_oy) * sc) + yoff;
                wv[0] = cl; wv[1] = ct; wv[2] = std::max({cl, xoff, cl}); wv[3] = std::min({cr, xoff + WW, cl + WW, W}); wv[4] = std::max({ct, yoff}); wv[5] = std::min({cb, yoff + HH, ct + HH, H});
            }
            const float xorg = wv[0], ct = wv[1], x0 = wv[2], x1 = wv[3], y0 = wv[4], y1 = wv[5];
            if (x0 >= x1 || y0 >= y1) continue;
            st.polys++;
            // vertices in linear homogeneous eye-space coordinates: (x*Z, y*Z, Z, u, v, i)
            PolyV& poly = tmp_poly; poly.n = std::min(p.nv, 16);
            for (int k = 0; k < p.nv; k++) {
                const g3d::PV& q = p.pv[k]; const float w = q.p[0] != 0 ? q.p[0] : 1e-6f, Z = 1.f / w;
                const float xs = q.x * sc + xorg, ys = q.y * sc + ct;
                poly[k] = HV{{xs * Z, ys * Z, Z, q.p[1] * Z, q.p[2] * Z, q.p[3] * Z + rd.intensity_base}};
            }
            // clipping: window and screen; the trapezoid of slanted windows
            clip(poly, 0, 2, -x0, 1); clip(poly, 0, 2, x1, -1); clip(poly, 1, 2, -y0, 1); clip(poly, 1, 2, y1, -1);
            if (rd.slant) slant_clip(poly, rd, sc, xoff, yoff);
            if (poly.size() < 3) continue;
            // texture cells touched (raw u, v space; tbase is part of the key)
            float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
            for (auto& h : poly) { umin = std::min(umin, h.a[3]); umax = std::max(umax, h.a[3]); vmin = std::min(vmin, h.a[4]); vmax = std::max(vmax, h.a[4]); }
            // level of detail: if a pixel covers many texels (distant or very oblique polygon) the cell takes one
            // texel every 1<<lvl (like an unfiltered mipmap): less memory and fewer cells, almost the same look
            int lvl = 0;
            {   double sa = 0, ta = 0; const size_t n = poly.size(); float sx[48], sy[48];
                for (size_t k = 0; k < n; k++) { sx[k] = poly[k].a[0] / poly[k].a[2]; sy[k] = poly[k].a[1] / poly[k].a[2]; }   // screen x, y: 2 divisions per vertex instead of 8
                for (size_t k = 0; k < n; k++) { const size_t j = k + 1 < n ? k + 1 : 0; const HV& A = poly[k]; const HV& B = poly[j];
                    sa += (double)sx[k] * sy[j] - (double)sx[j] * sy[k]; ta += (double)A.a[3] * B.a[4] - (double)B.a[3] * A.a[4]; }
                sa = std::fabs(sa) * 0.5; ta = std::fabs(ta) * 0.5;
                if (sa > 0.5 && ta > sa * 4) lvl = std::min(max_lvl, (int)std::floor(0.5 * std::log2(ta / sa) + lod_bias));
                if (lvl < 0) lvl = 0; }
            const float CS = (float)(CELL << lvl);
            const int cu0 = (int)std::floor(umin / CS), cu1 = (int)std::floor((umax - 1e-3f) / CS), cv0 = (int)std::floor(vmin / CS), cv1 = (int)std::floor((vmax - 1e-3f) / CS);
            const int ncell = (cu1 - cu0 + 1) * (cv1 - cv0 + 1);
            if (ncell <= 0) continue;
            if (ncell > 64) { st.dropped++; continue; }                      // polygon that repeats the texture a huge number of times
            Batch b0; fill_state(b0, rd);
            for (int cu = cu0; cu <= cu1; cu++) for (int cv = cv0; cv <= cv1; cv++) {
                PolyV& c = tmp_cell; c.copy(poly);
                if (cu0 != cu1) { clip(c, 3, -1, -(float)cu * CS, 1); clip(c, 3, -1, (float)(cu + 1) * CS, -1); }
                if (cv0 != cv1) { clip(c, 4, -1, -(float)cv * CS, 1); clip(c, 4, -1, (float)(cv + 1) * CS, -1); }
                if (c.size() < 3) continue;
                Key k{cu, cv, rd.pens, rd.tbase, rd.cmode, rd.alpha_pen, (uint8_t)(rd.stencil ? 1 : 0), (uint8_t)lvl};
                const int slot = get_slot(k);
                if (slot < 0) { st.dropped++; continue; }
                st.cells++;
                Batch b = b0; b.slot = be->tex_of(slot);
                emit(c, b, be->slot_map(slot), rd, cu, cv, CS);
            }
        }
    }

private:
    struct HV { float a[6]; };                       // x*Z, y*Z, Z, u, v, i (i: absolute light, i.e. already + base)
    // fixed-capacity polygon: no allocations during clipping (malloc is expensive on 3DS)
    struct PolyV {
        HV v[48]; int n = 0;
        size_t size() const { return (size_t)n; } HV& operator[](size_t i) { return v[i]; } const HV& operator[](size_t i) const { return v[i]; }
        void push_back(const HV& h) { if (n < 48) v[n++] = h; } void reserve(size_t) {}
        HV* begin() { return v; } HV* end() { return v + n; } const HV* begin() const { return v; } const HV* end() const { return v + n; }
        void copy(const PolyV& o) { n = o.n; memcpy(v, o.v, sizeof(HV) * n); }
    };
    std::unordered_map<Key, int, KeyHash> map;
    std::vector<Key> slot_key; std::vector<uint8_t> slot_valid; std::vector<uint32_t> slot_used, slot_born;
    std::vector<uint32_t> pal_prev, pal_stamp; uint32_t frame = 1, sten_stamp = 0; std::vector<uint16_t> sten_prev;
    const uint32_t* pal = nullptr;
    uint16_t texbuf[CELL * CELL];

    // keeps the part where  s*a[i] + d*a[j] (+ off if j<0) >= 0 ; j = 2 (Z) for the screen planes
    static void clip(PolyV& p, int i, int j, float d, float s) {
        if (p.size() < 3) return;
        auto f = [&](const HV& h) { return s * h.a[i] + (j >= 0 ? d * h.a[j] : d); };
        {   bool in = true; for (int k = 0; k < p.n && in; k++) in = f(p.v[k]) >= 0;          // usual case: nothing to cut
            if (in) return; }
        PolyV o;
        for (size_t k = 0; k < p.size(); k++) {
            const HV& A = p[k]; const HV& B = p[k + 1 < p.size() ? k + 1 : 0];
            const float fa = f(A), fb = f(B);
            if (fa >= 0) o.push_back(A);
            if ((fa >= 0) != (fb >= 0)) { const float t = fa / (fa - fb); HV C; for (int q = 0; q < 6; q++) C.a[q] = A.a[q] + t * (B.a[q] - A.a[q]); o.push_back(C); }
        }
        p.copy(o);
    }
    // trapezoid window: limit x = K + M * y (like gl3d::export_polys, converted to homogeneous coordinates)
    static void slant_clip(PolyV& p, const g3d::RD& rd, float sc, float xoff, float yoff) {
        const double cx = 320 + rd.vp_ox;
        for (int k = 0; k < 2; k++) {
            const double a = rd.cpl[k][0], b = rd.cpl[k][1], c = rd.cpl[k][2]; if (std::fabs(a) < 1e-9) continue;
            const float M = (float)(b / a), K = (float)(sc * cx + xoff - (sc * (b * (240 - rd.vp_oy) + c * rd.cfov) - b * 0.5) / a) - M * yoff;
            const bool left = (a > 0) == (c > 0);
            // left:  x - K - M*y >= 0   ->  xZ - M yZ - K Z >= 0 ; right: K + M*y - x >= 0
            auto f = [&](const HV& h) { float v = h.a[0] - M * h.a[1] - K * h.a[2]; return left ? v : -v; };
            PolyV o;
            for (size_t q = 0; q < p.size(); q++) {
                const HV& A = p[q]; const HV& B = p[q + 1 < p.size() ? q + 1 : 0]; const float fa = f(A), fb = f(B);
                if (fa >= 0) o.push_back(A);
                if ((fa >= 0) != (fb >= 0)) { const float t = fa / (fa - fb); HV C; for (int z = 0; z < 6; z++) C.a[z] = A.a[z] + t * (B.a[z] - A.a[z]); o.push_back(C); }
            }
            p.copy(o); if (p.size() < 3) return;
        }
    }
    void fill_state(Batch& b, const g3d::RD& rd) {
        const bool palpha = rd.alpha != 0xff;
        b.ca = palpha ? (uint8_t)std::min<int>(255, rd.alpha) : 255;
        b.mode = palpha ? 1 : rd.blend ? 2 : 0;
        b.sten = rd.stencil ? 1 : 0;
        if (rd.fadefactor != 0xff) {                                     // r = (r * ff + fc * ffi) >> 8
            const float a = rd.fadefactor / 256.f, k = rd.fadefactor_inv / 256.f, den = std::max(1e-3f, 1.f - a);
            auto cc = [&](int c) { return (uint32_t)std::min(255.f, c * k / den + 0.5f); };
            b.fade = cc(rd.fcr) << 24 | cc(rd.fcg) << 16 | cc(rd.fcb) << 8 | (uint32_t)std::min<int>(255, rd.fadefactor);
        } else b.fade = 0x000000ffu;
        cur_alpha_all = palpha && rd.alpha_enabled;
    }
    bool cur_alpha_all = false;
    PolyV tmp_poly, tmp_cell;
    void emit(const PolyV& c, Batch b, const SlotMap& m, const g3d::RD& rd, int cu, int cv, float CS) {
        if (batches.empty() || !batches.back().same(b)) { b.first = (int)verts.size(); b.count = 0; batches.push_back(b); st.batches++; }
        // half a texel of margin at the cell edges: with "nearest" sampling the neighbouring cell is never read
        // in the atlas (the cell is stretched by 1/CELL, invisible)
        const float in0 = 0.5f / CELL, ink = (CELL - 1.f) / CELL;
        auto mk = [&](const HV& h) {
            V v; const float Z = h.a[2];
            v.x = h.a[0]; v.y = h.a[1]; v.z = 0.5f * Z; v.w = Z;
            const float ls = in0 + ink * std::min(1.f, std::max(0.f, (h.a[3] - (float)cu * CS) / CS));
            const float lt = in0 + ink * std::min(1.f, std::max(0.f, (h.a[4] - (float)cv * CS) / CS));
            v.s = m.s0 + ls * m.ds; v.t = m.t0 + lt * m.dt;
            float sh = rd.shade ? std::min(255.f, std::max(0.f, h.a[5])) : 64.f;     // light: colour * sh / 64
            auto ch = [&](int pc) { float x = sh * (rd.pfade ? pc / 256.f : 1.f); return (uint8_t)std::min(255.f, x + 0.5f); };
            v.r = ch(rd.pcr); v.g = ch(rd.pcg); v.b = ch(rd.pcb); v.a = cur_alpha_all ? b.ca : 255;   // see the combiner
            return v;
        };
        // every vertex converted once, then the fan written straight into the array
        const int nv = (int)c.size(); V vv[48]; for (int k = 0; k < nv; k++) vv[k] = mk(c[k]);
        const size_t base = verts.size(), nt = (size_t)(nv - 2); verts.resize(base + 3 * nt); V* o = verts.data() + base;
        for (int k = 1; k + 1 < nv; k++) { *o++ = vv[0]; *o++ = vv[k]; *o++ = vv[k + 1]; }
        batches.back().count += (int)(3 * nt); st.tris += (unsigned)nt;
    }
    void check_stencil() {
        const uint16_t* s = g3d::C.c412_sram; const size_t n = sizeof g3d::C.c412_sram / sizeof(uint16_t);
        if (sten_prev.size() != n || memcmp(sten_prev.data(), s, n * 2)) { sten_prev.assign(s, s + n); sten_stamp = frame; }
    }
    int get_slot(const Key& k) {
        const int pb = (int)((k.pens >> 8) & 0x7f);
        auto it = map.find(k);
        if (it != map.end()) {
            const int s = it->second;
            if (slot_born[s] >= pal_stamp[pb] && (!k.sten || slot_born[s] >= sten_stamp)) { slot_used[s] = frame; return s; }
            fill(s, k); slot_born[s] = frame; slot_used[s] = frame; st.new_slots++; st.reloads++; return s;   // palette changed: reload
        }
        int best = -1; uint32_t bu = 0xffffffffu;                                                 // free slot or least recently used
        // preference: the same atlas as the previous batch (if it has a free slot or one unused for at least 2 frames),
        // so cells drawn one after the other tend to be in the same texture and the batches merge
        const int per = be->slots_per_tex();
        if (per > 1 && !batches.empty() && batches.back().slot >= 0) {
            const int a0 = batches.back().slot * per, a1 = std::min(nslots, a0 + per);
            for (int s = a0; s < a1; s++) { if (!slot_valid[s]) { best = s; break; } if (slot_used[s] + 1 < frame && slot_used[s] < bu) { bu = slot_used[s]; best = s; } }
            bu = 0xffffffffu;
        }
        if (best < 0) for (int s = 0; s < nslots; s++) { if (!slot_valid[s]) { best = s; break; } if (slot_used[s] != frame && slot_used[s] < bu) { bu = slot_used[s]; best = s; } }
        if (best < 0) return -1;
        if (slot_valid[best]) { map.erase(slot_key[best]); st.evicted++; }
        slot_key[best] = k; slot_valid[best] = 1; map[k] = best;
        fill(best, k); slot_born[best] = frame; slot_used[best] = frame; st.new_slots++;
        return best;
    }
    // the cell's texels: a row of a 16x16 tile of the texture space comes from 16 consecutive (or, for rotated tiles,
    // 16-apart) bytes of the texture ROM, so the tilemap is read once per tile row and the palette once per cell
    // (256-entry table already in the GPU format). Same result as g3d::tex_lookup texel by texel.
    void fill(int s, const Key& k) {
        const uint64_t t0 = clock_ns ? clock_ns() : 0;
        fill_(s, k);
        if (clock_ns) st.ms_tex += (clock_ns() - t0) / 1e6;
    }
    void fill_(int s, const Key& k) {
        u32 pens = k.pens; int penmask = 0xff, penshift = 0;
        if (k.cmode & 4) { pens += 0xec + ((k.cmode & 8) << 1); penmask = 3; penshift = 2 * (~k.cmode & 3); }
        else if (k.cmode & 2) { pens += 0xe0 + ((k.cmode & 8) << 1); penmask = 0xf; penshift = 4 * (~k.cmode & 1); }
        uint16_t lut[256];
        for (int p = 0; p < 256; p++) {
            const u32 rgb = pal[(pens + ((p >> penshift) & penmask)) & 0x7fff];
            lut[p] = rgba5551((rgb << 8) | ((!k.sten && p == k.apen) ? 0u : 255u));      // "alpha pen": takes the polygon's alpha
        }
        const g3d::Rom& R = g3d::R; const u8* rom = R.texrom; const u32 tidm = R.tileid_mask;
        const int st = 1 << k.lvl, run = std::max(1, 16 >> k.lvl); const int32_t bu = k.cu * (CELL << k.lvl), bv = k.cv * (CELL << k.lvl);
        for (int y = 0; y < CELL; y++) {
            const u32 ty = (u32)(bv + y * st), v = ty + k.tbase; uint16_t* o = texbuf + y * CELL;
            for (int x = 0; x < CELL; x += run) {
                const u32 u = (u32)(bu + x * st), tileid = ((u >> 4) & 0xff) | ((v << 4) & tidm);
                const u32 tile = R.tm_decoded[tileid], attr = R.tm_attr[tileid], vv = (attr & 1) ? ~v : v;
                const bool fu = (attr & 2) != 0, sw = (attr & 4) != 0;
                // u is a multiple of 16: within the tile row the texel j is at 16ths (rotated tile) or units, maybe mirrored
                const u32 base = (sw ? (tile | (vv & 15)) : (tile | ((vv << 4) & 0xf0))) & 0x1ffffff;
                const int unit = sw ? 16 : 1, step = (fu ? -st : st) * unit;
                const u8* src = rom + base + (fu ? 15 * unit : 0); uint16_t* d = o + x;
                if (!k.sten) { for (int j = 0; j < run; j++) d[j] = lut[src[j * step]]; }
                else for (int j = 0; j < run; j++) { uint16_t c = lut[src[j * step]]; if (g3d::stencil_lookup(u + j * st, ty)) c &= 0xfffe; d[j] = c; }
            }
        }
        be->upload_slot(s, texbuf);
    }
};

// fits the final gamma table (C404) to a line g(x) = k*x + b per channel (on the GPU it is applied with blending)
// The line goes exactly through g(0): black stays black (a free least-squares line over a curved gamma table
// had an offset > 0, so black became grey); the slope is the best one for the other values.
inline void gamma_fit(const uint8_t* g, float& k, float& b) {
    double sxx = 0, sxy = 0; const double g0 = g[0];
    for (int x = 1; x < 256; x++) { sxx += (double)x * x; sxy += (double)x * (g[x] - g0); }
    k = (float)(sxy / sxx); b = (float)(g0 / 255.0);
}

}  // namespace pica
