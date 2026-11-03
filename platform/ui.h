// Menu (launcher) graphics: text with real fonts (Bangers / Lilita One, black outline and gradient), shapes
// (slanted buttons, rounded boxes, switches), optional images ui/background.png and ui/logo.png.
// Everything with SDL_Renderer + SDL_RenderGeometry: works with any renderer (OpenGL, GLES, software, Wii U).
#pragma once
#ifdef USE_SDL
#include <SDL.h>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "ui_font.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ui {

inline SDL_Color rgba(int r, int g, int b, int a = 255) { return SDL_Color{(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a}; }
inline SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return rgba((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t), (int)(a.a + (b.a - a.a) * t));
}
inline SDL_Color alpha(SDL_Color c, float k) { c.a = (Uint8)std::max(0.f, std::min(255.f, c.a * k)); return c; }

// palette (from the cabinet: blue sea, yellow->red letters with black outline, multicoloured zig-zag band)
static const SDL_Color INK = {12, 10, 20, 255}, WHITE = {255, 255, 255, 255}, YEL = {255, 228, 40, 255}, ORA = {255, 140, 20, 255},
                       RED = {232, 32, 30, 255}, SEA0 = {40, 120, 205, 255}, SEA1 = {10, 40, 100, 255}, NAVY = {12, 28, 66, 255},
                       CYAN = {120, 220, 255, 255}, GREEN = {30, 170, 80, 255}, PINK = {255, 80, 160, 255}, GREY = {170, 185, 210, 255};

struct FontTex {
    const uifont::Font* f = nullptr; SDL_Texture* tex = nullptr; int tw = 0, th = 0;
    short lat[256]; std::vector<std::pair<uint32_t, int>> ext; float cap_top = 0, cap_bot = 0;
    int find(uint32_t cp) const {
        if (cp < 256) return lat[cp];
        for (auto& e : ext) if (e.first == cp) return e.second;
        if (cp == 0x2018 || cp == 0x2019) return lat['\''];
        return -1;
    }
};

struct Image { SDL_Texture* tex = nullptr; int w = 0, h = 0; };

struct Ui {
    SDL_Renderer* ren = nullptr;
    FontTex disp, body;
    SDL_Texture* chev = nullptr;                    // zig-zag band (selected button)
    Image bg, logo;
    float W = 1280, H = 720;                        // current logical area
    std::vector<SDL_Vertex> vb; std::vector<int> ib;

