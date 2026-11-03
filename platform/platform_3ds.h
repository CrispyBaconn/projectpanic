// Nintendo 3DS (New 3DS / New 2DS XL): finestra, comandi, audio e disegno sulla GPU (PICA200) - incluso da
// platform_stub.cpp quando si compila con __3DS__ (Makefile.3ds). Usa libctru, citro3d e citro2d.
//   schermo superiore: il gioco (320x240 al centro dei 400x240), 3D con pica3d.h + livello del testo + gamma
//   schermo inferiore: menu (GIOCA / IMPOSTAZIONI / ESCI), in gioco MONETA / MENU e le statistiche
//   core 0: questo thread (disegno), core 1: H8 + audio, core 2: R4650 (il gioco)
#pragma once
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include "pica3d.h"
#include "pica3d_shbin.h"                              // generato da 3ds/pica3d.v.pica (picasso + bin2s)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "third_party/stb_image.h"

// ---------------------------------------------------------------- memoria all'avvio
// Sostituisce __system_allocateHeaps di libctru (prima di main): quella chiedeva tutto il resto come heap in un colpo e,
// se il sistema lo rifiutava, si fermava con svcBreak (crash visto sul New 3DS). Qui: prima 14 MB per la GPU, poi l'heap
// piu' grande che il sistema concede (scendendo a passi di 2 MB). I numeri finiscono nel log (righe MEM).
#include <3ds/allocator/mappable.h>
extern "C" {
extern u32 __ctru_heap, __ctru_linear_heap, __ctru_heap_size, __ctru_linear_heap_size;
extern char* fake_heap_start; extern char* fake_heap_end;
u32 s23_mem_remaining = 0, s23_mem_tries = 0, s23_mem_linear_want = 12u << 20;
void __system_allocateHeaps(void) {
    Handle rl = 0; s64 maxc = 0, curc = 0; ResourceLimitType t = RESLIMIT_COMMIT;
    if (R_SUCCEEDED(svcGetResourceLimit(&rl, CUR_PROCESS_HANDLE))) {
        svcGetResourceLimitLimitValues(&maxc, rl, &t, 1); svcGetResourceLimitCurrentValues(&curc, rl, &t, 1); svcCloseHandle(rl); }
    const u32 rem = (u32)(maxc - curc) & ~0xFFFu, AREA = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN; s23_mem_remaining = rem;
    // l'heap normale non puo' superare 96 MB (area virtuale): quello che avanza va tutto alla memoria lineare
    // (la usano la GPU e i file degli asset che ci stanno), invece di restare inutilizzato (11,7 MB sul New 3DS)
    u32 want = s23_mem_linear_want; if (want + (8u << 20) > rem) want = (rem / 4) & ~0xFFFu;
    u32 heap = rem - want; if (heap > AREA) heap = AREA; heap &= ~0xFFFu;
    u32 lin = (rem - heap) & ~0xFFFu;
    while (lin >= (2u << 20) && R_FAILED(svcControlMemory(&__ctru_linear_heap, 0, 0, lin, MEMOP_ALLOC_LINEAR, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) { lin -= 1u << 20; s23_mem_tries++; }
    __ctru_linear_heap_size = lin;
    while (heap >= (4u << 20) && R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0, heap, MEMOP_ALLOC, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) { heap -= 2u << 20; s23_mem_tries++; }
    if (heap < (4u << 20)) svcBreak(USERBREAK_PANIC);
    __ctru_heap_size = heap;
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char*)__ctru_heap; fake_heap_end = fake_heap_start + __ctru_heap_size;
}
}
#include <malloc.h>
#include <new>

namespace n3ds {

// ---------------------------------------------------------------- texture nel formato della GPU
// 8x8 tile in ordine Morton; la riga 0 in memoria e' quella in alto (t = 1)
static inline u32 morton(u32 x, u32 y) { static const u8 xl[8] = {0, 1, 4, 5, 16, 17, 20, 21}, yl[8] = {0, 2, 8, 10, 32, 34, 40, 42}; return xl[x & 7] + yl[y & 7]; }
static inline u32 tex_off(u32 x, u32 y, u32 w) { return (y & ~7u) * w + (x & ~7u) * 8 + morton(x, y); }   // in texel
static inline u16 rgba5551(u32 rgba) { return (u16)((((rgba >> 27) & 31) << 11) | (((rgba >> 19) & 31) << 6) | (((rgba >> 11) & 31) << 1) | ((rgba & 0xff) >= 128 ? 1 : 0)); }

// gli slot stanno in atlanti 1024x1024 (64 slot da 128x128 ciascuno): poligoni consecutivi con celle nello stesso
// atlante si disegnano con una sola chiamata (con una texture per slot erano ~2000 chiamate a fotogramma)
struct C3DBackend : pica::Backend {
    static const int SIDE = 1024, PER = (SIDE / pica::CELL) * (SIDE / pica::CELL), ROW = SIDE / pica::CELL;
    std::vector<C3D_Tex> tex; int nslots = 0;
    bool flip_t() const override { return true; }
    int tex_of(int s) const override { return s / PER; }
    int slots_per_tex() const override { return PER; }
    pica::SlotMap slot_map(int s) const override {
        const int i = s % PER; const float C = (float)pica::CELL / SIDE;
        return {(i % ROW) * C, C, 1.f - (i / ROW) * C, -C};        // riga 0 della cella = riga (i/ROW)*CELL dall'alto
    }
    int init(int want) {                                 // ritorna il numero di slot ottenuti
        const int n = (want + PER - 1) / PER; tex.resize(n);
        for (int i = 0; i < n; i++) {
            if (!C3D_TexInit(&tex[i], SIDE, SIDE, GPU_RGBA5551)) { tex.resize(i); break; }
            C3D_TexSetFilter(&tex[i], GPU_NEAREST, GPU_NEAREST); C3D_TexSetWrap(&tex[i], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            memset(tex[i].data, 0, (size_t)SIDE * SIDE * 2);
        }
        nslots = (int)tex.size() * PER; return nslots;
    }
    void upload_slot(int s, const uint32_t* rgba) override {
        u16* d = (u16*)tex[s / PER].data; const int C = pica::CELL, i = s % PER, ox = (i % ROW) * C, oy = (i / ROW) * C;
        for (int y = 0; y < C; y++) { const uint32_t* r = rgba + y * C; for (int x = 0; x < C; x++) d[tex_off(ox + x, oy + y, SIDE)] = rgba5551(r[x]); }
    }
};

// ---------------------------------------------------------------- stato del disegno
struct Gfx {
    C3D_RenderTarget *top = nullptr, *bot = nullptr;
    DVLB_s* dvlb = nullptr; shaderProgram_s prog; int uProj = -1; C3D_Mtx proj;
    pica::V* vbo = nullptr; int vmax = 0;
    C3DBackend be; pica::Renderer pr; bool ok = false;
    C3D_Tex text; bool text_ok = false;                // livello del testo 320x240 in una texture 512x256
    C3D_Tex bg, logo; Tex3DS_SubTexture bg_st, logo_st; bool bg_ok = false, logo_ok = false;
    C2D_TextBuf tbuf = nullptr;
    float k[3] = {1, 1, 1}, b[3] = {0, 0, 0};          // gamma approssimata: k * x + b
    unsigned last_draws = 0, last_skipped = 0; double ms_build = 0, ms_submit = 0;
};
static Gfx G;

// memoria: totale dell'heap, usata, libera (MB) - nel log per capire i problemi di memoria
static void mem_report(const char* tag) {
    struct mallinfo mi = mallinfo();
    const double heap = __ctru_heap_size / 1048576.0, used = mi.uordblks / 1048576.0, arena = mi.arena / 1048576.0;
    printf("MEM %s: heap %.1f MB (usati %.1f, liberi ~%.1f) | GPU %.1f MB (liberi %.1f) | concessi all'avvio %.1f MB, tentativi %lu\n", tag, heap, used,
           heap - arena + mi.fordblks / 1048576.0, __ctru_linear_heap_size / 1048576.0, linearSpaceFree() / 1048576.0, s23_mem_remaining / 1048576.0, (unsigned long)s23_mem_tries);
    fflush(stdout);
}
static bool load_png_tex(const std::string& path, C3D_Tex& t, Tex3DS_SubTexture& st, int maxw, int maxh) {
    int w, h, n; unsigned char* p = stbi_load(path.c_str(), &w, &h, &n, 4); if (!p) return false;
    float s = std::min(1.f, std::min((float)maxw / w, (float)maxh / h)); int dw = std::max(1, (int)(w * s)), dh = std::max(1, (int)(h * s));
    int tw = 8, th = 8; while (tw < dw) tw <<= 1; while (th < dh) th <<= 1;
    if (!C3D_TexInit(&t, tw, th, GPU_RGBA8)) { stbi_image_free(p); return false; }
    u32* d = (u32*)t.data; memset(d, 0, (size_t)tw * th * 4);
    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {           // riduzione con la media dei texel coperti
        const int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw), y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
        u32 acc[4] = {0, 0, 0, 0}, cnt = 0;
        for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { const unsigned char* q = p + ((size_t)yy * w + xx) * 4; for (int c = 0; c < 4; c++) acc[c] += q[c]; cnt++; }
        d[tex_off(x, y, tw)] = (acc[0] / cnt) << 24 | (acc[1] / cnt) << 16 | (acc[2] / cnt) << 8 | (acc[3] / cnt);   // GPU_RGBA8: 0xRRGGBBAA
    }
    stbi_image_free(p); C3D_TexSetFilter(&t, GPU_LINEAR, GPU_LINEAR); C3D_TexFlush(&t);
    st.width = (u16)dw; st.height = (u16)dh; st.left = 0; st.top = 1.f; st.right = (float)dw / tw; st.bottom = 1.f - (float)dh / th;
    return true;
}

static void on_oom();
static bool init(const std::string& uidir) {
    std::set_new_handler(on_oom); mem_report("avvio");
    gfxInitDefault();
    if (!C3D_Init(0x100000)) return false;                // buffer dei comandi da 1 MB (con gli atlanti bastano poche centinaia di chiamate)
    C2D_Init(4096); C2D_Prepare();
    G.top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT); G.bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    G.dvlb = DVLB_ParseFile((u32*)pica3d_shbin, pica3d_shbin_size);
    shaderProgramInit(&G.prog); shaderProgramSetVsh(&G.prog, &G.dvlb->DVLE[0]);
    G.uProj = shaderInstanceGetUniformLocation(G.prog.vertexShader, "projection");
    Mtx_OrthoTilt(&G.proj, 0.f, 400.f, 240.f, 0.f, 0.f, 1.f, true);      // pixel con y in basso, z in [-w, 0]
    G.vmax = 26000; G.vbo = (pica::V*)linearAlloc(sizeof(pica::V) * G.vmax);
    int slots = G.be.init(192); if (slots <= 0) return false;      // 3 atlanti 1024x1024 (6 MB)
    G.pr.init(&G.be, slots);
    G.text_ok = C3D_TexInit(&G.text, 512, 256, GPU_RGBA5551); if (G.text_ok) { C3D_TexSetFilter(&G.text, GPU_NEAREST, GPU_NEAREST); memset(G.text.data, 0, 512 * 256 * 2); }
    G.bg_ok = load_png_tex(uidir + "/background.png", G.bg, G.bg_st, 512, 256);
    G.logo_ok = load_png_tex(uidir + "/logo.png", G.logo, G.logo_st, 256, 256);
    G.tbuf = C2D_TextBufNew(4096);
    printf("3DS: GPU pronta, %d slot di texture, sfondo %s, logo %s\n", slots, G.bg_ok ? "si" : "no", G.logo_ok ? "si" : "no"); fflush(stdout); mem_report("grafica pronta");
    G.ok = G.vbo != nullptr; return G.ok;
}

// stato fisso del nostro programma (da rifare dopo citro2d, che usa il suo)
static void bind_3d() {
    C3D_BindProgram(&G.prog);
    C3D_AttrInfo* ai = C3D_GetAttrInfo(); AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 4); AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2); AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);
    C3D_BufInfo* bi = C3D_GetBufInfo(); BufInfo_Init(bi); BufInfo_Add(bi, G.vbo, sizeof(pica::V), 3, 0x210);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, G.uProj, &G.proj);
    C3D_CullFace(GPU_CULL_NONE); C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR); C3D_AlphaTest(true, GPU_GREATER, 0);
    for (int i = 2; i < 6; i++) { C3D_TexEnv* e = C3D_GetTexEnv(i); C3D_TexEnvInit(e); }
}
static void env_batch(const pica::Batch& b) {
    C3D_TexEnv* e0 = C3D_GetTexEnv(0); C3D_TexEnvInit(e0);
    C3D_TexEnvSrc(e0, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR); C3D_TexEnvFunc(e0, C3D_RGB, GPU_MODULATE); C3D_TexEnvScale(e0, C3D_RGB, GPU_TEVSCALE_4);
    if (b.sten) { C3D_TexEnvSrc(e0, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR); C3D_TexEnvFunc(e0, C3D_Alpha, GPU_MODULATE); }
    else { C3D_TexEnvSrc(e0, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_CONSTANT, GPU_TEXTURE0); C3D_TexEnvFunc(e0, C3D_Alpha, GPU_INTERPOLATE); }
    C3D_TexEnvColor(e0, (u32)b.ca << 24);
    C3D_TexEnv* e1 = C3D_GetTexEnv(1); C3D_TexEnvInit(e1);
    if ((b.fade & 0xff) != 0xff) {
        C3D_TexEnvSrc(e1, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_CONSTANT); C3D_TexEnvOpRgb(e1, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(e1, C3D_RGB, GPU_INTERPOLATE);
        C3D_TexEnvColor(e1, (b.fade >> 24) | ((b.fade >> 16) & 0xff) << 8 | ((b.fade >> 8) & 0xff) << 16 | (b.fade & 0xff) << 24);
    } else { C3D_TexEnvSrc(e1, C3D_RGB, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS); C3D_TexEnvFunc(e1, C3D_RGB, GPU_REPLACE); }
    C3D_TexEnvSrc(e1, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS); C3D_TexEnvFunc(e1, C3D_Alpha, GPU_REPLACE);
    if (b.mode == 1) C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
    else if (b.mode == 2) { C3D_BlendingColor(0x80000000u); C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, GPU_ONE, GPU_ZERO); }
    else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
}
// rettangolo con un colore costante (texture non usata): per lo sfondo, la gamma e le bande laterali
static int quad_col(int at, float x0, float y0, float x1, float y1, u32 rgba) {
    if (at + 6 > G.vmax) return at;
    auto v = [&](float x, float y) { pica::V q; q.x = x; q.y = y; q.z = 0.5f; q.w = 1; q.s = 0; q.t = 0; q.r = rgba >> 24; q.g = rgba >> 16; q.b = rgba >> 8; q.a = rgba; return q; };
    pica::V* o = G.vbo + at; o[0] = v(x0, y0); o[1] = v(x1, y0); o[2] = v(x1, y1); o[3] = v(x0, y0); o[4] = v(x1, y1); o[5] = v(x0, y1);
    return at + 6;
}
static void env_color() {
    C3D_TexEnv* e0 = C3D_GetTexEnv(0); C3D_TexEnvInit(e0); C3D_TexEnvSrc(e0, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR); C3D_TexEnvFunc(e0, C3D_Both, GPU_REPLACE);
    C3D_TexEnv* e1 = C3D_GetTexEnv(1); C3D_TexEnvInit(e1);
}

