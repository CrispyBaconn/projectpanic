// SDL2 frontend for recompiled Panic Park: input mapping (keyboard + controller), configuration saved to a file
// and settings screen before startup. The inputs go to the emulated ASCA I/O board (AscaInputs).
#pragma once
#ifdef USE_SDL
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "font8x16.h"
#include "i18n.h"
#include "../h8/ascasys.h"

// ---------------------------------------------------------------- screen refresh (the game always runs at 59.9 Hz)
static const double FPS_LIST[] = {0, 59.9, 60, 75, 100, 120, 144, 165, 240, -1, 30};   // 0 = VSync, -1 = unlimited, 30 = half (slow consoles)
static const int FPS_N = (int)(sizeof FPS_LIST / sizeof FPS_LIST[0]);

// ---------------------------------------------------------------- actions
enum Act { A_LEFT, A_RIGHT, A_COIN, A_START1, A_START2, A_UP, A_DOWN, A_ENTER, A_TEST, A_SERVICE, A_COUNT };
struct ActInfo { const char* id; const char* name; };
static const ActInfo ACT_INFO[A_COUNT] = {
    {"left", "Move left"}, {"right", "Move right"}, {"coin", "Coin (credit)"}, {"start1", "Player 1 start"},
    {"start2", "Player 2 start"}, {"up", "Up (select)"}, {"down", "Down (select)"}, {"enter", "Enter / button 1"},
    {"test", "Test (cabinet)"}, {"service", "Config DIP on/off"},
};

