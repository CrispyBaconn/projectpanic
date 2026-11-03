// 3D drawing (C435) and final compositing on the GPU: OpenGL 3.0+ (PC) or OpenGL ES 3.0 (Android, Switch).
//
// The CPU does what it already did (g3d::prepare): transforms, lights, projects and sorts the polygons. Instead of the
// software rasteriser, the polygons (already in drawing order, no z-buffer) go to the GPU as triangles, and the
// the fragment shader redoes exactly gpu3d.h's scanline(): tile map, 8-bit texrom, palette, C412 stencil,
// shading, alpha/average with the background, per-polygon clipping (rectangular window and the monitor's trapezoid).
// The graphics ROMs (32 MB texrom, tile map) are loaded once as whole textures; palettes, stencil, data of the
// polygons and text layer are updated every frame. Compositing (background, 3D, text, gamma) is a
// second shader that writes into SDL's texture, which then shows it as before (OSD and touch buttons included).
//
// No OpenGL headers: types, constants and functions are declared here and loaded with SDL_GL_GetProcAddress, so
// the same file builds on Windows (mingw), Linux, Android and Switch.
#pragma once
#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gl3d {
using g3d::s32;

// ---------------------------------------------------------------- OpenGL types and functions (loaded at runtime)
typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef float GLfloat;
typedef unsigned char GLboolean; typedef char GLchar; typedef ptrdiff_t GLsizeiptr; typedef ptrdiff_t GLintptr; typedef unsigned int GLbitfield;
#if defined(_WIN32) && !defined(_WIN64)
#define S23_GLAPI __stdcall
#else
#define S23_GLAPI
#endif
#define S23_GLFUNCS(X) \
    X(GLenum, glGetError, (void)) X(const unsigned char*, glGetString, (GLenum)) X(void, glGetIntegerv, (GLenum, GLint*)) \
    X(void, glEnable, (GLenum)) X(void, glDisable, (GLenum)) X(GLboolean, glIsEnabled, (GLenum)) \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) X(void, glBlendEquation, (GLenum)) \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X(void, glClear, (GLbitfield)) \
    X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, glGenTextures, (GLsizei, GLuint*)) X(void, glDeleteTextures, (GLsizei, const GLuint*)) X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glActiveTexture, (GLenum)) X(void, glTexParameteri, (GLenum, GLenum, GLint)) X(void, glPixelStorei, (GLenum, GLint)) \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, glTexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, glGenBuffers, (GLsizei, GLuint*)) X(void, glBindBuffer, (GLenum, GLuint)) X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, glEnableVertexAttribArray, (GLuint)) X(void, glDisableVertexAttribArray, (GLuint)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) X(void, glVertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void*)) \
    X(GLuint, glCreateShader, (GLenum)) X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) X(void, glCompileShader, (GLuint)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*)) X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(GLuint, glCreateProgram, (void)) X(void, glAttachShader, (GLuint, GLuint)) X(void, glLinkProgram, (GLuint)) X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*)) X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) X(void, glUseProgram, (GLuint)) \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar*)) X(void, glUniform1i, (GLint, GLint)) X(void, glUniform1f, (GLint, GLfloat)) \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat)) X(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glUniform1ui, (GLint, GLuint)) X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint*)) X(void, glBindFramebuffer, (GLenum, GLuint)) X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)) X(void, glFinish, (void)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint*)) X(void, glBindVertexArray, (GLuint))
#define S23_GLDECL(ret, name, args) typedef ret (S23_GLAPI* PFN_##name) args; static PFN_##name name = nullptr;
S23_GLFUNCS(S23_GLDECL)
#undef S23_GLDECL

