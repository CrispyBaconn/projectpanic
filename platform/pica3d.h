// Disegno del 3D per GPU a funzioni fisse (PICA200 del Nintendo 3DS): niente shader per i pixel, quindi la
// texture a 8 bit con palette del System 23 non si puo' leggere direttamente come su PC/Switch (gl3d.h).
// Si fa cosi':
//  * lo spazio delle texture (4096 x N texel, a tile 16x16 rimappate, palette per poligono) viene diviso in celle
//    da CELL x CELL texel; per ogni cella usata da un poligono, con la sua palette, si prepara (una volta) una texture
//    RGBA in una cache di "slot" sulla GPU;
//  * ogni poligono viene ritagliato sulla CPU contro le celle (in spazio lineare dell'occhio, quindi la prospettiva
//    resta esatta), contro la finestra della viewport (anche quella a trapezio) e contro lo schermo;
//  * luce, dissolvenza per poligono e trasparenza diventano colore dei vertici e due stadi del combinatore:
//      stadio 0: rgb = texture * colore_vertice * 4 (luce 0..255/64 e dissolvenza del poligono nel colore)
//                a   = interpolate(vertice.a, costante.a, texture.a)   (texel "alpha pen" -> alpha del poligono)
//      stadio 1: rgb = interpolate(precedente, costante.rgb, costante.a) (dissolvenza verso un colore)
//    poligoni consecutivi con lo stesso stato finiscono in un'unica chiamata di disegno.
// Questo file non dipende dalla piattaforma: il backend (citro3d sul 3DS, OpenGL a funzioni fisse per il banco di
// prova su PC) carica gli slot ed esegue i lotti.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace pica {
using g3d::u8; using g3d::u32;

static const int CELL = 128;                       // lato di una cella / slot (texel)

struct V { float x, y, z, w; float s, t; uint8_t r, g, b, a; };      // x,y,z gia' moltiplicati per w (pixel)

struct Batch {
    int slot = -1;              // texture della GPU che contiene lo slot (Backend::tex_of); -1 = nessuna
    int first = 0, count = 0;   // vertici (triangoli)
    uint8_t ca = 255;           // alpha costante (stadio 0)
    uint8_t mode = 0;           // 0 opaco, 1 alpha, 2 media 50/50 con lo sfondo
    uint8_t sten = 0;           // 1: texel ritagliati dalla C412 (alpha 0 = scartato)
    uint32_t fade = 0xff000000u;// stadio 1: rgb = colore, alpha = peso del precedente (255 = nessuna dissolvenza)
    bool same(const Batch& o) const { return slot == o.slot && ca == o.ca && mode == o.mode && sten == o.sten && fade == o.fade; }
};

struct Key {
    int32_t cu, cv; uint32_t pens, tbase; uint8_t cmode, apen, sten, lvl;     // lvl: la cella copre CELL<<lvl texel, uno ogni 1<<lvl
    bool operator==(const Key& o) const { return cu == o.cu && cv == o.cv && pens == o.pens && tbase == o.tbase && cmode == o.cmode && apen == o.apen && sten == o.sten && lvl == o.lvl; }
};
struct KeyHash { size_t operator()(const Key& k) const {
    uint64_t h = 1469598103934665603ull; auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix((uint32_t)k.cu); mix((uint32_t)k.cv); mix(k.pens); mix(k.tbase); mix(k.cmode | (k.apen << 8) | (k.sten << 16) | ((uint32_t)k.lvl << 24)); return (size_t)h; } };

// backend: carica il contenuto di uno slot (CELL x CELL, RGBA 8 bit per canale in un uint32 0xRRGGBBAA, riga 0 =
// texel v piu' basso) e del livello del testo (tw x th, stesso formato)
// Gli slot possono stare in poche texture grandi (atlanti): cosi' poligoni vicini nell'ordine di disegno che usano
// slot diversi dello stesso atlante finiscono nella stessa chiamata di disegno.
struct SlotMap { float s0, ds, t0, dt; };          // coordinate finali = (s0 + ls * ds, t0 + lt * dt), ls/lt in [0,1] nella cella
struct Backend {
    virtual ~Backend() {}
    virtual void upload_slot(int slot, const uint32_t* rgba) = 0;
    virtual bool flip_t() const = 0;               // true: t = 1 in basso (PICA200), false: OpenGL
    virtual int tex_of(int slot) const { return slot; }          // texture della GPU (atlante) dello slot
    virtual int slots_per_tex() const { return 1; }              // slot per atlante (gli slot di un atlante sono consecutivi)
    virtual SlotMap slot_map(int) const { return flip_t() ? SlotMap{0, 1, 1, -1} : SlotMap{0, 1, 0, 1}; }
};