struct Config {
    int key[A_COUNT][2];                       // SDL scancode (0 = none), two keys per action
    int pad[A_COUNT];                          // SDL_GameControllerButton (-1 = none)
    int deadzone = 12;                         // % of stick travel
    int sens = 100;                            // % of analog axis travel
    int invert = 0;                            // inverts the axis
    int axis = 0;                              // 0 = left stick X, 1 = right stick X
    int fullscreen = 0;
    std::string ui_lang;                       // files in lang/ (without .json); empty = system language
    int game_lang = -1;                        // -1 = leave unchanged; 0..7 = JPN ENG GER FRA SPA POR ITA KOR
    int fps = 0;                               // screen refresh: 0 = monitor VSync, then FPS_LIST (the game stays at 59.9 Hz)
    int wide = 0;                              // 1 = experimental 16:9 (more is visible at the sides, text stays 4:3)
    int res3d = 1;                             // 0 = 3D at 640x480, 1 = at window resolution (4:3), 2 = 320x240 (fast)
    int touch = 1;                             // touch: 1 = trackpad (the finger moves the lever, which stays where it is), 0 = joystick (returns to centre)
    int touchui = 1;                           // on-screen buttons (coin, service, menu) on touchscreens: 1 yes, 0 no
    int gpu = 1;                               // 1 = 3D drawn by the graphics card (OpenGL), 0 = by the CPU
    int skip_post = 0;                         // DIP 7 (skip POST)
    int service_dip = 0;                       // Service DIP on at startup
    Config() { defaults(); }
    void defaults() {
        memset(key, 0, sizeof key); for (int i = 0; i < A_COUNT; i++) pad[i] = -1;
        set(A_LEFT, SDL_SCANCODE_LEFT, SDL_SCANCODE_A, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        set(A_RIGHT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_D, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        set(A_COIN, SDL_SCANCODE_5, SDL_SCANCODE_C, SDL_CONTROLLER_BUTTON_A);
        set(A_START1, SDL_SCANCODE_1, SDL_SCANCODE_RETURN, SDL_CONTROLLER_BUTTON_START);
        set(A_START2, SDL_SCANCODE_2, 0, SDL_CONTROLLER_BUTTON_Y);
        set(A_UP, SDL_SCANCODE_UP, SDL_SCANCODE_W, SDL_CONTROLLER_BUTTON_DPAD_UP);
        set(A_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_S, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        set(A_ENTER, SDL_SCANCODE_Z, SDL_SCANCODE_SPACE, SDL_CONTROLLER_BUTTON_B);
        set(A_TEST, SDL_SCANCODE_F2, 0, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        set(A_SERVICE, SDL_SCANCODE_F3, 0, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        deadzone = 12; sens = 100; invert = 0; axis = 0; fullscreen = 0; res3d = 1; wide = 0; game_lang = -1; fps = 0; skip_post = 0; service_dip = 0; gpu = 1; touchui = 1;
#if defined(__WIIU__) || defined(__SWITCH__) || defined(__ANDROID__)
#if !defined(__SWITCH__)
        gpu = 0;                                                 // GPU not yet tested on Wii U (no OpenGL) and phones
#endif
        fullscreen = 1; res3d = 2; skip_post = 1;               // consoles: 3D at 320x240 (slow CPU), always full screen
#ifdef __ANDROID__
        res3d = 0;                                              // phones can handle the 3D at 640x480
#endif
#endif
    }
    void set(int a, int k1, int k2, int p) { key[a][0] = k1; key[a][1] = k2; pad[a] = p; }

    static std::string path() {
#if defined(__WIIU__) || defined(__SWITCH__) || defined(__ANDROID__)
        return std::string(S23_APP_DIR) + "/panicprk.ini";
#else
        std::string p; if (char* d = SDL_GetPrefPath("panicprk", "recomp")) { p = d; SDL_free(d); }
        return p + "panicprk.ini";
#endif
    }
    void save() const {
        FILE* f = fopen(path().c_str(), "w"); if (!f) { fprintf(stderr, "cannot save %s\n", path().c_str()); return; }
        fprintf(f, "# Panic Park recompiled - settings (editable by hand or from the start screen)\n");
        for (int i = 0; i < A_COUNT; i++) fprintf(f, "%s_key1=%d\n%s_key2=%d\n%s_pad=%d\n", ACT_INFO[i].id, key[i][0], ACT_INFO[i].id, key[i][1], ACT_INFO[i].id, pad[i]);
        fprintf(f, "deadzone=%d\nsens=%d\ninvert=%d\naxis=%d\nfullscreen=%d\nres3d=%d\nwide=%d\nfps=%d\ntouch=%d\ntouchui=%d\ngpu=%d\ngame_lang=%d\nui_lang=%s\nskip_post=%d\nservice_dip=%d\n", deadzone, sens, invert, axis, fullscreen, res3d, wide, fps, touch, touchui, gpu, game_lang, ui_lang.c_str(), skip_post, service_dip);
        fclose(f);
    }
    bool load() {
        FILE* f = fopen(path().c_str(), "r"); if (!f) return false;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char* eq = strchr(line, '='); if (line[0] == '#' || !eq) continue; *eq = 0; int v = atoi(eq + 1); std::string k = line;
            for (int i = 0; i < A_COUNT; i++) {
                std::string id = ACT_INFO[i].id;
                if (k == id + "_key1") key[i][0] = v; else if (k == id + "_key2") key[i][1] = v; else if (k == id + "_pad") pad[i] = v;
            }
            if (k == "deadzone") deadzone = v; else if (k == "sens") sens = v; else if (k == "invert") invert = v; else if (k == "axis") axis = v;
            else if (k == "fullscreen") fullscreen = v; else if (k == "res3d") res3d = v < 0 ? 0 : v > 2 ? 2 : v; else if (k == "wide") wide = v; else if (k == "fps") fps = v; else if (k == "touch") touch = v ? 1 : 0; else if (k == "gpu") gpu = v ? 1 : 0; else if (k == "touchui") touchui = v ? 1 : 0; else if (k == "game_lang") game_lang = v;
            else if (k == "ui_lang") { std::string t = eq + 1; while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back(); ui_lang = t; } else if (k == "skip_post") skip_post = v; else if (k == "service_dip") service_dip = v;
        }
        fclose(f); deadzone = deadzone < 0 ? 0 : deadzone > 60 ? 60 : deadzone; sens = sens < 20 ? 20 : sens > 100 ? 100 : sens; return true;
    }
};

// ---------------------------------------------------------------- controller
struct Pads {
    std::vector<SDL_GameController*> list;
    void rescan() {
        for (auto c : list) SDL_GameControllerClose(c); list.clear();
        for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) if (SDL_GameController* c = SDL_GameControllerOpen(i)) list.push_back(c);
    }
    bool button(int b) const { if (b < 0) return false; for (auto c : list) if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b)) return true; return false; }
    float axis(SDL_GameControllerAxis a) const {            // sum over the controllers, in [-1, 1]
        float v = 0; for (auto c : list) { float x = SDL_GameControllerGetAxis(c, a) / 32767.0f; if (fabsf(x) > fabsf(v)) v = x; } return v;
    }
    std::string name() const { return list.empty() ? "" : SDL_GameControllerName(list[0]); }
};

// ---------------------------------------------------------------- input state -> ASCA board
struct InputState {
    Config cfg; Pads pads;
    bool test_on = false, service_dip = false;
    uint64_t coin_until = 0;
    bool prev_service = false, prev_test = false;
    // ---- touch (Android, but works with any touchscreen): dragging the finger right/left moves the
    // player 1 like the stick (relative to where the finger was placed); a short tap = Start + Enter;
    // the top-left corner = coin. Normalised 0..1 screen coordinates.
    SDL_FingerID t_id = -1; bool t_on = false, t_used = false; float t_pos = 0; float t_xs = 0, t_x0 = 0, t_y0 = 0, t_axis = 0, t_maxd = 0; uint64_t t_down = 0, t_btn_until = 0, t_coin_until = 0, t_svc_until = 0;
    static constexpr float T_RANGE = 0.18f;                    // offset (fraction of the width) for full steering
    static bool coin_zone(float x, float y) { return x < 0.16f && y < 0.22f; }
    static bool svc_zone(float x, float y) { return x > 0.84f && y < 0.22f; }      // top-right corner = service DIP
    static bool menu_zone(float x, float y) { return x > 0.42f && x < 0.58f && y < 0.10f; }   // top centre = settings menu
    bool t_menu_req = false;                                   // tap on MENU: the window opens the settings
    // on-screen buttons for the service/test menu (they appear only there): UP, DOWN, ENTER, held while the finger stays down
    struct TBtn { float x0, y0, x1, y1; uint32_t bit; const char* key; };
    static const TBtn* menu_btns(int& n) {
        static const TBtn B[3] = { {0.84f, 0.26f, 0.98f, 0.45f, 0x20, "touch_up"}, {0.84f, 0.47f, 0.98f, 0.66f, 0x10, "touch_down"}, {0.84f, 0.70f, 0.98f, 0.89f, 0x02, "touch_ok"} };
        n = 3; return B;
    }
    bool menu_mode() const { return service_dip || test_on; }
    SDL_FingerID mb_id[4] = {-1, -1, -1, -1}; uint32_t mb_bit[4] = {0, 0, 0, 0};
    uint32_t menu_held() const { uint32_t m = 0; for (int i = 0; i < 4; i++) if (mb_id[i] != -1) m |= mb_bit[i]; return m; }
    void touch(const SDL_Event& e, uint64_t now_ms) {
        if (e.type == SDL_FINGERDOWN) {
            if (cfg.touchui) {                                  // on-screen buttons (can be disabled in the settings)
                if (coin_zone(e.tfinger.x, e.tfinger.y)) { t_coin_until = now_ms + 120; return; }
                if (svc_zone(e.tfinger.x, e.tfinger.y)) { t_svc_until = now_ms + 100; return; }
                if (menu_zone(e.tfinger.x, e.tfinger.y)) { t_menu_req = true; return; }
            }
            if (menu_mode() && cfg.touchui) { int n; const TBtn* B = menu_btns(n);
                for (int k = 0; k < n; k++) if (e.tfinger.x >= B[k].x0 && e.tfinger.x < B[k].x1 && e.tfinger.y >= B[k].y0 && e.tfinger.y < B[k].y1) {
                    for (int i = 0; i < 4; i++) if (mb_id[i] == -1) { mb_id[i] = e.tfinger.fingerId; mb_bit[i] = B[k].bit; break; }
                    return; } }
            if (t_on) return;                                   // only the first finger steers
            t_on = true; t_id = e.tfinger.fingerId; t_xs = t_x0 = e.tfinger.x; t_y0 = e.tfinger.y; t_axis = 0; t_maxd = 0; t_down = now_ms;
        } else if (e.type == SDL_FINGERMOTION && t_on && e.tfinger.fingerId == t_id) {
            t_maxd = std::max(t_maxd, std::max(std::fabs(e.tfinger.x - t_xs), std::fabs(e.tfinger.y - t_y0)));
            if (cfg.touch) {                                    // trackpad: moving the finger moves the lever (with acceleration,
                const float dx = e.tfinger.dx;                  // like a laptop touchpad); lifting the finger leaves it there
                const float acc = 1.f + std::min(2.f, std::fabs(dx) * 40.f);
                t_pos = std::max(-1.f, std::min(1.f, t_pos + dx * 4.f * acc));
                t_used = true; return;
            }
            float d = e.tfinger.x - t_x0;
            if (d > T_RANGE) { t_x0 = e.tfinger.x - T_RANGE; d = T_RANGE; }          // past the end of travel the starting point follows the finger:
            if (d < -T_RANGE) { t_x0 = e.tfinger.x + T_RANGE; d = -T_RANGE; }        // when going back the steering responds immediately
            t_axis = d / T_RANGE;
        } else if (e.type == SDL_FINGERUP && [&] { for (int i = 0; i < 4; i++) if (mb_id[i] == e.tfinger.fingerId) { mb_id[i] = -1; return true; } return false; }()) {
            // a menu button was released
        } else if (e.type == SDL_FINGERUP && t_on && e.tfinger.fingerId == t_id) {
            if (now_ms - t_down < 250 && t_maxd < 0.03f) t_btn_until = now_ms + 150;  // short tap: Start + Enter
            t_on = false; t_axis = 0;
        }
    }
    bool key_down(const Uint8* ks, int a) const { return (cfg.key[a][0] && ks[cfg.key[a][0]]) || (cfg.key[a][1] && ks[cfg.key[a][1]]); }
    bool down(const Uint8* ks, int a) const { return key_down(ks, a) || pads.button(cfg.pad[a]); }
    // updates AscaInputs; returns true if the service DIP changed (the host updates g_dsw)
    bool update(AscaInputs& in, uint64_t now_ms) {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        uint32_t p1 = 0;
        if (down(ks, A_UP)) p1 |= 0x20;
        if (down(ks, A_DOWN)) p1 |= 0x10;
        if (down(ks, A_ENTER)) p1 |= 0x02;
        if (down(ks, A_START1)) p1 |= 0x01;
        if (down(ks, A_START2)) p1 |= 0x8000;
        if (now_ms < t_btn_until) p1 |= 0x03;                                         // short tap
        if (menu_mode()) p1 |= menu_held(); else for (auto& id : mb_id) id = -1;        // menu on-screen buttons
        in.p1 = p1;
        if (down(ks, A_COIN)) coin_until = now_ms + 80;                                // the coin stays active for at least 80 ms (the firmware samples every ~16 ms)
        if (now_ms < t_coin_until) coin_until = std::max(coin_until, t_coin_until);
        in.coin1 = now_ms < coin_until ? 1 : 0;
        bool tst = down(ks, A_TEST); if (tst && !prev_test) test_on = !test_on; prev_test = tst;
        in.system = test_on ? 0x80 : 0;
        float v = pads.axis(cfg.axis ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX);
        float dz = cfg.deadzone / 100.0f;
        v = fabsf(v) < dz ? 0 : (v > 0 ? (v - dz) : (v + dz)) / (1 - dz);
        if (cfg.touch) { if (t_used && std::fabs(v) < 0.05f) v = t_pos; }               // finger on the screen: trackpad
        else if (t_on && t_axis != 0) v = t_axis;                                     // finger on the screen: joystick
        if (down(ks, A_LEFT)) v = -1; if (down(ks, A_RIGHT)) v = 1;
        if (cfg.invert) v = -v;
        float x = 0x8000 + v * 0x7fff * (cfg.sens / 100.0f);
        in.analog[0] = (uint16_t)(x < 0 ? 0 : x > 0xffff ? 0xffff : x);
        bool sv = down(ks, A_SERVICE) || now_ms < t_svc_until, changed = false;
        if (sv && !prev_service) { service_dip = !service_dip; changed = true; }
        prev_service = sv; return changed;
    }
};

// ---------------------------------------------------------------- text (font atlas)
struct Text {
    SDL_Renderer* ren = nullptr; SDL_Texture* atlas = nullptr;
    void init(SDL_Renderer* r) {
        ren = r; SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, FONT_COUNT * 8, 16, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_FillRect(s, nullptr, 0);
        Uint32* px = (Uint32*)s->pixels;
        for (int g = 0; g < FONT_COUNT; g++) for (int y = 0; y < 16; y++) for (int x = 0; x < 8; x++)
            if (FONT_DATA[g][y] & (0x80 >> x)) px[y * (s->pitch / 4) + g * 8 + x] = 0xffffffffu;
        atlas = SDL_CreateTextureFromSurface(r, s); SDL_FreeSurface(s); SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    }
    static int glyph(uint32_t c) { for (int i = 0; i < FONT_COUNT; i++) if (FONT_CODES[i] == c) return i; return 0x3f - 0x20; }
    int draw(int x, int y, const std::string& s, SDL_Color col, int scale = 1) {
        SDL_SetTextureColorMod(atlas, col.r, col.g, col.b);
        for (size_t i = 0; i < s.size();) {                                       // UTF-8
            uint32_t c = (uint8_t)s[i]; int n = 1;
            if (c >= 0xf0) { c &= 7; n = 4; } else if (c >= 0xe0) { c &= 15; n = 3; } else if (c >= 0xc0) { c &= 31; n = 2; }
            for (int k = 1; k < n && i + k < s.size(); k++) c = (c << 6) | ((uint8_t)s[i + k] & 63);
            i += n; int g = glyph(c); SDL_Rect src{g * 8, 0, 8, 16}, dst{x, y, 8 * scale, 16 * scale}; SDL_RenderCopy(ren, atlas, &src, &dst); x += 8 * scale;
        }
        return x;
    }
};

static std::string key_name(int sc) { if (!sc) return "-"; const char* n = SDL_GetScancodeName((SDL_Scancode)sc); return n && *n ? n : "?"; }
static std::string pad_name(int b) { if (b < 0) return "-"; const char* n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b); return n ? n : "?"; }

static void (*g_launcher_shot)(SDL_Renderer*) = nullptr;
static void (*g_menu_shot)(SDL_Renderer*) = nullptr;           // test: in-game menu (save and resume)       // non-interactive test: saves the screen
#include "launcher.h"
#endif
