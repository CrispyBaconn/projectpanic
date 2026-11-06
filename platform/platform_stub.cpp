// Platform stub for bringing up recompiled Panic Park.
// - loads the ELF into RAM and starts at 0x80000320
// - hardware memories as RAM (C361 char/text, C404, palette, C422, NVRAM), data ROM from the original files
// - CP0 (Count/Compare/Status/Cause) + timer interrupts delivered at BACKEDGE points
// - C416: memory shared with the H8 + "fake H8" (minimal HLE)
// - CTL: DSW and shift ports; at the end of the run it prints the text shown in VRAM
#include "../runtime.h"
#include "../gen_full/funcs.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
static inline void localtime_r(const time_t* t, struct tm* r) { localtime_s(r, t); }
#else
#include <sys/time.h>
#endif
#ifdef __WIIU__
// Wii U (Aroma, wut): no POSIX signals or pthread; coreinit threads, time from OSGetSystemTime
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <malloc.h>
#ifndef S23_WIIU_DIR
#define S23_WIIU_DIR "fs:/vol/external01/wiiu/apps/panicprk"
#endif
#ifndef S23_POLL_TICK
#define S23_POLL_TICK 1
#endif
#endif
#ifdef __SWITCH__
// Switch (libnx): pthread and POSIX clock are there, timer signals aren't -> tick driven by backward branches
#ifndef S23_SWITCH_DIR
#define S23_SWITCH_DIR "sdmc:/switch/panicprk"
#endif
#ifndef S23_POLL_TICK
#define S23_POLL_TICK 1
#endif
extern "C" bool s23_switch_localtime(struct tm* out);       // platform/switch_sys.cpp (system local time)
extern "C" bool s23_switch_thread(void* (*fn)(void*), size_t stack, int core, int prio);   // libnx thread on a specific core
extern "C" void s23_switch_thread_exit();
extern "C" void s23_switch_join_all();
#endif
#if defined(__WIIU__)
#define S23_CONSOLE 1
#define S23_APP_DIR S23_WIIU_DIR
#elif defined(__SWITCH__)
#define S23_CONSOLE 1
#define S23_APP_DIR S23_SWITCH_DIR
#elif defined(__3DS__)
// Nintendo 3DS (libctru + citro3d, no SDL): files in sdmc:/3ds/panicprk, tick without signals, GPU with pica3d.h
#include <3ds.h>
#ifndef S23_3DS_DIR
#define S23_3DS_DIR "sdmc:/3ds/panicprk"
#endif
#ifndef S23_POLL_TICK
#define S23_POLL_TICK 1
#endif
#define S23_CONSOLE 1
#define S23_APP_DIR S23_3DS_DIR
#elif defined(__ANDROID__)
// Android (SDL2): app files folder (Android/data/<package>/files), tick without signals, exit with the Back button
#define S23_CONSOLE 1
#ifndef S23_POLL_TICK
#define S23_POLL_TICK 1
#endif
#include <string>
#include <dirent.h>
#include <sys/stat.h>
static std::string s23_android_dir();
#define S23_APP_DIR s23_android_dir()
#endif
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <array>
#ifndef __WIIU__
#include <pthread.h>
#endif
#ifdef USE_SDL
#ifndef __ANDROID__
#define SDL_MAIN_HANDLED                                       // on Android main() becomes SDL_main, called by SDLActivity
#endif
#include <SDL.h>
#ifdef __ANDROID__
// Files folder: preferably the public "PanicPark" folder in phone storage (easy to fill from a PC or
// from a file manager); it needs the "all files access" permission, which PanicParkActivity asks for at startup. Alternatively
// the app's private folder (Android/data/com.recomp.panicpark/files). The one containing panicprk_main.elf is used;
// while the user grants the permission we wait up to 90 seconds.
static std::string s23_android_dir() {
    static std::string dir;
    if (!dir.empty()) return dir;
    const char* ext = getenv("EXTERNAL_STORAGE"); std::string pub = std::string(ext ? ext : "/storage/emulated/0") + "/PanicPark";
    const char* pp = SDL_AndroidGetExternalStoragePath(); std::string priv = pp ? pp : pub;
    auto has = [](const std::string& d) { FILE* f = fopen((d + "/panicprk_main.elf").c_str(), "rb"); if (f) fclose(f); return f != nullptr; };
    mkdir(pub.c_str(), 0775);                                                    // only succeeds with the permission
    for (int i = 0; i < 180; i++) {
        if (has(pub)) { dir = pub; return dir; }
        if (has(priv)) { dir = priv; return dir; }
        mkdir(pub.c_str(), 0775);                                                 // permission granted but folder empty: use it
        FILE* t = fopen((pub + "/.probe").c_str(), "w"); if (t) { fclose(t); remove((pub + "/.probe").c_str()); dir = pub; return dir; }
        struct timespec ts{0, 500000000}; nanosleep(&ts, nullptr);
    }
    dir = priv; return dir;
}
#endif
#endif
#include <map>
#include "i18n.h"
#include <dirent.h>
#include <string>
#include <vector>

uint8_t* g_ram;
static uint8_t* g_boot;
static uint32_t cp0[32][8];
static unsigned g_events = 0, g_limit = 600;
static uint64_t now_ns();
// emulation time: like now_ns() but stopped while the game is paused (settings menu open in game)
static std::atomic<bool> g_pause{false}; static volatile uint64_t g_emu_off = 0, g_pause_emu = 0;
static uint64_t emu_ns() { if (g_pause.load(std::memory_order_acquire)) return g_pause_emu; return now_ns() - g_emu_off; }
static void emu_pause(bool on) {
    if (on && !g_pause.load()) { g_pause_emu = now_ns() - g_emu_off; g_pause.store(true, std::memory_order_release); }
    else if (!on && g_pause.load()) { g_emu_off = now_ns() - g_pause_emu; g_pause.store(false, std::memory_order_release); }
}
static void sleep_ns(uint64_t ns);
#ifdef S23_POLL_TICK
static void poll_tick();                                         // without signals: the 1 kHz tick is checked by the backward branches
#endif
static volatile bool g_exit_req = false;                         // orderly thread shutdown (consoles)
static bool g_quiet = false; static bool g_win_mode = false;
// ---------------------------------------------------------------- main CPU speed limiter
// measurements for the on-screen indicator (accumulated microseconds, reset every second; 32-bit: the Wii U has no 64-bit atomics)
static std::atomic<uint32_t> g_mips_idle_us{0}, g_h8_idle_us{0}, g_t_gather_us{0}, g_t_3d_us{0}, g_t_comp_us{0}, g_t_pres_us{0};
static uint64_t thread_cpu_ns_fwd(); static uint64_t g_spin_ns = 0;
static double g_be_ns = 22.5;                                    // minimum ns per backward branch (3 R4650 cycles at 133 MHz)
static uint64_t g_thr_t0 = 0, g_thr_s0 = 0;
void backedge_throttle(CPU* c) {
#ifdef S23_POLL_TICK
    const uint64_t CHUNK = 4096;                                  // ~0.1 ms: the system tick goes through here too
#if defined(__WIIU__)
    if (g_exit_req) OSExitThread(0);
#elif defined(__SWITCH__)
    if (g_exit_req) s23_switch_thread_exit();
#elif defined(__3DS__)
    if (g_exit_req) threadExit(0);
#elif defined(__ANDROID__)
    if (g_exit_req) pthread_exit(nullptr);
#endif
    poll_tick();
#else
    const uint64_t CHUNK = 65536;                                 // ~1.5 ms of wait cycles at the R4650's rate
#endif
    if (g_pause.load()) { while (g_pause.load() && !g_exit_req) sleep_ns(5000000); g_thr_t0 = 0; }   // pause (in-game menu)
    uint64_t n = emu_ns();
    { static const bool cost = getenv("S23_MIPSCOST") != nullptr; static uint64_t ct = 0, cc = 0; if (cost && now_ns() > ct + 2000000000ull) { uint64_t cn = thread_cpu_ns_fwd(), tn = now_ns(); static uint64_t sp = 0; if (ct) printf("MIPSCOST %.0f%% of a core (of which busy-waiting %.0f%%)\n", (cn - cc) / 1e7 / ((tn - ct) / 1e9), (g_spin_ns - sp) / 1e7 / ((tn - ct) / 1e9)); sp = g_spin_ns; ct = tn; cc = cn; } }
    { static const bool rate = getenv("S23_BERATE") != nullptr; static uint64_t rt = 0, rs = 0; if (rate && n > rt + 1000000000ull) { if (rt) printf("BERATE %.1f M backedges/s\n", (c->steps - rs) / ((n - rt) / 1e3)); rt = n; rs = c->steps; } }
    if (!g_thr_t0 || g_be_ns <= 0) { g_thr_t0 = n; g_thr_s0 = c->steps; c->steps_lim = c->steps + CHUNK; return; }
    uint64_t expect = g_thr_t0 + (uint64_t)((double)(c->steps - g_thr_s0) * g_be_ns);
    if (expect > n) {                                             // too fast: the game is waiting, yield the CPU
        uint64_t left = expect - n; g_mips_idle_us += (uint32_t)(left / 1000);
#ifdef _WIN32
        if (left > 1200000) Sleep((DWORD)((left - 200000) / 1000000));
#elif defined(S23_CONSOLE)
        if (left > 20000) sleep_ns(left);                         // consoles: sleep (the core is needed by the render threads)
#else
        if (left > 200000) sleep_ns(left - 100000);
#endif
        { const uint64_t sp0 = now_ns(); while (emu_ns() < expect) {} g_spin_ns += now_ns() - sp0; }
    } else if (n - expect > 20000000ull) { g_thr_t0 = n; g_thr_s0 = c->steps; }   // behind (heavy code): no catching up
    c->steps_lim = c->steps + CHUNK;
}                          // in windowed mode no access log (the Windows console is very slow)
static std::map<uint32_t, unsigned> g_seen;
static uint64_t g_ctlreads = 0, g_c361reads = 0;
static uint32_t g_c361_x = 0, g_c361_y = 0;
static unsigned g_frames_saved = 0; static uint64_t g_c361_irqs = 0;
static CPU* g_cpu = nullptr;
static volatile uint32_t g_ip = 0;
static unsigned g_ticks = 0, g_deliv = 0;
static uint64_t g_deliv_ip[8] = {0};
static const double VBL_PERIOD_NS = 1e9 / 59.9;        // pixel clock 25.6 MHz / (814*525), see the MAME driver
static const double COUNT_HZ = 169344000.0 / 2.0;     // Count = clock/2 (mips3.cpp), clock = BUSCLOCK*5
static uint64_t t_zero = 0; static uint32_t count_zero = 0;

// ---------------------------------------------------------------- hardware RAM blocks
struct Blk { uint32_t lo, hi; const char* name; std::vector<uint8_t> m; unsigned logged = 0; uint64_t reads = 0, writes = 0; };
static std::vector<Blk> g_blk;
static const uint8_t* g_data = nullptr; static size_t g_data_n = 0;   // 16 MB data ROM (asset data.bin)
#ifdef __3DS__
// 3DS: the data ROM stays on the SD card and is read in 4 KB pages (2 MB direct-mapped cache): 14 MB less RAM
static FILE* g_data_f = nullptr; static uint8_t* g_dp_buf = nullptr; static uint32_t g_dp_tag[512];
static inline uint8_t data_byte(size_t o) {
    if (o >= g_data_n) return 0xff;
    if (!g_data_f) return g_data ? g_data[o] : 0xff;
    const uint32_t page = (uint32_t)(o >> 12), slot = page & 511;
    if (g_dp_tag[slot] != page) { fseek(g_data_f, (long)page << 12, SEEK_SET); if (fread(g_dp_buf + slot * 4096, 1, 4096, g_data_f) != 4096) memset(g_dp_buf + slot * 4096, 0xff, 4096); g_dp_tag[slot] = page; }
    return g_dp_buf[slot * 4096 + (o & 4095)];
}
#else
static inline uint8_t data_byte(size_t o) { return o < g_data_n ? g_data[o] : 0xff; }
#endif
static uint32_t g_dsw = 0xff;
static void blk_init() {
    struct D { uint32_t lo, hi; const char* n; } d[] = {
        {0x06000000, 0x0600ffff, "NVRAM"}, {0x06200000, 0x06203fff, "C422 RAM"},
        {0x06800000, 0x0681ffff, "C361 char+text"}, {0x06a08000, 0x06a0ffff, "C404 regs/gamma"},
        {0x06a10000, 0x06a3ffff, "Palette RAM"} };
    for (auto& x : d) { Blk b; b.lo = x.lo; b.hi = x.hi; b.name = x.n; b.m.assign(x.hi - x.lo + 1, 0); g_blk.push_back(std::move(b)); }
}
static Blk* blk_find(uint32_t p) { for (auto& b : g_blk) if (p >= b.lo && p <= b.hi) return &b; return nullptr; }
static Blk* blk_named(const char* n) { for (auto& b : g_blk) if (std::string(b.name) == n) return &b; return nullptr; }

// ---------------------------------------------------------------- NVRAM (backup RAM: service menu settings, calibration, records)
static std::string g_nv_path; static std::vector<uint8_t> g_nv_saved; static bool g_nv_on = false;
static void nvram_load() {
    Blk* b = blk_named("NVRAM"); if (!b) return;
    g_nv_path = getenv("S23_NVRAM") ? getenv("S23_NVRAM") : "nvram.bin"; g_nv_on = true;
    if (FILE* f = fopen(g_nv_path.c_str(), "rb")) {
        std::vector<uint8_t> d(b->m.size()); size_t n = fread(d.data(), 1, d.size(), f); fclose(f);
        if (n == d.size()) { b->m = d; printf("NVRAM loaded from %s\n", g_nv_path.c_str()); }
        else printf("NVRAM: %s has a different size, ignored\n", g_nv_path.c_str());
    }
    g_nv_saved = b->m;
}
static void nvram_save() {                                     // saves only if it changed (temporary file + rename)
    if (!g_nv_on) return; Blk* b = blk_named("NVRAM"); if (!b) return;
    std::vector<uint8_t> cur = b->m; if (cur == g_nv_saved) return;
    std::string tmp = g_nv_path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb"); if (!f) return;
    bool ok = fwrite(cur.data(), 1, cur.size(), f) == cur.size(); ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp.c_str()); return; }
    remove(g_nv_path.c_str());
    if (rename(tmp.c_str(), g_nv_path.c_str()) == 0) g_nv_saved = cur;
}

// game language (OTHERS > LANGUAGE option of the service menu): byte 307 of the backup data.
// The data lives in the odd bytes of the NVRAM (8-bit bus): 8-byte header, then 8 banks of 0x98c bytes;
// the bank in use is the 32-bit word in bytes 4-7 of the header. In RAM the game keeps a copy at 0x8028d930.
static int g_game_lang = -1;                                   // -1 = leave unchanged; 0..7 = JPN ENG GER FRA SPA POR ITA KOR
static void apply_game_lang(bool ram) {
    if (g_game_lang < 0 || g_game_lang > 7) return;
    if (Blk* b = blk_named("NVRAM")) {
        auto H = [&](int k) { return (uint32_t)b->m[1 + 2 * k]; };
        uint32_t bank = (H(4) << 24) | (H(5) << 16) | (H(6) << 8) | H(7); if (bank >= 8) bank = 0;
        size_t off = 0x11 + (size_t)bank * 0x1318 + 2 * 307; if (off < b->m.size()) b->m[off] = (uint8_t)g_game_lang;
    }
    if (ram) g_ram[0x28d930 + 307] = (uint8_t)g_game_lang;
}