// livello del testo (640x480, 0 = trasparente) -> 320x240 nella texture del testo
static void upload_text(const uint32_t* txl) {
    if (!G.text_ok) return;
    u16* d = (u16*)G.text.data;
    for (int y = 0; y < 240; y++) { const uint32_t* r = txl + (size_t)(y * 2) * 640; for (int x = 0; x < 320; x++) { const uint32_t c = r[x * 2]; d[tex_off(x, y, 512)] = c ? rgba5551(((c & 0xffffff) << 8) | 0xff) : 0; } }
}

// disegna un fotogramma del gioco sullo schermo superiore (chiamare dopo C3D_FrameBegin)
// in: dati raccolti dalle memorie video; F/order: geometria preparata (g3d::prepare)
static void draw_game(const FrameIn& in, const g3d::Frame& F, const std::vector<int>& order, bool text_new, pica::Stats& st) {
    const float X0 = 40, Y0 = 0, SC = 0.5f;            // 640x480 -> 320x240 al centro
    const uint64_t tb0 = now_ns();
    if (in.have3d) G.pr.build(F, order, in.pal3d.data(), SC, X0, Y0, 360.f, 240.f); else { G.pr.verts.clear(); G.pr.batches.clear(); G.pr.st = pica::Stats{}; }
    const uint64_t tb1 = now_ns(); G.ms_build = (tb1 - tb0) / 1e6;
    st = G.pr.st;
    if (text_new) upload_text(in.txl.data());
    for (int c = 0; c < 3; c++) { const uint8_t* g = c == 0 ? in.gr : c == 1 ? in.gg : in.gb; pica::gamma_fit(g, G.k[c], G.b[c]); }
    // vertici: [sfondo] [3D] [testo] [gamma]
    int n = 0;
    const u32 bgc = (u32)std::min<uint32_t>(255, in.bg[0]) << 24 | (u32)std::min<uint32_t>(255, in.bg[1]) << 16 | (u32)std::min<uint32_t>(255, in.bg[2]) << 8 | 0xff;
    n = quad_col(n, X0, Y0, X0 + 320, Y0 + 240, bgc); const int n3d = n;
    const int nv = std::min((int)G.pr.verts.size(), G.vmax - n - 24);
    if (nv > 0) memcpy(G.vbo + n, G.pr.verts.data(), sizeof(pica::V) * nv);
    n += std::max(0, nv);
    const int ntext = n;
    {   auto v = [&](float x, float y, float s, float t) { pica::V q; q.x = x; q.y = y; q.z = 0.5f; q.w = 1; q.s = s; q.t = t; q.r = q.g = q.b = q.a = 255; return q; };
        const float s1 = 320.f / 512, t1 = 1.f - 240.f / 256; pica::V* o = G.vbo + n;
        o[0] = v(X0, Y0, 0, 1); o[1] = v(X0 + 320, Y0, s1, 1); o[2] = v(X0 + 320, Y0 + 240, s1, t1); o[3] = v(X0, Y0, 0, 1); o[4] = v(X0 + 320, Y0 + 240, s1, t1); o[5] = v(X0, Y0 + 240, 0, t1); n += 6; }
    const int ngam = n;
    const bool gam_mul = G.k[0] < 0.98f || G.k[1] < 0.98f || G.k[2] < 0.98f || G.k[0] > 1.02f || G.k[1] > 1.02f || G.k[2] > 1.02f;
    auto cl = [](float v) { return (u32)std::max(0.f, std::min(255.f, v * 255.f + 0.5f)); };
    if (gam_mul) n = quad_col(n, X0, Y0, X0 + 320, Y0 + 240, cl(G.k[0] * 0.5f) << 24 | cl(G.k[1] * 0.5f) << 16 | cl(G.k[2] * 0.5f) << 8 | 0xff);
    const int nadd = n; const bool gam_add = G.b[0] > 0.01f || G.b[1] > 0.01f || G.b[2] > 0.01f;
    if (gam_add) n = quad_col(n, X0, Y0, X0 + 320, Y0 + 240, cl(G.b[0]) << 24 | cl(G.b[1]) << 16 | cl(G.b[2]) << 8 | 0xff);

    C2D_TargetClear(G.top, C2D_Color32(0, 0, 0, 255)); C3D_FrameDrawOn(G.top);
    bind_3d();
    env_color(); C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO); C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
    unsigned draws = 1, skipped = 0;
    // lo stato (combinatore, blending, texture) si manda alla GPU solo quando cambia: ogni cambio costa spazio nel
    // buffer dei comandi; se il buffer sta per finire si smette di disegnare il 3D (prima andava in crash: svcBreak in GPUCMD_Add)
    const pica::Batch* prev = nullptr; int bound = -1;
    for (const pica::Batch& b : G.pr.batches) {
        if (b.first + b.count > nv) break;
        { u32* cb; u32 cs, co; GPUCMD_GetBuffer(&cb, &cs, &co); if (cs - co < 0x8000) { skipped = (unsigned)(&G.pr.batches.back() - &b) + 1; break; } }   // 128 KB di riserva
        if (b.slot >= 0 && b.slot != bound) { C3D_TexBind(0, &G.be.tex[b.slot]); bound = b.slot; }
        if (!prev || prev->ca != b.ca || prev->mode != b.mode || prev->sten != b.sten || prev->fade != b.fade) env_batch(b);
        prev = &b;
        C3D_DrawArrays(GPU_TRIANGLES, n3d + b.first, b.count); draws++;
    }
    G.last_skipped = skipped;
    if (G.text_ok) {                                    // testo: sopra al 3D, trasparente dove vale 0
        C3D_TexBind(0, &G.text);
        C3D_TexEnv* e0 = C3D_GetTexEnv(0); C3D_TexEnvInit(e0); C3D_TexEnvSrc(e0, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR); C3D_TexEnvFunc(e0, C3D_Both, GPU_REPLACE);
        C3D_TexEnv* e1 = C3D_GetTexEnv(1); C3D_TexEnvInit(e1);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);   // texel con alpha 0 scartati dal test alpha
        C3D_DrawArrays(GPU_TRIANGLES, ntext, 6); draws++;
    }
    env_color();
    if (gam_mul) { C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_SRC_COLOR, GPU_ZERO, GPU_ONE); C3D_DrawArrays(GPU_TRIANGLES, ngam, 6); draws++; }   // dst * k (fino a 2x)
    if (gam_add) { C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE); C3D_DrawArrays(GPU_TRIANGLES, nadd, 6); draws++; }
    G.last_draws = draws; G.ms_submit = (now_ns() - tb1) / 1e6;
}

