// Menu translations: JSON files in "lang/" (one per language, e.g. lang/it.json, lang/en.json).
// Each file is a flat object { "key": "text", ... }; "language_name" is the name shown in the menu.
// Missing keys use the built-in English text, so an incomplete file still works.
// The new menu draws every language (tru); the old 8x16 on-screen font is ASCII only (tr): accented letters are
// simplified (e' -> e') and texts it cannot show (Russian, Japanese, Chinese) use the English text.
#pragma once
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>

namespace i18n {

// ---------------------------------------------------------------- default texts (English)
inline const std::map<std::string, std::string>& builtin() {
    static const std::map<std::string, std::string> m = {
        {"language_name", "English"},
        {"subtitle", "- recompiled version (Namco System 23)"},
        {"ctrl_none", "Controller: none detected (plug it in now, or use the keyboard)"},
        {"ctrl", "Controller: "},
        {"press_key", "press a key..."}, {"press_button", "press a button..."},
        {"start_game", ">>> START THE GAME <<<"},
        {"col_kb1", "Keyboard 1"}, {"col_kb2", "Keyboard 2"}, {"col_pad", "Controller"},
        {"act_left", "Move left"}, {"act_right", "Move right"}, {"act_coin", "Coin (credit)"},
        {"act_start1", "Player 1 start"}, {"act_start2", "Player 2 start"}, {"act_up", "Up (select)"},
        {"act_down", "Down (select)"}, {"act_enter", "Enter / button 1"}, {"act_test", "Test (cabinet)"},
        {"act_service", "Service DIP on/off"},
        {"deadzone", "Left stick dead zone"}, {"sens", "Analog axis range"},
        {"invert", "Invert axis"}, {"axis", "Stick used for movement"}, {"axis_left", "left"}, {"axis_right", "right"},
        {"fullscreen", "Fullscreen (F11)"},
        {"res3d", "3D resolution"}, {"res3d_screen", "same as screen (4:3)"}, {"res3d_orig", "original 640x480"}, {"res3d_half", "fast 320x240"},
        {"gpu", "3D rendering"}, {"gpu_on", "graphics card (OpenGL)"}, {"gpu_off", "processor (CPU)"},
        {"fps", "Screen refresh"}, {"fps_vsync", "same as monitor (VSync)"}, {"fps_free", "unlimited"},
        {"wide", "Aspect ratio (experimental)"}, {"wide_169", "16:9 widescreen"}, {"wide_43", "4:3 original"},
        {"post", "Skip POST at boot (DIP 7)"}, {"svc", "Service DIP on at boot"}, {"svc_on", "yes (service mode)"},
        {"ui_lang", "Menu language"}, {"game_lang", "Game language"}, {"game_lang_keep", "as set in the game"},
        {"yes", "yes"}, {"no", "no"},
        {"reset", "Restore default settings"}, {"quit", "Quit"},
        {"help_wait", "Press the new key/button - Esc cancel - Backspace clear"},
        {"help_nav", "Up/Down choose - Left/Right column or value - Enter edit - Esc quit"},
        {"help_game", "In game: Esc, Select+Start or MENU = settings, F3 DIP"}, {"help_ingame", "Game paused - Esc / B / Back: resume"}, {"resume_game", ">>> RESUME THE GAME <<<"}, {"touchui", "On-screen buttons (touch)"}, {"touch_menu", "MENU"},
        {"play", "PLAY"}, {"resume", "RESUME"}, {"settings", "SETTINGS"}, {"exit", "EXIT"}, {"paused", "PAUSED"}, {"back", "Back"},
        {"tab_controls", "CONTROLS"}, {"tab_stick", "STICK & TOUCH"}, {"tab_video", "VIDEO"}, {"tab_game", "GAME"},
        {"h_select", "Select"}, {"h_back", "Exit"}, {"h_resume", "Resume"}, {"h_change", "Change"}, {"h_assign", "Assign"}, {"h_tabs", "Tabs"},
        {"h_touch", "Tap a row to change it, drag to scroll"}, {"h_touch_bind", "Tap a box, then press the new key or button"},
        {"press_key_t", "PRESS A KEY"}, {"press_button_t", "PRESS A BUTTON"},
        {"wait_hint_key", "Esc cancels, Backspace clears"}, {"wait_hint_pad", "on the controller (Esc cancels, Backspace clears)"},
        {"cancel", "CANCEL"}, {"clear", "CLEAR"}, {"reset_confirm", "Are you sure? Press again to restore"},
        {"ctrl_none_short", "No controller"}, {"key_enter", "Enter"}, {"loading", "Loading..."}, {"stats", "On-screen statistics"}, {"h3ds_help", "SELECT+START or MENU: pause  -  SELECT or B: coin"}, {"pad_up", "D-pad up"}, {"pad_down", "D-pad down"}, {"pad_left", "D-pad left"}, {"pad_right", "D-pad right"},
        {"touch_coin", "COIN"}, {"touch_svc", "SERVICE"}, {"touch_up", "UP"}, {"touch_down", "DOWN"}, {"touch_ok", "ENTER"},
    };
    return m;
}

// ---------------------------------------------------------------- minimal JSON (object of strings)
inline void skip_ws(const std::string& s, size_t& i) { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
inline bool parse_str(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++; out.clear();
    while (i < s.size() && s[i] != '"') {
        char ch = s[i++];
        if (ch == '\\' && i < s.size()) {
            char e = s[i++];
            if (e == 'n') out += '\n'; else if (e == 't') out += '\t';
            else if (e == 'u' && i + 4 <= s.size()) { unsigned v = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                if (v < 0x80) out += (char)v; else if (v < 0x800) { out += (char)(0xc0 | (v >> 6)); out += (char)(0x80 | (v & 63)); }
                else { out += (char)(0xe0 | (v >> 12)); out += (char)(0x80 | ((v >> 6) & 63)); out += (char)(0x80 | (v & 63)); } }
            else out += e;
        } else out += ch;
    }
    if (i >= s.size()) return false;
    i++; return true;
}
// returns false if the file is not a valid { "k": "v", ... } object
inline bool parse_object(const std::string& s, std::map<std::string, std::string>& out) {
    size_t i = 0; skip_ws(s, i);
    if (s.compare(i, 3, "\xEF\xBB\xBF") == 0) { i += 3; skip_ws(s, i); }   // BOM UTF-8
    if (i >= s.size() || s[i] != '{') return false;
    i++;
    for (;;) {
        skip_ws(s, i); if (i < s.size() && s[i] == '}') return true;
        std::string k, v; if (!parse_str(s, i, k)) return false;
        skip_ws(s, i); if (i >= s.size() || s[i] != ':') return false; i++; skip_ws(s, i);
        if (!parse_str(s, i, v)) return false;
        out[k] = v;
        skip_ws(s, i); if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
}

// UTF-8 accented letters -> ASCII (the menu font only has characters 32-126)
inline std::string ascii(const std::string& in) {
    static const std::map<std::string, std::string> rep = {
        {"\xC3\xA0", "a'"}, {"\xC3\xA8", "e'"}, {"\xC3\xA9", "e'"}, {"\xC3\xAC", "i'"}, {"\xC3\xB2", "o'"}, {"\xC3\xB9", "u'"},
        {"\xC3\x80", "A'"}, {"\xC3\x88", "E'"}, {"\xC3\x89", "E'"}, {"\xC3\x8C", "I'"}, {"\xC3\x92", "O'"}, {"\xC3\x99", "U'"},
        {"\xC3\xA4", "ae"}, {"\xC3\xB6", "oe"}, {"\xC3\xBC", "ue"}, {"\xC3\x9F", "ss"}, {"\xC3\xB1", "n"}, {"\xC3\xA7", "c"},
        {"\xC3\xA2", "a"}, {"\xC3\xAA", "e"}, {"\xC3\xAE", "i"}, {"\xC3\xB4", "o"}, {"\xC3\xBB", "u"}, {"\xC3\xA1", "a'"}, {"\xC3\xAD", "i'"}, {"\xC3\xB3", "o'"}, {"\xC3\xBA", "u'"},
    };
    std::string o;
    for (size_t i = 0; i < in.size();) {
        unsigned char ch = (unsigned char)in[i];
        if (ch < 0x80) { o += (char)ch; i++; continue; }
        size_t n = (ch >= 0xF0) ? 4 : (ch >= 0xE0) ? 3 : 2;
        auto it = rep.find(in.substr(i, 2));
        o += (it != rep.end()) ? it->second : "?";
        i += std::min(n, in.size() - i);
    }
    return o;
}

// ---------------------------------------------------------------- available languages and active language
struct Lang { std::string code, name; std::map<std::string, std::string> t; };
inline std::string& dir() { static std::string d = getenv("S23_LANGDIR") ? getenv("S23_LANGDIR") : "lang"; return d; }
inline std::vector<Lang>& all() { static std::vector<Lang> v; return v; }
inline int& cur() { static int c = -1; return c; }
// language of the system (two-letter code, set by the platform code); used while the user has not chosen one
inline std::string& sys() { static std::string s; return s; }

inline void scan() {
    auto& v = all(); v.clear();
    if (DIR* d = opendir(dir().c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name; if (n.size() < 6 || n.substr(n.size() - 5) != ".json") continue;
            std::string p = dir() + "/" + n; FILE* f = fopen(p.c_str(), "rb"); if (!f) continue;
            std::string s; char buf[4096]; size_t k; while ((k = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, k); fclose(f);
            Lang L; L.code = n.substr(0, n.size() - 5);
            if (!parse_object(s, L.t)) { fprintf(stderr, "language %s: invalid JSON, ignored\n", p.c_str()); continue; }
            L.name = L.t.count("language_name") ? ascii(L.t["language_name"]) : L.code;
            if (L.name.find('?') != std::string::npos) L.name = L.code;
            v.push_back(std::move(L));
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end(), [](const Lang& a, const Lang& b) { return a.code < b.code; });
}
// empty code: the system language if there is a file for it, otherwise English
inline void select(const std::string& code) {
    cur() = -1; for (size_t i = 0; i < all().size(); i++) if (all()[i].code == code) cur() = (int)i;
    if (cur() < 0 && code.empty()) { for (const std::string& c : {sys(), std::string("en")}) if (cur() < 0) for (size_t i = 0; i < all().size(); i++) if (all()[i].code == c) cur() = (int)i; }
}
inline std::string cur_code() { return cur() >= 0 ? all()[cur()].code : "en"; }
inline std::string cur_name() { return cur() >= 0 ? all()[cur()].name : "English"; }
inline std::string cur_name_u() { if (cur() >= 0) { auto& t = all()[cur()].t; auto it = t.find("language_name"); if (it != t.end()) return it->second; } return cur_name(); }
// like tr() but keeps accented letters (the new menu has a font with the full Latin character set)
inline std::string tru(const std::string& key) {
    if (cur() >= 0) { auto& t = all()[cur()].t; auto it = t.find(key); if (it != t.end()) return it->second; }
    auto it = builtin().find(key); return it != builtin().end() ? it->second : key;
}
// text for the key in the active language (then built-in English, then the key itself)
inline std::string tr(const std::string& key) {
    if (cur() >= 0) { auto& t = all()[cur()].t; auto it = t.find(key);
        if (it != t.end()) { std::string a = ascii(it->second); if (a.find('?') == std::string::npos || it->second.find('?') != std::string::npos) return a; } }
    auto it = builtin().find(key); return it != builtin().end() ? it->second : key;
}

}  // namespace i18n