// ---------------------------------------------------------------- translated game texts (lang/<code>.json, "game:..." keys)
// The game keeps its texts in 80-byte records: 0xa84d47b0, length (32-bit), then a 16-bit code per character
// (font index: A-T 0-19, U-Z 100-105, digits 213-219/300-302, punctuation 3xx, space 1214, accented letters 13xx-14xx).
struct GameFont { uint32_t magic; std::vector<std::pair<int, const char*>> chars; };
static const GameFont GAME_FONTS[] = {
    {0xa84d47b0u, {{213, "0"}, {214, "1"}, {215, "2"}, {216, "3"}, {217, "4"}, {218, "5"}, {219, "6"}, {300, "7"}, {301, "8"}, {302, "9"},   // small font
                   {304, "-"}, {307, "!"}, {313, "("}, {314, ")"}, {315, ","}, {316, "."}, {317, "'"}, {319, ":"}, {1214, " "}, {1519, "?"}, {1600, "\""}}},
    {0xa84c6a90u, {{1100, "0"}, {1101, "1"}, {1102, "2"}, {1103, "3"}, {1104, "4"}, {1105, "5"}, {1106, "6"}, {1107, "7"}, {1108, "8"}, {1109, "9"},   // large font
                   {219, " "}, {1110, "!"}, {2919, ":"}, {3000, "("}, {3001, ")"}}},
};
static std::string game_char(const GameFont& F, int c) {
    if (c >= 0 && c < 20) return std::string(1, (char)('A' + c));
    if (c >= 100 && c < 106) return std::string(1, (char)('U' + c - 100));
    for (auto& p : F.chars) if (p.first == c) return p.second;
    char b[16]; snprintf(b, sizeof b, "{%d}", c); return b;
}
static std::vector<int> game_encode(const GameFont& F, const std::string& t) {   // text -> codes; "{n}" inserts code n
    std::vector<int> o; int space = 1214; for (auto& p : F.chars) if (p.second[0] == ' ') space = p.first;
    for (size_t i = 0; i < t.size(); i++) {
        char ch = (char)toupper((unsigned char)t[i]);
        if (ch == '{') { size_t j = t.find('}', i); if (j != std::string::npos) { o.push_back(atoi(t.substr(i + 1, j - i - 1).c_str())); i = j; continue; } }
        if (ch >= 'A' && ch <= 'T') { o.push_back(ch - 'A'); continue; }
        if (ch >= 'U' && ch <= 'Z') { o.push_back(100 + ch - 'U'); continue; }
        int code = space; for (auto& p : F.chars) if (p.second[0] == ch && !p.second[1]) code = p.first;   // character the font doesn't have -> space
        o.push_back(code);
    }
    return o;
}
static void game_text_patch(const std::string& code, bool list_only = false) {
    if (i18n::all().empty()) i18n::scan();
    const i18n::Lang* L = nullptr; for (auto& l : i18n::all()) if (l.code == code) L = &l;
    if (!L && !list_only) return;
    unsigned n = 0, rec = 0;
    for (uint32_t a = 0x100000; a + 80 <= 0x140000; a += 2) {      // records area (program data)
        uint32_t mg = ((uint32_t)g_ram[a] << 24) | (g_ram[a + 1] << 16) | (g_ram[a + 2] << 8) | g_ram[a + 3];
        const GameFont* F = nullptr; for (auto& f : GAME_FONTS) if (f.magic == mg) F = &f;
        if (!F) continue;
        uint32_t len = ((uint32_t)g_ram[a + 4] << 24) | (g_ram[a + 5] << 16) | (g_ram[a + 6] << 8) | g_ram[a + 7]; if (len == 0 || len > 36) continue;
        rec++;
        std::string txt; for (uint32_t k = 0; k < len; k++) txt += game_char(*F, (g_ram[a + 8 + 2 * k] << 8) | g_ram[a + 9 + 2 * k]);
        if (list_only) { if (txt.find('{') == std::string::npos) printf("  \"game:%s\": \"\",\n", txt.c_str()); continue; }
        auto it = L->t.find("game:" + txt); if (it == L->t.end() || it->second.empty()) continue;
        std::vector<int> enc = game_encode(*F, i18n::ascii(it->second)); if (enc.size() > 36) enc.resize(36);
        for (uint32_t k = 0; k < 36; k++) { uint32_t v = k < enc.size() ? (uint32_t)enc[k] : 0; g_ram[a + 8 + 2 * k] = v >> 8; g_ram[a + 9 + 2 * k] = v & 255; }
        uint32_t L2 = (uint32_t)enc.size(); g_ram[a + 4] = L2 >> 24; g_ram[a + 5] = L2 >> 16; g_ram[a + 6] = L2 >> 8; g_ram[a + 7] = L2;
        a += 78; n++;
    }
    // ASCII strings (system font, e.g. "COIN(S)" at the bottom): keys "ascii:<text>", same length as the original
    if (!list_only) for (auto& kv : L->t) {
        if (kv.first.compare(0, 6, "ascii:") != 0) continue;
        std::string from = kv.first.substr(6), to = i18n::ascii(kv.second); if (from.empty()) continue;
        to.resize(from.size(), ' ');
        for (uint32_t a = 0x0c0000; a + from.size() < 0x200000; a++) {
            if (memcmp(g_ram + a, from.data(), from.size()) != 0 || g_ram[a + from.size()] != 0 || g_ram[a - 1] != 0) continue;
            memcpy(g_ram + a, to.data(), to.size()); n++;
        }
    }
    if (!list_only) printf("game texts: %u translated out of %u (lang/%s.json)\n", n, rec, code.c_str());
}

static void c435_report();
static void dump_screen() {
    Blk* b = blk_named("C361 char+text"); if (!b) return;
    printf("\n--- text in C361 text VRAM (0x0681e000, 64 columns) ---\n");
    uint32_t base = 0x0681e000 - b->lo; bool any = false;
    for (int r = 0; r < 64; r++) {
        std::string line; bool has = false;
        for (int c = 0; c < 64; c++) {
            uint32_t o = base + r * 0x80 + c * 2; uint8_t ch = b->m[o + 1];
            if (ch >= 32 && ch < 127) { line += (char)ch; if (ch != ' ') has = true; } else line += ' ';
        }
        if (has) { any = true; while (!line.empty() && line.back() == ' ') line.pop_back(); printf("%2d| %s\n", r, line.c_str()); }
    }
    if (!any) printf("(empty)\n");
}
static void (*g_on_stop)() = nullptr;                               // analysis: called at the end (H8 profile)
static void summary(const char* why) {
    printf("\n== STOP: %s | events=%u CTL accesses=%llu C361=%llu | timer ticks=%u interrupts delivered=%u\n", why, g_events,
           (unsigned long long)g_ctlreads, (unsigned long long)g_c361reads, g_ticks, g_deliv);
    printf("   interrupts delivered per IP:"); for (int i = 0; i < 8; i++) if (g_deliv_ip[i]) printf(" IP%d=%llu", i, (unsigned long long)g_deliv_ip[i]); printf("\n");
    for (auto& b : g_blk) printf("   %-16s reads=%llu writes=%llu\n", b.name, (unsigned long long)b.reads, (unsigned long long)b.writes);
    c435_report();
    printf("   PNG frames saved: %u | C361 scanline IRQs raised: %llu\n", g_frames_saved, (unsigned long long)g_c361_irqs);
    if (const char* d = getenv("S23_DUMP")) {                                  // saves the video memories for offline analysis
        for (auto& b : g_blk) { std::string n = std::string(d) + "/" + (b.lo == 0x06800000 ? "charram" : b.lo == 0x06a10000 ? "palette" : b.lo == 0x06a08000 ? "c404" : b.lo == 0x06000000 ? "nvram" : "c422") + ".bin";
            FILE* f = fopen(n.c_str(), "wb"); if (f) { fwrite(b.m.data(), 1, b.m.size(), f); fclose(f); } }
        FILE* f = fopen((std::string(d) + "/c361regs.txt").c_str(), "w"); if (f) { fprintf(f, "xscroll=%x yscroll=%x\n", g_c361_x, g_c361_y); fclose(f); }
    }
    dump_screen();
    if (g_on_stop) g_on_stop();
    printf("distinct HW addresses (outside the RAM blocks): %zu\n", g_seen.size());
    unsigned n = 0; for (auto& kv : g_seen) { if (n++ < 80) printf("  %08x  x%u\n", kv.first, kv.second); }
    fflush(stdout); _exit(0);
}
static void on_alarm(int) { summary("timeout (the game runs but produces no new events)"); }
static void note(const char* k, uint32_t a, int sz, uint32_t v, bool wr) {
    uint32_t p = a & 0x0fffffff; g_seen[p]++;
    if (!g_quiet && g_events < g_limit) printf("%s%d %08x %s %08x\n", k, sz, a, wr ? "<=" : "=>", v);
    if (++g_events >= g_limit) summary("event limit");
}

// ---------------------------------------------------------------- timer / IRQ
#ifdef __WIIU__
static uint64_t now_ns() { return (uint64_t)OSTicksToNanoseconds((uint64_t)OSGetSystemTime()); }
static void sleep_ns(uint64_t ns) { OSSleepTicks(OSNanosecondsToTicks(ns)); }
#elif defined(__3DS__)
static uint64_t now_ns() { return (uint64_t)((double)svcGetSystemTick() * (1e9 / SYSCLOCK_ARM11)); }
static void sleep_ns(uint64_t ns) { svcSleepThread((s64)ns); }
#else
static uint64_t now_ns() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec; }
static void sleep_ns(uint64_t ns) { timespec ts{(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)}; nanosleep(&ts, nullptr); }
#endif
// ---------------------------------------------------------------- threads (pthread, or coreinit on Wii U)
typedef void* (*ThreadFn)(void*);
#ifdef __WIIU__
struct WiiUThread { OSThread t; ThreadFn fn; };                                       // OSThread first: 16-byte aligned
static int wiiu_thread_entry(int, const char** argv) { WiiUThread* w = (WiiUThread*)argv; w->fn(nullptr); return 0; }
// core: 0..2 (the Wii U has 3 cores; the main thread with SDL is on core 1)
static OSThread* start_thread(ThreadFn fn, size_t stack, int core, int prio, const char* name) {
    WiiUThread* w = (WiiUThread*)memalign(16, sizeof(WiiUThread)); uint8_t* st = (uint8_t*)memalign(16, stack);
    if (!w || !st) return nullptr;
    memset(w, 0, sizeof *w); w->fn = fn;
    OSThreadAttributes a = core == 0 ? OS_THREAD_ATTRIB_AFFINITY_CPU0 : core == 1 ? OS_THREAD_ATTRIB_AFFINITY_CPU1 : OS_THREAD_ATTRIB_AFFINITY_CPU2;
    if (!OSCreateThread(&w->t, wiiu_thread_entry, 0, (char*)w, st + stack, (uint32_t)stack, prio, a)) return nullptr;
    OSSetThreadName(&w->t, name); OSResumeThread(&w->t);
    return &w->t;
}
static OSThread* g_th_game = nullptr; static OSThread* g_th_h8 = nullptr;
#elif defined(__3DS__)
// 3DS: libctru thread on a specific core (0 = this one, 1 = system, limited to 80%, 2 = New 3DS only)
static Thread g_t3[8]; static int g_t3n = 0;
static void t3_entry(void* a) { ((ThreadFn)a)(nullptr); }
static bool start_thread(ThreadFn fn, size_t stack, int core, int prio, const char*) {
    Thread t = threadCreate(t3_entry, (void*)fn, stack, prio ? prio : 0x30, core, false);
    if (!t && core == 2) t = threadCreate(t3_entry, (void*)fn, stack, prio ? prio : 0x30, 1, false);   // Old 3DS: no core 2
    if (!t) return false; if (g_t3n < 8) g_t3[g_t3n++] = t; return true;
}
#elif defined(__SWITCH__)
// Switch: libnx threads on separate cores (with pthread they would all end up on core 0 of the main thread, and a
// thread that doesn't yield the CPU blocks the others). Core 0 = window/SDL, 1 = R4650, 2 = H8/ASCA.
static bool start_thread(ThreadFn fn, size_t stack, int core, int prio, const char*) { return s23_switch_thread(fn, stack, core, prio ? prio : 0x2C); }
#else
static bool start_thread(ThreadFn fn, size_t stack, int, int, const char*) {
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, stack);
    pthread_t th; return pthread_create(&th, &at, fn, nullptr) == 0;
}
#endif
#ifdef S23_CONSOLE
// orderly shutdown: the threads check g_exit_req (R4650 at backward branches, H8 in its loop) and exit
static void console_stop_threads() {
    g_exit_req = true;
#if defined(__WIIU__)
    // wait with a limit (2 s per thread): with OSJoinThread, if a thread didn't reach the g_exit_req check the
    // console stayed stuck on the HOME menu logo ("Close software"). If it doesn't finish we exit anyway.
    for (OSThread* t : {g_th_game, g_th_h8}) {
        if (!t) continue; const uint64_t t0 = now_ns();
        while (!OSIsThreadTerminated(t) && now_ns() - t0 < 2000000000ull) sleep_ns(5000000);
        printf("exit: thread %s %s\n", t == g_th_game ? "R4650" : "H8", OSIsThreadTerminated(t) ? "finished" : "NOT finished (exiting anyway)"); fflush(stdout);
    }
#elif defined(__SWITCH__)
    s23_switch_join_all();
#elif defined(__3DS__)
    for (int i = 0; i < g_t3n; i++) { threadJoin(g_t3[i], 2000000000ull); threadFree(g_t3[i]); } g_t3n = 0;
#else
    sleep_ns(200000000ull);                                         // Android: the (detached) threads exit on their own
#endif
}
#endif
// ---------------------------------------------------------------- helper threads for 3D drawing
// The rasteriser splits the screen into bands of lines; the workers take them from an atomic counter.
// With no work they sleep 50 us. On consoles they run on the game and H8 cores at HIGHER priority: a worker at
// low priority that has taken a band and gets suspended blocks the whole frame (148 ms measured on Switch).
struct RenderPool { std::atomic<unsigned> gen{0}; std::atomic<int> next{0}, done{0}; std::atomic<int> njobs{0}; void (*fn)(void*, int) = nullptr; void* ctx = nullptr; int workers = 0; };
static RenderPool g_pool;
#ifndef S23_CONSOLE
static std::mutex g_pool_m; static std::condition_variable g_pool_cv;
#endif
static void* pool_worker(void*) {
    unsigned seen = g_pool.gen.load();
    for (;;) {
#ifdef S23_CONSOLE
        if (g_exit_req) return nullptr;
#endif
        unsigned g = g_pool.gen.load(std::memory_order_acquire);
#ifdef S23_CONSOLE
        if (g == seen) { sleep_ns(50000); continue; }
#else
        if (g == seen) { std::unique_lock<std::mutex> lk(g_pool_m); g_pool_cv.wait_for(lk, std::chrono::milliseconds(20), [&] { return g_pool.gen.load() != seen; }); continue; }   // PC: immediate wake-up
#endif
        seen = g;
        for (;;) { int j = g_pool.next.fetch_add(1); if (j >= g_pool.njobs.load()) break; g_pool.fn(g_pool.ctx, j); g_pool.done.fetch_add(1); }
    }
}
static void pool_run(int n, void (*fn)(void*, int), void* ctx) {
    g_pool.fn = fn; g_pool.ctx = ctx; g_pool.njobs = n; g_pool.done = 0; g_pool.next = 0;
#ifndef S23_CONSOLE
    { std::lock_guard<std::mutex> lk(g_pool_m); g_pool.gen.fetch_add(1, std::memory_order_release); } g_pool_cv.notify_all();
#else
    g_pool.gen.fetch_add(1, std::memory_order_release);
#endif
    for (;;) { int j = g_pool.next.fetch_add(1); if (j >= n) break; fn(ctx, j); g_pool.done.fetch_add(1); }
    while (g_pool.done.load() < n) {
#ifdef __WIIU__
        if (g_exit_req) break;                                             // at shutdown a worker may already have exited
        OSYieldThread();                                                   // Wii U: no time slicing between threads of equal priority
#endif
    }
    g_pool.njobs = 0;                                                      // a late worker doesn't take anything else
}
static uint32_t count_now() { return count_zero + (uint32_t)((emu_ns() - t_zero) * COUNT_HZ / 1e9); }
static bool g_c361_armed = false; static uint64_t g_c361_at = 0;
static uint32_t g_rowscroll[480]; static uint32_t g_linex[1024]; static unsigned g_lastrow = 0; static int64_t g_rs_frame = -1;
static uint64_t vbl_period() { return (uint64_t)VBL_PERIOD_NS; }
static unsigned cur_line(uint64_t n) { uint64_t P = vbl_period(), ph = (n - t_zero) % P; return (480 + (unsigned)(ph * 525 / P)) % 525; }  // vpos 0 = first visible line
static int64_t frame_idx(uint64_t n) { uint64_t P = vbl_period(), vbend = P * 45 / 525; return n - t_zero < vbend ? 0 : (int64_t)((n - t_zero - vbend) / P); }
static uint64_t time_of_line(unsigned N) {                                   // next moment when vpos == N
    uint64_t n = emu_ns(), P = vbl_period(), k = (n - t_zero) / P, ph = (n - t_zero) % P;
    uint64_t target = ((uint64_t)((N + 525 - 480) % 525) * P) / 525;
    return ph < target ? t_zero + k * P + target : t_zero + (k + 1) * P + target;
}
static void update_rowscroll() {                                              // like update_text_rowscroll() in MAME
    uint64_t n = emu_ns(); int64_t f = frame_idx(n);
    if (f != g_rs_frame) { g_rs_frame = f; g_lastrow = 0; }
    uint32_t sx = (g_c361_x - 0x35c) & 0x3ff; unsigned y = std::min(cur_line(n) + 4, 480u);
    for (unsigned i = g_lastrow; i < y; i++) g_rowscroll[i] = sx;
    if (y > g_lastrow) g_lastrow = y;
}
static void update_pending() {
    uint32_t st = cp0[12][0]; bool ie = (st & 1) && !(st & 6);
    if (g_cpu) g_cpu->irq_pending = (ie && (g_ip & st & 0xff00)) || g_c361_armed;       // if armed, the clock is checked at every branch
}
static bool g_cmp_armed = false;
static uint64_t g_next_vbl = 0;
// copy of the video registers at the real vblank (MAME draws in screen_update, i.e. at the start of vblank): the game changes them right after
struct VSnap { std::vector<uint8_t> cr, pl, c4; uint32_t x = 0, y = 0; };
static VSnap g_sn[2]; static volatile int g_sn_idx = -1; static Blk *g_b_cr = nullptr, *g_b_pl = nullptr, *g_b_c4 = nullptr;
static void take_vbl_snapshot();
static void on_tick(int) {                             // 1 kHz system tick (SIGALRM)
    ++g_ticks;
    if (g_cmp_armed && !(g_ip & 0x8000) && (int32_t)(count_now() - cp0[11][0]) >= 0) g_ip |= 0x8000;     // IP7: Count == Compare
    uint64_t n = emu_ns();
    if (n >= g_next_vbl) { g_ip |= 0x0400; g_next_vbl = t_zero + (uint64_t)((double)((n - t_zero) / (uint64_t)VBL_PERIOD_NS + 1) * VBL_PERIOD_NS); take_vbl_snapshot(); }   // fixed grid: a late tick doesn't stretch the frame (exactly 59.9 Hz)                  // IP2: vblank (IRQ0), stays until ack
    update_pending();
}
#ifdef S23_POLL_TICK
static uint64_t g_poll_next = 0;
static void poll_tick() { uint64_t n = emu_ns(); if (n >= g_poll_next) { g_poll_next = n + 1000000; on_tick(0); } }
#endif

