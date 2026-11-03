// Main menu and settings (included by frontend.h). Two screens:
//   HOME: logo, PLAY (or RESUME in game), SETTINGS, EXIT
//   SETTINGS: tabs Controls / Controller and touch / Video / Game, large rows to tap
// It is used the same way with mouse, finger, keyboard and controller: the pointer chooses and presses, arrows/d-pad/stick
// move the selection, Enter/A confirms, Esc/B goes back, Q/E or LB/RB (also Tab) change tab.
// Returns true to start (or resume) the game, false to exit.
#pragma once
#include "ui.h"

static std::string ui_dir() {
    if (const char* e = getenv("S23_UIDIR")) return e;
    std::string d = i18n::dir();
    if (d.size() >= 4 && d.compare(d.size() - 4, 4, "lang") == 0) return d.substr(0, d.size() - 4) + "ui";
    return "ui";
}

// readable name of the controller button
static std::string pad_label(int b) {
    switch (b) {
        case -1: return "-";
        case SDL_CONTROLLER_BUTTON_A: return "A"; case SDL_CONTROLLER_BUTTON_B: return "B"; case SDL_CONTROLLER_BUTTON_X: return "X"; case SDL_CONTROLLER_BUTTON_Y: return "Y";
        case SDL_CONTROLLER_BUTTON_BACK: return "Select / -"; case SDL_CONTROLLER_BUTTON_START: return "Start / +"; case SDL_CONTROLLER_BUTTON_GUIDE: return "Home";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "LB / L"; case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB / R";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L3"; case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R3";
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return i18n::tru("pad_up"); case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return i18n::tru("pad_down");
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return i18n::tru("pad_left"); case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return i18n::tru("pad_right");
    }
    return pad_name(b);
}