// ---------------------------------------------------------------- schermo inferiore (citro2d)
static inline u32 col(u8 r, u8 g, u8 b, u8 a = 255) { return C2D_Color32(r, g, b, a); }
static void text(const std::string& s, float x, float y, float sz, u32 c, u32 flags = 0, float wrap = 0.f) {
    C2D_Text t; C2D_TextParse(&t, G.tbuf, s.c_str()); C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, sz, sz, c, wrap);
}
// pulsante inclinato in stile Panic Park; ritorna true se toccato in questo fotogramma
static bool button(float x, float y, float w, float h, const std::string& label, bool sel, const touchPosition& tp, bool touched) {
    const float sk = h * 0.25f;
    auto para = [&](float ox, float oy, float ww, float hh, u32 c0, u32 c1) {
        C2D_DrawTriangle(ox + sk, oy, c0, ox + ww, oy, c0, ox, oy + hh, c1, 0.5f);
        C2D_DrawTriangle(ox + ww, oy, c0, ox + ww - sk, oy + hh, c1, ox, oy + hh, c1, 0.5f); };
    para(x - 3, y - 3, w + 6, h + 6, col(12, 10, 20), col(12, 10, 20));
    if (sel) para(x, y, w, h, col(255, 214, 40), col(232, 60, 30)); else para(x, y, w, h, col(46, 104, 196), col(16, 44, 104));
    text(label, x + w / 2, y + h / 2 - 11 * (h / 40), 0.7f * (h / 40), sel ? col(30, 10, 10) : col(255, 255, 255), C2D_AlignCenter);
    return touched && tp.px >= x && tp.px < x + w && tp.py >= y && tp.py < y + h;
}

