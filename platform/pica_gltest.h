// Banco di prova su PC del renderer per il 3DS (pica3d.h): esegue gli stessi lotti con OpenGL a funzioni fisse
// (texture env combine, come i combinatori del PICA200) in un FBO, per confrontarli con il disegno software.
// Solo test (S23_PICATEST con S23_GLCMP); richiede un contesto OpenGL di compatibilita'.
#pragma once
#include <SDL.h>
#include <vector>
#include "pica3d.h"

namespace picatest {
typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef float GLfloat; typedef unsigned char GLubyte;
enum : GLenum {
    TEXTURE_2D = 0x0DE1, RGBA = 0x1908, UNSIGNED_BYTE = 0x1401, NEAREST = 0x2600, TEX_MIN = 0x2801, TEX_MAG = 0x2800, WRAP_S = 0x2802, WRAP_T = 0x2803,
    CLAMP_EDGE = 0x812F, TEXTURE_ENV = 0x2300, TEX_ENV_MODE = 0x2200, TEX_ENV_COLOR = 0x2201, COMBINE = 0x8570, COMBINE_RGB = 0x8571, COMBINE_ALPHA = 0x8572,
    SRC0_RGB = 0x8580, SRC1_RGB = 0x8581, SRC2_RGB = 0x8582, SRC0_ALPHA = 0x8588, SRC1_ALPHA = 0x8589, SRC2_ALPHA = 0x858A,
    OP0_RGB = 0x8590, OP1_RGB = 0x8591, OP2_RGB = 0x8592, OP0_ALPHA = 0x8598, OP1_ALPHA = 0x8599, OP2_ALPHA = 0x859A,
    RGB_SCALE = 0x8573, MODULATE = 0x2100, INTERPOLATE = 0x8575, REPLACE = 0x1E01, TEXTURE = 0x1702, PRIMARY_COLOR = 0x8577, CONSTANT = 0x8576, PREVIOUS = 0x8578,
    SRC_COLOR = 0x0300, SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303, CONSTANT_ALPHA = 0x8003, ONE_MINUS_CONSTANT_ALPHA = 0x8004, ZERO = 0, ONE = 1,
    BLEND = 0x0BE2, ALPHA_TEST = 0x0BC0, GREATER = 0x0204, DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44, SCISSOR = 0x0C11, TEXTURE0 = 0x84C0,
    FRAMEBUFFER = 0x8D40, COLOR_ATTACHMENT0 = 0x8CE0, COLOR_BUFFER_BIT = 0x4000, PROJECTION = 0x1701, MODELVIEW = 0x1700, TRIANGLES = 0x0004,
    VIEWPORT = 0x0BA2, FRAMEBUFFER_BINDING = 0x8CA6, CURRENT_PROGRAM = 0x8B8D, PACK_ALIGNMENT = 0x0D05, UNPACK_ALIGNMENT = 0x0CF5, UNPACK_ROW_LENGTH = 0x0CF2,
    ACTIVE_TEXTURE = 0x84E0, TEXTURE_BINDING_2D = 0x8069, ARRAY_BUFFER = 0x8892, ARRAY_BUFFER_BINDING = 0x8894, VERTEX_ARRAY_BINDING = 0x85B5
};
#define PT_FN(ret, name, args) static ret (*name) args = nullptr;
PT_FN(void, glGenTextures, (GLsizei, GLuint*)) PT_FN(void, glBindTexture, (GLenum, GLuint)) PT_FN(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))
PT_FN(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) PT_FN(void, glTexParameteri, (GLenum, GLenum, GLint))
PT_FN(void, glTexEnvi, (GLenum, GLenum, GLint)) PT_FN(void, glTexEnvfv, (GLenum, GLenum, const GLfloat*)) PT_FN(void, glActiveTexture, (GLenum))
PT_FN(void, glEnable, (GLenum)) PT_FN(void, glDisable, (GLenum)) PT_FN(void, glBlendFunc, (GLenum, GLenum)) PT_FN(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))
PT_FN(void, glAlphaFunc, (GLenum, GLfloat)) PT_FN(void, glGenFramebuffers, (GLsizei, GLuint*)) PT_FN(void, glBindFramebuffer, (GLenum, GLuint))
PT_FN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) PT_FN(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) PT_FN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))
PT_FN(void, glClear, (GLenum)) PT_FN(void, glMatrixMode, (GLenum)) PT_FN(void, glLoadIdentity, ()) PT_FN(void, glOrtho, (double, double, double, double, double, double))
PT_FN(void, glBegin, (GLenum)) PT_FN(void, glEnd, ()) PT_FN(void, glVertex4f, (GLfloat, GLfloat, GLfloat, GLfloat)) PT_FN(void, glTexCoord2f, (GLfloat, GLfloat))
PT_FN(void, glColor4ub, (GLubyte, GLubyte, GLubyte, GLubyte)) PT_FN(void, glMultiTexCoord2f, (GLenum, GLfloat, GLfloat)) PT_FN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))
PT_FN(void, glGetIntegerv, (GLenum, GLint*)) PT_FN(void, glUseProgram, (GLuint)) PT_FN(void, glPixelStorei, (GLenum, GLint)) PT_FN(void, glBindBuffer, (GLenum, GLuint)) PT_FN(void, glBindVertexArray, (GLuint))
PT_FN(void, glPushAttrib, (GLenum)) PT_FN(void, glPopAttrib, ())
#undef PT_FN