enum : GLenum {
    GL_NO_ERROR = 0, GL_TRIANGLES = 0x0004, GL_BLEND = 0x0BE2, GL_SCISSOR_TEST = 0x0C11, GL_DEPTH_TEST = 0x0B71, GL_CULL_FACE = 0x0B44,
    GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303, GL_ONE = 1, GL_ZERO = 0, GL_FUNC_ADD = 0x8006,
    GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_2D_ARRAY = 0x8C1A, GL_TEXTURE0 = 0x84C0, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800,
    GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803, GL_NEAREST = 0x2600, GL_CLAMP_TO_EDGE = 0x812F,
    GL_UNPACK_ALIGNMENT = 0x0CF5, GL_UNPACK_ROW_LENGTH = 0x0CF2, GL_PACK_ALIGNMENT = 0x0D05,
    GL_UNSIGNED_BYTE = 0x1401, GL_UNSIGNED_SHORT = 0x1403, GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406,
    GL_RGBA = 0x1908, GL_RGBA8 = 0x8058, GL_RED_INTEGER = 0x8D94, GL_RGBA_INTEGER = 0x8D99,
    GL_R8UI = 0x8232, GL_R16UI = 0x8234, GL_R32UI = 0x8236, GL_RGBA32UI = 0x8D70, GL_RGBA32F = 0x8814,
    GL_ARRAY_BUFFER = 0x8892, GL_STREAM_DRAW = 0x88E0, GL_FRAGMENT_SHADER = 0x8B30, GL_VERTEX_SHADER = 0x8B31,
    GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_INFO_LOG_LENGTH = 0x8B84,
    GL_FRAMEBUFFER = 0x8D40, GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_FRAMEBUFFER_BINDING = 0x8CA6, GL_VIEWPORT = 0x0BA2, GL_CURRENT_PROGRAM = 0x8B8D, GL_ACTIVE_TEXTURE = 0x84E0,
    GL_TEXTURE_BINDING_2D = 0x8069, GL_ARRAY_BUFFER_BINDING = 0x8894, GL_VERTEX_ARRAY_BINDING = 0x85B5,
    GL_BLEND_SRC_RGB = 0x80C9, GL_BLEND_DST_RGB = 0x80C8, GL_BLEND_SRC_ALPHA = 0x80CB, GL_BLEND_DST_ALPHA = 0x80CA,
    GL_VERSION = 0x1F02, GL_RENDERER = 0x1F01, GL_SHADING_LANGUAGE_VERSION = 0x8B8C,
};

// ---------------------------------------------------------------- frame data (prepared by the render thread)
struct Vtx { float x, y, w, u, v, i; uint32_t pid; };          // x, y: destination pixels; w: depth (perspective)
struct Frame {
    int W = 640, H = 480, xoff = 0, W43 = 640;                   // target, margin and 4:3 width (widescreen)
    bool have3d = false;
    std::vector<Vtx> vtx;
    std::vector<uint32_t> pu; std::vector<float> pf;               // per polygon: 4 x uvec4, 2 x vec4 (see export_polys)
    std::vector<uint32_t> pal;                                     // 0x8000 colours 0xRRGGBB
    std::vector<uint16_t> sten;                                    // C412 SRAM (0x20000 words)
    std::vector<uint32_t> text;                                    // 640x480 text layer: 0 = transparent, otherwise 0x01RRGGBB
    uint32_t bg[3] = {0, 0, 0}; uint8_t gamma[256 * 4];
    uint64_t prep_ns = 0;                                          // CPU time to prepare it (statistics)
};