// ---------------------------------------------------------------- audio (ndsp, 44,1 kHz stereo)
static bool g_snd = false; static ndspWaveBuf g_wb[6]; static int16_t* g_wbmem = nullptr; static int g_wbi = 0; static size_t g_wbfill = 0;
static const int WB_FRAMES = 1024;
static void audio_init() {
    if (R_FAILED(ndspInit())) { printf("3DS: audio non disponibile (manca sdmc:/3ds/dspfirm.cdc? si crea con DSP1)\n"); fflush(stdout); return; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO); ndspChnReset(0); ndspChnSetInterp(0, NDSP_INTERP_LINEAR); ndspChnSetRate(0, 44100); ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {1.f, 1.f}; ndspChnSetMix(0, mix);
    g_wbmem = (int16_t*)linearAlloc(sizeof(int16_t) * 2 * WB_FRAMES * 6); if (!g_wbmem) return;
    memset(g_wb, 0, sizeof g_wb);
    for (int i = 0; i < 6; i++) { g_wb[i].data_vaddr = g_wbmem + i * 2 * WB_FRAMES; g_wb[i].nsamples = WB_FRAMES; g_wb[i].status = NDSP_WBUF_DONE; }
    g_snd = true;
}
// chiamata dal thread dell'H8 con i campioni del C352
static void audio_push(const int16_t* s, size_t frames) {
    if (!g_snd) return;
    while (frames) {
        ndspWaveBuf& w = g_wb[g_wbi];
        if (w.status != NDSP_WBUF_DONE && w.status != NDSP_WBUF_FREE) return;          // coda piena: si perde (meglio che bloccare l'H8)
        const size_t k = std::min(frames, (size_t)WB_FRAMES - g_wbfill);
        memcpy((int16_t*)w.data_vaddr + g_wbfill * 2, s, k * 4); g_wbfill += k; s += k * 2; frames -= k;
        if (g_wbfill == (size_t)WB_FRAMES) { DSP_FlushDataCache(w.data_vaddr, WB_FRAMES * 4); ndspChnWaveBufAdd(0, &w); g_wbi = (g_wbi + 1) % 6; g_wbfill = 0; }
    }
}

}  // namespace n3ds