struct GLTest : pica::Backend {
    std::vector<GLuint> tex; GLuint white = 0, fbo = 0, out = 0; int ow = 0, oh = 0; bool ok = false;
    bool flip_t() const override { return false; }
    int per = 1;                                       // slot per atlante (64 = atlanti 1024x1024 come sul 3DS; S23_PICAATLAS)
    int tex_of(int s) const override { return s / per; }
    int slots_per_tex() const override { return per; }
    pica::SlotMap slot_map(int s) const override {
        if (per == 1) return {0, 1, 0, 1};
        const int i = s % per, ax = i % 8, ay = i / 8; const float C = (float)pica::CELL;
        return {ax * C / 1024.f, C / 1024.f, ay * C / 1024.f, C / 1024.f};
    }
    void upload_slot(int s, const uint32_t* rgba) override {
        std::vector<uint8_t> b(pica::CELL * pica::CELL * 4);
        for (int i = 0; i < pica::CELL * pica::CELL; i++) { uint32_t v = rgba[i]; b[4 * i] = v >> 24; b[4 * i + 1] = v >> 16; b[4 * i + 2] = v >> 8; b[4 * i + 3] = v; }
        glPixelStorei(UNPACK_ALIGNMENT, 4); glPixelStorei(UNPACK_ROW_LENGTH, 0);
        const int i = s % per; glBindTexture(TEXTURE_2D, tex[s / per]);
        glTexSubImage2D(TEXTURE_2D, 0, per > 1 ? (i % 8) * pica::CELL : 0, per > 1 ? (i / 8) * pica::CELL : 0, pica::CELL, pica::CELL, RGBA, UNSIGNED_BYTE, b.data());
    }
    bool init(int slots, int per_atlas = 1) {
        per = per_atlas > 1 ? 64 : 1; const int ntex = (slots + per - 1) / per, side = per > 1 ? 1024 : pica::CELL;
#define L(n) n = (decltype(n))SDL_GL_GetProcAddress(#n); if (!n) { printf("PICATEST: manca %s\n", #n); return false; }
        L(glGenTextures) L(glBindTexture) L(glTexImage2D) L(glTexSubImage2D) L(glTexParameteri) L(glTexEnvi) L(glTexEnvfv) L(glActiveTexture) L(glEnable) L(glDisable)
        L(glBlendFunc) L(glBlendColor) L(glAlphaFunc) L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D) L(glViewport) L(glClearColor) L(glClear)
        L(glMatrixMode) L(glLoadIdentity) L(glOrtho) L(glBegin) L(glEnd) L(glVertex4f) L(glTexCoord2f) L(glColor4ub) L(glMultiTexCoord2f) L(glReadPixels) L(glGetIntegerv)
        L(glUseProgram) L(glPixelStorei) L(glBindBuffer) L(glBindVertexArray) L(glPushAttrib) L(glPopAttrib)
#undef L
        tex.resize(ntex); glGenTextures(ntex, tex.data());
        for (GLuint t : tex) { glBindTexture(TEXTURE_2D, t); params(); glTexImage2D(TEXTURE_2D, 0, RGBA, side, side, 0, RGBA, UNSIGNED_BYTE, nullptr); }
        glGenTextures(1, &white); glBindTexture(TEXTURE_2D, white); params(); const uint32_t w = 0xffffffffu; glTexImage2D(TEXTURE_2D, 0, RGBA, 1, 1, 0, RGBA, UNSIGNED_BYTE, &w);
        glGenFramebuffers(1, &fbo); ok = true; return true;
    }
    static void params() { glTexParameteri(TEXTURE_2D, TEX_MIN, NEAREST); glTexParameteri(TEXTURE_2D, TEX_MAG, NEAREST); glTexParameteri(TEXTURE_2D, WRAP_S, CLAMP_EDGE); glTexParameteri(TEXTURE_2D, WRAP_T, CLAMP_EDGE); }
    // disegna i lotti in un'immagine W x H (RGB, riga 0 in alto)
    void draw(const pica::Renderer& r, int W, int H, std::vector<uint8_t>& rgb) {
        GLint prevFbo = 0, prevProg = 0, prevVp[4], prevAct = 0, prevTex = 0, prevBuf = 0, prevVao = 0;
        glGetIntegerv(FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(CURRENT_PROGRAM, &prevProg); glGetIntegerv(VIEWPORT, prevVp); glGetIntegerv(ACTIVE_TEXTURE, &prevAct);
        glGetIntegerv(ARRAY_BUFFER_BINDING, &prevBuf); glGetIntegerv(VERTEX_ARRAY_BINDING, &prevVao);
        glPushAttrib(0x000fffff);
        if (ow != W || oh != H) { glGenTextures(1, &out); glBindTexture(TEXTURE_2D, out); params(); glTexImage2D(TEXTURE_2D, 0, RGBA, W, H, 0, RGBA, UNSIGNED_BYTE, nullptr); ow = W; oh = H; }
        glBindFramebuffer(FRAMEBUFFER, fbo); glFramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, out, 0);
        glUseProgram(0); glBindBuffer(ARRAY_BUFFER, 0); glBindVertexArray(0);
        glViewport(0, 0, W, H); glClearColor(0, 0, 0, 1); glClear(COLOR_BUFFER_BIT);
        glMatrixMode(PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1); glMatrixMode(MODELVIEW); glLoadIdentity();   // y in basso: la lettura capovolge
        glDisable(DEPTH_TEST); glDisable(CULL_FACE); glDisable(SCISSOR); glEnable(ALPHA_TEST); glAlphaFunc(GREATER, 0.f);
        for (const pica::Batch& b : r.batches) {
            glActiveTexture(TEXTURE0); glEnable(TEXTURE_2D); glBindTexture(TEXTURE_2D, b.slot >= 0 ? tex[b.slot] : white);
            glTexEnvi(TEXTURE_ENV, TEX_ENV_MODE, COMBINE);
            glTexEnvi(TEXTURE_ENV, COMBINE_RGB, MODULATE); glTexEnvi(TEXTURE_ENV, SRC0_RGB, TEXTURE); glTexEnvi(TEXTURE_ENV, SRC1_RGB, PRIMARY_COLOR);
            glTexEnvi(TEXTURE_ENV, OP0_RGB, SRC_COLOR); glTexEnvi(TEXTURE_ENV, OP1_RGB, SRC_COLOR); glTexEnvi(TEXTURE_ENV, RGB_SCALE, 4);
            if (b.sten) { glTexEnvi(TEXTURE_ENV, COMBINE_ALPHA, MODULATE); glTexEnvi(TEXTURE_ENV, SRC0_ALPHA, TEXTURE); glTexEnvi(TEXTURE_ENV, SRC1_ALPHA, PRIMARY_COLOR); }
            else { glTexEnvi(TEXTURE_ENV, COMBINE_ALPHA, INTERPOLATE); glTexEnvi(TEXTURE_ENV, SRC0_ALPHA, PRIMARY_COLOR); glTexEnvi(TEXTURE_ENV, SRC1_ALPHA, CONSTANT); glTexEnvi(TEXTURE_ENV, SRC2_ALPHA, TEXTURE); }
            glTexEnvi(TEXTURE_ENV, OP0_ALPHA, SRC_ALPHA); glTexEnvi(TEXTURE_ENV, OP1_ALPHA, SRC_ALPHA); glTexEnvi(TEXTURE_ENV, OP2_ALPHA, SRC_ALPHA);
            const GLfloat c0[4] = {0, 0, 0, b.ca / 255.f}; glTexEnvfv(TEXTURE_ENV, TEX_ENV_COLOR, c0);
            glActiveTexture(TEXTURE0 + 1); glEnable(TEXTURE_2D); glBindTexture(TEXTURE_2D, white);
            glTexEnvi(TEXTURE_ENV, TEX_ENV_MODE, COMBINE);
            glTexEnvi(TEXTURE_ENV, COMBINE_RGB, INTERPOLATE); glTexEnvi(TEXTURE_ENV, SRC0_RGB, PREVIOUS); glTexEnvi(TEXTURE_ENV, SRC1_RGB, CONSTANT); glTexEnvi(TEXTURE_ENV, SRC2_RGB, CONSTANT);
            glTexEnvi(TEXTURE_ENV, OP0_RGB, SRC_COLOR); glTexEnvi(TEXTURE_ENV, OP1_RGB, SRC_COLOR); glTexEnvi(TEXTURE_ENV, OP2_RGB, SRC_ALPHA); glTexEnvi(TEXTURE_ENV, RGB_SCALE, 1);
            glTexEnvi(TEXTURE_ENV, COMBINE_ALPHA, REPLACE); glTexEnvi(TEXTURE_ENV, SRC0_ALPHA, PREVIOUS); glTexEnvi(TEXTURE_ENV, OP0_ALPHA, SRC_ALPHA);
            const GLfloat c1[4] = {(b.fade >> 24) / 255.f, ((b.fade >> 16) & 255) / 255.f, ((b.fade >> 8) & 255) / 255.f, (b.fade & 255) / 255.f}; glTexEnvfv(TEXTURE_ENV, TEX_ENV_COLOR, c1);
            if (b.mode == 1) { glEnable(BLEND); glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA); }
            else if (b.mode == 2) { glEnable(BLEND); glBlendColor(0, 0, 0, 0.5f); glBlendFunc(CONSTANT_ALPHA, ONE_MINUS_CONSTANT_ALPHA); }
            else glDisable(BLEND);
            glBegin(TRIANGLES);
            for (int i = b.first; i < b.first + b.count; i++) {
                const pica::V& v = r.verts[i];
                glMultiTexCoord2f(TEXTURE0, v.s, v.t); glColor4ub(v.r, v.g, v.b, v.a);
                glVertex4f(v.x, (float)H * v.w - v.y, v.z * 0, v.w);                  // y capovolta (H - y), sempre moltiplicata per w
            }
            glEnd();
        }
        rgb.resize((size_t)W * H * 3); glPixelStorei(PACK_ALIGNMENT, 1);
        std::vector<uint8_t> tmp((size_t)W * H * 3); glReadPixels(0, 0, W, H, 0x1907 /*RGB*/, UNSIGNED_BYTE, tmp.data());
        for (int y = 0; y < H; y++) memcpy(&rgb[(size_t)y * W * 3], &tmp[(size_t)(H - 1 - y) * W * 3], (size_t)W * 3);
        glActiveTexture(TEXTURE0 + 1); glDisable(TEXTURE_2D);
        glPopAttrib();
        glActiveTexture((GLenum)prevAct); glBindFramebuffer(FRAMEBUFFER, (GLuint)prevFbo); glUseProgram((GLuint)prevProg); glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
        glBindBuffer(ARRAY_BUFFER, (GLuint)prevBuf); glBindVertexArray((GLuint)prevVao);
    }
};
}  // namespace picatest