// ---------------------------------------------------------------- C416 + fake H8
static uint8_t g_shared[0x10000];
static bool g_h8_run = false;
static bool g_use_h8 = false;
static unsigned g_sh_log = 0;
static uint32_t H8_VERSION = 0x0100;                  // assumption: the real value is written by the H8 firmware
static uint32_t sh_get(uint32_t off, int sz) { uint32_t v = 0; for (int i = 0; i < sz; i++) v = (v << 8) | g_shared[(off + i) & 0xffff]; return v; }
static void sh_put(uint32_t off, int sz, uint32_t v) { for (int i = sz - 1; i >= 0; i--) { g_shared[(off + i) & 0xffff] = v & 0xff; v >>= 8; } }
static void h8_reacts(uint32_t off, int sz, uint32_t v) {
    if (g_use_h8 || !g_h8_run || sz != 2) return;
    if (off == 0x3002 && v == 0x3163) {
        sh_put(0x3002, 2, 0x7601); sh_put(0x3000, 2, H8_VERSION); sh_put(0x3018, 2, 0xffff); sh_put(0x301a, 2, 0xffff);
        if (g_sh_log++ < 40) printf("H8*: 0x3002 3163 -> 7601 (version %04x, ramcheck ffff/ffff)\n", H8_VERSION);
    }
    if (off == 0x3078 && v != 0) { sh_put(0x3078, 2, 0x0000); if (g_sh_log++ < 60) printf("H8*: command %04x in 0x3078 executed (cell cleared)\n", v); }
    if (off == 0x306a && v == 0x3163) { sh_put(0x306a, 2, 0x0000); if (g_sh_log++ < 40) printf("H8*: 0x306a 3163 -> 0000\n"); }
}
static bool sh_log_read(uint32_t off) { static bool seen[0x8000]; bool n = !seen[off >> 1]; seen[off >> 1] = true; return n; }