struct Stats { unsigned polys = 0, tris = 0, batches = 0, new_slots = 0, cells = 0, dropped = 0; };

class Renderer {
public:
    int nslots = 0; int max_lvl = 4; float lod_bias = 0.f;
    Backend* be = nullptr;
    std::vector<V> verts; std::vector<Batch> batches; Stats st;

    void init(Backend* b, int slots) {
        be = b; nslots = slots; slot_key.assign(slots, Key{}); slot_valid.assign(slots, 0); slot_used.assign(slots, 0); slot_born.assign(slots, 0);
        pal_prev.assign(0x8000, 0xffffffffu); pal_stamp.assign(128, 0); map.clear(); frame = 1; sten_stamp = 0;
    }

    // W x H: area di destinazione (pixel), sc: scala rispetto a 640x480, xoff/yoff: spostamento (es. 40,0 per 320x240 dentro 400x240)
    void build(const g3d::Frame& F, const std::vector<int>& order, const uint32_t* pal3d, float sc, float xoff, float yoff, float W, float H) {
        verts.clear(); batches.clear(); st = Stats{}; frame++;
        // palette cambiate (a blocchi di 256 colori, come i pens dei poligoni)
        for (int b = 0; b < 128; b++) if (memcmp(&pal_prev[b * 256], &pal3d[b * 256], 256 * 4)) { memcpy(&pal_prev[b * 256], &pal3d[b * 256], 256 * 4); pal_stamp[b] = frame; }
        pal = pal3d;
        bool sten_checked = false;
        const int HH = (int)std::lround(480 * sc), WW = (int)std::lround(640 * sc);
        for (int oi : order) {
            const g3d::Poly& p = F.polys[oi]; const g3d::RD& rd = p.rd;
            if (rd.stencil && !sten_checked) { sten_checked = true; check_stencil(); }
            // finestra (come gl3d::export_polys / gpu3d scanline)
            const float cl = std::lround(((320 - rd.vp_sx) + rd.vp_ox) * sc) + xoff, cr = std::lround(((320 + rd.vp_sx) + rd.vp_ox) * sc) + xoff;
            const float ct = std::lround(((240 - rd.vp_sy) - rd.vp_oy) * sc) + yoff, cb = std::lround(((240 + rd.vp_sy) - rd.vp_oy) * sc) + yoff;
            const float xorg = cl, x0 = std::max({cl, xoff, xorg}), x1 = std::min({cr, xoff + WW, xorg + WW, W}), y0 = std::max({ct, yoff}), y1 = std::min({cb, yoff + HH, ct + HH, H});
            if (x0 >= x1 || y0 >= y1) continue;
            st.polys++;
            // vertici in coordinate omogenee lineari nello spazio dell'occhio: (x*Z, y*Z, Z, u, v, i)
            PolyV& poly = tmp_poly; poly.n = std::min(p.nv, 16);
            for (int k = 0; k < p.nv; k++) {
                const g3d::PV& q = p.pv[k]; const float w = q.p[0] != 0 ? q.p[0] : 1e-6f, Z = 1.f / w;
                const float xs = q.x * sc + xorg, ys = q.y * sc + ct;
                poly[k] = HV{{xs * Z, ys * Z, Z, q.p[1] * Z, q.p[2] * Z, q.p[3] * Z + rd.intensity_base}};
            }
            // ritaglio: finestra e schermo; il trapezio delle finestre inclinate
            clip(poly, 0, 2, -x0, 1); clip(poly, 0, 2, x1, -1); clip(poly, 1, 2, -y0, 1); clip(poly, 1, 2, y1, -1);
            if (rd.slant) slant_clip(poly, rd, sc, xoff, yoff);
            if (poly.size() < 3) continue;
            // celle di texture toccate (spazio u, v grezzo; la tbase entra nella chiave)
            float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
            for (auto& h : poly) { umin = std::min(umin, h.a[3]); umax = std::max(umax, h.a[3]); vmin = std::min(vmin, h.a[4]); vmax = std::max(vmax, h.a[4]); }
            // livello di dettaglio: se un pixel copre molti texel (poligono lontano o molto obliquo) la cella prende un
            // texel ogni 1<<lvl (come una mipmap senza filtro): meno memoria e meno celle, aspetto quasi uguale
            int lvl = 0;
            {   double sa = 0, ta = 0; const size_t n = poly.size();
                for (size_t k = 0; k < n; k++) { const HV& A = poly[k]; const HV& B = poly[(k + 1) % n];
                    sa += (double)(A.a[0] / A.a[2]) * (B.a[1] / B.a[2]) - (double)(B.a[0] / B.a[2]) * (A.a[1] / A.a[2]); ta += (double)A.a[3] * B.a[4] - (double)B.a[3] * A.a[4]; }
                sa = std::fabs(sa) * 0.5; ta = std::fabs(ta) * 0.5;
                if (sa > 0.5 && ta > sa * 4) lvl = std::min(max_lvl, (int)std::floor(0.5 * std::log2(ta / sa) + lod_bias));
                if (lvl < 0) lvl = 0; }
            const float CS = (float)(CELL << lvl);
            const int cu0 = (int)std::floor(umin / CS), cu1 = (int)std::floor((umax - 1e-3f) / CS), cv0 = (int)std::floor(vmin / CS), cv1 = (int)std::floor((vmax - 1e-3f) / CS);
            const int ncell = (cu1 - cu0 + 1) * (cv1 - cv0 + 1);
            if (ncell <= 0) continue;
            if (ncell > 64) { st.dropped++; continue; }                      // poligono che ripete la texture moltissime volte
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
    struct HV { float a[6]; };                       // x*Z, y*Z, Z, u, v, i (i: luce assoluta, cioe' gia' + base)
    // poligono a capacita' fissa: niente allocazioni durante il ritaglio (sul 3DS malloc costa)
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
    uint32_t texbuf[CELL * CELL];

    // tiene la parte dove  s*a[i] + d*a[j] (+ off se j<0) >= 0 ; j = 2 (Z) per i piani dello schermo
    static void clip(PolyV& p, int i, int j, float d, float s) {
        if (p.size() < 3) return;
        auto f = [&](const HV& h) { return s * h.a[i] + (j >= 0 ? d * h.a[j] : d); };
        PolyV o;
        for (size_t k = 0; k < p.size(); k++) {
            const HV& A = p[k]; const HV& B = p[(k + 1) % p.size()];
            const float fa = f(A), fb = f(B);
            if (fa >= 0) o.push_back(A);
            if ((fa >= 0) != (fb >= 0)) { const float t = fa / (fa - fb); HV C; for (int q = 0; q < 6; q++) C.a[q] = A.a[q] + t * (B.a[q] - A.a[q]); o.push_back(C); }
        }
        p.copy(o);
    }
    // finestra a trapezio: x limite = K + M * y (come gl3d::export_polys, convertito in coordinate omogenee)
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
                const HV& A = p[q]; const HV& B = p[(q + 1) % p.size()]; const float fa = f(A), fb = f(B);
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
        // mezzo texel di margine sui bordi della cella: con il campionamento "nearest" non si legge mai la cella accanto
        // nell'atlante (la cella viene stirata di 1/CELL, invisibile)
        const float in0 = 0.5f / CELL, ink = (CELL - 1.f) / CELL;
        auto mk = [&](const HV& h) {
            V v; const float Z = h.a[2];
            v.x = h.a[0]; v.y = h.a[1]; v.z = 0.5f * Z; v.w = Z;
            const float ls = in0 + ink * std::min(1.f, std::max(0.f, (h.a[3] - (float)cu * CS) / CS));
            const float lt = in0 + ink * std::min(1.f, std::max(0.f, (h.a[4] - (float)cv * CS) / CS));
            v.s = m.s0 + ls * m.ds; v.t = m.t0 + lt * m.dt;
            float sh = rd.shade ? std::min(255.f, std::max(0.f, h.a[5])) : 64.f;     // luce: colore * sh / 64
            auto ch = [&](int pc) { float x = sh * (rd.pfade ? pc / 256.f : 1.f); return (uint8_t)std::min(255.f, x + 0.5f); };
            v.r = ch(rd.pcr); v.g = ch(rd.pcg); v.b = ch(rd.pcb); v.a = cur_alpha_all ? b.ca : 255;   // vedi il combinatore
            return v;
        };
        const V v0 = mk(c[0]);
        for (size_t k = 1; k + 1 < c.size(); k++) { verts.push_back(v0); verts.push_back(mk(c[k])); verts.push_back(mk(c[k + 1])); batches.back().count += 3; st.tris++; }
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
            fill(s, k); slot_born[s] = frame; slot_used[s] = frame; st.new_slots++; return s;   // palette cambiata: si ricarica
        }
        int best = -1; uint32_t bu = 0xffffffffu;                                                 // slot libero o usato meno di recente
        // preferenza: lo stesso atlante del lotto precedente (se ha uno slot libero o non usato da almeno 2 fotogrammi),
        // cosi' le celle disegnate una dopo l'altra tendono a stare nella stessa texture e i lotti si uniscono
        const int per = be->slots_per_tex();
        if (per > 1 && !batches.empty() && batches.back().slot >= 0) {
            const int a0 = batches.back().slot * per, a1 = std::min(nslots, a0 + per);
            for (int s = a0; s < a1; s++) { if (!slot_valid[s]) { best = s; break; } if (slot_used[s] + 1 < frame && slot_used[s] < bu) { bu = slot_used[s]; best = s; } }
            bu = 0xffffffffu;
        }
        if (best < 0) for (int s = 0; s < nslots; s++) { if (!slot_valid[s]) { best = s; break; } if (slot_used[s] != frame && slot_used[s] < bu) { bu = slot_used[s]; best = s; } }
        if (best < 0) return -1;
        if (slot_valid[best]) map.erase(slot_key[best]);
        slot_key[best] = k; slot_valid[best] = 1; map[k] = best;
        fill(best, k); slot_born[best] = frame; slot_used[best] = frame; st.new_slots++;
        return best;
    }
    void fill(int s, const Key& k) {
        u32 pens = k.pens; int penmask = 0xff, penshift = 0;
        if (k.cmode & 4) { pens += 0xec + ((k.cmode & 8) << 1); penmask = 3; penshift = 2 * (~k.cmode & 3); }
        else if (k.cmode & 2) { pens += 0xe0 + ((k.cmode & 8) << 1); penmask = 0xf; penshift = 4 * (~k.cmode & 1); }
        const int st = 1 << k.lvl; const int32_t bu = k.cu * (CELL << k.lvl), bv = k.cv * (CELL << k.lvl);
        for (int y = 0; y < CELL; y++) {
            const u32 ty = (u32)(bv + y * st);
            for (int x = 0; x < CELL; x++) {
                const u32 tx = (u32)(bu + x * st); u8 pen = 0;
                uint32_t a = 255;
                if (k.sten && g3d::stencil_lookup(tx, ty)) a = 0;
                const u32 rgb = g3d::tex_lookup(pal, pens, penshift, penmask, tx, ty + k.tbase, pen);
                if (!k.sten && pen == k.apen) a = 0;                     // "alpha pen": prende l'alpha del poligono
                texbuf[y * CELL + x] = (rgb << 8) | a;
            }
        }
        be->upload_slot(s, texbuf);
    }
};

// adatta la tabella gamma finale (C404) a una retta g(x) = k*x + b per canale (sulla GPU si applica con il blending)
inline void gamma_fit(const uint8_t* g, float& k, float& b) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0; for (int x = 0; x < 256; x++) { sx += x; sy += g[x]; sxx += (double)x * x; sxy += (double)x * g[x]; }
    const double n = 256, d = n * sxx - sx * sx; k = (float)((n * sxy - sx * sy) / d); b = (float)((sy - k * sx) / n) / 255.f;
}

}  // namespace pica