// The polygons already sorted by g3d::prepare become triangles + per-polygon data. Same rules as tri()/scanline():
// coordinates relative to the polygon's window, rectangular and trapezoid clipping computed per line.
static void export_polys(const g3d::Frame& F, const std::vector<int>& order, Frame& G, double sc, int xoff) {
    G.vtx.clear(); G.pu.clear(); G.pf.clear();
    const int HH = (int)std::lround(480 * sc), WW = (int)std::lround(640 * sc);
    uint32_t pid = 0;
    for (int oi : order) {
        const g3d::Poly& p = F.polys[oi]; const g3d::RD& rd = p.rd;
        // clipping (like scanline): viewport window, screen, tri() limits
        s32 cl = (s32)std::lround(((320 - rd.vp_sx) + rd.vp_ox) * sc), cr = (s32)std::lround(((320 + rd.vp_sx) + rd.vp_ox) * sc);
        const s32 ct = (s32)std::lround(((240 - rd.vp_sy) - rd.vp_oy) * sc); s32 cb = (s32)std::lround(((240 + rd.vp_sy) - rd.vp_oy) * sc);
        if (xoff) { const bool full = rd.vp_sx >= 320 && rd.vp_ox == 0; cl += xoff; cr += xoff; if (full) { cl = 0; cr = G.W; } }
        const s32 xorg = (s32)std::lround(((320 - rd.vp_sx) + rd.vp_ox) * sc) + xoff;
        s32 x0 = std::max<s32>({cl, 0, xorg - xoff}), x1 = std::min<s32>({cr, G.W, xorg + WW + xoff});
        s32 y0 = std::max<s32>({ct, 0}), y1 = std::min<s32>({cb, G.H, ct + HH});
        if (x0 >= x1 || y0 >= y1) continue;
        uint32_t fl = (rd.stencil ? 1u : 0) | (rd.shade ? 2u : 0) | (rd.pfade ? 4u : 0) | (rd.blend ? 8u : 0) | (rd.alpha_enabled ? 16u : 0);
        float K[2] = {0, 0}, M[2] = {0, 0};
        if (rd.slant) {                                             // trapezoid: limit x = K + M * row (then rounded, like lround)
            fl |= 32u; const double cx = 320 + rd.vp_ox;
            for (int k = 0; k < 2; k++) {
                const double a = rd.cpl[k][0], b = rd.cpl[k][1], c = rd.cpl[k][2]; if (std::fabs(a) < 1e-9) continue;
                // bound(oy) = -(b * syw + c * cfov) / a, syw = (240 - vp_oy) - (oy + 0.5) / sc; x = (cx + bound) * sc + xoff
                M[k] = (float)(b / a); K[k] = (float)(sc * cx + xoff - (sc * (b * (240 - rd.vp_oy) + c * rd.cfov) - b * 0.5) / a);
                fl |= 64u << k; if ((a > 0) == (c > 0)) fl |= 256u << k;   // 256: left limit, otherwise right
            }
        }
        const u32 pens = rd.pens;
        G.pu.insert(G.pu.end(), {pens, rd.tbase, (uint32_t)rd.cmode | (fl << 8) | ((uint32_t)rd.alpha_pen << 20), (uint32_t)rd.alpha | ((uint32_t)rd.alpha_inv << 16),
                                 (uint32_t)rd.pcr | ((uint32_t)rd.pcg << 8) | ((uint32_t)rd.pcb << 16), (uint32_t)rd.fadefactor | ((uint32_t)rd.fadefactor_inv << 16),
                                 (uint32_t)rd.fcr | ((uint32_t)rd.fcg << 8) | ((uint32_t)rd.fcb << 16), (uint32_t)p.index,
                                 (uint32_t)x0, (uint32_t)x1, (uint32_t)y0, (uint32_t)y1, 0u, 0u, 0u, 0u});
        G.pf.insert(G.pf.end(), {rd.intensity_base, K[0], M[0], K[1], M[1], 0.f, 0.f, 0.f});
        auto put = [&](const g3d::PV& q) {
            const float w = q.p[0] != 0 ? 1.f / q.p[0] : 1.f;
            G.vtx.push_back(Vtx{(float)(q.x * sc + xorg), (float)(q.y * sc + ct), w, q.p[1] * w, q.p[2] * w, q.p[3] * w, pid});
        };
        auto tri = [&](const g3d::PV& a, const g3d::PV& b, const g3d::PV& c) { put(a); put(b); put(c); };
        if (p.nv == 4) {                                            // same diagonal as draw_poly (concave quads)
            auto side = [&](const g3d::PV& a, const g3d::PV& b, const g3d::PV& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
            const double s1 = side(p.pv[0], p.pv[2], p.pv[1]), s3 = side(p.pv[0], p.pv[2], p.pv[3]);
            if (s1 * s3 > 0) { tri(p.pv[1], p.pv[2], p.pv[3]); tri(p.pv[1], p.pv[3], p.pv[0]); }
            else { tri(p.pv[0], p.pv[1], p.pv[2]); tri(p.pv[0], p.pv[2], p.pv[3]); }
        } else for (int k = 1; k + 1 < p.nv; k++) tri(p.pv[0], p.pv[k], p.pv[k + 1]);
        pid++;
    }
}

// ---------------------------------------------------------------- shader
static const char* VS3D = R"(
in vec4 aPos; in vec3 aUVI; in uint aPid;
uniform vec2 uSize;
out vec3 vUVI; flat out uint vPid;
void main() {
    float w = aPos.z;
    gl_Position = vec4((aPos.x / uSize.x * 2.0 - 1.0) * w, (aPos.y / uSize.y * 2.0 - 1.0) * w, 0.0, w);
    vUVI = aUVI; vPid = aPid;
}
)";
static const char* FS3D = R"(
in vec3 vUVI; flat in uint vPid;
uniform usampler2DArray uRom; uniform usampler2D uTm; uniform usampler2D uPal; uniform usampler2D uSten;
uniform usampler2D uPU; uniform sampler2D uPF; uniform uint uTileMask; uniform int uDbg;
out vec4 oCol;
uvec4 PU(int k) { int i = int(vPid) * 4 + k; return texelFetch(uPU, ivec2(i & 1023, i >> 10), 0); }
vec4 PF(int k) { int i = int(vPid) * 2 + k; return texelFetch(uPF, ivec2(i & 1023, i >> 10), 0); }
void main() {
    uvec4 t0 = PU(0), t1 = PU(1), t2 = PU(2); vec4 f0 = PF(0), f1 = PF(1);
    int px = int(gl_FragCoord.x), oy = int(gl_FragCoord.y);
    int cl = int(t2.x), cr = int(t2.y);
    if (oy < int(t2.z) || oy >= int(t2.w)) discard;
    uint fl = (t0.z >> 8) & 0xfffu;
    if ((fl & 32u) != 0u) {
        float yy = float(oy);
        if ((fl & 64u) != 0u) { int b = int(floor(f0.y + f0.z * yy + 0.5)); if ((fl & 256u) != 0u) cl = max(cl, b); else cr = min(cr, b); }
        if ((fl & 128u) != 0u) { int b = int(floor(f0.w + f1.x * yy + 0.5)); if ((fl & 512u) != 0u) cl = max(cl, b); else cr = min(cr, b); }
    }
    if (px < cl || px >= cr) discard;
    // +1/1000: coordinates that are constant along an edge (e.g. v = 2880 at the vertices) interpolated in float reach 2879.9998 and,
    // truncated, took the row of texels above (the software, in double, stays at 2880)
    uint tx = uint(int(vUVI.x + (vUVI.x >= 0.0 ? 0.001 : -0.001))), ty = uint(int(vUVI.y + (vUVI.y >= 0.0 ? 0.001 : -0.001)));
    if ((fl & 1u) != 0u) {
        uint offs = ((ty << 6) | (tx >> 4)) & 0x1ffffu;
        uint w = texelFetch(uSten, ivec2(int(offs & 1023u), int(offs >> 10)), 0).r;
        if (((w >> ((tx & 15u) ^ 15u)) & 1u) == 0u) discard;
    }
    uint cmode = t0.z & 0xffu, pens = t0.x, penmask = 0xffu, penshift = 0u;
    if ((cmode & 4u) != 0u) { pens += 0xecu + ((cmode & 8u) << 1); penmask = 3u; penshift = 2u * (~cmode & 3u); }
    else if ((cmode & 2u) != 0u) { pens += 0xe0u + ((cmode & 8u) << 1); penmask = 15u; penshift = 4u * (~cmode & 1u); }
    uint u = tx, v = ty + t0.y;
    uint tileid = ((u >> 4) & 0xffu) | ((v << 4) & uTileMask);
    uint tmv = texelFetch(uTm, ivec2(int(tileid & 4095u), int(tileid >> 12)), 0).r;
    uint attr = tmv >> 25;
    if ((attr & 1u) != 0u) v = ~v;
    if ((attr & 2u) != 0u) u = ~u;
    if ((attr & 4u) != 0u) { uint t = u; u = v; v = t; }
    uint addr = ((tmv & 0x1ffffffu) | ((v << 4) & 0xf0u) | (u & 0x0fu)) & 0x1ffffffu;
    uint pen = texelFetch(uRom, ivec3(int(addr & 2047u), int((addr >> 11) & 2047u), int(addr >> 22)), 0).r;
    uint pi = (pens + ((pen >> penshift) & penmask)) & 0x7fffu;
    uint rgb = texelFetch(uPal, ivec2(int(pi & 255u), int(pi >> 8)), 0).r;
    int r = int((rgb >> 16) & 255u), g = int((rgb >> 8) & 255u), b = int(rgb & 255u);
    if ((fl & 2u) != 0u) { int sh = clamp(int(f0.x + vUVI.z), 0, 255); r = min((r * sh) >> 6, 255); g = min((g * sh) >> 6, 255); b = min((b * sh) >> 6, 255); }
    if ((fl & 4u) != 0u) { r = (r * int(t1.x & 255u)) >> 8; g = (g * int((t1.x >> 8) & 255u)) >> 8; b = (b * int((t1.x >> 16) & 255u)) >> 8; }
    uint ff = t1.y & 0xffffu;
    if (ff != 255u) { int fi = int(t1.y >> 16), f = int(ff); r = (r * f + int(t1.z & 255u) * fi) >> 8; g = (g * f + int((t1.z >> 8) & 255u) * fi) >> 8; b = (b * f + int((t1.z >> 16) & 255u) * fi) >> 8; }
    uint alpha = t0.w & 0xffffu;
    bool ua = alpha != 255u && ((fl & 16u) != 0u || pen == ((t0.z >> 20) & 0xffu));
    float a = ua ? max(float(alpha) / 256.0, 1.0 / 255.0) : ((fl & 8u) != 0u ? 0.5 : 1.0);
    oCol = vec4(vec3(float(r), float(g), float(b)) / 255.0, a);
    if (uDbg == 1) oCol = vec4(float(vPid & 255u), float((vPid >> 8) & 255u), float((vPid >> 16) & 255u), 255.0) / 255.0;
    if (uDbg == 2) oCol = vec4(float(tx & 255u), float(ty & 255u), float(clamp(int(f0.x + vUVI.z), 0, 255)), 255.0) / 255.0;
    if (uDbg == 3) oCol = vec4(float(pen), float(rgb & 255u), float((rgb >> 8) & 255u), 255.0) / 255.0;
}
)";
static const char* VSCOMP = R"(
in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
static const char* FSCOMP = R"(
uniform sampler2D u3d; uniform sampler2D uText; uniform sampler2D uGamma;
uniform vec3 uBg; uniform vec4 uGeo; uniform int uHave3d;
out vec4 oCol;
void main() {
    int X = int(gl_FragCoord.x), Y = int(gl_FragCoord.y);
    vec3 c = uBg;
    if (uHave3d != 0) { vec4 q = texelFetch(u3d, ivec2(X, Y), 0); if (q.a > 0.0) c = floor(q.rgb * 255.0 + 0.5); }
    int xr = X - int(uGeo.z);
    if (xr >= 0 && xr < int(uGeo.w)) {
        int tx = min(639, int((float(xr) + 0.5) * 640.0 / uGeo.w)), ty = min(479, int((float(Y) + 0.5) * 480.0 / uGeo.y));
        vec4 t = texelFetch(uText, ivec2(tx, ty), 0); if (t.a > 0.5) c = floor(t.rgb * 255.0 + 0.5);
    }
    ivec3 ci = ivec3(c);
    oCol = vec4(texelFetch(uGamma, ivec2(ci.r, 0), 0).r, texelFetch(uGamma, ivec2(ci.g, 0), 0).g, texelFetch(uGamma, ivec2(ci.b, 0), 0).b, 1.0);
}
)";