// ================================================================ parte che usa lo stato del gioco (platform_stub.cpp)
namespace n3ds {

struct Cfg {                                           // sdmc:/3ds/panicprk/panicprk.ini
    std::string ui_lang = "it"; int game_lang = -1, skip_post = 0, service_dip = 0, stats = 1;
    static std::string path() { return std::string(S23_APP_DIR) + "/panicprk.ini"; }
    void load() {
        FILE* f = fopen(path().c_str(), "r"); if (!f) return; char l[256];
        while (fgets(l, sizeof l, f)) { char* eq = strchr(l, '='); if (!eq) continue; *eq = 0; std::string k = l, v = eq + 1; while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
            if (k == "ui_lang") ui_lang = v; else if (k == "game_lang") game_lang = atoi(v.c_str()); else if (k == "skip_post") skip_post = atoi(v.c_str());
            else if (k == "service_dip") service_dip = atoi(v.c_str()); else if (k == "stats") stats = atoi(v.c_str()); }
        fclose(f);
    }
    void save() const { FILE* f = fopen(path().c_str(), "w"); if (!f) return; fprintf(f, "ui_lang=%s\ngame_lang=%d\nskip_post=%d\nservice_dip=%d\nstats=%d\n", ui_lang.c_str(), game_lang, skip_post, service_dip, stats); fclose(f); }
};
static Cfg cfg;
static std::string T(const char* k) { return i18n::tru(k); }

// schermata di attesa durante il caricamento (gli asset sono ~85 MB letti dalla SD)
static void splash(const std::string& msg) {
    if (!G.ok) return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_TextBufClear(G.tbuf); C2D_Prepare();
    C2D_TargetClear(G.top, col(20, 60, 140)); C2D_SceneBegin(G.top);
    if (G.bg_ok) { C2D_Image im{&G.bg, &G.bg_st}; const float s = std::max(400.f / G.bg_st.width, 240.f / G.bg_st.height); C2D_DrawImageAt(im, 200 - G.bg_st.width * s / 2, 120 - G.bg_st.height * s / 2, 0.4f, nullptr, s, s); }
    if (G.logo_ok) { C2D_Image im{&G.logo, &G.logo_st}; const float s = std::min(260.f / G.logo_st.width, 170.f / G.logo_st.height); C2D_DrawImageAt(im, 200 - G.logo_st.width * s / 2, 30, 0.5f, nullptr, s, s); }
    C2D_TargetClear(G.bot, col(12, 28, 66)); C2D_SceneBegin(G.bot);
    text("PANIC PARK", 160, 70, 1.0f, col(255, 214, 40), C2D_AlignCenter); text(msg, 160, 130, 0.6f, col(220, 230, 255), C2D_AlignCenter);
    C3D_FrameEnd(0);
}

// errore: lo mostra su entrambi gli schermi (e nel log) finche' non si preme START, invece di un crash
static void fatal(const std::string& msg) {
    printf("ERRORE: %s\n", msg.c_str()); mem_report("errore"); fflush(stdout);
    if (!G.ok) { gfxExit(); return; }
    char m[160]; snprintf(m, sizeof m, "heap %.0f MB, GPU %.0f MB", __ctru_heap_size / 1048576.0, __ctru_linear_heap_size / 1048576.0);
    while (aptMainLoop()) {
        hidScanInput(); if (hidKeysDown() & (KEY_START | KEY_A | KEY_B)) break;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_TextBufClear(G.tbuf); C2D_Prepare();
        C2D_TargetClear(G.top, col(90, 10, 10)); C2D_SceneBegin(G.top);
        text("Panic Park: errore", 200, 30, 0.9f, col(255, 220, 80), C2D_AlignCenter);
        text(msg, 10, 80, 0.55f, col(255, 255, 255), C2D_WordWrap, 380.f);
        text(m, 200, 190, 0.5f, col(255, 200, 200), C2D_AlignCenter);
        C2D_TargetClear(G.bot, col(12, 28, 66)); C2D_SceneBegin(G.bot);
        text("Dettagli in sd:/3ds/panicprk/log.txt", 160, 90, 0.5f, col(220, 230, 255), C2D_AlignCenter);
        text("START: esci", 160, 130, 0.6f, col(255, 214, 40), C2D_AlignCenter);
        C3D_FrameEnd(0);
    }
}
static void on_oom() {                                  // new fallita (senza eccezioni: altrimenti abort)
    std::set_new_handler(nullptr);
    printf("ERRORE: memoria esaurita (new)\n"); mem_report("new fallita"); fflush(stdout);
    char m[200]; snprintf(m, sizeof m, "Memoria esaurita. Il sistema ha concesso %.1f MB (heap %.1f MB + GPU %.1f MB).", s23_mem_remaining / 1048576.0, __ctru_heap_size / 1048576.0, __ctru_linear_heap_size / 1048576.0);
    fatal(m); exit(0);
}

// righe delle impostazioni: ritorna false con B (indietro)
static int settings_row = 0;
static bool settings(u32 kd, const touchPosition& tp, bool touched) {
    static const char* GL[8] = {"JPN", "ENG", "GER", "FRA", "SPA", "POR", "ITA", "KOR"};
    const int N = 6;
    if (kd & KEY_DOWN) settings_row = (settings_row + 1) % N; if (kd & KEY_UP) settings_row = (settings_row + N - 1) % N;
    int d = (kd & (KEY_RIGHT | KEY_A)) ? 1 : (kd & KEY_LEFT) ? -1 : 0;
    int hit = -1; if (touched && tp.py >= 34 && tp.py < 34 + N * 32) hit = (tp.py - 34) / 32;
    if (hit >= 0) { settings_row = hit; d = tp.px < 160 && hit < 5 ? -1 : 1; }
    if (d && settings_row == 0) { auto& L = i18n::all(); if (!L.empty()) { int k = i18n::cur(); k = (k < 0 ? 0 : (k + d + (int)L.size()) % (int)L.size()); cfg.ui_lang = L[k].code; i18n::select(cfg.ui_lang); } }
    if (d && settings_row == 1) cfg.game_lang = (cfg.game_lang + 1 + d + 9) % 9 - 1;
    if (d && settings_row == 2) cfg.skip_post ^= 1;
    if (d && settings_row == 3) cfg.service_dip ^= 1;
    if (d && settings_row == 4) cfg.stats ^= 1;
    const bool back = (kd & KEY_B) || (d && settings_row == 5);
    text(T("settings"), 160, 4, 0.8f, col(255, 214, 40), C2D_AlignCenter);
    const std::string vals[5] = { i18n::cur_name_u(), cfg.game_lang < 0 ? T("game_lang_keep") : GL[cfg.game_lang & 7], cfg.skip_post ? T("yes") : T("no"), cfg.service_dip ? T("yes") : T("no"), cfg.stats ? T("yes") : T("no") };
    const char* keys[5] = {"ui_lang", "game_lang", "post", "svc", "stats"};
    for (int i = 0; i < N; i++) {
        const float y = 34 + i * 32; const bool s = settings_row == i;
        C2D_DrawRectSolid(6, y, 0.4f, 308, 29, s ? col(44, 100, 190) : col(20, 36, 80));
        if (i < 5) { text(T(keys[i]), 12, y + 6, 0.48f, s ? col(255, 230, 80) : col(235, 240, 255)); text(vals[i], 308, y + 6, 0.48f, col(200, 225, 255), C2D_AlignRight); }
        else text(T("back"), 160, y + 6, 0.55f, s ? col(255, 230, 80) : col(235, 240, 255), C2D_AlignCenter);
    }
    if (back) { cfg.save(); return false; }
    return true;
}

// menu principale (prima del gioco) o di pausa (in gioco): 0 = gioca/riprendi, 1 = esci
static int menu(bool ingame) {
    int sel = 0; bool in_set = false; uint64_t t0 = svcGetSystemTick();
    while (aptMainLoop()) {
        hidScanInput(); const u32 kd = hidKeysDown(); touchPosition tp; hidTouchRead(&tp); const bool touched = (kd & KEY_TOUCH) != 0;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_TextBufClear(G.tbuf); C2D_Prepare();
        const float t = (float)((svcGetSystemTick() - t0) / (double)SYSCLOCK_ARM11);
        C2D_TargetClear(G.top, col(20, 60, 140)); C2D_SceneBegin(G.top);
        if (G.bg_ok) { C2D_Image im{&G.bg, &G.bg_st}; const float s = std::max(400.f / G.bg_st.width, 240.f / G.bg_st.height) * 1.06f; C2D_DrawImageAt(im, 200 - G.bg_st.width * s / 2 + sinf(t * 0.2f) * 10, 120 - G.bg_st.height * s / 2, 0.4f, nullptr, s, s); }
        C2D_DrawRectangle(0, 140, 0.45f, 400, 100, col(12, 28, 66, 0), col(12, 28, 66, 0), col(12, 28, 66, 200), col(12, 28, 66, 200));
        if (G.logo_ok) { C2D_Image im{&G.logo, &G.logo_st}; const float s = std::min(280.f / G.logo_st.width, 180.f / G.logo_st.height) * (1.f + 0.015f * sinf(t * 1.3f)); C2D_DrawImageAt(im, 200 - G.logo_st.width * s / 2, 20 + sinf(t * 2.1f) * 4, 0.5f, nullptr, s, s); }
        else text("PANIC PARK", 200, 70, 1.6f, col(255, 214, 40), C2D_AlignCenter);
        if (ingame) text(T("paused"), 200, 210, 0.8f, col(255, 255, 255), C2D_AlignCenter);
        C2D_TargetClear(G.bot, col(12, 28, 66)); C2D_SceneBegin(G.bot);
        int act = -1;
        if (in_set) { if (!settings(kd, tp, touched)) in_set = false; }
        else {
            if (kd & KEY_DOWN) sel = (sel + 1) % 3; if (kd & KEY_UP) sel = (sel + 2) % 3;
            const char* L[3] = {ingame ? "resume" : "play", "settings", "exit"};
            for (int i = 0; i < 3; i++) { const float w = i == 0 ? 250 : i == 1 ? 230 : 160, h = i == 0 ? 56 : i == 1 ? 46 : 38, y = i == 0 ? 30 : i == 1 ? 104 : 168;
                if (button(160 - w / 2, y, w, h, T(L[i]), sel == i, tp, touched)) { sel = i; act = i; } }
            if (kd & KEY_A) act = sel;
            if ((kd & KEY_START) && !ingame) act = 0;
            if ((kd & KEY_B) && ingame) act = 0;
            if (act == 1) { in_set = true; settings_row = 0; act = -1; }
        }
        C3D_FrameEnd(0);
        if (act == 0) return 0;
        if (act == 2) return 1;
    }
    return 1;
}

static int g_test_on = 0;
// comandi del 3DS -> scheda I/O (JVS). Pulsanti come sul PC con un controller: B moneta, A pulsante, START start 1,
// Y start 2, L test, R DIP di servizio, croce su/giu' per i menu di servizio, Circle Pad o croce sx/dx = leva.
// SELECT = moneta; SELECT+START o MENU sullo schermo = pausa.
static bool input(u32 kd, u32 kh, bool& want_menu, bool coin_touch) {
    static uint64_t coin_until = 0; const uint64_t now = now_ns() / 1000000;
    uint32_t p1 = 0;
    if (kh & KEY_A) p1 |= 0x02; if (kh & KEY_START) p1 |= 0x01; if (kh & KEY_Y) p1 |= 0x8000; if (kh & KEY_DUP) p1 |= 0x20; if (kh & KEY_DDOWN) p1 |= 0x10;
    if ((kh & KEY_SELECT) && (kh & KEY_START)) { want_menu = true; p1 &= ~0x01u; }
    else if ((kd & KEY_B) || ((kd & KEY_SELECT) && !(kh & KEY_START)) || coin_touch) coin_until = now + 120;
    g_in.p1 = p1; g_in.coin1 = now < coin_until ? 1 : 0;
    if (kd & KEY_L) g_test_on ^= 1; g_in.system = g_test_on ? 0x80 : 0;
    circlePosition cp; hidCircleRead(&cp); float v = cp.dx / 150.f; v = std::max(-1.f, std::min(1.f, v)); if (std::fabs(v) < 0.12f) v = 0; else v = (v > 0 ? v - 0.12f : v + 0.12f) / 0.88f;
    if (kh & KEY_DLEFT) v = -1; if (kh & KEY_DRIGHT) v = 1;
    g_in.analog[0] = (uint16_t)std::max(0.f, std::min(65535.f, 0x8000 + v * 0x7fff));
    if (kd & KEY_R) { g_dsw ^= 1u; printf("DIP configurazione: %s\n", (g_dsw & 1) ? "spento" : "ACCESO"); fflush(stdout); return true; }
    return false;
}

// ciclo principale durante il gioco (il R4650 gira gia' sul suo thread)
static void apply_lang() {
    static const char* LC[8] = {"ja", "en", "de", "fr", "es", "pt", "it", "ko"};
    game_text_patch(cfg.game_lang >= 0 ? LC[cfg.game_lang & 7] : i18n::cur_code());
    g_game_lang = cfg.game_lang;
}
static void run() {
    static FrameIn in; g3d::Frame F; std::vector<int> order; pica::Stats st;
    uint64_t t_last = now_ns(), vbl_last = g_deliv_ip[2]; unsigned frames = 0;
    double hz = 0, fps = 0, acc_prep = 0, acc_gpu = 0, ms_prep = 0, ms_gpu = 0, acc_g = 0, acc_b = 0, acc_s = 0, acc_d = 0, acc_w = 0, ms_g = 0, ms_b = 0, ms_s = 0, ms_d = 0, ms_w = 0;
    unsigned draws_max = 0, skip_tot = 0;
    while (aptMainLoop()) {
        hidScanInput(); const u32 kd = hidKeysDown(), kh = hidKeysHeld(); touchPosition tp; hidTouchRead(&tp); const bool touched = (kd & KEY_TOUCH) != 0;
        bool want_menu = false, coin_touch = false;
        if (touched && tp.py >= 150) { if (tp.px < 160) coin_touch = true; else want_menu = true; }
        input(kd, kh, want_menu, coin_touch);
        if (want_menu) {                                   // pausa: il tempo dell'emulazione si ferma
            emu_pause(true); g_in.p1 = 0; g_in.coin1 = 0;
            const int r = menu(true);
            apply_lang(); apply_game_lang(true);
            emu_pause(false); if (r == 1) break;
            continue;
        }
        const uint64_t w0 = now_ns();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_TextBufClear(G.tbuf);
        const uint64_t a = now_ns();
        gather(in); const uint64_t ag = now_ns(); F.polys.clear(); F.pal.clear(); order.clear(); if (in.have3d) g3d::prepare(in.lst, F, order);
        const uint64_t b = now_ns();
        draw_game(in, F, order, in.txl_new, st);
        acc_prep += (b - a) / 1e6; acc_gpu += (now_ns() - b) / 1e6;
        acc_w += (a - w0) / 1e6; acc_g += (ag - a) / 1e6; acc_b += G.ms_build; acc_s += G.ms_submit; acc_d += C3D_GetDrawingTime();
        draws_max = std::max(draws_max, G.last_draws); skip_tot += G.last_skipped;
        C2D_Prepare(); C2D_TargetClear(G.bot, col(12, 28, 66)); C2D_SceneBegin(G.bot);
        button(10, 160, 140, 70, T("touch_coin"), false, tp, false); button(170, 160, 140, 70, T("touch_menu"), false, tp, false);
        if (cfg.stats) {
            char l[160];
            snprintf(l, sizeof l, "gioco %.1f Hz   immagini %.1f/s   H8 %.0f%%", hz, fps, (double)g_h8_pct); text(l, 8, 6, 0.45f, col(140, 255, 140));
            snprintf(l, sizeof l, "raccolta+geometria %.1f ms   preparazione GPU %.1f ms", ms_prep, ms_gpu); text(l, 8, 26, 0.45f, col(255, 220, 120));
            snprintf(l, sizeof l, "poligoni %u  lotti %u  slot nuovi %u  scartati %u", st.polys, st.batches, st.new_slots, st.dropped); text(l, 8, 46, 0.45f, col(200, 220, 255));
            snprintf(l, sizeof l, "testo %.1f  geo %.1f  celle %.1f  invio %.1f  GPU %.1f  attesa %.1f ms", ms_g, ms_prep - ms_g, ms_b, ms_s, ms_d, ms_w); text(l, 8, 86, 0.42f, col(255, 200, 200));
            snprintf(l, sizeof l, "CPU %.0f%%  GPU %.0f%%  comandi %.0f%%", C3D_GetProcessingTime() * 6.f, C3D_GetDrawingTime() * 6.f, C3D_GetCmdBufUsage() * 100.f); text(l, 8, 66, 0.45f, col(200, 220, 255));
        }
        text(T("h3ds_help"), 160, 120, 0.42f, col(170, 185, 210), C2D_AlignCenter);
        C3D_FrameEnd(0);
        frames++;
        const uint64_t n = now_ns();
        if (n - t_last >= 1000000000ull) {
            const double el = (n - t_last) / 1e9; fps = frames / el; hz = (g_deliv_ip[2] - vbl_last) / el; ms_prep = acc_prep / frames; ms_gpu = acc_gpu / frames;
            static unsigned logn = 0;
            ms_g = acc_g / frames; ms_b = acc_b / frames; ms_s = acc_s / frames; ms_d = acc_d / frames; ms_w = acc_w / frames;
            if (++logn % 5 == 0) { printf("PERF 3DS gioco %.1f Hz img %.1f/s H8 %.0f%% | raccolta %.1f geo %.1f celle %.1f invio %.1f GPU %.1f attesa %.1f ms | poligoni %u lotti %u disegni max %u nuovi %u scartati %u saltati %u comandi %.0f%%\n",
                hz, fps, (double)g_h8_pct, ms_g, ms_prep - ms_g, ms_b, ms_s, ms_d, ms_w, st.polys, st.batches, draws_max, st.new_slots, st.dropped, skip_tot, C3D_GetCmdBufUsage() * 100.f); fflush(stdout); draws_max = 0; skip_tot = 0; }
            frames = 0; acc_prep = acc_gpu = 0; acc_g = acc_b = acc_s = acc_d = acc_w = 0; t_last = n; vbl_last = g_deliv_ip[2];
            nvram_save();
        }
    }
}

}  // namespace n3ds