// ---------------------------------------------------------------- real H8/3002 + ASCA I/O board (original firmware interpreted)
#include "assets.h"
#include "../h8/h8sys.h"
#include "../h8/ascasys.h"
#include "jvs_hle.h"
static H8Sys* g_h8 = nullptr;
static AscaSys* g_asca = nullptr;                                  // emulated ASCA firmware (only with S23_ASCA=1)
static JvsHle* g_jvs = nullptr;                                    // native I/O board (default)
static AscaInputs g_in;                                               // written by the frontend, read by the ASCA firmware
static std::atomic<int> g_h8_cmd{0};                                  // 1 = reset and start, 2 = stopped
static void block_timer_signals(bool block);
static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static void rtc_fill() {                                              // RTC4543: sec, min, hours, weekday, day, month, year (BCD)
    time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
#ifdef __SWITCH__
    s23_switch_localtime(&tm);                                     // newlib on Switch doesn't know the time zone
#endif
    uint8_t r[7] = {bcd(tm.tm_sec), bcd(tm.tm_min), bcd(tm.tm_hour), (uint8_t)tm.tm_wday, bcd(tm.tm_mday), bcd(tm.tm_mon + 1), bcd(tm.tm_year % 100)};
    memcpy(g_h8->rtc_regs, r, 7);
}
static uint64_t thread_cpu_ns() {                                     // CPU time of the current thread (statistics)
#if defined(CLOCK_THREAD_CPUTIME_ID) && !defined(S23_CONSOLE)
    timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
#else
    return 0;
#endif
}
static uint64_t thread_cpu_ns_fwd() { return thread_cpu_ns(); }
static volatile double g_h8_pct = 100;                                 // H8 speed in the last second (on-screen indicator)
static void* h8_thread(void*) {
    block_timer_signals(true);
    unsigned h8_lost = 0;
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
    bool running = false; uint64_t t0 = 0, cyc0 = 0, acyc0 = 0; const uint64_t LINE = (uint64_t)(H8Sys::CLOCK / 59.9 / 525);
    const double ratio = AscaSys::CLOCK / H8Sys::CLOCK;
    static uint8_t pin_low = 0;
    g_h8->p8_in = [] { return (uint8_t)(0xff & ~(pin_low ? 0x02 : 0)); };
    g_h8->p6_in = [] { return (uint8_t)(0xfd | (((g_jvs ? g_jvs->sense : g_asca->sense_ext) != 2) << 1)); };
    static const bool jvslog = getenv("S23_JVSLOG") != nullptr;
    g_h8->on_sci_tx = [](int n, uint8_t b) { if (n == 0) { if (jvslog) printf("JVS> %02x\n", b); if (g_jvs) g_jvs->rx(b); else g_asca->push_rx({b}); } };
    g_h8->on_rtc_latch = [] { rtc_fill(); };                       // PC date and time on every RTC read
    if (g_jvs) g_jvs->on_tx = [](const std::vector<uint8_t>& v) { if (jvslog) for (uint8_t b : v) printf("JVS< %02x\n", b); g_h8->sci_push(0, v); };
    else g_asca->on_tx = [](uint8_t b) { if (jvslog) printf("JVS< %02x\n", b); g_h8->sci_push(0, {b}); };
    for (;;) {
        if (g_pause.load() && !g_exit_req) { sleep_ns(5000000); continue; }            // pause (in-game menu)
        int c = g_h8_cmd.exchange(0);
        if (c == 1) { g_h8->reset(); rtc_fill(); running = true; t0 = emu_ns(); cyc0 = g_h8->cpu.m_cycles; acyc0 = g_asca ? g_asca->cpu.m_cycles : 0; }
        else if (c == 2) running = false;
#ifdef S23_CONSOLE
        if (g_exit_req) return nullptr;
#endif
        if (!running) { sleep_ns(500000); continue; }
        uint64_t n = emu_ns(), want = cyc0 + (uint64_t)((double)(n - t0) * H8Sys::CLOCK / 1e9);
        if (g_h8->cpu.m_cycles >= want) { static const int hs = getenv("S23_H8SLEEP") ? atoi(getenv("S23_H8SLEEP")) : 500; const uint64_t s0 = now_ns(); sleep_ns((uint64_t)hs * 1000); g_h8_idle_us += (uint32_t)((now_ns() - s0) / 1000); continue; }
        if (want - g_h8->cpu.m_cycles > (uint64_t)(H8Sys::CLOCK / 4)) { t0 = n; cyc0 = g_h8->cpu.m_cycles; acyc0 = g_asca ? g_asca->cpu.m_cycles : 0; h8_lost++; }   // >250 ms behind: resynchronise
        { static uint64_t r1 = 0, c1 = 0; if (n > r1 + 1000000000ull) { if (r1) g_h8_pct = (g_h8->cpu.m_cycles - c1) / H8Sys::CLOCK / ((n - r1) / 1e9) * 100; r1 = n; c1 = g_h8->cpu.m_cycles; } }
        { static uint64_t rep = 0, c_rep = 0; if (n > rep + 5000000000ull) { if (rep) { double pct = (g_h8->cpu.m_cycles - c_rep) / H8Sys::CLOCK / ((n - rep) / 1e9) * 100; if (pct < 97 || h8_lost) { printf("H8: %.0f%% of real time, resyncs %u\n", pct, h8_lost); fflush(stdout); }
              { static const bool cost = getenv("S23_H8COST") != nullptr; static uint64_t cpu0 = 0; uint64_t cn = thread_cpu_ns(); if (cost && cpu0) printf("H8COST %.0f ms of CPU per emulated second (%.0f%% of a core)\n", (cn - cpu0) / 1e6 / ((g_h8->cpu.m_cycles - c_rep) / H8Sys::CLOCK), (cn - cpu0) / 1e7 / ((n - rep) / 1e9)); cpu0 = cn; } h8_lost = 0; } rep = n; c_rep = g_h8->cpu.m_cycles; } }
        static uint64_t dbg_next = 0; static const bool dbg = getenv("S23_H8DBG") != nullptr;
        if (dbg && n > dbg_next) { dbg_next = n + 1000000000ull; printf("H8dbg: PC=%06x ill=%llu 3000=%02x%02x 3002=%02x%02x adcsr=%02x adcl=%lld ascaPC=%04x tx=%llu rx=%llu sense=%d p1=%x\n", g_h8->cpu.m_NPC, (unsigned long long)g_h8->cpu.illegal_count, g_shared[0x3000], g_shared[0x3001], g_shared[0x3002], g_shared[0x3003], g_h8->adcsr, (long long)g_h8->adc_left, g_asca ? g_asca->cpu.m_NPC : 0, (unsigned long long)(g_asca ? g_asca->tx_bytes : g_jvs->tx_bytes), (unsigned long long)(g_asca ? g_asca->rx_bytes : g_jvs->rx_bytes), g_asca ? g_asca->sense_ext : g_jvs->sense, (unsigned)g_in.p1); }
        { static std::vector<std::array<double, 3>> scr; static bool scr_init = false;              // S23_INPUTS="sec:p1_mask:duration,..." (automated tests)
          if (!scr_init) { scr_init = true; if (const char* q = getenv("S23_INPUTS")) { std::string t = q; size_t i = 0; while (i < t.size()) { size_t j = t.find(',', i); if (j == std::string::npos) j = t.size(); double a0 = 0, d0 = 0; unsigned m0 = 0; if (sscanf(t.substr(i, j - i).c_str(), "%lf:%x:%lf", &a0, &m0, &d0) == 3) scr.push_back({a0, (double)m0, d0}); i = j + 1; } } }
          if (!scr.empty()) { double sec = g_deliv_ip[2] / 59.9; /* game seconds (vblank) */ uint32_t m = 0; for (auto& e : scr) if (sec >= e[0] && sec < e[0] + e[2]) m |= (uint32_t)e[1]; g_in.p1 = m; } }
        { static const int ac = getenv("S23_AUTOCOIN") ? atoi(getenv("S23_AUTOCOIN")) : 0; if (ac == 2) { double sec = (n - t_zero) / 1e9; g_in.coin1 = ((sec > 8 && sec < 8.3) || (sec > 10 && sec < 10.3)) ? 1 : 0;
                    { static std::vector<double> cs; static bool ci = false; if (!ci) { ci = true; if (const char* c = getenv("S23_COINS")) { std::string t = c; size_t i = 0; while (i < t.size()) { cs.push_back(atof(t.c_str() + i)); size_t j = t.find(',', i); if (j == std::string::npos) break; i = j + 1; } } }
                      for (double c : cs) if (sec > c && sec < c + 0.3) g_in.coin1 = 1; } g_in.p1 = (sec > 16 && fmod(sec, 3.0) < 0.5) ? 0x03 : 0; } else if (ac) { double sec = (n - t_zero) / 1e9; g_in.coin1 = (fmod(sec, 4.0) > 8.0 / 4 && fmod(sec, 4.0) < 2.1 && sec > 8) ? 1 : 0; g_in.p1 = (sec > 14 && fmod(sec, 4.0) < 0.3) ? 1 : 0; } }
        uint64_t ne = t0 + (uint64_t)((double)(g_h8->cpu.m_cycles - cyc0) * 1e9 / H8Sys::CLOCK);   // emulated H8 time: the raster stays consistent even if the thread runs in bursts (Windows sleeps ~1-15 ms)
        static const bool wallv = getenv("S23_WALLVPOS") != nullptr; unsigned vpos = cur_line(wallv ? n : ne);
        g_h8->portb_in = (vpos >= 480) ? 0x80 : 0x00; g_h8->adtrg_w(vpos >= 480);
        uint64_t base = g_h8->cpu.m_cycles;
        auto sync = [&](uint64_t to) { g_h8->run_to(to); if (g_asca) g_asca->run_to(acyc0 + (uint64_t)((to - cyc0) * ratio)); };
        if (g_jvs) g_jvs->poll_time(0);                                                     // coins
        if (vpos < 72) { pin_low = 1; g_h8->set_irq_pin(1, true); sync(base + LINE / 2); pin_low = 0; g_h8->set_irq_pin(1, false); sync(base + LINE); }
        else sync(base + LINE);
    }
    return nullptr;
}
static bool load_swapped(const std::string& path, size_t size, std::vector<uint8_t>& raw, bool swap) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    raw.resize(size); size_t k = fread(raw.data(), 1, size, f); fclose(f); if (k != size) return false;
    if (swap) for (size_t i = 0; i + 1 < raw.size(); i += 2) std::swap(raw[i], raw[i + 1]);          // WORD_SWAP
    return true;
}
// ---------------------------------------------------------------- audio (C352 -> SDL / WAV file)
static std::atomic<uint32_t> g_adev{0};                               // SDL audio device (0 = none)
static FILE* g_wav = nullptr; static uint32_t g_wav_bytes = 0;
static void wav_header() {
    uint8_t h[44]; auto w32 = [&](int o, uint32_t v) { memcpy(h + o, &v, 4); }; auto w16 = [&](int o, uint16_t v) { memcpy(h + o, &v, 2); };
    memcpy(h, "RIFF", 4); w32(4, 36 + g_wav_bytes); memcpy(h + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 2);
    w32(24, 44100); w32(28, 44100 * 4); w16(32, 4); w16(34, 16); memcpy(h + 36, "data", 4); w32(40, g_wav_bytes);
    long pos = ftell(g_wav); fseek(g_wav, 0, SEEK_SET); fwrite(h, 1, 44, g_wav); if (pos > 44) fseek(g_wav, pos, SEEK_SET);
}
static void (*g_audio_push)(const int16_t*, size_t) = nullptr;   // 3DS: ndsp
static void audio_sink(const int16_t* s, size_t frames) {
    if (g_wav) { fwrite(s, 4, frames, g_wav); g_wav_bytes += (uint32_t)(frames * 4); wav_header(); }
    if (g_audio_push) g_audio_push(s, frames);
#ifdef USE_SDL
    uint32_t dev = g_adev.load();
    if (dev && SDL_GetQueuedAudioSize(dev) < 44100 * 4 / 6) SDL_QueueAudio(dev, s, (Uint32)(frames * 4));   // max ~170 ms queued
#endif
}
static void h8_start_thread(const std::string& dir) {
    std::vector<uint8_t> raw, araw;
    if (!load_swapped(dir + "/pnp1vera.ic3", 0x80000, raw, true)) { printf("WARNING: pnp1vera.ic3 not found in %s: using the fake H8\n", dir.c_str()); return; }
    g_h8 = new H8Sys(); g_h8->rom = raw; g_h8->shared = g_shared;
    g_on_stop = [] { g_h8->dump_prof("h8prof.txt"); printf("H8: cycles skipped while waiting %.1f%%, in delays %.1f%%, in timeouts %.1f%%\n", 100.0 * g_h8->idle_skipped / g_h8->cpu.m_cycles, 100.0 * g_h8->delay_skipped / g_h8->cpu.m_cycles, 100.0 * g_h8->timeout_skipped / g_h8->cpu.m_cycles); };
#ifdef __3DS__
    if (g_h8->snd.set_wave_file(getenv("S23_NOSOUND") ? nullptr : fopen((assets::dir() + "/wave.bin").c_str(), "rb"))) {
#else
    size_t wn = 0; const uint8_t* wp = getenv("S23_NOSOUND") ? nullptr : assets::get("wave.bin", wn, [&](std::vector<uint8_t>& v) { return C352::build_wave(dir, v); });
    if (g_h8->snd.set_wave(wp, wn)) {
#endif
        g_h8->snd.sink = audio_sink; printf("audio: C352 ROMs loaded\n");
        if (const char* w = getenv("S23_WAV")) { g_wav = fopen(w, "wb"); if (g_wav) wav_header(); }
    } else printf("audio: pnp1wavel.2c / pnp1waveh.2a not found (or S23_NOSOUND): no sound\n");
    // I/O board: native JVS reply (platform/jvs_hle.h); S23_ASCA=1 = original asca-3a.ic14 firmware emulated
    if (getenv("S23_ASCA")) {
        g_asca = new AscaSys(); g_asca->in = &g_in;
        if (!g_asca->load_rom(dir + "/asca-3a.ic14")) { printf("WARNING: asca-3a.ic14 not found in %s: using the fake H8\n", dir.c_str()); return; }
        g_asca->reset(); printf("I/O: emulated ASCA-3 firmware\n");
    } else { g_jvs = new JvsHle(); g_jvs->in = &g_in; }
    g_h8->on_main_irq = [](uint16_t v) { if (v == 0x3170) { g_ip |= 0x0800; update_pending(); } };
    g_use_h8 = true;
#ifdef __WIIU__
    g_th_h8 = start_thread(h8_thread, 1u << 20, 0, 15, "H8/ASCA");                        // core 0
#elif defined(__SWITCH__)
    start_thread(h8_thread, 2u << 20, 2, 0, "H8");                                         // core 2
#elif defined(__3DS__)
    start_thread(h8_thread, 512u << 10, 1, 0x2f, "H8");                                    // core 1 (system, 80%)
#else
    start_thread(h8_thread, 16u << 20, 0, 0, "H8");
#endif
    printf("Real H8/3002 + ASCA-3A: firmware loaded from %s\n", dir.c_str());
}
#ifdef USE_SDL
#include "frontend.h"
#endif
#include "gpu3d.h"
static void start_render_pool() {
    int n = 0;
#if defined(__SWITCH__)
    n = 2;                                                                  // core 1 (game) and 2 (H8)
#elif defined(__3DS__)
    n = 0;                                                                  // 3DS: the GPU does the 3D, the cores are already all busy
#elif defined(__WIIU__)
    n = 0;                                                                  // no helper: core 2 belongs entirely to the R4650 (99%), core 0 turned out to be very slow
#else
    { unsigned hc = std::thread::hardware_concurrency(); n = hc > 2 ? (int)std::min(hc - 2, 6u) : 0; }   // game and H8 have their own core
#endif
    if (const char* e = getenv("S23_RTHREADS")) n = atoi(e);
    for (int i = 0; i < n; i++) {
#if defined(__SWITCH__)
        if (!start_thread(pool_worker, 256u << 10, i == 0 ? 1 : 2, 0x2B, "R3D")) break;
#elif defined(__WIIU__)
        if (!start_thread(pool_worker, 256u << 10, i == 0 ? 1 : 2, 15, "R3D")) break;
#else
        if (!start_thread(pool_worker, 1u << 20, 0, 0, "R3D")) break;
#endif
        g_pool.workers++;
    }
    if (g_pool.workers > 0) { g3d::par_run = pool_run; g3d::par_bands = (g_pool.workers + 1) * 3; }
    printf("3D drawing: %d helper threads\n", g_pool.workers);
}
static void take_vbl_snapshot() {
    if (!g_b_cr) return;
    int w = g_sn_idx == 0 ? 1 : 0; VSnap& d = g_sn[w];
    d.cr = g_b_cr->m; d.pl = g_b_pl->m; d.c4 = g_b_c4->m; d.x = g_c361_x; d.y = g_c361_y;   // (assignments to already sized vectors: no allocation after the first)
    g_sn_idx = w; g3d::mark_vbl();
}
// ---------------------------------------------------------------- C417 (3D): registers from the MAME driver
static bool c417_read(uint32_t p, uint32_t& v) {
    if (p >= 0x0c000000 && p <= 0x0c400007) { g_seen[p]++; if (g3d::c412_r(p, v) || g3d::c421_r(p, v)) return true; return false; }
    if (p < 0x02000000 || p > 0x0200000f) return false;
    g_seen[p]++;
    switch (p & ~1u) {
        case 0x02000000: v = 0x008e; return true;                 // status: no unit busy (inverted bits)
        case 0x02000006: v = 0; g3d::C.c417_test = false; return true;
        case 0x02000008: v = g3d::C.c417_ram[g3d::C.c417_adr]; return true;
        case 0x0200000a: v = g3d::C.c417_prom_adr >= 0x800000 ? 0xffff : (g3d::ptrom_word() >> 16); return true;
        case 0x0200000c: if (g3d::C.c417_prom_adr >= 0x800000) { v = 0xffff; return true; } v = g3d::ptrom_word() & 0xffff; g3d::C.c417_prom_adr++; return true;
        case 0x02000002: v = g3d::C.c417_adr; return true;
    }
    v = 0; return true;
}
static bool c417_write(uint32_t p, uint32_t v) {
    if (p >= 0x0c000000 && p <= 0x0c400007) return g3d::c412_w(p, v) || g3d::c421_w(p, v);
    if (p < 0x02000000 || p > 0x0200000f) return false;
    g_seen[p]++;
    switch (p & ~1u) {
        case 0x02000002: g3d::C.c417_adr = (uint16_t)v; break;
        case 0x02000004: g3d::C.c417_prom_adr = (g3d::C.c417_prom_adr << 16) | (v & 0xffff); break;
        case 0x02000006: g3d::C.c417_prom_adr = 0; g3d::C.c417_test = true; break;
        case 0x02000008: g3d::C.c417_ram[g3d::C.c417_adr] = (uint16_t)v; break;
    }
    return true;
}

// ---------------------------------------------------------------- C435 (3D): command framing as in MAME (decode only, no rendering)
static uint32_t g_c435_addr = 0, g_c435_size = 0, g_c435_pos = 0;
static uint16_t g_c435_buf[0x200];
static std::map<uint32_t, uint64_t> g_c435_stats;            // key: class<<16 | type
static unsigned g_c435_logged = 0; static uint64_t g_c435_dma = 0, g_c435_words = 0;
static void c435_irq(bool on) { if (on) g_ip |= 0x1000; else g_ip &= ~0x1000u; update_pending(); }   // IRQ2 -> IP4
static void c435_dispatch() {
    uint16_t h = g_c435_buf[0]; uint32_t cls = h & 0xc000, sub = 0;
    if (cls == 0x0000) sub = h & 0xf0;
    else if (cls == 0x4000) sub = (h & 0x3f00) == 0x0f00 ? (0x0f00 | (g_c435_buf[1] & 0xff)) : (h & 0x3f00);
    g_c435_stats[(cls << 16) | sub]++;
    if (g_c435_logged < 40) { g_c435_logged++; printf("C435 cmd h=%04x:", h); for (uint32_t i = 1; i <= (cls == 0x4000 ? (h & 0xff) : (h & 0xf)) && i < 12; i++) printf(" %04x", g_c435_buf[i]); printf("\n"); }
    if (cls == 0x4000 && (h & 0x3f00) == 0x0f00 && (h & 0xff) && g_c435_buf[1] == 0x0001) c435_irq(g_c435_buf[2] & 1);   // state_set_interrupt
    if (cls == 0x8000 && g_c435_buf[1] == 0) c435_irq(true);                                                          // render with parameter 0: end of list
}
static void c435_pio(uint16_t w) {
    if (g3d::R.ok) { g3d::pio(w); g_c435_words++; return; }
    g_c435_buf[g_c435_pos++ & 0x1ff] = w; g_c435_words++;
    uint16_t h = g_c435_buf[0];
    uint32_t psize = ((h & 0x4000) == 0x4000) ? (h & 0xff) : (h & 0xf);
    if (g_c435_pos < psize + 1) return;
    c435_dispatch(); g_c435_pos = 0;
}
static void c435_dma_run() {
    ++g_c435_dma;
    for (uint32_t pos = 0; pos < g_c435_size; pos += 2) c435_pio((uint16_t)mrd16((g_c435_addr & 0x1fffffff) + pos));
}
static bool c435_write(uint32_t p, uint32_t v) {
    switch (p) {
        case 0x0100001c: g_c435_addr = v; return true;
        case 0x01000020: g_c435_size = v; return true;
        case 0x01000024: if (v & 1) c435_dma_run(); return true;
        case 0x01000060: g_c435_pos = 0; g3d::state_reset(); return true;
        case 0x0c800010: g_c435_pos = 0; g3d::state_reset(); return true;
        case 0x0c800016: g3d::state_pio((uint16_t)v); return true;
        case 0x0cc00000: g3d::direct_buf_w((uint16_t)v); return true;
        case 0x0cc00002: g3d::direct_start((uint16_t)v); return true;
        case 0x0d00000c: g3d::ctl_direct_w((uint16_t)v); return true;
        case 0x02000000: c435_pio((uint16_t)v); return true;            // direct PIO
        case 0x0200000e: c435_irq(false); return true;                 // c417_irq_ack_w
    }
    return false;
}
static void c435_report() {
    printf("   3D: end-of-list=%u vblank=%u models=%u direct=%u immediate=%u\n", g3d::n_eol, g3d::n_vbl, g3d::stat_models, g3d::stat_direct, g3d::stat_imm);
    printf("   C435: DMA=%llu words=%llu\n", (unsigned long long)g_c435_dma, (unsigned long long)g_c435_words);
    for (auto& kv : g_c435_stats) { uint32_t cls = kv.first >> 16, sub = kv.first & 0xffff;
        printf("     class %04x type %04x : %llu\n", cls, sub, (unsigned long long)kv.second); }
}

// ---------------------------------------------------------------- CTL
static uint16_t g_ctl_buf[2] = { 0xfff, 0xfff };
static uint32_t ctl_read(uint32_t p, bool& handled) {
    handled = true;
    switch (p) {
        case 0x0d000002: return g_dsw;
        case 0x0d000004: { uint32_t d = (g_ctl_buf[0] & 0x800) ? 0xffff : 0; g_ctl_buf[0] = (g_ctl_buf[0] << 1) | 1; return d; }
        case 0x0d000006: { uint32_t d = (g_ctl_buf[1] & 0x800) ? 0xffff : 0; g_ctl_buf[1] = (g_ctl_buf[1] << 1) | 1; return d; }
    }
    handled = false; return 0;
}

// ---------------------------------------------------------------- hardware hooks
uint32_t hw_read(uint32_t a, int sz) {
    uint32_t ph = a & 0x1fffffff;
    if (ph >= 0x1fc00000) {                               // boot ROM / user1
        uint32_t o = ph - 0x1fc00000; uint32_t v = 0;
        for (int i = 0; i < sz; i++) v = (v << 8) | g_boot[o + i];
        note("ROM", a, sz, v, false); return v;
    }
    if (ph >= 0x08000000 && ph <= 0x0bffffff) {           // data ROM
        uint32_t o = (ph - 0x08000000) & 0x1ffffff, v = 0;
        for (int i = 0; i < sz; i++) v = (v << 8) | data_byte(o + i);
        g_seen[0x08000000]++; return v;
    }
    if (Blk* b = blk_find(ph)) {
        uint32_t o = ph - b->lo, v = 0; for (int i = 0; i < sz; i++) v = (v << 8) | b->m[o + i];
        b->reads++; return v;
    }
    if (ph >= 0x04400000 && ph <= 0x0440ffff) {
        uint32_t off = ph - 0x04400000, v = sh_get(off, sz);
        if (!g_quiet && sh_log_read(off) && g_events < g_limit) printf("C416 R%d %04x => %0*x\n", sz, off, sz * 2, v);
        g_seen[0x04400000]++; return v;
    }
    { bool h; uint32_t v = ctl_read(ph & 0x0fffffff, h); if (h) { g_seen[ph & 0x0fffffff]++; ++g_ctlreads; return v; } }
    if (ph == 0x01000028) { g_seen[ph]++; return 1; }                  // c435_busy_flag_r: always 1 (MAME driver)
    { uint32_t cv; if (c417_read(ph, cv)) return cv; }
    if (ph == 0x0682000a || ph == 0x0682000c) {            // C361 vpos / vblank (driver MAME: c361_vpos_r, c361_vblank_r)
        uint64_t phase = (emu_ns() - t_zero) % (uint64_t)VBL_PERIOD_NS;
        unsigned line = (480 + (unsigned)(phase * 525 / (uint64_t)VBL_PERIOD_NS)) % 525;   // vblank starts at line 480
        unsigned vb = line >= 480;
        g_seen[ph]++; g_c361reads++; g_ip &= ~0x0800u; update_pending();
        return ph == 0x0682000a ? ((line * 2) | vb) : vb;
    }
    note("R", a, sz, 0, false); return 0;
}
void hw_write(uint32_t a, int sz, uint32_t v) {
    uint32_t p = a & 0x0fffffff;
    { static unsigned cnt[8][4]; static const bool szd = getenv("S23_SZ") != nullptr; if (szd && ((p >= 0x0c000000 && p < 0x0d000010) || (p >= 0x02000000 && p < 0x02000010))) { unsigned k = ((p >> 20) & 7); if (cnt[k][sz & 3]++ < 3) printf("SZ w%d @%08x = %x\n", sz, p, v); } }
    if (Blk* b = blk_find(p)) {
        uint32_t o = p - b->lo; for (int i = sz - 1; i >= 0; i--) { b->m[o + i] = v & 0xff; v >>= 8; }
        b->writes++; return;
    }
    if (p >= 0x04400000 && p <= 0x0440ffff) {
        uint32_t off = p - 0x04400000; uint32_t old = sh_get(off, sz); sh_put(off, sz, v);
        if (!g_quiet && v != old && v != 0 && g_events < g_limit) printf("C416 W%d %04x <= %0*x\n", sz, off, sz * 2, v);
        g_seen[0x04400000]++; h8_reacts(off, sz, v); return;
    }
    if (p == 0x04c3ff04) { g_ip &= ~0x0800u; update_pending(); return; }                           // ack IRQ sub-CPU
    if (p == 0x04c3ff0a) { if (g_use_h8) g_h8_cmd = v ? 1 : 2; g_h8_run = v != 0; printf("MCUEN <= %x (H8 %s)\n", v, g_h8_run ? "started" : "in reset"); return; }
    if (p >= 0x06820000 && p <= 0x0682000f && getenv("S23_TRACE_SCROLL")) { static int t2 = 0; if (t2++ < 60) printf("C361W size=%d addr=%08x val=%08x\n", sz, p, v); }
    if (p == 0x06820000) {
        static int tr = 0; static const bool want = getenv("S23_TRACE_SCROLL") != nullptr;
        if (want && tr++ < 400) printf("SCROLLX frame=%lld line=%u val=%04x (scroll=%u)\n", (long long)frame_idx(now_ns()), cur_line(now_ns()), v, (v - 0x35c) & 0x3ff);
        update_rowscroll(); g_c361_x = v; }
    if (p == 0x06820002) g_c361_y = v;
    if (p == 0x06820008) {                                                                           // c361_irq_scanline_w
        unsigned N = v & 0x1ff;
        if (N >= 525) { g_c361_armed = false; g_ip &= ~0x0800u; }                                    // 0x1ff = disabled
        else { g_c361_at = time_of_line(N); g_c361_armed = true; }
        update_pending();
    }
    if (c435_write(p, v)) return;
    if (c417_write(p, v)) return;
    if (p == 0x0d00000a) { g_ip &= ~0x0400u; update_pending(); return; }                 // ctl_vbl_ack_w
    if (p == 0x0d000004) { g_ctl_buf[0] = 0xfff; ++g_ctlreads; return; }
    if (p == 0x0d000006) { g_ctl_buf[1] = 0xfff; ++g_ctlreads; return; }
    note("W", a, sz, v, true);
}
uint32_t cp0_read(CPU*, int r, int s) {
    if (r == 9) return count_now();
    if (r == 13) return g_ip | cp0[13][0];
    return cp0[r][s];
}
void cp0_write(CPU*, int r, int s, uint32_t v) {
    if (r == 9)  { count_zero = v; t_zero = emu_ns(); return; }
    if (r == 11) { cp0[11][0] = v; g_ip &= ~0x8000u; g_cmp_armed = true; update_pending(); return; }
    if (r == 12) { static int n = 0; if (!g_quiet && n++ < 24) printf("MTC0 Status <= %08x\n", v); cp0[12][0] = v; update_pending(); return; }
    if (r == 13) { g_ip = (g_ip & ~0x300u) | (v & 0x300u); return; }
    cp0[r][s] = v;
}
void cache_op(CPU*, int, uint32_t) {}
void guest_break(CPU*, uint32_t pc, uint32_t code) { char b[64]; snprintf(b, 64, "break @%08x code %x", pc, code); summary(b); }
void guest_unimpl(CPU* c, uint32_t pc, uint32_t w)   {
    if (pc >= 0xbfc013bc && pc < 0xbfc015bc && !((pc - 0xbfc013bc) & 7)) {           // boot ROM services (8-byte stubs)
        unsigned idx = (pc - 0xbfc013bc) / 8; char b[96];
        snprintf(b, 96, "boot ROM service #%u called (a0=%08x a1=%08x); service 0 = exit(a0)", idx, c->r[4], c->r[5]); summary(b);
    }
    char b[80]; snprintf(b, 80, "unhandled instruction/call @%08x (%08x)", pc, w); summary(b); }
// ---------------------------------------------------------------- video: C361/C404 text layer + PNG
static uint32_t g_crc_t[256]; static bool g_crc_init = false;
static uint32_t crc32_buf(const uint8_t* p, size_t n, uint32_t crc = 0xffffffffu) {
    if (!g_crc_init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; g_crc_t[i] = c; } g_crc_init = true; }
    for (size_t i = 0; i < n; i++) crc = g_crc_t[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return crc;
}
static void png_chunk(FILE* f, const char* t, const std::vector<uint8_t>& d) {
    uint8_t len[4] = { (uint8_t)(d.size() >> 24), (uint8_t)(d.size() >> 16), (uint8_t)(d.size() >> 8), (uint8_t)d.size() };
    fwrite(len, 1, 4, f); fwrite(t, 1, 4, f); if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
    uint32_t c = crc32_buf((const uint8_t*)t, 4); c = crc32_buf(d.data(), d.size(), c); c ^= 0xffffffffu;
    uint8_t cb[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c }; fwrite(cb, 1, 4, f);
}
static bool write_png(const std::string& path, const uint8_t* rgb, int w, int h) {      // zlib "stored", no compression
    std::vector<uint8_t> raw; raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgb + (size_t)y * w * 3, rgb + (size_t)(y + 1) * w * 3); }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    for (size_t o = 0; o < raw.size(); o += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - o); bool last = o + n >= raw.size();
        z.push_back(last ? 1 : 0); z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + o, raw.begin() + o + n);
    }
    uint32_t a = 1, b = 0; for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    uint32_t ad = (b << 16) | a; for (int i = 3; i >= 0; i--) z.push_back((ad >> (i * 8)) & 255);
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ih = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w, (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0 };
    png_chunk(f, "IHDR", ih); png_chunk(f, "IDAT", z); png_chunk(f, "IEND", {}); fclose(f); return true;
}
static inline uint32_t h16(const Blk* b, uint32_t o) { return (b->m[o] << 8) | b->m[o + 1]; }
// Like draw_text_layer() + gamma of screen_update() in namcos23.cpp (MAME). No 3D yet.
// draws the frame into out (RGB, W x H). The 3D is rasterised directly at W x H (scale W/640);
// the text (640x480) is scaled up by sampling.
// runs fn(y0, y1) on bands of lines in parallel (3D render threads), or all at once if there are none
template <class F> static void par_rows(int H, const F& fn) {
    if (!g3d::par_run || H < 64) { fn(0, H); return; }
    const int nb = g3d::par_bands; struct C { const F* f; int H, nb; } c{&fn, H, nb};
    g3d::par_run(nb, [](void* p, int b) { C& c = *(C*)p; (*c.f)(c.H * b / c.nb, c.H * (b + 1) / c.nb); }, &c);
}
static int g_3d_div = 1;                                               // 2 = 3D at half resolution (slow consoles), full text
// frame data gathered from the video memories (palette, text layer, gamma, 3D list): used both by the
// software renderer (compose_sw) and the GPU one (platform/gl3d.h)
struct FrameIn {
    bool have3d = false; uint32_t bg[3] = {0, 0, 0}; uint8_t gr[256], gg[256], gb[256];
    std::vector<uint32_t> pal3d = std::vector<uint32_t>(0x8000), txl = std::vector<uint32_t>(640 * 480);
    std::vector<g3d::Entry> lst; uint32_t flags = 0;
    // text layer of the previous frame: if character memory, palette and scroll don't change
    // (almost always) it is not redrawn (on consoles it cost ~12 ms per frame)
    std::vector<uint8_t> k_cr; uint32_t k_tp[256] = {0}; uint32_t k_ys = ~0u, k_on = ~0u; std::vector<uint32_t> k_linex; bool k_ok = false;
    bool txl_new = true;                                                // the text layer was redrawn in this frame
};
static void gather(FrameIn& in) {
    static Blk s_cr, s_pl, s_c4; Blk *cr = &s_cr, *pl = &s_pl, *c4 = &s_c4;
    int si = g_sn_idx; uint32_t snx = g_c361_x, sny = g_c361_y;
    if (si >= 0) { s_cr.m = g_sn[si].cr; s_pl.m = g_sn[si].pl; s_c4.m = g_sn[si].c4; snx = g_sn[si].x; sny = g_sn[si].y; }
    else { cr = blk_named("C361 char+text"); pl = blk_named("Palette RAM"); c4 = blk_named("C404 regs/gamma"); }
    uint32_t palbase = (h16(c4, 0x36) << 8) & 0x7f00, flags = h16(c4, 0x3e); in.flags = flags;
    in.bg[0] = h16(c4, 0x10) & 255; in.bg[1] = h16(c4, 0x12) & 255; in.bg[2] = h16(c4, 0x14) & 255;
    uint32_t ys = sny & 0x3ff;
    static const bool no3d = getenv("S23_NO3D") != nullptr, notext = getenv("S23_NOTEXT") != nullptr;
    in.have3d = g3d::R.ok && !no3d && (flags & 1);
    if (in.have3d) {
        for (int i = 0; i < 0x8000; i++) in.pal3d[i] = ((uint32_t)pl->m[2 * i + 1] << 16) | ((uint32_t)pl->m[0x10000 + 2 * i + 1] << 8) | pl->m[0x20000 + 2 * i + 1];
        g3d::get_display(in.lst);
    } else in.lst.clear();
    static const bool rows = getenv("S23_ROWSCROLL") != nullptr;
    if (rows) { update_rowscroll(); for (int i = 0; i < 480; i++) g_linex[(i + ys + 4) & 0x3ff] = g_rowscroll[i]; }
    else { uint32_t u = (snx - 0x35c) & 0x3ff; for (int i = 0; i < 1024; i++) g_linex[i] = u; }
    // text layer at 640x480: 0 = transparent, otherwise 0x01RRGGBB
    uint32_t* txl = in.txl.data();
    const uint32_t on = ((flags & 4) && !notext) ? 1 : 0;
    uint32_t tp[256];                                                   // text palette: 16 groups x 16 colours
    for (uint32_t k = 0; k < 256; k++) { uint32_t pen = (palbase | k) & 0x7fff; tp[k] = 0x01000000u | ((uint32_t)pl->m[2 * pen + 1] << 16) | ((uint32_t)pl->m[0x10000 + 2 * pen + 1] << 8) | pl->m[0x20000 + 2 * pen + 1]; }
    const size_t CRN = std::min<size_t>(cr->m.size(), 0x20000);        // characters (0x0000-0x1dfff) + map (0x1e000-0x1ffff)
    static const bool nocache = getenv("S23_NOTEXTCACHE") != nullptr;
    const bool same = !nocache && in.k_ok && in.k_on == on && (!on || (in.k_ys == ys && in.k_cr.size() == CRN && !memcmp(in.k_tp, tp, sizeof tp) &&
                      !memcmp(in.k_linex.data(), g_linex, 1024 * sizeof(uint32_t)) && !memcmp(in.k_cr.data(), cr->m.data(), CRN)));
    in.txl_new = !same;
    if (!same) {
        in.k_ok = true; in.k_on = on; in.k_ys = ys; memcpy(in.k_tp, tp, sizeof tp); in.k_cr.assign(cr->m.begin(), cr->m.begin() + CRN); in.k_linex.assign(g_linex, g_linex + 1024);
        if (!on) std::fill(in.txl.begin(), in.txl.end(), 0u);
        else par_rows(480, [&](int ya, int yb) { for (int y = ya; y < yb; y++) {
            const uint32_t sy = (y + ys) & 0x3ff, trow = sy >> 4, ly = sy & 15, scroll = g_linex[sy];
            uint32_t* tl = &txl[(size_t)y * 640];
            for (int x = 0; x < 640;) {                                    // in character-sized pieces (16 pixels)
                const uint32_t sx = (x + scroll) & 0x3ff, tcol = sx >> 4, px0 = sx & 15;
                const int n = std::min<int>(16 - (int)px0, 640 - x);
                const uint32_t td = h16(cr, 0x1e000 + 2 * ((trow & 63) * 64 + tcol));
                const uint32_t code = td & 0x3ff, fx = (td >> 10) & 1, fy = (td >> 11) & 1, ps = (td >> 12) & 15;
                if (code >= 0x3c0) { for (int k = 0; k < n; k++) tl[x + k] = 0; x += n; continue; }
                const uint8_t* row = &cr->m[code * 128 + (fy ? 15 - ly : ly) * 8]; const uint32_t* pp = &tp[ps << 4];
                for (int k = 0; k < n; k++) {
                    const uint32_t xx = fx ? 15 - (px0 + k) : px0 + k; const uint8_t byte = row[xx >> 1];
                    const uint32_t val = (xx & 1) ? (byte & 15) : (byte >> 4);
                    tl[x + k] = val == 15 ? 0 : pp[val];
                }
                x += n;
            }
        } });
    }
    for (int v = 0; v < 256; v++) { in.gr[v] = h16(c4, 0x200 + 2 * v) & 255; in.gg[v] = h16(c4, 0x400 + 2 * v) & 255; in.gb[v] = h16(c4, 0x600 + 2 * v) & 255; }   // final C404 gamma
}
static void compose_sw(const FrameIn& in, uint8_t* out, int W, int H, int bpp = 3) {   // bpp 3 = RGB, 4 = R,G,B,255
    const double scale = (double)H / 480;
    const int W43 = (int)std::lround(640 * scale), xoff = W > W43 ? (W - W43) / 2 : 0;    // widescreen: side margins
    const int dv = std::max(1, g_3d_div), W3 = W / dv, H3 = H / dv;
    static std::vector<uint32_t> fb3d; if (fb3d.size() != (size_t)W3 * H3) fb3d.assign((size_t)W3 * H3, 0);
    const bool have3d = in.have3d; const uint32_t flags = in.flags; const std::vector<g3d::Entry>& lst = in.lst;
    if (have3d) {
        par_rows(H3, [&](int y0, int y1) { std::fill(fb3d.begin() + (size_t)y0 * W3, fb3d.begin() + (size_t)y1 * W3, 0u); });
        { const uint64_t a = now_ns(); g3d::draw_list(lst, in.pal3d.data(), fb3d.data(), true, W3, H3, scale / dv, xoff / dv); g_t_3d_us += (uint32_t)((now_ns() - a) / 1000); }
        { static bool done = false; static const bool want = getenv("S23_DUMPSTAR") != nullptr;
          if (want && !done && W == 640) { size_t red = 0, yel = 0; for (auto v : fb3d) { red += ((v >> 16) & 255) > 240 && ((v >> 8) & 255) < 20 && (v & 255) < 20; yel += ((v >> 16) & 255) > 200 && ((v >> 8) & 255) > 200 && (v & 255) < 80; } if (red > 30000 && red < 200000 && yel > 1500) { done = true; printf("=== STAR FRAME\n"); g3d::dump = true; std::fill(fb3d.begin(), fb3d.end(), 0u); g3d::draw_list(lst, in.pal3d.data(), fb3d.data(), true); g3d::dump = false; { std::vector<uint8_t> rgb(640 * 480 * 3); for (int q = 0; q < 640 * 480; q++) { rgb[3 * q] = (fb3d[q] >> 16) & 255; rgb[3 * q + 1] = (fb3d[q] >> 8) & 255; rgb[3 * q + 2] = fb3d[q] & 255; } write_png("/tmp/star3d.png", rgb.data(), 640, 480); } } } }
        if (g3d::tl) { uint64_t hl = 1469598103934665603ull, hf = hl; for (auto& e : lst) { const uint8_t* q = (const uint8_t*)&e; for (size_t k = 0; k < sizeof e; k++) hl = (hl ^ q[k]) * 1099511628211ull; } for (auto v : fb3d) hf = (hf ^ v) * 1099511628211ull; size_t nz = 0; for (auto v : fb3d) nz += (v & 0xffffff) != 0; printf("RH vbl=%u flags=%x list=%016llx fb=%016llx px=%zu n=%zu\n", g3d::n_vbl, flags, (unsigned long long)hl, (unsigned long long)hf, nz, lst.size()); }
    }
    const uint64_t comp0 = now_ns();
    static std::vector<int> xmap, x3map; xmap.resize(W); x3map.resize(W);
    for (int X = 0; X < W; X++) { int xr = X - xoff; xmap[X] = (xr < 0 || xr >= W43) ? -1 : std::min(639, (int)((xr + 0.5) * 640 / W43)); x3map[X] = std::min(W3 - 1, X / dv); }
    const uint32_t* txl = in.txl.data(); const uint8_t *gr = in.gr, *gg = in.gg, *gb = in.gb; const uint32_t* bg = in.bg;
    const uint32_t bgc = 0x01000000u | (bg[0] << 16) | (bg[1] << 8) | bg[2];
    const int* xm = xmap.data(); const int* x3 = x3map.data();
    par_rows(H, [&](int ya, int yb) { for (int Y = ya; Y < yb; Y++) {
        const uint32_t* tl = &txl[(size_t)std::min(479, (int)((Y + 0.5) * 480 / H)) * 640];
        const uint32_t* f3 = have3d ? &fb3d[(size_t)std::min(H3 - 1, Y / dv) * W3] : nullptr;
        uint8_t* o = out + (size_t)Y * W * bpp;
        for (int X = 0; X < W; X++, o += bpp) {
            uint32_t c = bgc;
            bool ovr = false;
            if (f3) { const uint32_t q = f3[x3[X]]; const uint32_t hb = q >> 24; if (hb) { c = q; ovr = hb == 0xfe; } }
            if (!ovr) { const int t0 = xm[X]; if (t0 >= 0) { const uint32_t t = tl[t0]; if (t) c = t; } }
            o[0] = gr[(c >> 16) & 255]; o[1] = gg[(c >> 8) & 255]; o[2] = gb[c & 255];
            if (bpp == 4) o[3] = 255;
        }
    } });
    g_t_comp_us += (uint32_t)((now_ns() - comp0) / 1000);
}
static int g_out_bpp = 3;                                            // image format for the screen: 3 = RGB24, 4 = RGBA32 (no conversion in SDL)
static void render_frame(uint8_t* out, int W = 640, int H = 480, int bpp = 3) { static FrameIn in; const uint64_t a = now_ns(); gather(in); g_t_gather_us += (uint32_t)((now_ns() - a) / 1000); compose_sw(in, out, W, H, bpp); }
#ifdef USE_SDL
#include "gl3d.h"
#include "pica_gltest.h"
// prepares for the GPU the same frame as compose_sw (same scale and same margins)
static void build_gl(const FrameIn& in, gl3d::Frame& G, int W, int H) {
    const uint64_t t0 = now_ns();
    const double scale = (double)H / 480; const int W43 = (int)std::lround(640 * scale), xoff = W > W43 ? (W - W43) / 2 : 0;
    G.W = W; G.H = H; G.xoff = xoff; G.W43 = W43; G.have3d = in.have3d;
    G.text = in.txl; for (int k = 0; k < 3; k++) G.bg[k] = in.bg[k];
    for (int v = 0; v < 256; v++) { G.gamma[4 * v] = in.gr[v]; G.gamma[4 * v + 1] = in.gg[v]; G.gamma[4 * v + 2] = in.gb[v]; G.gamma[4 * v + 3] = 255; }
    if (in.have3d) {
        static g3d::Frame F; static std::vector<int> order; F = g3d::Frame(); order.clear();
        g3d::prepare(in.lst, F, order); gl3d::export_polys(F, order, G, scale, xoff);
        G.pal = in.pal3d; G.sten.assign(g3d::C.c412_sram, g3d::C.c412_sram + 0x20000);
    } else { G.vtx.clear(); G.pu.clear(); G.pf.clear(); }
    G.prep_ns = now_ns() - t0;
}
#endif
static const char* g_frame_dir = nullptr; static unsigned g_frame_every = 120, g_vbl_seen = 0;
static void maybe_save_frame() {
    { static unsigned k = 0; if (g_nv_on && !g_win_mode && ++k % 120 == 0) nvram_save(); }   // without a window: saves every ~2 s
    if (!g_frame_dir) return;
    if (++g_vbl_seen % g_frame_every) return;
    { static const unsigned from = getenv("S23_FRAME_FROM") ? (unsigned)atoi(getenv("S23_FRAME_FROM")) : 0, to = getenv("S23_FRAME_TO") ? (unsigned)atoi(getenv("S23_FRAME_TO")) : ~0u; if (g_vbl_seen < from || g_vbl_seen > to) return; }   // only a range (test)
    static const int FW = getenv("S23_FRAMES_WIDE") ? 854 : 640;   // 16:9 test without a window
    static std::vector<uint8_t> buf(FW * 480 * 3); { static const char* da = getenv("S23_DUMPAT"); g3d::dump = da && (int)g_vbl_seen >= atoi(da) && (int)g_vbl_seen <= atoi(da) + 1; } if (g3d::dump) printf("=== FRAME %u\n", g_vbl_seen); { static const char* sd = getenv("S23_SEGDUMPS"); if (sd) { char n[256]; snprintf(n, sizeof n, "%s/seg_%05u.txt", sd, g_vbl_seen); g3d::segdump = fopen(n, "w"); } } { static const bool rt = getenv("S23_RTIME") != nullptr; uint64_t r0 = now_ns(); render_frame(buf.data(), FW, 480); if (rt) { static uint64_t acc = 0, cnt = 0; acc += now_ns() - r0; if (++cnt % 50 == 0) { printf("RTIME %.2f ms\n", acc / 1e6 / 50); acc = 0; } } } if (g3d::segdump) { fclose(g3d::segdump); g3d::segdump = nullptr; } g3d::dump = false;
    if (getenv("S23_LMSTAT")) printf("LMFRAME %u\n", g_vbl_seen);
    char n[512]; snprintf(n, sizeof n, "%s/frame_%05u.png", g_frame_dir, g_vbl_seen); if (write_png(n, buf.data(), FW, 480)) ++g_frames_saved;
    if (getenv("S23_SEGVAR")) {                               // test: sorting variants (frame_N_m<mode><b>.png)
        int sm = g3d::seg_mode; bool nb = g3d::no_bias;
        for (int b = 1; b < 3; b++) { g3d::zalt = b == 2; render_frame(buf.data(), FW, 480); snprintf(n, sizeof n, "%s/frame_%05u_%s.png", g_frame_dir, g_vbl_seen, b == 2 ? "q" : "cur"); write_png(n, buf.data(), FW, 480); }
        g3d::seg_mode = sm; g3d::no_bias = nb; g3d::zalt = false;
    }
    if (getenv("S23_BIASCMP")) {                               // test: same frame with the alternative depth offset
        static const int alt_seg = getenv("S23_SEGCMP") ? atoi(getenv("S23_SEGCMP")) : -1;
        bool o = g3d::neg_bias; int sm = g3d::seg_mode; if (alt_seg >= 0) g3d::seg_mode = alt_seg; else g3d::neg_bias = !o; render_frame(buf.data(), FW, 480); g3d::neg_bias = o; g3d::seg_mode = sm;
        snprintf(n, sizeof n, "%s/frame_%05u_alt.png", g_frame_dir, g_vbl_seen); write_png(n, buf.data(), FW, 480);
    }
    if (getenv("S23_RASTCMP")) {                               // test: same frame with the slow rasteriser (frame_N_slow.png)
        auto pr = g3d::par_run; g3d::par_run = nullptr; g3d::force_slow_rast = true; render_frame(buf.data(), FW, 480); g3d::force_slow_rast = false; g3d::par_run = pr;
        snprintf(n, sizeof n, "%s/frame_%05u_slow.png", g_frame_dir, g_vbl_seen); write_png(n, buf.data(), FW, 480);
    }
}