// ---------------------------------------------------------------- engine
struct Renderer {
    bool ok = false, es = false; std::string err;
    GLuint p3d = 0, pcomp = 0, vbo = 0, vboq = 0, vao = 0;
    GLuint tRom = 0, tTm = 0, tPal = 0, tSten = 0, tPU = 0, tPF = 0, tText = 0, tGamma = 0, t3d = 0, fb3d = 0, fbOut = 0;
    int puRows = 0, w3d = 0, h3d = 0; GLuint outTex = 0;
    GLint u3dSize = -1, uTileMask = -1, uDbg = -1; int dbg = 0;
    GLint uDummy_ = -1, uBg = -1, uGeo = -1, uHave = -1;
    uint64_t frames = 0;

    bool load() {
#define S23_GLLOAD(ret, name, args) name = (PFN_##name)SDL_GL_GetProcAddress(#name); if (!name) { err = std::string("missing function: ") + #name; if (std::string(#name) != "glGenVertexArrays" && std::string(#name) != "glBindVertexArray") return false; }
        S23_GLFUNCS(S23_GLLOAD)
#undef S23_GLLOAD
        err.clear(); return true;
    }
    GLuint shader(GLenum t, const char* src) {
        const char* hdr = es ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp usampler2D;\nprecision highp usampler2DArray;\nprecision highp sampler2D;\n"
                             : "#version 130\n";
        const char* s[2] = {hdr, src}; GLuint sh = glCreateShader(t); glShaderSource(sh, 2, s, nullptr); glCompileShader(sh);
        GLint okc = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &okc);
        if (!okc) { char log[2048] = {0}; glGetShaderInfoLog(sh, sizeof log, nullptr, log); err += std::string("shader: ") + log; return 0; }
        return sh;
    }
    GLuint program(const char* vs, const char* fs, const std::vector<const char*>& attrs) {
        GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs); if (!v || !f) return 0;
        GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
        for (size_t i = 0; i < attrs.size(); i++) glBindAttribLocation(p, (GLuint)i, attrs[i]);
        glLinkProgram(p); GLint okl = 0; glGetProgramiv(p, GL_LINK_STATUS, &okl);
        if (!okl) { char log[2048] = {0}; glGetProgramInfoLog(p, sizeof log, nullptr, log); err += std::string("link: ") + log; return 0; }
        return p;
    }
    static void tex_params(GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    GLuint new_tex2d(GLint ifmt, int w, int h, GLenum fmt, GLenum type, const void* data) {
        GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); tex_params(GL_TEXTURE_2D);
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data); return t;
    }

    // to be called with SDL's GL context current (after SDL_CreateRenderer). Also loads the graphics ROMs.
    bool init() {
        if (!load()) return false;
        const char* ver = (const char*)glGetString(GL_VERSION); es = ver && strstr(ver, "OpenGL ES");
        printf("GPU: %s | %s\n", ver ? ver : "?", (const char*)glGetString(GL_RENDERER)); fflush(stdout);
        if (!g3d::R.ok) { err = "graphics ROMs not loaded"; return false; }
        p3d = program(VS3D, FS3D, {"aPos", "aUVI", "aPid"}); pcomp = program(VSCOMP, FSCOMP, {"aPos"});
        if (!p3d || !pcomp) return false;
        GLint prevTex = 0, prevAct = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prevAct); glActiveTexture(GL_TEXTURE0 + 7);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // SDL leaves GL_UNPACK_ROW_LENGTH set after its uploads (for example the menu images):
        // the graphics ROMs would be read with skewed rows (textures all noise). It is reset and then restored.
        GLint prevRow = 0; glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRow); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        // texrom: 32 MB of 8-bit pens as a 2048 x 2048 x 8 array (ES 3.0 only guarantees 2048 per side)
        { std::vector<uint8_t> z; const uint8_t* src = g3d::R.texrom; if (g3d::R.tex_n < 0x2000000) { z.assign(0x2000000, 0); memcpy(z.data(), g3d::R.texrom, g3d::R.tex_n); src = z.data(); }
          glGenTextures(1, &tRom); glBindTexture(GL_TEXTURE_2D_ARRAY, tRom); tex_params(GL_TEXTURE_2D_ARRAY);
          glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8UI, 2048, 2048, 8, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, src); }
        // tile map: offset into the texrom (25 bits) + attributes (3 bits) in a 32-bit integer
        { const size_t n = std::max<size_t>(g3d::R.tm_n, (size_t)(g3d::R.tileid_mask | 0xff) + 1); const int rows = (int)((n + 4095) / 4096);
          std::vector<uint32_t> tm((size_t)rows * 4096, 0);
          for (size_t i = 0; i < g3d::R.tm_n; i++) tm[i] = (g3d::R.tm_decoded[i] & 0x1ffffff) | ((uint32_t)(g3d::R.tm_attr[i] & 7) << 25);
          tTm = new_tex2d(GL_R32UI, 4096, rows, GL_RED_INTEGER, GL_UNSIGNED_INT, tm.data()); }
        tPal = new_tex2d(GL_R32UI, 256, 128, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        tSten = new_tex2d(GL_R16UI, 1024, 128, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
        tText = new_tex2d(GL_RGBA8, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        tGamma = new_tex2d(GL_RGBA8, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glActiveTexture((GLenum)prevAct); (void)prevTex; glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRow);
        glGenBuffers(1, &vbo); glGenBuffers(1, &vboq);
        if (glGenVertexArrays && glBindVertexArray) glGenVertexArrays(1, &vao);
        glGenFramebuffers(1, &fb3d); glGenFramebuffers(1, &fbOut);
        u3dSize = glGetUniformLocation(p3d, "uSize"); uDbg = glGetUniformLocation(p3d, "uDbg"); uTileMask = glGetUniformLocation(p3d, "uTileMask");
        uBg = glGetUniformLocation(pcomp, "uBg"); uGeo = glGetUniformLocation(pcomp, "uGeo"); uHave = glGetUniformLocation(pcomp, "uHave3d");
        glUseProgram(p3d);
        static const char* s3[] = {"uRom", "uTm", "uPal", "uSten", "uPU", "uPF"}; for (int i = 0; i < 6; i++) glUniform1i(glGetUniformLocation(p3d, s3[i]), i);
        glUseProgram(pcomp);
        static const char* sc[] = {"u3d", "uText", "uGamma"}; for (int i = 0; i < 3; i++) glUniform1i(glGetUniformLocation(pcomp, sc[i]), i);
        glUseProgram(0);
        GLenum e = glGetError(); if (e != GL_NO_ERROR) { char b[64]; snprintf(b, sizeof b, "GL error 0x%x at startup", e); err = b; return false; }
        ok = true; return true;
    }

    // SDL's GL state: saved and restored around our drawing (SDL keeps a state cache)
    struct Saved { GLint fb, vp[4], prog, act, tex[8], abuf, vao; GLboolean blend, scis, depth, cull; GLint bsr, bdr, bsa, bda, unpack, rowlen; };
    void save(Saved& s) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fb); glGetIntegerv(GL_VIEWPORT, s.vp); glGetIntegerv(GL_CURRENT_PROGRAM, &s.prog);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &s.act);
        for (int i = 0; i < 8; i++) { glActiveTexture(GL_TEXTURE0 + i); glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex[i]); }
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.abuf); s.vao = 0; if (vao) glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
        s.blend = glIsEnabled(GL_BLEND); s.scis = glIsEnabled(GL_SCISSOR_TEST); s.depth = glIsEnabled(GL_DEPTH_TEST); s.cull = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_BLEND_SRC_RGB, &s.bsr); glGetIntegerv(GL_BLEND_DST_RGB, &s.bdr); glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.bsa); glGetIntegerv(GL_BLEND_DST_ALPHA, &s.bda);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &s.unpack); glGetIntegerv(GL_UNPACK_ROW_LENGTH, &s.rowlen);
    }
    void restore(const Saved& s) {
        if (vao) glBindVertexArray((GLuint)s.vao);                 // the enabled attributes live in our VAO: SDL's stays as it was
        else { glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2); }
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s.abuf);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        for (int i = 7; i >= 0; i--) { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, (GLuint)s.tex[i]); }
        glActiveTexture((GLenum)s.act); glUseProgram((GLuint)s.prog);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s.fb); glViewport(s.vp[0], s.vp[1], s.vp[2], s.vp[3]);
        if (s.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND); if (s.scis) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (s.depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); if (s.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glBlendFuncSeparate((GLenum)s.bsr, (GLenum)s.bdr, (GLenum)s.bsa, (GLenum)s.bda); glPixelStorei(GL_UNPACK_ALIGNMENT, s.unpack); glPixelStorei(GL_UNPACK_ROW_LENGTH, s.rowlen);
    }

    // draws the frame into the GL texture "target" (SDL's texture), F.W x F.H in size
    bool draw(const Frame& F, GLuint target) {
        if (!ok) return false;
        Saved sv; save(sv);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);                       // SDL leaves it set after its uploads (OSD text): the rows would be read too long
        if (vao) glBindVertexArray(vao);
        // 3D into an FBO the same size as the output
        if (F.have3d) {
            if (w3d != F.W || h3d != F.H) {
                // new texture before deleting the old one: so it doesn't get the same number (with the same number
                // the FBO stayed bound to the deleted texture and the 3D came out small or not at all, e.g. in full screen)
                const GLuint old = t3d; glActiveTexture(GL_TEXTURE0); t3d = new_tex2d(GL_RGBA8, F.W, F.H, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); w3d = F.W; h3d = F.H;
                glBindFramebuffer(GL_FRAMEBUFFER, fb3d); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t3d, 0);
                if (old) glDeleteTextures(1, &old);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { err = "3D FBO incomplete"; restore(sv); ok = false; return false; }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, fb3d); glViewport(0, 0, F.W, F.H);
            glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
            if (!F.vtx.empty()) {
                const int np = (int)(F.pu.size() / 16); const int rows = std::max(1, (np * 4 + 1023) / 1024);
                glActiveTexture(GL_TEXTURE0 + 0); glBindTexture(GL_TEXTURE_2D_ARRAY, tRom);
                glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, tTm);
                glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, tPal); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 128, GL_RED_INTEGER, GL_UNSIGNED_INT, F.pal.data());
                glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, tSten); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 128, GL_RED_INTEGER, GL_UNSIGNED_SHORT, F.sten.data());
                if (rows > puRows) {
                    if (tPU) { glDeleteTextures(1, &tPU); glDeleteTextures(1, &tPF); }
                    puRows = rows + 16;
                    glActiveTexture(GL_TEXTURE0 + 4); tPU = new_tex2d(GL_RGBA32UI, 1024, puRows, GL_RGBA_INTEGER, GL_UNSIGNED_INT, nullptr);
                    glActiveTexture(GL_TEXTURE0 + 5); tPF = new_tex2d(GL_RGBA32F, 1024, (puRows + 1) / 2, GL_RGBA, GL_FLOAT, nullptr);
                }
                { std::vector<uint32_t> pu(F.pu); pu.resize((size_t)rows * 1024 * 4, 0);
                  glActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, tPU); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, rows, GL_RGBA_INTEGER, GL_UNSIGNED_INT, pu.data());
                  const int frows = std::max(1, (np * 2 + 1023) / 1024); std::vector<float> pf(F.pf); pf.resize((size_t)frows * 1024 * 4, 0.f);
                  glActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, tPF); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, frows, GL_RGBA, GL_FLOAT, pf.data()); }
                glUseProgram(p3d); glUniform2f(u3dSize, (float)F.W, (float)F.H); glUniform1ui(uTileMask, g3d::R.tileid_mask); glUniform1i(uDbg, dbg);
                glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(F.vtx.size() * sizeof(Vtx)), F.vtx.data(), GL_STREAM_DRAW);
                glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(Vtx), (const void*)offsetof(Vtx, x));
                glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, sizeof(Vtx), (const void*)offsetof(Vtx, u));
                glEnableVertexAttribArray(2); glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(Vtx), (const void*)offsetof(Vtx, pid));
                glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)F.vtx.size());
                glDisable(GL_BLEND);
                glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
            }
        }
        // compositing into the output texture
        glBindFramebuffer(GL_FRAMEBUFFER, fbOut);
        // every time: SDL, when it recreates its texture (resolution change), may hand out the same number
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0); outTex = target;
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { err = "Output FBO incomplete"; restore(sv); ok = false; return false; }
        glViewport(0, 0, F.W, F.H);
        glActiveTexture(GL_TEXTURE0 + 0); glBindTexture(GL_TEXTURE_2D, F.have3d ? t3d : 0);
        glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, tText);
        { std::vector<uint8_t> t(640 * 480 * 4); for (size_t i = 0; i < F.text.size() && i < 640 * 480; i++) { uint32_t c = F.text[i]; t[4 * i] = (c >> 16) & 255; t[4 * i + 1] = (c >> 8) & 255; t[4 * i + 2] = c & 255; t[4 * i + 3] = c ? 255 : 0; }
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, t.data()); }
        glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, tGamma); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, F.gamma);
        glUseProgram(pcomp); glUniform3f(uBg, (float)F.bg[0], (float)F.bg[1], (float)F.bg[2]); glUniform4f(uGeo, (float)F.W, (float)F.H, (float)F.xoff, (float)F.W43); glUniform1i(uHave, F.have3d ? 1 : 0);
        static const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
        glBindBuffer(GL_ARRAY_BUFFER, vboq); glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 8, nullptr);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        GLenum e = glGetError();
        restore(sv); frames++;
        if (e != GL_NO_ERROR) { char b[64]; snprintf(b, sizeof b, "GL error 0x%x while drawing", e); err = b; static int n = 0; if (n++ < 5) { printf("GPU: %s\n", err.c_str()); fflush(stdout); } }
        return true;
    }
    // debug: reads a pixel of the 3D FBO (row 0 = top)
    uint32_t read3d(int x, int y) { Saved sv; save(sv); glBindFramebuffer(GL_FRAMEBUFFER, fb3d); uint8_t px[4] = {0}; glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px); restore(sv); return px[0] | (px[1] << 8) | (px[2] << 16); }
    // reads the last output (RGB, row 0 = top) for comparisons with the software renderer
    void read_out(int W, int H, std::vector<uint8_t>& rgb) {
        Saved sv; save(sv); glBindFramebuffer(GL_FRAMEBUFFER, fbOut); glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::vector<uint8_t> px((size_t)W * H * 4); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        rgb.resize((size_t)W * H * 3); for (size_t i = 0; i < (size_t)W * H; i++) { rgb[3 * i] = px[4 * i]; rgb[3 * i + 1] = px[4 * i + 1]; rgb[3 * i + 2] = px[4 * i + 2]; }
        restore(sv);
    }
};

} // namespace gl3d