static bool launcher(SDL_Window* win, SDL_Renderer* ren, InputState& is, bool ingame = false) {
    using namespace ui;
    static Ui U; static SDL_Renderer* U_ren = nullptr;
    Config& c = is.cfg; Pads& pads = is.pads;
    i18n::scan(); i18n::select(c.ui_lang);
    if (U_ren != ren) { U = Ui(); U.init(ren, ui_dir()); U_ren = ren; }
    auto T = [](const char* k) { return i18n::tru(k); };

    // ---------------------------------------------------------------- state
    enum Screen { HOME, SETTINGS };
    enum Dev { D_KEYS, D_PAD, D_POINTER };
    Screen scr = HOME; int home_sel = 0, tab = 0, row = 0, col = 0;
    float scroll = 0, scroll_draw = 0; bool follow = true;          // follow: the list follows the selection (keyboard/controller)
    int waiting = -1;                                               // action waiting for a key/button
    int confirm_reset = 0; bool quit = false, start = false;
    Dev dev = !pads.list.empty() ? D_PAD : D_KEYS;
#if defined(__ANDROID__)
    dev = pads.list.empty() ? D_POINTER : D_PAD;
#endif
    const Uint32 t_open = SDL_GetTicks(); Uint32 t_screen = t_open;
    if (const char* s = getenv("S23_UI_SCREEN")) { if (!strncmp(s, "settings", 8)) { scr = SETTINGS; tab = atoi(s + 8 + (s[8] == ':')); } }
    if (getenv("S23_UI_WAIT")) { waiting = 0; col = atoi(getenv("S23_UI_WAIT")) % 3; }
    static const char* GL[8] = {"JPN", "ENG", "GER", "FRA", "SPA", "POR", "ITA", "KOR"};
    auto cycle_ui = [&](int d) {
        auto& L = i18n::all(); if (L.empty()) return;
        int k = i18n::cur(); k = (k < 0 ? 0 : (k + d + (int)L.size()) % (int)L.size()); c.ui_lang = L[k].code; i18n::select(c.ui_lang); };
    auto set_fs = [&](int v) { c.fullscreen = v; SDL_SetWindowFullscreen(win, c.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); };

    // ---------------------------------------------------------------- settings rows
    enum Kind { K_BIND, K_TOGGLE, K_CYCLE, K_SLIDER, K_ACTION };
    struct Row { Kind k; std::string label; int act = 0; bool* dummy = nullptr; int* iv = nullptr; int lo = 0, hi = 0, step = 1; std::string val; bool on = false; int id = 0; };
    enum { R_DZ = 100, R_SENS, R_INV, R_AXIS, R_TOUCHUI, R_FS, R_RES, R_GPU, R_FPS, R_WIDE, R_UILANG, R_GAMELANG, R_POST, R_SVC, R_RESET };
    const char* TABS[4] = {"tab_controls", "tab_stick", "tab_video", "tab_game"};
    auto rows_of = [&](int tb) {
        std::vector<Row> v;
        auto tog = [&](int id, const char* key, bool on) { Row r; r.k = K_TOGGLE; r.id = id; r.label = T(key); r.on = on; v.push_back(r); };
        auto cyc = [&](int id, const char* key, const std::string& val) { Row r; r.k = K_CYCLE; r.id = id; r.label = T(key); r.val = val; v.push_back(r); };
        auto sld = [&](int id, const char* key, int* p, int lo, int hi, int st) { Row r; r.k = K_SLIDER; r.id = id; r.label = T(key); r.iv = p; r.lo = lo; r.hi = hi; r.step = st; r.val = std::to_string(*p) + " %"; v.push_back(r); };
        if (tb == 0) for (int i = 0; i < A_COUNT; i++) { Row r; r.k = K_BIND; r.act = i; r.label = T((std::string("act_") + ACT_INFO[i].id).c_str()); v.push_back(r); }
        if (tb == 1) {
            sld(R_DZ, "deadzone", &c.deadzone, 0, 60, 2); sld(R_SENS, "sens", &c.sens, 20, 100, 5);
            tog(R_INV, "invert", c.invert); cyc(R_AXIS, "axis", c.axis ? T("axis_right") : T("axis_left"));
            tog(R_TOUCHUI, "touchui", c.touchui);
        }
        if (tb == 2) {
#if !defined(S23_CONSOLE) && !defined(__ANDROID__)
            tog(R_FS, "fullscreen", c.fullscreen);
#endif
            cyc(R_RES, "res3d", c.res3d == 1 ? T("res3d_screen") : c.res3d == 2 ? T("res3d_half") : T("res3d_orig"));
            cyc(R_GPU, "gpu", c.gpu ? T("gpu_on") : T("gpu_off"));
            { double f = FPS_LIST[c.fps % FPS_N]; char b[48]; if (f == 0) snprintf(b, sizeof b, "%s", T("fps_vsync").c_str()); else if (f < 0) snprintf(b, sizeof b, "%s", T("fps_free").c_str()); else snprintf(b, sizeof b, f == (int)f ? "%.0f Hz" : "%.1f Hz", f);
              cyc(R_FPS, "fps", b); }
            cyc(R_WIDE, "wide", c.wide ? T("wide_169") : T("wide_43"));
        }
        if (tb == 3) {
            cyc(R_UILANG, "ui_lang", i18n::cur_name_u());
            cyc(R_GAMELANG, "game_lang", c.game_lang < 0 ? T("game_lang_keep") : GL[c.game_lang & 7]);
            tog(R_POST, "post", c.skip_post); tog(R_SVC, "svc", c.service_dip);
            Row r; r.k = K_ACTION; r.id = R_RESET; r.label = confirm_reset ? T("reset_confirm") : T("reset"); v.push_back(r);
        }
        return v;
    };
    auto change = [&](const Row& r, int d) {                         // d = -1 / +1; 0 = confirm (Enter, A, tap)
        int s = d ? d : 1;
        switch (r.id) {
            case R_DZ: c.deadzone = std::max(0, std::min(60, c.deadzone + 2 * s)); break;
            case R_SENS: c.sens = std::max(20, std::min(100, c.sens + 5 * s)); break;
            case R_INV: c.invert = !c.invert; break;  case R_AXIS: c.axis = !c.axis; break;
            case R_TOUCHUI: c.touchui = !c.touchui; break;  case R_FS: set_fs(!c.fullscreen); break;
            case R_RES: c.res3d = (c.res3d + s + 3) % 3; break;  case R_GPU: c.gpu = !c.gpu; break;
            case R_FPS: c.fps = (c.fps + s + FPS_N) % FPS_N; break;  case R_WIDE: c.wide = !c.wide; break;
            case R_UILANG: cycle_ui(s); break;
            case R_GAMELANG: c.game_lang = (c.game_lang + 1 + s + 9) % 9 - 1; break;
            case R_POST: c.skip_post = !c.skip_post; break;  case R_SVC: c.service_dip = !c.service_dip; break;
            case R_RESET: if (d == 0) { if (confirm_reset) { std::string l = c.ui_lang; c.defaults(); c.ui_lang = l; confirm_reset = 0; } else confirm_reset = 1; } break;
        }
        if (r.id != R_RESET) confirm_reset = 0;
    };

    // ---------------------------------------------------------------- touch zones (from the previous frame)
    enum HitT { H_HOME, H_TAB, H_BACK, H_ROW, H_CELL, H_LEFT, H_RIGHT, H_SLIDER, H_CANCEL, H_PANEL };
    struct Hit { SDL_FRect r; HitT t; int a, b; };
    std::vector<Hit> hits, hits_prev;
    auto hit_at = [&](float x, float y, bool panel_ok = true) -> const Hit* {
        for (auto it = hits_prev.rbegin(); it != hits_prev.rend(); ++it) {
            if (!panel_ok && it->t == H_PANEL) continue;
            if (x >= it->r.x && x < it->r.x + it->r.w && y >= it->r.y && y < it->r.y + it->r.h) return &*it;
        }
        return nullptr;
    };
    bool mdown = false, dragging = false; float md_x = 0, md_y = 0, md_scroll = 0; bool md_in_panel = false;
    // analog stick as d-pad, with repeat
    int stick_dir = 0; Uint32 stick_next = 0;
    float LW = 1280, LH = 720;
    auto go = [&](Screen s) { scr = s; t_screen = SDL_GetTicks(); waiting = -1; confirm_reset = 0; if (s == SETTINGS) { row = 0; col = 0; scroll = scroll_draw = 0; follow = true; } };
    auto set_tab = [&](int t) { tab = (t + 4) % 4; row = 0; col = 0; scroll = 0; follow = true; confirm_reset = 0; };
    auto activate_home = [&](int i) { if (i == 0) start = true; else if (i == 1) go(SETTINGS); else quit = true; };

    // ---------------------------------------------------------------- abstract commands
    auto nav = [&](int dx, int dy, bool ok, bool back, int tabd) {
        if (scr == HOME) {
            if (dy) home_sel = (home_sel + dy + 3) % 3;
            if (ok) activate_home(home_sel);
            if (back) { if (ingame) start = true; else if (home_sel == 2) quit = true; else home_sel = 2; }
            return;
        }
        auto R = rows_of(tab); int n = (int)R.size();
        if (tabd) set_tab(tab + tabd);
        if (back) { go(HOME); home_sel = 1; return; }
        if (n == 0) return;
        if (dy) { row = (row + dy + n) % n; follow = true; confirm_reset = 0; }
        row = std::min(row, n - 1);
        const Row& r = R[row];
        if (dx) { if (r.k == K_BIND) col = (col + dx + 3) % 3; else if (r.k != K_ACTION) change(r, dx); }
        if (ok) { if (r.k == K_BIND) waiting = r.act; else change(r, 0); }
    };

    while (!quit && !start) {
        // logical area: height 720, width depending on the window (4:3 ... 21:9)
        { int ow = 1280, oh = 720; SDL_GetRendererOutputSize(ren, &ow, &oh); if (oh <= 0) oh = 1;
          LW = std::max(960.f, std::min(1700.f, roundf(720.f * ow / oh))); LH = 720;
          SDL_RenderSetLogicalSize(ren, (int)LW, (int)LH); U.W = LW; U.H = LH; }
        const Uint32 now = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) pads.rescan();
            if (e.type == SDL_KEYDOWN) dev = D_KEYS;
            if (e.type == SDL_CONTROLLERBUTTONDOWN) dev = D_PAD;
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_FINGERDOWN || (e.type == SDL_MOUSEMOTION && e.motion.which != SDL_TOUCH_MOUSEID && (abs(e.motion.xrel) + abs(e.motion.yrel)) > 2)) dev = D_POINTER;
            const bool kd = e.type == SDL_KEYDOWN, cb = e.type == SDL_CONTROLLERBUTTONDOWN;
            if (kd && e.key.keysym.sym == SDLK_F11) { set_fs(!c.fullscreen); continue; }
            // ---- waiting for a new key / button
            if (waiting >= 0) {
                const int a = waiting;
                if (kd && !e.key.repeat) {
                    const SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_ESCAPE) waiting = -1;
                    else if (sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_DELETE) { if (col < 2) c.key[a][col] = 0; else c.pad[a] = -1; waiting = -1; }
                    else if (col < 2) { c.key[a][col] = sc; waiting = -1; }
                } else if (cb && col == 2) { c.pad[a] = e.cbutton.button; waiting = -1; }
                else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    const Hit* h = hit_at((float)e.button.x, (float)e.button.y);
                    if (h && h->t == H_CANCEL) { if (h->a == 1) { if (col < 2) c.key[a][col] = 0; else c.pad[a] = -1; } waiting = -1; }
                }
                continue;
            }
            int dx = 0, dy = 0, tabd = 0; bool ok = false, back = false;