void guest_eret(CPU*) {}
void backedge_hook(CPU* c) {
    if (g_c361_armed && emu_ns() >= g_c361_at) { g_c361_armed = false; g_ip |= 0x0800; ++g_c361_irqs; update_pending(); }   // IRQ1 -> IP3
    uint32_t st = cp0[12][0]; bool ie = (st & 1) && !(st & 6);
    uint32_t act = g_ip & st & 0xff00;
    if (!ie || !act) { update_pending(); return; }
    int bit = 31 - __builtin_clz(act), ip = bit - 8;
    uint32_t h = mrd32(0x8028D8B0u + 4u * (ip + 1));       // handler table (original dispatcher @0x80000180)
    if (!h) { g_ip &= ~(1u << bit); update_pending(); return; }
    ++g_deliv; ++g_deliv_ip[ip]; if (ip == 2) { if (g_deliv_ip[2] == 600) apply_game_lang(true);   // after startup (the empty NVRAM is initialised by the game)
 g3d::vblank(); maybe_save_frame(); { static const bool hz = getenv("S23_HZLOG") != nullptr; static uint64_t t = 0, c0 = 0; if (hz) { uint64_t n = now_ns(); if (!t) { t = n; c0 = g_deliv_ip[2]; } else if (n - t > 5000000000ull) { printf("HZ %.2f\n", (g_deliv_ip[2] - c0) * 1e9 / (n - t)); t = n; c0 = g_deliv_ip[2]; } } } }
    CPU saved = *c; c->r[29] -= 0x160; cp0[12][0] = st & ~7u; c->irq_pending = false;
    call_guest(c, h);
    *c = saved; cp0[12][0] = st; update_pending();
}

static uint32_t be32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
static bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); out.resize(n);
    size_t r = fread(out.data(), 1, n, f); fclose(f); return r == (size_t)n;
}


