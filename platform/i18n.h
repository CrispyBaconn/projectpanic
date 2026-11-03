// Traduzioni dei menu: file JSON in "lang/" (uno per lingua, es. lang/it.json, lang/en.json).
// Ogni file e' un oggetto piatto { "chiave": "testo", ... }; "language_name" e' il nome mostrato nel menu.
// Le chiavi mancanti usano il testo italiano interno, quindi un file incompleto funziona lo stesso.
// Il font del menu e' ASCII: le lettere accentate vengono semplificate (e' -> e').
#pragma once
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>

namespace i18n {

// ---------------------------------------------------------------- testi predefiniti (italiano)
inline const std::map<std::string, std::string>& builtin() {
    static const std::map<std::string, std::string> m = {
        {"language_name", "Italiano"},
        {"subtitle", "- versione ricompilata (Namco System 23)"},
        {"ctrl_none", "Controller: nessuno rilevato (collegalo ora, oppure usa la tastiera)"},
        {"ctrl", "Controller: "},
        {"press_key", "premi un tasto..."}, {"press_button", "premi un pulsante..."},
        {"start_game", ">>> AVVIA IL GIOCO <<<"},
        {"col_kb1", "Tastiera 1"}, {"col_kb2", "Tastiera 2"}, {"col_pad", "Controller"},
        {"act_left", "Muovi a sinistra"}, {"act_right", "Muovi a destra"}, {"act_coin", "Moneta (credito)"},
        {"act_start1", "Start giocatore 1"}, {"act_start2", "Start giocatore 2"}, {"act_up", "Su (selezione)"},
        {"act_down", "Giu' (selezione)"}, {"act_enter", "Invio / pulsante 1"}, {"act_test", "Test (cabinato)"},
        {"act_service", "DIP configurazione on/off"},
        {"deadzone", "Zona morta stick sinistro"}, {"sens", "Escursione asse analogico"},
        {"invert", "Inverti asse"}, {"axis", "Stick usato per il movimento"}, {"axis_left", "sinistro"}, {"axis_right", "destro"},
        {"fullscreen", "Schermo intero (F11)"},
        {"res3d", "Risoluzione del 3D"}, {"res3d_screen", "come lo schermo (4:3)"}, {"res3d_orig", "originale 640x480"}, {"res3d_half", "veloce 320x240"},
        {"gpu", "Disegno del 3D"}, {"gpu_on", "scheda video (OpenGL)"}, {"gpu_off", "processore (CPU)"},
        {"fps", "Aggiornamento schermo"}, {"fps_vsync", "come il monitor (VSync)"}, {"fps_free", "senza limite"},
        {"wide", "Formato (sperimentale)"}, {"wide_169", "16:9 widescreen"}, {"wide_43", "4:3 originale"},
        {"post", "Salta il POST all'avvio (DIP 7)"}, {"svc", "DIP configurazione acceso all'avvio"}, {"svc_on", "si (modalita' servizio)"},
        {"ui_lang", "Lingua dei menu"}, {"game_lang", "Lingua del gioco"}, {"game_lang_keep", "quella scelta nel gioco"},
        {"yes", "si"}, {"no", "no"},
        {"reset", "Ripristina impostazioni predefinite"}, {"quit", "Esci"},
        {"help_wait", "Premi il nuovo tasto/pulsante - Esc annulla - Backspace cancella"},
        {"help_nav", "Su/Giu' scegli - Sx/Dx colonna o valore - Invio modifica - Esc esce"},
        {"help_game", "In gioco: Esc, Select+Start o MENU = impostazioni, F3 DIP"}, {"help_ingame", "Gioco in pausa - Esc / B / Indietro: riprendi"}, {"resume_game", ">>> RIPRENDI IL GIOCO <<<"}, {"touchui", "Pulsanti a schermo (tocco)"}, {"touch_menu", "MENU"},
        {"play", "GIOCA"}, {"resume", "RIPRENDI"}, {"settings", "IMPOSTAZIONI"}, {"exit", "ESCI"}, {"paused", "PAUSA"}, {"back", "Indietro"},
        {"tab_controls", "COMANDI"}, {"tab_stick", "LEVA E TOCCO"}, {"tab_video", "VIDEO"}, {"tab_game", "GIOCO"},
        {"h_select", "Scegli"}, {"h_back", "Esci"}, {"h_resume", "Riprendi"}, {"h_change", "Cambia"}, {"h_assign", "Assegna"}, {"h_tabs", "Schede"},
        {"h_touch", "Tocca una riga per cambiarla, trascina per scorrere"}, {"h_touch_bind", "Tocca una casella e premi il nuovo tasto o pulsante"},
        {"press_key_t", "PREMI UN TASTO"}, {"press_button_t", "PREMI UN PULSANTE"},
        {"wait_hint_key", "Esc annulla, Backspace cancella"}, {"wait_hint_pad", "sul controller (Esc annulla, Backspace cancella)"},
        {"cancel", "ANNULLA"}, {"clear", "CANCELLA"}, {"reset_confirm", "Sicuro? Premi di nuovo per ripristinare"},
        {"ctrl_none_short", "Nessun controller"}, {"key_enter", "Invio"}, {"loading", "Caricamento..."}, {"stats", "Statistiche a schermo"}, {"h3ds_help", "SELECT+START o MENU: pausa  -  SELECT o B: moneta"}, {"pad_up", "Croce su"}, {"pad_down", "Croce giù"}, {"pad_left", "Croce sinistra"}, {"pad_right", "Croce destra"},
        {"touch_coin", "MONETA"}, {"touch_svc", "SERVIZIO"}, {"touch_up", "SU"}, {"touch_down", "GIU'"}, {"touch_ok", "INVIO"},
    };
    return m;
}

// ---------------------------------------------------------------- JSON minimo (oggetto di stringhe)
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
// ritorna false se il file non e' un oggetto { "k": "v", ... } valido
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

// lettere accentate UTF-8 -> ASCII (il font del menu ha solo i caratteri 32-126)
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

// ---------------------------------------------------------------- lingue disponibili e lingua attiva
struct Lang { std::string code, name; std::map<std::string, std::string> t; };
inline std::string& dir() { static std::string d = getenv("S23_LANGDIR") ? getenv("S23_LANGDIR") : "lang"; return d; }
inline std::vector<Lang>& all() { static std::vector<Lang> v; return v; }
inline int& cur() { static int c = -1; return c; }

inline void scan() {
    auto& v = all(); v.clear();
    if (DIR* d = opendir(dir().c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name; if (n.size() < 6 || n.substr(n.size() - 5) != ".json") continue;
            std::string p = dir() + "/" + n; FILE* f = fopen(p.c_str(), "rb"); if (!f) continue;
            std::string s; char buf[4096]; size_t k; while ((k = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, k); fclose(f);
            Lang L; L.code = n.substr(0, n.size() - 5);
            if (!parse_object(s, L.t)) { fprintf(stderr, "lingua %s: JSON non valido, ignorato\n", p.c_str()); continue; }
            L.name = L.t.count("language_name") ? ascii(L.t["language_name"]) : L.code;
            v.push_back(std::move(L));
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end(), [](const Lang& a, const Lang& b) { return a.code < b.code; });
}
inline void select(const std::string& code) {
    cur() = -1; for (size_t i = 0; i < all().size(); i++) if (all()[i].code == code) cur() = (int)i;
}
inline std::string cur_code() { return cur() >= 0 ? all()[cur()].code : "it"; }
inline std::string cur_name() { return cur() >= 0 ? all()[cur()].name : "Italiano"; }
inline std::string cur_name_u() { if (cur() >= 0) { auto& t = all()[cur()].t; auto it = t.find("language_name"); if (it != t.end()) return it->second; } return cur_name(); }
// come tr() ma lascia le lettere accentate (il menu nuovo ha un font con i caratteri latini completi)
inline std::string tru(const std::string& key) {
    if (cur() >= 0) { auto& t = all()[cur()].t; auto it = t.find(key); if (it != t.end()) return it->second; }
    auto it = builtin().find(key); return it != builtin().end() ? it->second : key;
}
// testo per la chiave nella lingua attiva (poi italiano interno, poi la chiave stessa)
inline std::string tr(const std::string& key) {
    if (cur() >= 0) { auto& t = all()[cur()].t; auto it = t.find(key); if (it != t.end()) return ascii(it->second); }
    auto it = builtin().find(key); return it != builtin().end() ? it->second : key;
}

}  // namespace i18n