    // ------------------------------------------------------------ loading
    static SDL_Texture* tex_rgba(SDL_Renderer* r, const unsigned char* px, int w, int h) {
        SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);   // R,G,B,A bytes (also big endian)
        if (!t) return nullptr;
        SDL_UpdateTexture(t, nullptr, px, w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        return t;
    }
    bool load_font(FontTex& ft, const uifont::Font& f) {
        ft.f = &f; int w, h, n;
        unsigned char* g = stbi_load_from_memory(f.png, (int)f.png_size, &w, &h, &n, 1);
        if (!g) return false;
        std::vector<unsigned char> px((size_t)w * h * 4);
        for (size_t i = 0; i < (size_t)w * h; i++) { px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255; px[i * 4 + 3] = g[i]; }
        stbi_image_free(g);
        ft.tex = tex_rgba(ren, px.data(), w, h); ft.tw = w; ft.th = h;
        for (short& v : ft.lat) v = -1;
        for (int i = 0; i < f.n; i++) { if (f.g[i].cp < 256) ft.lat[f.g[i].cp] = (short)i; else ft.ext.push_back({f.g[i].cp, i}); }
        int H_ = ft.find('H');
        if (H_ >= 0) { const auto& q = f.g[H_]; ft.cap_top = q.oy + f.R + 1; ft.cap_bot = q.oy + q.h - f.R - 1; }
        return ft.tex != nullptr;
    }
    Image load_png(const std::string& path) {
        Image im; int w, h, n;
        unsigned char* p = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!p) return im;
        std::vector<unsigned char> cur(p, p + (size_t)w * h * 4); stbi_image_free(p);
        int lim = 2048;                             // safe GLES2 texture limit (and less memory on consoles)
        while (w > lim || h > lim) {                // halves with a 2x2 average
            int nw = w / 2, nh = h / 2; std::vector<unsigned char> o((size_t)nw * nh * 4);
            for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) for (int c = 0; c < 4; c++)
                o[((size_t)y * nw + x) * 4 + c] = (unsigned char)((cur[((size_t)(2 * y) * w + 2 * x) * 4 + c] + cur[((size_t)(2 * y) * w + 2 * x + 1) * 4 + c] +
                                                                   cur[((size_t)(2 * y + 1) * w + 2 * x) * 4 + c] + cur[((size_t)(2 * y + 1) * w + 2 * x + 1) * 4 + c] + 2) / 4);
            cur.swap(o); w = nw; h = nh;
        }
        im.tex = tex_rgba(ren, cur.data(), w, h); im.w = w; im.h = h;
        return im;
    }
    void make_chevrons() {
        const int w = 512, h = 128; std::vector<unsigned char> px((size_t)w * h * 4);
        static const SDL_Color C[5] = {RED, YEL, GREEN, PINK, {40, 110, 230, 255}};
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int zx = std::abs((x % 48) - 24);                       // zig-zag
            int band = ((y + zx) / 14) % 5; SDL_Color c = C[band];
            unsigned char* o = &px[((size_t)y * w + x) * 4]; o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255;
        }
        chev = tex_rgba(ren, px.data(), w, h);
    }
    void init(SDL_Renderer* r, const std::string& uidir) {
        ren = r;
        load_font(disp, uifont::display); load_font(body, uifont::body); make_chevrons();
        bg = load_png(uidir + "/background.png"); logo = load_png(uidir + "/logo.png");
        printf("menu: background %s, logo %s (folder %s)\n", bg.tex ? "yes" : "no", logo.tex ? "yes" : "no", uidir.c_str());
    }

    // ------------------------------------------------------------ geometry
    void flush(SDL_Texture* t) {
        if (!vb.empty()) SDL_RenderGeometry(ren, t, vb.data(), (int)vb.size(), ib.empty() ? nullptr : ib.data(), (int)ib.size());
        vb.clear(); ib.clear();
    }
    // SDL rejects the whole draw if a u/v coordinate goes even slightly outside 0..1 (rounding): clamp here
    void vtx(float x, float y, SDL_Color c, float u = 0, float v = 0) { u = u < 0 ? 0 : u > 1 ? 1 : u; v = v < 0 ? 0 : v > 1 ? 1 : v; SDL_Vertex q; q.position = {x, y}; q.color = c; q.tex_coord = {u, v}; vb.push_back(q); }
    // convex polygon (fan), colour according to the height between y0 and y1
    void poly(const std::vector<SDL_FPoint>& p, SDL_Color c0, SDL_Color c1, float y0, float y1, SDL_Texture* t = nullptr, SDL_FRect uvr = {0, 0, 1, 1}, float bx = 0, float bw = 1) {
        if (p.size() < 3) return;
        int base = (int)vb.size(); float cx = 0, cy = 0; for (auto& q : p) { cx += q.x; cy += q.y; } cx /= p.size(); cy /= p.size();
        auto col = [&](float y) { return mix(c0, c1, (y - y0) / std::max(1.f, y1 - y0)); };
        auto uv = [&](float x, float y, float& u, float& v) { u = uvr.x + (x - bx) / bw * uvr.w; v = uvr.y + (y - y0) / std::max(1.f, y1 - y0) * uvr.h; };
        float u, v; uv(cx, cy, u, v); vtx(cx, cy, col(cy), u, v);
        for (auto& q : p) { uv(q.x, q.y, u, v); vtx(q.x, q.y, col(q.y), u, v); }
        for (size_t i = 0; i < p.size(); i++) { ib.push_back(base); ib.push_back(base + 1 + (int)i); ib.push_back(base + 1 + (int)((i + 1) % p.size())); }
        flush(t);
    }
    static std::vector<SDL_FPoint> rr_pts(float x, float y, float w, float h, float r) {
        std::vector<SDL_FPoint> p; r = std::min(r, std::min(w, h) / 2);
        const float cx[4] = {x + w - r, x + w - r, x + r, x + r}, cy[4] = {y + r, y + h - r, y + h - r, y + r};
        const float a0[4] = {-90, 0, 90, 180};
        for (int k = 0; k < 4; k++) for (int s = 0; s <= 6; s++) { float a = (a0[k] + s * 15) * 3.14159265f / 180; p.push_back({cx[k] + r * cosf(a), cy[k] + r * sinf(a)}); }
        return p;
    }
    void rrect(float x, float y, float w, float h, float r, SDL_Color c0, SDL_Color c1) { poly(rr_pts(x, y, w, h, r), c0, c1, y, y + h); }
    void rrect_ol(float x, float y, float w, float h, float r, SDL_Color c0, SDL_Color c1, float ol, SDL_Color oc) {
        if (ol > 0) rrect(x - ol, y - ol, w + 2 * ol, h + 2 * ol, r + ol, oc, oc);
        rrect(x, y, w, h, r, c0, c1);
    }
    static std::vector<SDL_FPoint> para_pts(float x, float y, float w, float h, float sk) { return {{x + sk, y}, {x + w, y}, {x + w - sk, y + h}, {x, y + h}}; }
    void rect(float x, float y, float w, float h, SDL_Color c0, SDL_Color c1) { poly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, c0, c1, y, y + h); }
    void circle(float cx, float cy, float r, SDL_Color c) {
        std::vector<SDL_FPoint> p; for (int i = 0; i < 28; i++) { float a = i * 6.2831853f / 28; p.push_back({cx + r * cosf(a), cy + r * sinf(a)}); }
        poly(p, c, c, cy - r, cy + r);
    }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, SDL_Color c) { poly({{x0, y0}, {x1, y1}, {x2, y2}}, c, c, 0, 1); }
    // solid arrow pointing left (d = -1) or right (d = 1)
    void arrow(float cx, float cy, float s, int d, SDL_Color c, bool outline = true) {
        if (outline) tri(cx - d * s * 0.62f, cy - s * 0.95f, cx + d * s * 0.8f, cy, cx - d * s * 0.62f, cy + s * 0.95f, INK);
        tri(cx - d * s * 0.4f, cy - s * 0.62f, cx + d * s * 0.5f, cy, cx - d * s * 0.4f, cy + s * 0.62f, c);
    }
    // pointed star (comic burst) behind the selected button
    void burst(float cx, float cy, float rx, float ry, int n, float rot, SDL_Color c) {
        std::vector<SDL_FPoint> p;
        for (int i = 0; i < 2 * n; i++) { float a = rot + i * 3.14159265f / n, k = (i & 1) ? 0.78f : 1.f; p.push_back({cx + rx * k * cosf(a), cy + ry * k * sinf(a)}); }
        int base = (int)vb.size(); vtx(cx, cy, c);
        for (auto& q : p) vtx(q.x, q.y, c);
        for (size_t i = 0; i < p.size(); i++) { ib.push_back(base); ib.push_back(base + 1 + (int)i); ib.push_back(base + 1 + (int)((i + 1) % p.size())); }
        flush(nullptr);
    }

    // ------------------------------------------------------------ text
    static uint32_t next_cp(const std::string& s, size_t& i) {
        uint32_t c = (uint8_t)s[i]; int n = 1;
        if (c >= 0xf0) { c &= 7; n = 4; } else if (c >= 0xe0) { c &= 15; n = 3; } else if (c >= 0xc0) { c &= 31; n = 2; }
        for (int k = 1; k < n && i + k < s.size(); k++) c = (c << 6) | ((uint8_t)s[i + k] & 63);
        i += n; return c;
    }
    float text_w(const FontTex& ft, const std::string& s, float px) {
        float sc = px / ft.f->size, w = 0;
        for (size_t i = 0; i < s.size();) { int g = ft.find(next_cp(s, i)); if (g < 0) g = ft.find('?'); if (g >= 0) w += ft.f->g[g].adv4 / 4.f * sc; }
        return w;
    }
    // y = vertical centre of the capitals; align: 0 left, 1 centre, 2 right. Returns the width.
    float text(const FontTex& ft, float x, float cy, const std::string& s, float px, SDL_Color c0, SDL_Color c1,
               SDL_Color oc = INK, bool outline = true, bool shadow = true, int align = 0, float maxw = 0) {
        if (!ft.tex || s.empty()) return 0;
        float w = text_w(ft, s, px);
        if (maxw > 0 && w > maxw) { px *= maxw / w; w = maxw; }      // shrinks to fit the space
        const float sc = px / ft.f->size;
        if (align == 1) x -= w / 2; else if (align == 2) x -= w;
        const float top = cy - (ft.cap_top + ft.cap_bot) / 2 * sc, ct = top + ft.cap_top * sc, cb = top + ft.cap_bot * sc;
        auto pass = [&](bool ol, float dx, float dy, SDL_Color a, SDL_Color b) {
            float pen = x;
            for (size_t i = 0; i < s.size();) {
                int gi = ft.find(next_cp(s, i)); if (gi < 0) gi = ft.find('?'); if (gi < 0) continue;
                const auto& g = ft.f->g[gi];
                if (g.w) {
                    float x0 = pen + g.ox * sc + dx, y0 = top + g.oy * sc + dy, x1 = x0 + g.w * sc, y1 = y0 + g.h * sc;
                    float u0 = (g.x + (ol ? g.w + 1 : 0)) / (float)ft.tw, v0 = g.y / (float)ft.th, u1 = u0 + g.w / (float)ft.tw, v1 = v0 + g.h / (float)ft.th;
                    SDL_Color ca = mix(a, b, (y0 - dy - ct) / std::max(1.f, cb - ct)), cb_ = mix(a, b, (y1 - dy - ct) / std::max(1.f, cb - ct));
                    int base = (int)vb.size();
                    vtx(x0, y0, ca, u0, v0); vtx(x1, y0, ca, u1, v0); vtx(x1, y1, cb_, u1, v1); vtx(x0, y1, cb_, u0, v1);
                    ib.insert(ib.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
                }
                pen += g.adv4 / 4.f * sc;
            }
            flush(ft.tex);
        };
        if (shadow) { float o = std::max(2.f, px * 0.06f); pass(outline, o * 0.6f, o, alpha(INK, 0.55f), alpha(INK, 0.55f)); }
        if (outline) pass(true, 0, 0, oc, oc);
        pass(false, 0, 0, c0, c1);
        return w;
    }

    // ------------------------------------------------------------ background
    void background(float t, float dim) {
        if (bg.tex) {
            float s = std::max(W / bg.w, H / bg.h) * 1.10f;
            float dw = bg.w * s, dh = bg.h * s;
            float ox = (W - dw) / 2 + sinf(t * 0.11f) * (dw - W) * 0.4f, oy = (H - dh) / 2 + cosf(t * 0.07f) * (dh - H) * 0.4f;
            SDL_FRect d{ox, oy, dw, dh}; SDL_RenderCopyF(ren, bg.tex, nullptr, &d);
        } else {                                       // drawn spiral sea
            rect(0, 0, W, H, rgba(70, 170, 230), SEA1);
            for (int i = 14; i >= 1; i--) {
                float r = i * std::max(W, H) * 0.075f + fmodf(t * 18.f, std::max(W, H) * 0.075f);
                std::vector<SDL_FPoint> p; for (int k = 0; k < 40; k++) { float a = k * 6.2831853f / 40 + t * 0.05f * (i & 1 ? 1 : -1); p.push_back({W / 2 + r * cosf(a) * 1.25f, H * 0.62f + r * 0.55f * sinf(a)}); }
                SDL_Color c = (i & 1) ? rgba(30, 110, 190, 255) : rgba(45, 135, 210, 255);
                poly(p, c, mix(c, SEA1, 0.4f), H * 0.62f - r * 0.55f, H * 0.62f + r * 0.55f);
            }
            for (int i = 0; i < 6; i++) {               // clouds
                float cx = fmodf(i * 263.f + t * (8 + i * 2), W + 400) - 200, cy = 60 + (i * 97) % 220;
                for (int k = 0; k < 4; k++) circle(cx + k * 34 - 50, cy + ((k & 1) ? -14 : 6), 34 + (k == 1 ? 12 : 0), rgba(235, 248, 255, 200));
            }
        }
        rect(0, 0, W, H * 0.55f, alpha(NAVY, dim * 0.55f), alpha(NAVY, dim * 0.25f));
        rect(0, H * 0.55f, W, H * 0.45f, alpha(NAVY, dim * 0.25f), alpha(NAVY, dim * 0.95f));
    }
    // logo: image ui/logo.png or a title drawn with the font
    void title(float cx, float cy, float maxw, float maxh, float t) {
        float bob = sinf(t * 2.1f) * 5.f, sc = 1.f + sinf(t * 1.3f) * 0.012f;
        if (logo.tex) {
            float s = std::min(maxw / logo.w, maxh / logo.h) * sc, w = logo.w * s, h = logo.h * s;
            SDL_FRect d{cx - w / 2, cy - h / 2 + bob, w, h};
            SDL_FRect sh{d.x + 6, d.y + 10, w, h}; SDL_SetTextureColorMod(logo.tex, 0, 0, 0); SDL_SetTextureAlphaMod(logo.tex, 110);
            SDL_RenderCopyF(ren, logo.tex, nullptr, &sh); SDL_SetTextureColorMod(logo.tex, 255, 255, 255); SDL_SetTextureAlphaMod(logo.tex, 255);
            SDL_RenderCopyExF(ren, logo.tex, nullptr, &d, sinf(t * 0.9f) * 1.2, nullptr, SDL_FLIP_NONE);
        } else {
            float px = std::min(maxh * 0.5f, maxw / 3.4f) * sc;
            text(disp, cx, cy - px * 0.5f + bob, "PANIC", px, YEL, RED, INK, true, true, 1);
            text(disp, cx, cy + px * 0.5f + bob, "PARK", px, YEL, RED, INK, true, true, 1);
        }
    }
};

}  // namespace ui
#endif