// ---------------------------------------------------------------- SDL2 window (optional, -DUSE_SDL)
static CPU g_cpu_obj;
static int g_secs = 20;
static void block_timer_signals(bool block) {
#if !defined(_WIN32) && !defined(S23_CONSOLE)
    sigset_t m; sigemptyset(&m); sigaddset(&m, SIGALRM); sigaddset(&m, SIGVTALRM);
    pthread_sigmask(block ? SIG_BLOCK : SIG_UNBLOCK, &m, nullptr);
#else
    (void)block;
#endif
}
#ifdef _WIN32
// Windows has no SIGALRM: a 1 kHz thread suspends the game thread, runs on_tick and resumes it.
// It is the equivalent of an async-signal handler (on_tick takes no lock and does not allocate).
static HANDLE g_game_h = nullptr;
static DWORD WINAPI win_ticker(LPVOID) {
    timeBeginPeriod(1); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (;;) {
        Sleep(1);
        if (SuspendThread(g_game_h) == (DWORD)-1) continue;
        CONTEXT c; c.ContextFlags = CONTEXT_CONTROL; GetThreadContext(g_game_h, &c);       // waits for the actual suspension
        on_tick(0);
        ResumeThread(g_game_h);
    }
    return 0;
}
static void start_win_ticker() {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game_h, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0);
    CreateThread(nullptr, 0, win_ticker, nullptr, 0, nullptr);
}
#endif
static void* game_thread(void*) {
#ifdef _WIN32
    start_win_ticker();
#endif
    block_timer_signals(false);                                   // timer signals only reach the game thread
    f_80000320(&g_cpu_obj);
    summary("f_80000320 returned (the program has finished)");
    return nullptr;
}
#ifdef USE_SDL
static InputState g_is;
static SDL_Window* g_win = nullptr; static SDL_Renderer* g_ren = nullptr;
static bool g_gpu = false;                                             // 3D and compositing on the GPU (platform/gl3d.h)
static std::atomic<uint32_t> g_rt_us{0}; static std::atomic<unsigned> g_rt_n{0}; static unsigned g_pres_n = 0;   // g_rt_us: microseconds (32-bit: Wii U has no 64-bit atomics)
// ---------------------------------------------------------------- render thread
// The frame is prepared on a separate thread: the window presents at the monitor rate (VSync) without waiting
// the drawing, and the drawing doesn't wait for VSync. Buffer swap protected by a spinlock (an instant operation).
struct RenderShared {
    std::atomic_flag lk = ATOMIC_FLAG_INIT; std::vector<uint8_t> pend; int pw = 0, ph = 0; bool fresh = false;
    gl3d::Frame gpend; std::atomic<bool> gpu{false}; bool cmp_sw = false; std::vector<g3d::Entry> dbg_lst; std::vector<uint32_t> dbg_pal;                // GPU: frame data (and, when comparing, the software image too)
    std::atomic<int> want_w{640}, want_h{480}; std::atomic<bool> stop{false}, done{false}; std::atomic<int> req_seg{0}; std::atomic<bool> always{false};
    void lock() { while (lk.test_and_set(std::memory_order_acquire)) {} } void unlock() { lk.clear(std::memory_order_release); }
};
static RenderShared g_rs;
static void* render_thread(void*) {
    std::vector<uint8_t> buf; uint64_t last_vb = ~0ull, last_t = 0; int lw = 0, lh = 0; unsigned segn = 0;
    while (!g_rs.stop.load()) {
#ifdef S23_CONSOLE
        if (g_exit_req) break;
#endif
        const uint64_t vb = g_deliv_ip[2], tn = now_ns(); const int w = g_rs.want_w.load(), h = g_rs.want_h.load();
        const int seg = g_rs.req_seg.exchange(0);
        if (seg) {                                                                           // F10: 3D list structure + 640x480 image
            char n[64]; snprintf(n, sizeof n, "segdump_%03u.txt", ++segn); std::vector<uint8_t> b(640 * 480 * 3);
            g3d::segdump = fopen(n, "w"); render_frame(b.data()); if (g3d::segdump) { fclose(g3d::segdump); g3d::segdump = nullptr; printf("saved %s\n", n); }
            snprintf(n, sizeof n, "segdump_%03u.png", segn); write_png(n, b.data(), 640, 480); lw = 0;
        }
        if (vb == last_vb && tn - last_t < 50000000ull && w == lw && h == lh && !g_rs.always.load()) { sleep_ns(500000); continue; }
        last_vb = vb; last_t = tn; lw = w; lh = h;
        const int bpp = g_rs.gpu.load() ? 3 : g_out_bpp; buf.resize((size_t)w * h * bpp);
        const uint64_t r0 = now_ns();
        if (g_rs.gpu.load()) {                                                               // GPU: only the geometry is prepared
            static FrameIn in; static gl3d::Frame gb; static const bool cmp = getenv("S23_GLCMP") != nullptr;
            gather(in); build_gl(in, gb, w, h); if (cmp) compose_sw(in, buf.data(), w, h);
            g_rt_us += (uint32_t)((now_ns() - r0) / 1000); g_rt_n++;
            g_rs.lock(); std::swap(gb, g_rs.gpend); if (cmp) { std::swap(buf, g_rs.pend); g_rs.dbg_lst = in.lst; g_rs.dbg_pal = in.pal3d; } g_rs.cmp_sw = cmp; g_rs.pw = w; g_rs.ph = h; g_rs.fresh = true; g_rs.unlock();
            continue;
        }
        render_frame(buf.data(), w, h, bpp); g_rt_us += (uint32_t)((now_ns() - r0) / 1000); g_rt_n++;
        g_rs.lock(); std::swap(buf, g_rs.pend); g_rs.pw = w; g_rs.ph = h; g_rs.fresh = true; g_rs.unlock();
    }
    g_rs.done = true; return nullptr;
}
static void run_window() {
    SDL_Window* win = g_win; SDL_Renderer* ren = g_ren; InputState& is = g_is;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    int TW = 640, TH = 480;                                                                  // frame resolution (4:3)
    {   // format of the image drawn by the CPU: if the renderer accepts 32-bit RGBA that is used, so SDL doesn't
        // converts every frame (RGB24 is not a native format on almost any GPU: on Wii U it cost ~20 ms)
        SDL_RendererInfo ri0; g_out_bpp = 3;
        if (SDL_GetRendererInfo(ren, &ri0) == 0) { bool rgb24 = false, rgba32 = false;
            for (Uint32 k = 0; k < ri0.num_texture_formats; k++) { rgb24 |= ri0.texture_formats[k] == SDL_PIXELFORMAT_RGB24; rgba32 |= ri0.texture_formats[k] == SDL_PIXELFORMAT_RGBA32; }
            if (rgba32 && !rgb24 && !getenv("S23_RGB24")) g_out_bpp = 4;
            printf("image for the screen: %s (renderer %s)\n", g_out_bpp == 4 ? "RGBA 32 bit" : "RGB 24 bit", ri0.name); fflush(stdout); }
    }
    const Uint32 CPU_FMT = g_out_bpp == 4 ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24;
    SDL_Texture* tex = SDL_CreateTexture(ren, CPU_FMT, SDL_TEXTUREACCESS_STREAMING, TW, TH);
    std::vector<uint8_t> buf(640 * 480 * 4);
    const char* shot = getenv("S23_WINDOW_SHOT"); const char* ws = getenv("S23_WINDOW_SECS");
    double auto_secs = ws ? atof(ws) : 0;
    uint64_t t0 = now_ns(), tfps = t0, last_vbl = 0; unsigned shots = 0; bool full = false;
    printf("SDL window open (video driver: %s, renderer: ", SDL_GetCurrentVideoDriver());
    SDL_RendererInfo ri; if (SDL_GetRendererInfo(ren, &ri) == 0) printf("%s", ri.name); printf(")\n"); fflush(stdout);
    bool quit = false; full = is.cfg.fullscreen;
#ifdef S23_CONSOLE
    bool osd_on = !getenv("S23_NOOSD");                                                  // consoles: indicator on (turned off by clicking the stick)
#else
    bool osd_on = getenv("S23_OSD") != nullptr;
#endif
    std::string osd;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_RenderSetVSync(ren, FPS_LIST[is.cfg.fps % FPS_N] == 0 ? 1 : 0);              // VSync only if chosen
    SDL_GetRendererInfo(ren, &ri);
#endif
    g_rs.always = shot != nullptr;
    // 3D on GPU: the OpenGL context is the one of SDL's renderer (already current on this thread)
    static gl3d::Renderer glr; gl3d::Frame glf; gl3d::GLuint texid = 0; const bool glcmp = getenv("S23_GLCMP") != nullptr;
    std::vector<g3d::Entry> dbg_l; std::vector<uint32_t> dbg_p; uint64_t cmp_n = 0, cmp_bad = 0, cmp_px = 0; int cmp_max = 0; unsigned cmp_saved = 0;
    if (g_gpu) {
        const bool glr_ok = strstr(ri.name, "opengl") != nullptr && glr.init();
        if (!glr_ok) { printf("GPU not available (%s): drawing on the CPU\n", strstr(ri.name, "opengl") ? glr.err.c_str() : ri.name); g_gpu = false; }
        else { printf("3D su GPU (OpenGL%s)\n", glr.es ? " ES" : ""); SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, TW, TH); }
        fflush(stdout);
    }
    auto gl_texid = [&](SDL_Texture* t) -> gl3d::GLuint { float tw = 0, th = 0; if (SDL_GL_BindTexture(t, &tw, &th) != 0) return 0; gl3d::GLint id = 0; gl3d::glGetIntegerv(gl3d::GL_TEXTURE_BINDING_2D, &id); SDL_GL_UnbindTexture(t); return (gl3d::GLuint)id; };
    g_rs.gpu = g_gpu;
    bool want_menu = false;
    // in-game settings menu (Esc, Select/- + Start/+, phone Back button, on-screen MENU button):
    // the game pauses (emulation time stopped, audio suspended) and resumes where it was
    auto open_menu = [&]() {
        emu_pause(true); g_in.p1 = 0; g_in.coin1 = 0;
        if (uint32_t d = g_adev.load()) SDL_PauseAudioDevice(d, 1);
        const int gpu_before = is.cfg.gpu;
        const bool go = launcher(win, ren, is, true);
        full = is.cfg.fullscreen;
        SDL_RenderSetLogicalSize(ren, is.cfg.wide ? 854 : 640, 480);
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_RenderSetVSync(ren, FPS_LIST[is.cfg.fps % FPS_N] == 0 ? 1 : 0);
#endif
        if (is.cfg.gpu != gpu_before) {
            if (!is.cfg.gpu && g_gpu) { g_gpu = false; g_rs.gpu = false; SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, CPU_FMT, SDL_TEXTUREACCESS_STREAMING, TW, TH); texid = 0; printf("3D: drawing on the CPU\n"); }
            else if (is.cfg.gpu && !g_gpu) printf("3D on GPU: active from the next launch\n");
            fflush(stdout);
        }
        g_game_lang = is.cfg.game_lang; apply_game_lang(true);
        if (uint32_t d = g_adev.load()) SDL_PauseAudioDevice(d, 0);
        emu_pause(false);
        if (!go) quit = true;
    };
#if defined(__SWITCH__)
    start_thread(render_thread, 8u << 20, 0, 0x2C, "Drawing");                           // core 0, like the window
#elif defined(__WIIU__)
    // core 1 like the window but at LOWER priority (higher number): the main thread, when it has to present,
    // goes through immediately; the drawing works while it waits for VSync. Measurements on the console: on core 1 at equal priority
    // the screen ran at 25 Hz (they blocked each other); on core 0 the drawing was 25 times slower.
    printf("Wii U: main thread priority %d\n", OSGetThreadPriority(OSGetCurrentThread())); fflush(stdout);
    start_thread(render_thread, 8u << 20, 1, 20, "Drawing");
#else
    start_thread(render_thread, 8u << 20, 0, 0, "Drawing");