#ifdef __ANDROID__
            if (kd && e.key.keysym.sym == SDLK_AC_BACK) back = true;
#endif
            if (kd) switch (e.key.keysym.sym) {
                case SDLK_UP: dy = -1; break; case SDLK_DOWN: dy = 1; break;
                case SDLK_LEFT: dx = -1; break; case SDLK_RIGHT: dx = 1; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if (!e.key.repeat) ok = true; break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: if (!e.key.repeat) back = true; break;
                case SDLK_q: case SDLK_PAGEUP: tabd = -1; break; case SDLK_e: case SDLK_PAGEDOWN: tabd = 1; break;
                case SDLK_TAB: tabd = (SDL_GetModState() & KMOD_SHIFT) ? -1 : 1; break;
            }
            if (cb) switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: dy = -1; break; case SDL_CONTROLLER_BUTTON_DPAD_DOWN: dy = 1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: dx = -1; break; case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: dx = 1; break;
                case SDL_CONTROLLER_BUTTON_A: ok = true; break; case SDL_CONTROLLER_BUTTON_B: back = true; break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: tabd = -1; break; case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: tabd = 1; break;
                case SDL_CONTROLLER_BUTTON_START: if (scr == HOME) start = true; break;
            }
            if (scr == HOME && tabd) tabd = 0;
            if (dx || dy || ok || back || tabd) { nav(dx, dy, ok, back, tabd); continue; }
            // ---- pointer (mouse or finger: SDL turns taps into clicks)
            if (e.type == SDL_MOUSEWHEEL && scr == SETTINGS) { scroll -= e.wheel.y * 66.f; follow = false; }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                mdown = true; dragging = false; md_x = (float)e.button.x; md_y = (float)e.button.y; md_scroll = scroll;
                const Hit* h = hit_at(md_x, md_y); md_in_panel = false;
                for (auto& q : hits_prev) if (q.t == H_PANEL && md_x >= q.r.x && md_x < q.r.x + q.r.w && md_y >= q.r.y && md_y < q.r.y + q.r.h) md_in_panel = true;
                (void)h;
            }
            if (e.type == SDL_MOUSEMOTION) {
                const float mx = (float)e.motion.x, my = (float)e.motion.y;
                if (mdown && md_in_panel && (dragging || fabsf(my - md_y) > 14.f)) { dragging = true; scroll = md_scroll - (my - md_y); follow = false; }
                if (!mdown && e.motion.which != SDL_TOUCH_MOUSEID) {          // mouse hover = selection
                    if (const Hit* h = hit_at(mx, my, false)) {
                        if (h->t == H_HOME) home_sel = h->a;
                        else if (h->t == H_ROW || h->t == H_LEFT || h->t == H_RIGHT || h->t == H_SLIDER) { if (row != h->a) confirm_reset = 0; row = h->a; follow = false; }
                        else if (h->t == H_CELL) { row = h->a; col = h->b; follow = false; }
                    }
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && mdown) {
                mdown = false;
                if (dragging) { dragging = false; continue; }
                const Hit* h = hit_at((float)e.button.x, (float)e.button.y, false);
                if (!h) continue;
                if (h->t == H_HOME) { home_sel = h->a; activate_home(h->a); }
                else if (h->t == H_TAB) set_tab(h->a);
                else if (h->t == H_BACK) { go(HOME); home_sel = 1; }
                else if (scr == SETTINGS) {
                    auto R = rows_of(tab); if (h->a < 0 || h->a >= (int)R.size()) continue;
                    if (row != h->a) confirm_reset = 0;
                    row = h->a; follow = false; const Row& r = R[row];
                    if (h->t == H_CELL) { col = h->b; waiting = r.act; }
                    else if (h->t == H_LEFT) change(r, -1);
                    else if (h->t == H_RIGHT) change(r, 1);
                    else if (h->t == H_SLIDER && r.iv) {
                        float f = ((float)e.button.x - h->r.x) / h->r.w; f = std::max(0.f, std::min(1.f, f));
                        int v = r.lo + (int)roundf(f * (r.hi - r.lo) / r.step) * r.step; *r.iv = std::max(r.lo, std::min(r.hi, v));
                    }
                    else if (h->t == H_ROW) change(r, 0);
                }
            }
        }
        // ---- left stick as d-pad (with repeat)
        if (waiting < 0 && !pads.list.empty()) {
            float ax = pads.axis(SDL_CONTROLLER_AXIS_LEFTX), ay = pads.axis(SDL_CONTROLLER_AXIS_LEFTY);
            int d = fabsf(ay) > 0.6f && fabsf(ay) >= fabsf(ax) ? (ay > 0 ? 2 : 1) : fabsf(ax) > 0.6f ? (ax > 0 ? 4 : 3) : 0;
            if (d != stick_dir) { stick_dir = d; stick_next = now; }
            if (d && now >= stick_next) { dev = D_PAD; nav(d == 3 ? -1 : d == 4 ? 1 : 0, d == 1 ? -1 : d == 2 ? 1 : 0, false, false, 0); stick_next = now + (stick_next == now ? 380 : 110); }
        }

        // ================================================================ drawing
        const float t = (now - t_open) / 1000.f, ts = (now - t_screen) / 1000.f;
        hits.clear();
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
        U.background(t, scr == HOME ? 0.55f : 0.9f);
        const float W = LW, H = LH;

        // ---- hint bar at the bottom (changes with the last device used)
        auto hint_bar = [&](const std::vector<std::pair<std::string, std::string>>& items) {
            U.rect(0, H - 52, W, 52, rgba(5, 12, 30, 150), rgba(5, 12, 30, 225));
            U.rect(0, H - 52, W, 3, alpha(YEL, 0.8f), alpha(ORA, 0.8f));
            float x = W - 28;
            for (auto it = items.rbegin(); it != items.rend(); ++it) {
                float lw = U.text_w(U.body, it->second, 24); x -= lw;
                U.text(U.body, x, H - 26, it->second, 24, WHITE, WHITE, INK, true, false);
                x -= 12;
                const std::string& g = it->first;
                if (dev == D_PAD) {                                 // controller button: coloured circle (A green, B red, X blue, Y yellow)
                    SDL_Color bc = g == "A" ? GREEN : g == "B" ? RED : g == "X" ? rgba(40, 110, 230) : g == "Y" ? rgba(230, 180, 20) : rgba(70, 80, 100);
                    float gw = std::max(34.f, U.text_w(U.body, g, 20) + 20);
                    if (g.size() <= 1) U.circle(x - 17, H - 26, 17, INK), U.circle(x - 17, H - 26, 15, bc);
                    else U.rrect_ol(x - gw, H - 42, gw, 32, 12, rgba(90, 100, 125), rgba(50, 58, 80), 2, INK);
                    U.text(U.body, x - (g.size() <= 1 ? 17 : gw / 2), H - 26, g, 20, WHITE, WHITE, INK, false, false, 1);
                    x -= (g.size() <= 1 ? 34 : gw) + 26;
                } else {                                            // keyboard key
                    float gw = std::max(34.f, U.text_w(U.body, g, 20) + 18);
                    U.rrect_ol(x - gw, H - 42, gw, 32, 6, rgba(240, 240, 248), rgba(190, 196, 210), 2, INK);
                    U.text(U.body, x - gw / 2, H - 26, g, 20, INK, INK, INK, false, false, 1);
                    x -= gw + 26;
                }
            }
        };
        auto pad_or_key = [&](const char* pad, const char* key) { return std::string(dev == D_PAD ? pad : key); };

        if (scr == HOME) {
            // ---------------------------------------------------- main screen
            const float lh = H * 0.40f;
            U.title(W / 2, 40 + lh / 2 + (ingame ? 14 : 0), std::min(W * 0.62f, 760.f), lh, t);
            if (ingame) {                                           // "PAUSED" label
                float pw = U.text_w(U.disp, T("paused"), 34) + 44;
                U.poly(Ui::para_pts(W / 2 - pw / 2 - 4, 10 - 4, pw + 8, 52, 14), INK, INK, 0, 1);
                U.poly(Ui::para_pts(W / 2 - pw / 2, 10, pw, 44, 12), RED, rgba(150, 10, 20), 10, 54);
                U.text(U.disp, W / 2, 32, T("paused"), 34, WHITE, YEL, INK, true, false, 1);
            }
            const char* labels[3] = {ingame ? "resume" : "play", "settings", "exit"};
            const float bw[3] = {480, 440, 300}, bh[3] = {96, 78, 60}, fs[3] = {62, 48, 36};
            float y = 40 + lh + 26;
            for (int i = 0; i < 3; i++) {
                const bool sel = home_sel == i && dev != D_POINTER ? true : home_sel == i;
                float in = std::max(0.f, std::min(1.f, (ts * 1000.f - i * 70) / 260.f)); in = 1 - (1 - in) * (1 - in) * (1 - in);
                const float pulse = sel ? 1.f + 0.035f * sinf(t * 6.f) : 1.f;
                float w = bw[i] * pulse, h = bh[i] * pulse, x = W / 2 - w / 2, yy = y + (1 - in) * 120 - (h - bh[i]) / 2;
                const float sk = h * 0.28f;
                if (sel) U.burst(W / 2, yy + h / 2, w * 0.58f, h * 0.95f, 14, t * 0.6f, alpha(YEL, 0.22f * in));
                U.poly(Ui::para_pts(x - 6 + 4, yy - 6 + 7, w + 12, h + 12, sk), alpha(INK, 0.5f * in), alpha(INK, 0.5f * in), 0, 1);   // shadow
                U.poly(Ui::para_pts(x - 6, yy - 6, w + 12, h + 12, sk), alpha(INK, in), alpha(INK, in), 0, 1);                       // outline
                if (sel) {
                    U.poly(Ui::para_pts(x, yy, w, h, sk), rgba(190, 190, 190, (int)(255 * in)), rgba(190, 190, 190, (int)(255 * in)), yy, yy + h, U.chev, {0, (float)fmod(t * 0.15, 1.0) * 0.2f, 1, 0.8f}, x, w);
                    U.poly(Ui::para_pts(x, yy + h * 0.55f, w, h * 0.45f, sk * 0.45f), alpha(INK, 0), alpha(INK, 0.35f * in), yy + h * 0.55f, yy + h);
                } else {
                    U.poly(Ui::para_pts(x, yy, w, h, sk), alpha(rgba(46, 104, 196), in), alpha(rgba(16, 44, 104), in), yy, yy + h);
                    U.poly(Ui::para_pts(x + sk * 0.2f, yy + 4, w - sk * 0.4f, 6, 2), alpha(WHITE, 0.25f * in), alpha(WHITE, 0.05f * in), yy, yy + 10);
                }
                const std::string lab = T(labels[i]);
                if (sel) U.text(U.disp, W / 2, yy + h / 2 + 2, lab, fs[i] * pulse, YEL, RED, INK, true, true, 1, w - 2 * sk - 20);
                else U.text(U.disp, W / 2, yy + h / 2 + 2, lab, fs[i], alpha(WHITE, in), alpha(rgba(200, 225, 255), in), alpha(INK, in), true, true, 1, w - 2 * sk - 20);
                hits.push_back({{x, yy, w, h}, H_HOME, i, 0});
                y += bh[i] + 22;
            }
            // connected controller and subtitle
            std::string cn = pads.name();
            if (dev == D_POINTER) hint_bar({});
            else hint_bar({{pad_or_key("A", T("key_enter").c_str()), T("h_select")}, {pad_or_key("B", "Esc"), T(ingame ? "h_resume" : "h_back")}});
            U.text(U.body, 24, H - 26, cn.empty() ? T("ctrl_none_short") : cn, 20, cn.empty() ? GREY : CYAN, cn.empty() ? GREY : CYAN, INK, true, false, 0, W * 0.3f);
        } else {
            // ---------------------------------------------------- settings
            const float PW = std::min(W - 60, 1180.f), PX = (W - PW) / 2;
            // title and back button
            U.text(U.disp, W / 2, 50, T("settings"), 56, YEL, RED, INK, true, true, 1);
            {   float bx = PX, by = 24, bw2 = 64 + U.text_w(U.body, T("back"), 26), bh2 = 52;
                U.rrect_ol(bx, by, bw2, bh2, 26, rgba(46, 104, 196), rgba(16, 44, 104), 3, INK);
                U.arrow(bx + 30, by + bh2 / 2, 13, -1, YEL);
                U.text(U.body, bx + 50, by + bh2 / 2, T("back"), 26, WHITE, WHITE, INK, true, false);
                hits.push_back({{bx - 8, by - 8, bw2 + 16, bh2 + 16}, H_BACK, 0, 0});
            }
            // tabs
            const float ty = 96, th = 56; float tw = (PW - 3 * 10) / 4;
            if (dev == D_PAD || dev == D_KEYS) {                    // LB / RB at the sides
                const char* l = dev == D_PAD ? "LB" : "Q"; const char* r = dev == D_PAD ? "RB" : "E";
                U.text(U.body, PX - 8, ty + th / 2, l, 22, GREY, GREY, INK, true, false, 2);
                U.text(U.body, PX + PW + 8, ty + th / 2, r, 22, GREY, GREY, INK, true, false, 0);
            }
            for (int i = 0; i < 4; i++) {
                float x = PX + i * (tw + 10); bool s = tab == i;
                U.poly(Ui::para_pts(x - 4, ty - 4, tw + 8, th + 8, 16), INK, INK, 0, 1);
                if (s) { U.poly(Ui::para_pts(x, ty, tw, th, 14), rgba(175, 175, 175), rgba(175, 175, 175), ty, ty + th, U.chev, {0, 0, 1, 0.5f}, x, tw);
                         U.poly(Ui::para_pts(x + 18, ty + 8, tw - 22, th - 16, 10), alpha(INK, 0.45f), alpha(INK, 0.45f), 0, 1); }
                else U.poly(Ui::para_pts(x, ty, tw, th, 14), rgba(34, 70, 140), rgba(14, 34, 80), ty, ty + th);
                U.text(U.disp, x + tw / 2, ty + th / 2 + 1, T(TABS[i]), s ? 34 : 30, s ? YEL : rgba(205, 225, 255), s ? RED : rgba(160, 190, 235), INK, true, s, 1, tw - 36);
                hits.push_back({{x, ty - 6, tw, th + 12}, H_TAB, i, 0});
            }
            // panel
            const float py = ty + th + 16, ph = H - 52 - 14 - py;
            U.rrect_ol(PX, py, PW, ph, 18, rgba(10, 28, 70, 225), rgba(6, 16, 44, 235), 4, INK);
            hits.push_back({{PX, py, PW, ph}, H_PANEL, 0, 0});
            auto R = rows_of(tab); const int n = (int)R.size(); if (row >= n) row = std::max(0, n - 1);
            float hy = py + 10;
            const float RH = 64, gap = 6, lx = PX + 18, rw = PW - 36;
            const float cells_x = lx + rw * 0.40f, cw = (rw * 0.60f - 2 * 10) / 3;
            if (tab == 0) {                                          // column header (fixed)
                const char* H3[3] = {"col_kb1", "col_kb2", "col_pad"};
                for (int k = 0; k < 3; k++) U.text(U.body, cells_x + k * (cw + 10) + cw / 2, hy + 14, T(H3[k]), 22, CYAN, CYAN, INK, true, false, 1, cw);
                hy += 32;
            }
            const float list_top = hy, list_h = py + ph - 10 - list_top;
            const float content_h = n * (RH + gap) - gap;
            const float max_scroll = std::max(0.f, content_h - list_h);
            if (follow) { float ry = row * (RH + gap); if (ry - scroll < 0) scroll = ry; if (ry + RH - scroll > list_h) scroll = ry + RH - list_h; }
            if (!mdown || !dragging) scroll = std::max(0.f, std::min(max_scroll, scroll));
            scroll_draw += (scroll - scroll_draw) * 0.3f; if (fabsf(scroll - scroll_draw) < 0.5f) scroll_draw = scroll;
            SDL_Rect clip{(int)PX, (int)list_top, (int)PW, (int)list_h}; SDL_RenderSetClipRect(ren, &clip);
            for (int i = 0; i < n; i++) {
                const Row& r = R[i];
                const float y = list_top + i * (RH + gap) - scroll_draw;
                if (y + RH < list_top || y > list_top + list_h) continue;
                const bool sel = row == i;
                const bool vis = y >= list_top - 1 && y + RH <= list_top + list_h + 1;
                if (sel) {
                    U.rrect(lx, y, rw, RH, 12, rgba(44, 100, 190, 245), rgba(26, 62, 140, 245));
                                        U.poly({{lx, y + 6}, {lx + 10, y + 6}, {lx + 10, y + RH - 6}, {lx, y + RH - 6}}, WHITE, WHITE, y, y + RH, U.chev, {0, 0, 0.03f, 0.6f}, lx, 10);
                } else U.rrect(lx, y, rw, RH, 12, rgba(255, 255, 255, 14), rgba(255, 255, 255, 8));
                // click on the whole row
                SDL_FRect rr{lx, std::max(y, list_top), rw, std::min(y + RH, list_top + list_h) - std::max(y, list_top)};
                if (rr.h > 4) hits.push_back({rr, H_ROW, i, 0});
                const SDL_Color lc = sel ? YEL : WHITE;
                if (r.k == K_ACTION) {
                    SDL_Color a0 = confirm_reset ? rgba(255, 90, 60) : rgba(255, 150, 150), a1 = confirm_reset ? RED : rgba(230, 90, 90);
                    U.text(U.body, lx + rw / 2, y + RH / 2, r.label, 30, sel ? a0 : rgba(255, 190, 190), sel ? a1 : rgba(230, 150, 150), INK, true, false, 1, rw - 40);
                    continue;
                }
                U.text(U.body, lx + 26, y + RH / 2, r.label, 29, lc, lc, INK, true, false, 0, (r.k == K_BIND ? cells_x - lx : rw * 0.5f) - 40);
                if (r.k == K_BIND) {
                    for (int k = 0; k < 3; k++) {
                        const float cx = cells_x + k * (cw + 10), cy = y + 9, chh = RH - 18;
                        const bool cs = sel && col == k, wt = waiting == r.act && col == k;
                        U.rrect_ol(cx, cy, cw, chh, 10, cs ? rgba(255, 236, 120) : rgba(20, 40, 86), cs ? rgba(255, 170, 40) : rgba(12, 26, 60), cs ? 3 : 2, cs ? INK : rgba(70, 110, 180));
                        std::string v = k < 2 ? key_name(c.key[r.act][k]) : pad_label(c.pad[r.act]);
                        if (wt) v = "...";
                        U.text(U.body, cx + cw / 2, cy + chh / 2, v, 24, cs ? INK : WHITE, cs ? INK : rgba(200, 220, 255), INK, !cs, false, 1, cw - 16);
                        if (vis) hits.push_back({{cx, cy, cw, chh}, H_CELL, i, k});
                    }
                } else {
                    const float vw = std::min(rw * 0.46f, 470.f), vx = lx + rw - vw - 12, vy = y + 8, vh = RH - 16;
                    if (r.k == K_TOGGLE) {                           // switch
                        const float sw = 96, sh = vh - 6, sx = lx + rw - sw - 22, sy = vy + 3;
                        U.rrect_ol(sx, sy, sw, sh, sh / 2, r.on ? rgba(60, 200, 90) : rgba(70, 76, 96), r.on ? rgba(20, 140, 60) : rgba(40, 44, 60), 3, INK);
                        const float kx = r.on ? sx + sw - sh / 2 : sx + sh / 2;
                        U.circle(kx, sy + sh / 2, sh / 2 - 2, INK); U.circle(kx, sy + sh / 2, sh / 2 - 5, r.on ? YEL : rgba(220, 225, 235));
                        U.text(U.body, sx - 14, sy + sh / 2, r.on ? T("yes") : T("no"), 24, r.on ? rgba(150, 255, 170) : GREY, r.on ? rgba(150, 255, 170) : GREY, INK, true, false, 2);
                    } else {                                         // value with arrows (and a bar for percentages)
                        U.rrect_ol(vx, vy, vw, vh, vh / 2, rgba(6, 16, 40), rgba(10, 24, 56), 2, sel ? YEL : rgba(70, 110, 180));
                        U.arrow(vx + 24, vy + vh / 2, 12, -1, sel ? YEL : rgba(140, 170, 220), sel);
                        U.arrow(vx + vw - 24, vy + vh / 2, 12, 1, sel ? YEL : rgba(140, 170, 220), sel);
                        if (vis) { hits.push_back({{vx - 6, vy - 6, 56, vh + 12}, H_LEFT, i, 0}); hits.push_back({{vx + vw - 50, vy - 6, 56, vh + 12}, H_RIGHT, i, 0}); }
                        if (r.k == K_SLIDER) {
                            const float bx = vx + 52, bw3 = vw - 196, by = vy + vh / 2 - 7;
                            float f = (float)(*r.iv - r.lo) / std::max(1, r.hi - r.lo);
                            U.rrect(bx, by, bw3, 14, 7, rgba(30, 44, 80), rgba(30, 44, 80));
                            if (f > 0) U.rrect(bx, by, std::max(14.f, bw3 * f), 14, 7, YEL, ORA);
                            U.circle(bx + bw3 * f, by + 7, 12, INK); U.circle(bx + bw3 * f, by + 7, 9, WHITE);
                            U.text(U.body, vx + vw - 56, vy + vh / 2, r.val, 24, WHITE, WHITE, INK, true, false, 2);
                            if (vis) hits.push_back({{bx - 10, vy, bw3 + 20, vh}, H_SLIDER, i, 0});
                        } else U.text(U.body, vx + vw / 2, vy + vh / 2, r.val, 25, WHITE, rgba(210, 228, 255), INK, true, false, 1, vw - 110);
                    }
                }
            }
            SDL_RenderSetClipRect(ren, nullptr);
            if (max_scroll > 1) {                                    // scroll bar
                const float sbh = list_h * list_h / content_h, sby = list_top + (list_h - sbh) * (scroll_draw / max_scroll);
                U.rrect(PX + PW - 9, sby, 5, sbh, 2.5f, alpha(WHITE, 0.5f), alpha(WHITE, 0.3f));
            }
            if (dev == D_POINTER) hint_bar({});
            else if (tab == 0) hint_bar({{pad_or_key("A", T("key_enter").c_str()), T("h_assign")}, {pad_or_key("LB RB", "Q E"), T("h_tabs")}, {pad_or_key("B", "Esc"), T("back")}});
            else hint_bar({{pad_or_key("A", T("key_enter").c_str()), T("h_change")}, {pad_or_key("LB RB", "Q E"), T("h_tabs")}, {pad_or_key("B", "Esc"), T("back")}});
            if (dev == D_POINTER) U.text(U.body, 24, H - 26, T(tab == 0 ? "h_touch_bind" : "h_touch"), 21, GREY, GREY, INK, true, false, 0, W - 48);

            // ---- "press a key" window
            if (waiting >= 0) {
                hits.clear();
                U.rect(0, 0, W, H, rgba(0, 0, 0, 150), rgba(0, 0, 0, 190));
                const float mw = std::min(W - 80, 720.f), mh = 300, mx = (W - mw) / 2, my = (H - mh) / 2;
                U.rrect_ol(mx, my, mw, mh, 20, rgba(30, 70, 150), rgba(12, 30, 80), 5, INK);
                U.text(U.disp, W / 2, my + 52, T(col == 2 ? "press_button_t" : "press_key_t"), 44, YEL, RED, INK, true, true, 1, mw - 40);
                U.text(U.body, W / 2, my + 112, T((std::string("act_") + ACT_INFO[waiting].id).c_str()), 30, WHITE, WHITE, INK, true, false, 1, mw - 40);
                U.text(U.body, W / 2, my + 156, T(col == 2 ? "wait_hint_pad" : "wait_hint_key"), 22, GREY, GREY, INK, true, false, 1, mw - 40);
                const float bw4 = (mw - 3 * 24) / 2, by = my + mh - 92, bh4 = 64;
                for (int k = 0; k < 2; k++) {
                    const float bx = mx + 24 + k * (bw4 + 24);
                    U.rrect_ol(bx, by, bw4, bh4, 14, k ? rgba(200, 60, 50) : rgba(46, 104, 196), k ? rgba(130, 20, 20) : rgba(16, 44, 104), 3, INK);
                    U.text(U.disp, bx + bw4 / 2, by + bh4 / 2, T(k ? "clear" : "cancel"), 32, WHITE, WHITE, INK, true, false, 1, bw4 - 20);
                    hits.push_back({{bx, by, bw4, bh4}, H_CANCEL, k, 0});
                }
            }
        }
        // fade on screen change
        if (ts < 0.18f) U.rect(0, 0, W, H, rgba(0, 0, 0, (int)(180 * (1 - ts / 0.18f))), rgba(0, 0, 0, (int)(180 * (1 - ts / 0.18f))));
        hits_prev = hits;
        { static int fm = 0; if (g_menu_shot && ingame && ++fm % 40 == 0) { g_menu_shot(ren); start = true; } }
        static int fr = 0; static const int shot_fr = getenv("S23_LAUNCH_SHOT_FRAME") ? atoi(getenv("S23_LAUNCH_SHOT_FRAME")) : 30;
        if (g_launcher_shot && !ingame && ++fr == shot_fr) { g_launcher_shot(ren); start = true; }
        SDL_RenderPresent(ren); SDL_Delay(c.fps == 0 ? 1 : 16);
    }
    c.save();
    return start && !quit;
}