#endif
    uint64_t pres0 = 0; std::string osd2;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) is.pads.rescan();
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) is.touch(e, now_ns() / 1000000);   // finger: steering, tap = Start, corner = coin
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {                                       // Select/- together with Start/+ = exit (consoles without a keyboard)
                SDL_GameController* gc = SDL_GameControllerFromInstanceID(e.cbutton.which);
                if (gc && SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK) && SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) want_menu = true;   // Select/- + Start/+ = settings
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK) osd_on = !osd_on;
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: case SDLK_AC_BACK: case SDLK_F4: want_menu = true; break;   // settings (from there "Exit")
                    case SDLK_F11: full = !full; SDL_SetWindowFullscreen(win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); break;
                    case SDLK_F1: osd_on = !osd_on; break;
                    case SDLK_F9: g3d::seg_mode = (g3d::seg_mode + 1) % 3; printf("segment sorting: mode %d\n", g3d::seg_mode); fflush(stdout); break;
                    case SDLK_F10: g_rs.req_seg = 1; break;
                    case SDLK_F12: { char n[64]; snprintf(n, sizeof n, "screenshot_%03u.png", ++shots);          // the last frame shown
                                     if (g_gpu) { std::vector<uint8_t> g; glr.read_out(TW, TH, g); if (write_png(n, g.data(), TW, TH)) printf("saved %s\n", n); break; }
                                     if (buf.size() == (size_t)TW * TH * g_out_bpp) { std::vector<uint8_t> rgb((size_t)TW * TH * 3); for (size_t q = 0; q < (size_t)TW * TH; q++) memcpy(&rgb[q * 3], &buf[q * g_out_bpp], 3); if (write_png(n, rgb.data(), TW, TH)) printf("saved %s\n", n); } break; }
                }
            }
        }
        if (is.update(g_in, now_ns() / 1000000)) {                                          // F3: config DIP (bit0, 0 = on)
            g_dsw = is.service_dip ? (g_dsw & ~1u) : (g_dsw | 1u); printf("Config DIP: %s\n", is.service_dip ? "ON" : "off"); fflush(stdout);
        }
        {
          { int ow = 640, oh = 480; SDL_GetRendererOutputSize(ren, &ow, &oh);                 // 3D at screen resolution, always 4:3
            const int LW = is.cfg.wide ? 854 : 640;                                             // experimental 16:9
            double sc = is.cfg.res3d == 1 ? std::min(ow / (double)LW, oh / 480.0) : 1.0; sc = std::max(1.0, std::min(sc, 4.0));
            g_3d_div = is.cfg.res3d == 2 ? 2 : 1;
            int nh = (int)std::lround(480 * sc), nw = is.cfg.wide ? (int)std::lround(nh * 16.0 / 9.0) : (int)std::lround(640 * sc);
            { static int lw = 0; if (lw != LW) { lw = LW; SDL_RenderSetLogicalSize(ren, LW, 480); } }
            if (nw != g_rs.want_w.load() || nh != g_rs.want_h.load()) { g_rs.want_w = nw; g_rs.want_h = nh; printf("3D resolution: %dx%d\n", nw, nh); fflush(stdout); } }
          pres0 = now_ns();
          bool got = false, gotsw = false; g_rs.lock();
          if (g_rs.fresh) {
              if (g_gpu) { std::swap(glf, g_rs.gpend); if (g_rs.cmp_sw) { std::swap(buf, g_rs.pend); gotsw = true; dbg_l.swap(g_rs.dbg_lst); dbg_p.swap(g_rs.dbg_pal); } } else std::swap(buf, g_rs.pend);
              got = true; g_rs.fresh = false;
              if (g_rs.pw != TW || g_rs.ph != TH) { TW = g_rs.pw; TH = g_rs.ph; SDL_DestroyTexture(tex); texid = 0;
                  tex = g_gpu ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, TW, TH) : SDL_CreateTexture(ren, CPU_FMT, SDL_TEXTUREACCESS_STREAMING, TW, TH); }
          } g_rs.unlock();
          if (got && g_gpu && glf.W == TW && glf.H == TH) {
              SDL_RenderFlush(ren); if (!texid) texid = gl_texid(tex);
              if (!texid || !glr.draw(glf, texid)) {                                           // the GPU doesn't work: back to the CPU
                  printf("GPU: %s: drawing on the CPU\n", texid ? glr.err.c_str() : "SDL texture not accessible"); fflush(stdout);
                  g_gpu = false; g_rs.gpu = false; SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, CPU_FMT, SDL_TEXTUREACCESS_STREAMING, TW, TH); texid = 0;
              } else if (glcmp && gotsw) {                                                     // comparison with the software drawing of the same frame
                  std::vector<uint8_t> g; glr.read_out(TW, TH, g); size_t bad = 0; int mx = 0;
                  for (size_t i = 0; i < g.size() && i < buf.size(); i += 3) { int d = std::max({std::abs(g[i] - buf[i]), std::abs(g[i + 1] - buf[i + 1]), std::abs(g[i + 2] - buf[i + 2])}); if (d > 2) bad++; mx = std::max(mx, d); }
                  cmp_n++; cmp_bad += bad; cmp_px += (size_t)TW * TH; cmp_max = std::max(cmp_max, mx);
                  { static const int pt = getenv("S23_PICATEST") ? atoi(getenv("S23_PICATEST")) : 0;     // 3DS renderer test (pica3d.h)
                    if (pt > 0 && cmp_n % pt == 0 && !dbg_l.empty()) {
                        static picatest::GLTest gt; static pica::Renderer pr; static bool pinit = false; if (!pinit) { pinit = true; if (gt.init(192, getenv("S23_PICAATLAS") ? 64 : 1)) { pr.init(&gt, 192); if (getenv("S23_PICALOD")) pr.lod_bias = (float)atof(getenv("S23_PICALOD")); }; }
                        if (gt.ok) {
                            g3d::Frame F; std::vector<int> order; g3d::prepare(dbg_l, F, order);
                            const double sc = TH / 480.0; const int W43 = (int)std::lround(640 * sc), xo = TW > W43 ? (TW - W43) / 2 : 0;
                            pr.build(F, order, dbg_p.data(), (float)sc, (float)xo, 0.f, (float)TW, (float)TH);
                            std::vector<uint8_t> pg; gt.draw(pr, TW, TH, pg);
                            std::vector<uint32_t> fb((size_t)TW * TH, 0u); auto prun = g3d::par_run; g3d::par_run = nullptr; g3d::draw_list(dbg_l, dbg_p.data(), fb.data(), true, TW, TH, sc, xo); g3d::par_run = prun;
                            std::vector<uint8_t> sw((size_t)TW * TH * 3); size_t pbad = 0, pnz = 0; int pmx = 0;
                            for (size_t q = 0; q < (size_t)TW * TH; q++) { sw[3 * q] = (fb[q] >> 16) & 255; sw[3 * q + 1] = (fb[q] >> 8) & 255; sw[3 * q + 2] = fb[q] & 255;
                                int d = std::max({std::abs(sw[3 * q] - pg[3 * q]), std::abs(sw[3 * q + 1] - pg[3 * q + 1]), std::abs(sw[3 * q + 2] - pg[3 * q + 2])}); if (d > 8) pbad++; pmx = std::max(pmx, d); if (fb[q]) pnz++; }
                            static unsigned pn = 0; char nm[64]; ++pn;
                            printf("PICATEST #%u: polygons %u triangles %u batches %u cells %u new %u dropped %u | differing pixels (>8) %.3f%% (3D %.1f%% of the screen) max %d\n", pn, pr.st.polys, pr.st.tris, pr.st.batches, pr.st.cells, pr.st.new_slots, pr.st.dropped, 100.0 * pbad / ((double)TW * TH), 100.0 * pnz / ((double)TW * TH), pmx); fflush(stdout);
                            if (pn <= 12) { snprintf(nm, sizeof nm, "pica_%02u_gpu.png", pn); write_png(nm, pg.data(), TW, TH); snprintf(nm, sizeof nm, "pica_%02u_sw.png", pn); write_png(nm, sw.data(), TW, TH); }
                        } } }
                  static const int every = atoi(getenv("S23_GLCMP")) > 1 ? atoi(getenv("S23_GLCMP")) : 300;
                  static const bool savebad = !getenv("S23_GLCMP_SAMPLES"); if ((savebad && bad * 1000 > (size_t)TW * TH) || cmp_n % every == 0) {                    // differing frame (>0.1%) or sample: save both images
                      if (cmp_saved < 60) { char n[96]; cmp_saved++; snprintf(n, sizeof n, "glcmp_%04u_gpu.png", cmp_saved); write_png(n, g.data(), TW, TH); snprintf(n, sizeof n, "glcmp_%04u_cpu.png", cmp_saved); write_png(n, buf.data(), TW, TH);
                          printf("GLCMP #%u frame %llu: differing pixels %zu (%.3f%%), max difference %d\n", cmp_saved, (unsigned long long)cmp_n, bad, 100.0 * bad / ((double)TW * TH), mx); fflush(stdout);
                          if (const char* pr = getenv("S23_GLPROBE")) { int px = 0, py = 0; sscanf(pr, "%d,%d", &px, &py);      // which polygon covers the pixel (GPU)
                              glr.dbg = 1; glr.draw(glf, texid); uint32_t pid = glr.read3d(px, py); glr.dbg = 2; glr.draw(glf, texid); uint32_t d2 = glr.read3d(px, py); glr.dbg = 3; glr.draw(glf, texid); uint32_t d3 = glr.read3d(px, py); glr.dbg = 0; glr.draw(glf, texid);
                              printf("GLPX tx&255=%u ty&255=%u sh=%u pen=%u rgb_b=%02x rgb_g=%02x\n", d2 & 255, (d2 >> 8) & 255, (d2 >> 16) & 255, d3 & 255, (d3 >> 8) & 255, (d3 >> 16) & 255);
                              { const std::vector<g3d::Entry>& l = dbg_l; const std::vector<uint32_t>& pl = dbg_p; const double sc = TH / 480.0; const int w43 = (int)std::lround(640 * sc); uint32_t col = 0;
                                if (!pl.empty()) { int ow = g3d::probe_owner(l, pl.data(), TW, TH, sc, TW > w43 ? (TW - w43) / 2 : 0, px, py, &col); printf("GLPROBE cpu: polygon index %d colour %06x\n", ow, col & 0xffffff); } }
                              for (size_t q = 0; q < glf.vtx.size(); q++) if (glf.vtx[q].pid == pid) printf("GLV %zu x=%.4f y=%.4f w=%.6f u=%.4f v=%.4f i=%.4f\n", q, glf.vtx[q].x, glf.vtx[q].y, glf.vtx[q].w, glf.vtx[q].u, glf.vtx[q].v, glf.vtx[q].i);
                              if (pid * 16 + 15 < glf.pu.size()) { printf("GLPU"); for (int k = 0; k < 16; k++) printf(" %08x", glf.pu[pid * 16 + k]); printf(" | PF"); for (int k = 0; k < 8; k++) printf(" %g", glf.pf[pid * 8 + k]); printf("\n"); }
                              printf("GLPROBE %d,%d: gpu polygon no.%u (index %u) gpu colour %02x%02x%02x cpu %02x%02x%02x\n", px, py, pid, pid * 16 + 7 < glf.pu.size() ? glf.pu[pid * 16 + 7] : 0, g[(py * TW + px) * 3], g[(py * TW + px) * 3 + 1], g[(py * TW + px) * 3 + 2], buf[(py * TW + px) * 3], buf[(py * TW + px) * 3 + 1], buf[(py * TW + px) * 3 + 2]); fflush(stdout); } }
                  }
              }
          }
          else if (got && !g_gpu && buf.size() == (size_t)TW * TH * g_out_bpp) SDL_UpdateTexture(tex, nullptr, buf.data(), TW * g_out_bpp); }
        SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, nullptr, nullptr);
        if (is.t_menu_req) { is.t_menu_req = false; want_menu = true; }
        { static const double mt = getenv("S23_MENU_AT") ? atof(getenv("S23_MENU_AT")) : -1; static bool done = false;   // test: opens the menu at t seconds
          if (mt >= 0 && !done && (now_ns() - t0) / 1e9 >= mt) { done = true; want_menu = true; printf("MENUTEST opens vbl=%llu\n", (unsigned long long)g_deliv_ip[2]); fflush(stdout); } }
        if (want_menu && !quit) { want_menu = false; const uint64_t vb0 = g_deliv_ip[2]; open_menu(); { static const bool mt = getenv("S23_MENU_AT") != nullptr; if (mt) { printf("MENUTEST closed: vblanks during the menu %llu\n", (unsigned long long)(g_deliv_ip[2] - vb0)); fflush(stdout); } } continue; }
        if ((SDL_GetNumTouchDevices() > 0 || getenv("S23_TOUCHUI")) && is.cfg.touchui) {                       // touch: coin zone at the top left
            static Text ttx; static bool tinit = false; if (!tinit) { ttx.init(ren); tinit = true; }
            int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh); float lx0, ly0, lx1, ly1;
            SDL_RenderWindowToLogical(ren, 0, 0, &lx0, &ly0); SDL_RenderWindowToLogical(ren, (int)(ww * 0.16f), (int)(wh * 0.22f), &lx1, &ly1);
            lx0 = std::max(lx0, 0.f); ly0 = std::max(ly0, 0.f);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(ren, 255, 210, 70, is.t_coin_until > now_ns() / 1000000 ? 140 : 45);
            SDL_Rect cz{(int)lx0 + 4, (int)ly0 + (osd_on ? 22 : 4), (int)(lx1 - lx0) - 8, (int)(ly1 - ly0) - (osd_on ? 26 : 8)}; SDL_RenderFillRect(ren, &cz);
            std::string ct = i18n::tr("touch_coin"); ttx.draw(cz.x + std::max(2, (cz.w - (int)ct.size() * 8) / 2), cz.y + cz.h / 2 - 8, ct, SDL_Color{255, 240, 200, 255});
            float rx0, ry0, rx1, ry1; SDL_RenderWindowToLogical(ren, (int)(ww * 0.84f), 0, &rx0, &ry0); SDL_RenderWindowToLogical(ren, ww, (int)(wh * 0.22f), &rx1, &ry1);
            float lw = 0, lh = 0; { int a = 0, b = 0; SDL_RenderGetLogicalSize(ren, &a, &b); lw = (float)a; } rx1 = std::min(rx1, lw > 0 ? lw : rx1);
            SDL_SetRenderDrawColor(ren, 120, 200, 255, is.t_svc_until > now_ns() / 1000000 ? 140 : 35);
            SDL_Rect sz{(int)rx0 + 4, (int)ry0 + 4, (int)(rx1 - rx0) - 8, (int)(ry1 - ry0) - 8}; SDL_RenderFillRect(ren, &sz);
            if (is.cfg.touch && is.t_used) {                                                // trackpad: lever position at the bottom
                int bl = 0, bh = 0; SDL_RenderGetLogicalSize(ren, &bl, &bh); if (bl <= 0) { bl = 640; bh = 480; }
                SDL_Rect bar{bl / 2 - 120, bh - 10, 240, 4}; SDL_SetRenderDrawColor(ren, 255, 255, 255, 50); SDL_RenderFillRect(ren, &bar);
                SDL_Rect mk{bl / 2 + (int)(is.t_pos * 118) - 4, bh - 13, 8, 10}; SDL_SetRenderDrawColor(ren, 255, 210, 70, is.t_on ? 220 : 120); SDL_RenderFillRect(ren, &mk);
            }
            if (is.menu_mode()) {                                                            // service menu: UP / DOWN / ENTER buttons
                int n; const InputState::TBtn* B = InputState::menu_btns(n); const uint32_t held = is.menu_held();
                for (int k = 0; k < n; k++) {
                    float bx0, by0, bx1, by1; SDL_RenderWindowToLogical(ren, (int)(ww * B[k].x0), (int)(wh * B[k].y0), &bx0, &by0); SDL_RenderWindowToLogical(ren, (int)(ww * B[k].x1), (int)(wh * B[k].y1), &bx1, &by1);
                    SDL_Rect r{(int)bx0 + 3, (int)by0 + 3, (int)(bx1 - bx0) - 6, (int)(by1 - by0) - 6};
                    SDL_SetRenderDrawColor(ren, 90, 220, 140, (held & B[k].bit) ? 190 : 80); SDL_RenderFillRect(ren, &r);
                    SDL_SetRenderDrawColor(ren, 200, 255, 220, 200); SDL_RenderDrawRect(ren, &r);
                    std::string lb = i18n::tr(B[k].key); ttx.draw(r.x + std::max(2, (r.w - (int)lb.size() * 8) / 2), r.y + r.h / 2 - 8, lb, SDL_Color{255, 255, 255, 255});
                }
            }
            std::string st = i18n::tr("touch_svc"); ttx.draw(sz.x + std::max(2, (sz.w - (int)st.size() * 8) / 2), sz.y + sz.h / 2 - 8, st, SDL_Color{220, 240, 255, 255});
            {   // MENU (settings) at the top centre
                float mx0, my0, mx1, my1; SDL_RenderWindowToLogical(ren, (int)(ww * 0.42f), 0, &mx0, &my0); SDL_RenderWindowToLogical(ren, (int)(ww * 0.58f), (int)(wh * 0.10f), &mx1, &my1);
                SDL_Rect mz{(int)mx0 + 4, std::max(0, (int)my0) + (osd_on ? 22 : 4), (int)(mx1 - mx0) - 8, std::max(18, (int)(my1 - std::max(0.f, my0)) - 8)};
                SDL_SetRenderDrawColor(ren, 200, 200, 210, 45); SDL_RenderFillRect(ren, &mz);
                std::string mt = i18n::tr("touch_menu"); ttx.draw(mz.x + std::max(2, (mz.w - (int)mt.size() * 8) / 2), mz.y + mz.h / 2 - 8, mt, SDL_Color{240, 240, 245, 255});
            }
        }
        if (osd_on && !osd.empty()) {                                                      // performance indicator (F1 / left stick click)
            static Text otx; static bool oinit = false; if (!oinit) { otx.init(ren); oinit = true; }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
            SDL_Rect bg{2, 2, (int)std::max(osd.size(), osd2.size()) * 8 + 6, osd2.empty() ? 18 : 36}; SDL_RenderFillRect(ren, &bg);
            otx.draw(5, 3, osd, SDL_Color{120, 255, 120, 255});
            if (!osd2.empty()) otx.draw(5, 20, osd2, SDL_Color{255, 220, 120, 255});
        }
        uint64_t n = now_ns();
        if (shot && auto_secs > 0 && (n - t0) / 1e9 >= auto_secs) {                       // non-interactive test: saves what is in the window
            int ow = 0, oh = 0; SDL_GetRendererOutputSize(ren, &ow, &oh);                       // the read covers the whole output (window), not just 640x480
            std::vector<uint8_t> px((size_t)ow * oh * 3);
            if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGB24, px.data(), ow * 3) == 0) { write_png(shot, px.data(), ow, oh); printf("window screenshot (%dx%d): %s\n", ow, oh, shot); }
            else printf("SDL_RenderReadPixels: %s\n", SDL_GetError());
            break;
        }
        SDL_RenderPresent(ren); g_pres_n++; g_t_pres_us += (uint32_t)((now_ns() - pres0) / 1000);
        { static uint64_t nv_t = 0; if (n - nv_t > 2000000000ull) { nv_t = n; nvram_save(); } }    // backup RAM to file every 2 s if changed
        if (n - tfps >= 1000000000ull) {                                                   // title with game frames per second
            uint64_t v = g_deliv_ip[2]; const unsigned g_pres_n_last = g_pres_n; const unsigned rt_cnt = g_rt_n; const double rt_last = rt_cnt ? g_rt_us / 1e3 / rt_cnt : 0.0; char t[160]; snprintf(t, sizeof t, "Panic Park - recompiled | game %.1f Hz | images %.0f/s (drawing %.1f ms) | screen %.0f Hz | F9 3D order: %d", (v - last_vbl) * 1e9 / (n - tfps), rt_cnt * 1e9 / (n - tfps), rt_last, g_pres_n * 1e9 / (n - tfps), g3d::seg_mode); g_rt_us = 0; g_rt_n = 0; g_pres_n = 0;
            SDL_SetWindowTitle(win, t); { static const bool rl = getenv("S23_RTLOG") != nullptr; if (rl) { printf("RTLOG %s\n", t); fflush(stdout); } }
            { char o[160]; snprintf(o, sizeof o, "game %.1f Hz  img %.0f/s  screen %.0f Hz  3D %.1fms (%s)  H8 %.0f%%", (v - last_vbl) * 1e9 / (n - tfps), rt_cnt * 1e9 / (n - tfps), g_pres_n_last * 1e9 / (n - tfps), rt_last, g_gpu ? "GPU" : "CPU", (double)g_h8_pct); osd = o; }
            {   // second line: how much each part works (to understand where time is lost on consoles)
                const double el_us = (n - tfps) / 1e3, rn = rt_cnt ? rt_cnt : 1, pn = g_pres_n_last ? g_pres_n_last : 1;
                const double mips = 100.0 * (1.0 - std::min(1.0, g_mips_idle_us.exchange(0) / el_us)), h8b = 100.0 * (1.0 - std::min(1.0, g_h8_idle_us.exchange(0) / el_us));
                char o[200]; snprintf(o, sizeof o, "R4650 %.0f%% H8 %.0f%% ms: txt %.1f 3D %.1f geo %.1f (%u) merge %.1f scr %.1f", mips, h8b,
                    g_t_gather_us.exchange(0) / 1e3 / rn, g_t_3d_us.exchange(0) / 1e3 / rn, g3d::t_prep_us.exchange(0) / 1e3 / rn, (unsigned)g3d::n_polys.load(), g_t_comp_us.exchange(0) / 1e3 / rn, g_t_pres_us.exchange(0) / 1e3 / pn);
                osd2 = o; static const bool rl = getenv("S23_RTLOG") != nullptr;
#ifdef S23_CONSOLE
                static unsigned logn = 0; if (++logn % 5 == 0)                                         // consoles: also in log.txt every 5 s
#else
                if (rl)
#endif
                { printf("PERF %s | %s\n", osd.c_str(), o); fflush(stdout); } (void)rl;
            }
            last_vbl = v; tfps = n;
        }
        if (auto_secs > 0 && !shot && (n - t0) / 1e9 >= auto_secs) break;
        {   // screen rate: independent of the game, which stays at 59.9 Hz (real time)
            static uint64_t next = 0; double f = FPS_LIST[is.cfg.fps % FPS_N];
            if (f == 0 && !(ri.flags & SDL_RENDERER_PRESENTVSYNC)) f = 60;            // VSync not available: 60 Hz
            if (f > 0) {
                uint64_t period = (uint64_t)(1e9 / f), t = now_ns();
                if (!next || t > next + period) next = t;
                next += period;
                while (now_ns() + 1500000 < next) SDL_Delay(1);
                while (now_ns() < next) {}
            }
        }
    }
    printf("exit: requested\n"); fflush(stdout);
    g_rs.stop = true; { uint64_t tw = now_ns(); while (!g_rs.done.load() && now_ns() - tw < 2000000000ull) sleep_ns(1000000); }
    printf("exit: render thread %s\n", g_rs.done.load() ? "finished" : "NOT finished"); fflush(stdout);
    if (glcmp && cmp_n) printf("GLCMP total: %llu frames, differing pixels %.4f%%, max difference %d\n", (unsigned long long)cmp_n, 100.0 * cmp_bad / (double)cmp_px, cmp_max);
#ifdef S23_CONSOLE
    console_stop_threads();
#endif
    if (uint32_t d = g_adev.exchange(0)) { SDL_PauseAudioDevice(d, 1); SDL_CloseAudioDevice(d); }   // audio closed before SDL_Quit
    nvram_save();
    is.cfg.fullscreen = full; is.cfg.save();
    printf("exit: saves done, closing SDL\n"); fflush(stdout);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    printf("exit: done\n"); fflush(stdout);
#ifdef S23_CONSOLE
    fflush(stdout); return;
#endif
    summary("window closed");
}
#endif

#ifdef __3DS__
#include "platform_3ds.h"
#endif
int main(int argc, char** argv) {
    // without arguments (double click on the executable): window enabled, files found by themselves
    static char* defargs[] = { argv[0], (char*)"panicprk_main.elf", (char*)"main_r4650_BE_4MB.bin", (char*)"1000000000", (char*)"0", (char*)".", nullptr };
    if (argc == 1) {
        argc = 6; argv = defargs; if (!getenv("S23_WINDOW")) { static char w[] = "S23_WINDOW=1"; putenv(w); }
#ifndef S23_CONSOLE
        // PC: work in the executable's folder (assets/, nvram.bin, panicprk.ini, lang/ live there). The ELF and the program
        // image are next to it or, when started from dist/<system>/ of the project, two folders up; the ROMs are in
        // panicprkrom/ beside them (or in the same folder). This replaces the old start.bat.
        {
            char exe[4096] = {0};
#ifdef _WIN32
            GetModuleFileNameA(nullptr, exe, sizeof exe - 1);
            SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);   // like "start /high" in the old start.bat
#else
            if (readlink("/proc/self/exe", exe, sizeof exe - 1) < 0) exe[0] = 0;
#endif
            std::string d = exe; size_t sl = d.find_last_of("/\\");
            if (sl != std::string::npos && chdir(d.substr(0, sl).c_str()) == 0) {}
            auto exists = [](const std::string& f) { FILE* t = fopen(f.c_str(), "rb"); if (t) fclose(t); return t != nullptr; };
            static std::string base = ".", elf, bin, rom;
            if (!exists("panicprk_main.elf") && exists("../../panicprk_main.elf")) base = "../..";
            elf = base + "/panicprk_main.elf"; bin = base + "/main_r4650_BE_4MB.bin";
            rom = exists(base + "/panicprkrom/pnp2vera.ic1") || exists(base + "/panicprkrom/pnp1vera.ic3") ? base + "/panicprkrom" : base;
            defargs[1] = (char*)elf.c_str(); defargs[2] = (char*)bin.c_str(); defargs[5] = (char*)rom.c_str();
            printf("files: %s, %s, ROMs in %s\n", defargs[1], defargs[2], defargs[5]);
        }
#endif
    }
#ifdef S23_CONSOLE
    {   // everything lives in the app folder on the SD card (Wii U: wiiu/apps/panicprk/, Switch: switch/panicprk/), ROMs in panicprkrom/ or there
        const std::string D = S23_APP_DIR;
#ifdef __3DS__
        osSetSpeedupEnable(true); APT_SetAppCpuTimeLimit(80);                               // 804 MHz + cache L2; core 1 all'80%
#endif
        freopen((D + "/log.txt").c_str(), "w", stdout); setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
        setenv("S23_WINDOW", "1", 1);
        setenv("S23_ASSETS", (D + "/" S23_ASSETS_DEFAULT).c_str(), 0); setenv("S23_LANGDIR", (D + "/lang").c_str(), 0);
        setenv("S23_NVRAM", (D + "/nvram.bin").c_str(), 0);
        static std::string a1 = D + "/panicprk_main.elf", a2 = D + "/main_r4650_BE_4MB.bin", a5 = D + "/panicprkrom";
        if (DIR* dd = opendir(a5.c_str())) closedir(dd); else a5 = D;
        static char* wargs[] = { argv[0], (char*)a1.c_str(), (char*)a2.c_str(), (char*)"1000000000", (char*)"0", (char*)a5.c_str(), nullptr };
        argc = 6; argv = wargs;
        printf("console: folder %s, ROMs in %s\n", D.c_str(), a5.c_str());
#ifdef __3DS__
        n3ds::cfg.load(); i18n::scan(); i18n::select(n3ds::cfg.ui_lang);
        if (!n3ds::init(D + "/ui")) { printf("3DS: GPU not initialised\n"); fflush(stdout); }
        n3ds::splash(i18n::tru("loading"));
#endif
#ifdef __ANDROID__
        {   // missing files: instead of closing silently, explains what to copy and where
            std::string miss; auto has = [](const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (f) fclose(f); return f != nullptr; };
            if (!has(a1)) miss += "  panicprk_main.elf\n";
            if (!has(a2)) miss += "  main_r4650_BE_4MB.bin\n";
            if (!has(a5 + "/pnp1vera.ic3") || !has(a5 + "/pnp1cgll.4m")) miss += "  panicprkrom/ (all the ROMs)\n";
            if (!miss.empty()) {
                std::string m = "These files are missing:\n\n" + miss + "\nCopy them into the PanicPark folder in phone storage\n"
                    "(same level as Download, DCIM...), then reopen the app.\n\nFolder in use now:\n" + D +
                    "\n\nIf the app doesn't have all files access: Settings > Apps > Panic Park > Permissions.";
                printf("%s\n", m.c_str()); fflush(stdout);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Panic Park", m.c_str(), nullptr);
                exit(0);
            }
        }
#endif
    }
#endif
#if defined(USE_SDL) && !defined(__ANDROID__)
    SDL_SetMainReady();
#endif
    if (argc < 3) { fprintf(stderr, "usage: %s panicprk_main.elf main_r4650_BE_4MB.bin [event_limit] [seconds] [rom_folder]\n"
                                    "env: S23_DSW=0xNN (default 0xff; 0xfd = Skip POST)\n", argv[0]); return 2; }
    if (argc > 3) g_limit = atoi(argv[3]);
    int secs = argc > 4 ? atoi(argv[4]) : 20; g_secs = secs;
    // window: S23_WINDOW=1 enables it, S23_WINDOW=0 disables it; on Windows it is on by default
    const char* wenv = getenv("S23_WINDOW");
#ifdef _WIN32
    const bool want_window = !(wenv && wenv[0] == '0');
#else
    const bool want_window = wenv && wenv[0] != '0';
#endif
    g_quiet = want_window && !getenv("S23_VERBOSE"); g_win_mode = want_window;
    if (const char* b = getenv("S23_BENS")) g_be_ns = atof(b);
    if (const char* d = getenv("S23_DSW")) g_dsw = strtoul(d, nullptr, 0);
    g_frame_dir = getenv("S23_FRAMES"); if (const char* e = getenv("S23_FRAME_EVERY")) g_frame_every = atoi(e);
    g_ram = (uint8_t*)calloc(RAM_SIZE, 1);
#ifdef __3DS__
    g_boot = linearSpaceFree() > (7u << 20) ? (uint8_t*)linearAlloc(0x400000) : nullptr;                 // 4 MB outside the 96 MB heap
    if (g_boot) memset(g_boot, 0, 0x400000); else
#endif
    g_boot = (uint8_t*)calloc(0x400000, 1);
    FILE* f = fopen(argv[2], "rb"); if (!f || fread(g_boot, 1, 0x400000, f) != 0x400000) { fprintf(stderr, "ROM?\n"); return 3; } fclose(f);
    blk_init(); g3d::c404 = blk_named("C404 regs/gamma")->m.data(); g_b_cr = blk_named("C361 char+text"); g_b_pl = blk_named("Palette RAM"); g_b_c4 = blk_named("C404 regs/gamma");
    for (auto& sn : g_sn) { sn.cr = g_b_cr->m; sn.pl = g_b_pl->m; sn.c4 = g_b_c4->m; }          // sized beforehand: the tick doesn't allocate
    for (int k = 0; k < 2; k++) { g_sn[k].cr = g_b_cr->m; g_sn[k].pl = g_b_pl->m; g_sn[k].c4 = g_b_c4->m; }
    if ((want_window || getenv("S23_NVRAM")) && !getenv("S23_NONVRAM")) nvram_load();
    if (const char* gl = getenv("S23_GAMELANG")) { g_game_lang = atoi(gl); apply_game_lang(false); }
    if (argc > 5 && !getenv("S23_FAKEH8")) h8_start_thread(std::string(argv[5]));
    if (argc > 5 && !getenv("S23_NO3D")) { g3d::irq_cb = c435_irq; if (g3d::load_roms(argv[5])) printf("3D: graphics ROMs loaded\n"); }
    if (argc > 5) {                                       // data ROM: pnp1mtah.2j (even bytes) + pnp1mtal.2h (odd)
        std::string d = argv[5];
#ifdef __3DS__
        n3ds::splash(i18n::tru("loading"));
        if ((g_data_f = fopen((assets::dir() + "/data.bin").c_str(), "rb"))) {
            fseek(g_data_f, 0, SEEK_END); g_data_n = (size_t)ftell(g_data_f); g_dp_buf = (uint8_t*)malloc(512 * 4096); memset(g_dp_tag, 0xff, sizeof g_dp_tag);
            if (!g_dp_buf) { fclose(g_data_f); g_data_f = nullptr; g_data_n = 0; } else { static uint8_t one = 0; g_data = &one; }
        }
        if (!g_data_f)
#endif
        g_data = assets::get("data.bin", g_data_n, [&](std::vector<uint8_t>& out) {
            std::vector<uint8_t> hi, lo; if (!load_file(d + "/pnp1mtah.2j", hi) || !load_file(d + "/pnp1mtal.2h", lo)) return false;
            out.resize(hi.size() * 2); for (size_t i = 0; i < hi.size(); i++) { out[2 * i] = hi[i]; out[2 * i + 1] = lo[i]; } return true; });
        if (g_data) {
            printf("data ROM loaded: %zu bytes\n", g_data_n);
        } else printf("WARNING: data ROM not found in %s\n", d.c_str());
    }
    std::vector<uint8_t> e; if (!load_file(argv[1], e)) return 3;
    uint32_t phoff = be32(&e[0x1c]); uint16_t phnum = be16(&e[0x2c]), phent = be16(&e[0x2a]);
    uint32_t entry = be32(&e[0x18]);
    for (int i = 0; i < phnum; i++) {
        const uint8_t* ph = &e[phoff + i * phent];
        if (be32(ph) != 1) continue;
        uint32_t off = be32(ph + 4), va = be32(ph + 8), fsz = be32(ph + 16);
        memcpy(g_ram + (va & 0xffffff), &e[off], fsz);
    }
    printf("ELF %zu bytes, entry=%08x, DSW=%02x\n", e.size(), entry, g_dsw);
    { const size_t esz = e.size(); std::vector<uint8_t>().swap(e); (void)esz; }                     // already copied into RAM: 2.5 MB freed
    if (getenv("S23_LISTTEXT")) { game_text_patch("", true); return 0; }
    if (const char* gt = getenv("S23_GAMETEXT")) game_text_patch(gt);
#ifdef USE_SDL
    if (want_window) {
#ifdef __ANDROID__
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");                                // Back arrives as a key: exits the game
#endif
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 5; }
        if (g_h8 && g_h8->snd.ok() && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            SDL_AudioSpec want{}, have{}; want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
            SDL_AudioDeviceID d = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (d) { SDL_PauseAudioDevice(d, 0); g_adev = d; printf("audio: %d Hz stereo\n", have.freq); } else printf("audio: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        }
        g_is.cfg.load(); g_is.pads.rescan();
#ifdef S23_CONSOLE
        g_win = SDL_CreateWindow("Panic Park - recompiled", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_FULLSCREEN);   // TV (+ GamePad on Wii U)
#else
        g_win = SDL_CreateWindow("Panic Park - recompiled", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, getenv("S23_WIN_W") ? atoi(getenv("S23_WIN_W")) : 960, getenv("S23_WIN_H") ? atoi(getenv("S23_WIN_H")) : 720, SDL_WINDOW_RESIZABLE | (g_is.cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
#endif
        if (!g_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 5; }
#ifndef S23_CONSOLE
        {   // window / taskbar icon: ui/icon.png next to the program (optional, your own picture)
            int iw, ih, in_; unsigned char* px = stbi_load((ui_dir() + "/icon.png").c_str(), &iw, &ih, &in_, 4);
            if (px) {
                SDL_Surface* ic = SDL_CreateRGBSurfaceWithFormatFrom(px, iw, ih, 32, iw * 4, SDL_PIXELFORMAT_RGBA32);
                if (ic) { SDL_SetWindowIcon(g_win, ic); SDL_FreeSurface(ic); }
                stbi_image_free(px);
            }
        }
#endif
        { const char* ge = getenv("S23_GL"); g_gpu = ge ? atoi(ge) != 0 : g_is.cfg.gpu != 0; }
        if (g_gpu && !getenv("SDL_RENDER_DRIVER")) {                                         // 3D on GPU uses the OpenGL context of SDL's renderer
#if defined(__ANDROID__) || defined(__SWITCH__)
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2"); SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
        }
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
        if (!g_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 5; }
        SDL_RenderSetLogicalSize(g_ren, 640, 480);
        if (const char* ms = getenv("S23_MENU_SHOT")) { static std::string mp = ms; g_menu_shot = [](SDL_Renderer* r) { int w = 0, h = 0; SDL_GetRendererOutputSize(r, &w, &h); std::vector<uint8_t> px((size_t)w * h * 3); if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGB24, px.data(), w * 3) == 0) write_png(mp, px.data(), w, h); }; }
        if (const char* ls = getenv("S23_LAUNCH_SHOT")) { static std::string lp = ls; g_launcher_shot = [](SDL_Renderer* r) { int w = 0, h = 0; SDL_GetRendererOutputSize(r, &w, &h); std::vector<uint8_t> px((size_t)w * h * 3); if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGB24, px.data(), w * 3) == 0) write_png(lp, px.data(), w, h); }; }
        if (!getenv("S23_NOGUI") && !launcher(g_win, g_ren, g_is)) {
#ifdef S23_CONSOLE
            console_stop_threads();                                                         // the H8 thread has already started
            fflush(stdout);
#ifdef __ANDROID__
            SDL_Quit(); exit(0);
#endif
#endif
            SDL_Quit(); return 0;
        }
        if (!getenv("S23_DSW")) { if (g_is.cfg.skip_post) g_dsw &= ~2u; if (g_is.cfg.service_dip) g_dsw &= ~1u; }
        g_is.service_dip = !(g_dsw & 1);
        g_game_lang = g_is.cfg.game_lang; apply_game_lang(false);
        { static const char* LC[8] = {"ja", "en", "de", "fr", "es", "pt", "it", "ko"}; game_text_patch(g_game_lang >= 0 ? LC[g_game_lang & 7] : i18n::cur_code()); }
        printf("final DSW=%02x (skip POST: %s, config: %s)\n", g_dsw, (g_dsw & 2) ? "no" : "yes", Config::path().c_str()); fflush(stdout);
    }
#elif defined(__3DS__)
    n3ds::mem_report("after loading");
    if (want_window && (!g3d::R.ok || !g_data_n)) {                                       // assets missing or not enough memory
        std::string m = assets::last_error().empty() ? std::string("graphics ROMs or data ROM not loaded") : assets::last_error();
        m += ". Needs the assets already extracted to sd:/3ds/panicprk/assets (tex.bin, pt.bin, tm.bin, wave.bin, data.bin).";
        n3ds::fatal(m); console_stop_threads(); fflush(stdout); C2D_Fini(); C3D_Fini(); gfxExit(); return 0;
    }
    if (want_window) {
        n3ds::audio_init(); g_audio_push = n3ds::audio_push;
        if (n3ds::menu(false) == 1) { console_stop_threads(); fflush(stdout); C2D_Fini(); C3D_Fini(); gfxExit(); return 0; }
        if (!getenv("S23_DSW")) { if (n3ds::cfg.skip_post) g_dsw &= ~2u; if (n3ds::cfg.service_dip) g_dsw &= ~1u; }
        n3ds::apply_lang(); apply_game_lang(false);
        printf("final DSW=%02x\n", g_dsw); fflush(stdout);
    }
#endif
#if defined(S23_CONSOLE)
    (void)on_alarm; t_zero = emu_ns(); g_next_vbl = t_zero;                          // tick: poll_tick() from the backward branches
#elif !defined(_WIN32)
    signal(SIGVTALRM, on_alarm);
    if (secs > 0) { itimerval w{}; w.it_value.tv_sec = secs; setitimer(ITIMER_VIRTUAL, &w, nullptr); }     // 0 = no time limit
    t_zero = emu_ns(); g_next_vbl = t_zero;
#ifndef S23_POLL_TICK
    signal(SIGALRM, on_tick);
    { itimerval tk{}; tk.it_interval.tv_usec = 1000; tk.it_value.tv_usec = 1000; setitimer(ITIMER_REAL, &tk, nullptr); }
#endif
#else
    (void)on_alarm; t_zero = emu_ns(); g_next_vbl = t_zero;                          // the tick is started by game_thread (win_ticker)
#endif
    memset(&g_cpu_obj, 0, sizeof g_cpu_obj); g_cpu = &g_cpu_obj;
    if (want_window) {
#ifdef USE_SDL
        block_timer_signals(true);                                                          // the main thread (SDL) doesn't receive the timer signals
#ifdef __WIIU__
        g_th_game = start_thread(game_thread, 32u << 20, 2, 16, "R4650");                   // core 2
        if (!g_th_game) { fprintf(stderr, "thread not created\n"); return 4; }
#elif defined(__SWITCH__) || defined(__ANDROID__)
        if (!start_thread(game_thread, 32u << 20, 1, 0, "R4650")) { fprintf(stderr, "thread not created\n"); return 4; }   // core 1
#else
        if (!start_thread(game_thread, 256u << 20, 0, 0, "R4650")) { fprintf(stderr, "thread not created\n"); return 4; }
#endif
        start_render_pool();
        run_window();
#ifdef __ANDROID__
        fflush(stdout); exit(0);                                                            // new process at every launch (global state isn't reset)
#endif
#ifdef S23_CONSOLE
        return 0;                                                                           // back to the menu (Aroma / Homebrew Menu)
#endif
#elif defined(__3DS__)
        if (!start_thread(game_thread, 4u << 20, 2, 0x30, "R4650")) { printf("game thread not created\n"); fflush(stdout); return 4; }   // core 2 (New 3DS)
        n3ds::run();
        console_stop_threads(); nvram_save(); printf("exit\n"); fflush(stdout);
        if (n3ds::g_snd) ndspExit();
        C2D_Fini(); C3D_Fini(); gfxExit(); return 0;
#else
        fprintf(stderr, "this binary was built without SDL2 (install libsdl2-dev and rebuild)\n"); return 5;
#endif
    }
    if (getenv("S23_RTHREADS")) start_render_pool();                                  // test without a window
    game_thread(nullptr);
}
