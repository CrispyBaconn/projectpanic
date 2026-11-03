// Namco C352: 32 voci PCM 8 bit (lineare o mu-law), 4 uscite. Derivato da c352.cpp di MAME
// (R. Belmont, superctr, cync, hoot team). license:BSD-3-Clause
// Il chip gira a 25401600/288 = 88200 campioni/s; qui viene avanzato in base ai cicli dell'H8
// (16934400 Hz -> esattamente 192 cicli per campione) e l'uscita va in un buffer stereo a 44100 Hz.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <functional>

class C352 {
public:
    enum : uint16_t {
        BUSY = 0x8000, KEYON = 0x4000, KEYOFF = 0x2000, LOOPHIST = 0x0800, PHASERL = 0x0200, PHASEFL = 0x0100,
        PHASEFR = 0x0080, LDIR = 0x0040, LINK = 0x0020, NOISE = 0x0010, MULAW = 0x0008, FILTER = 0x0004,
        LOOP = 0x0002, REVERSE = 0x0001
    };
    struct Voice {
        uint32_t pos = 0, counter = 0; int16_t sample = 0, last_sample = 0;
        uint16_t vol_f = 0, vol_r = 0; uint8_t curr_vol[4] = {0, 0, 0, 0};
        uint16_t freq = 0, flags = 0, wave_bank = 0, wave_start = 0, wave_end = 0, wave_loop = 0;
    };
    static constexpr int CYCLES_PER_SAMPLE = 192;                 // cicli H8 per campione a 88200 Hz

    const uint8_t* wave = nullptr;                                // 16 MB: pnp1wavel (0) + pnp1waveh (0x800000), anche mappato da file
    Voice v[32];
    uint16_t control = 0, random = 0x1234;
    int16_t mulaw[256];
    uint64_t last_cycle = 0;
    // uscita: coppie L/R a 44100 Hz (media di due campioni a 88200)
    std::function<void(const int16_t*, size_t)> sink;             // riceve blocchi di campioni stereo interleaved
    std::vector<int16_t> out; int acc_l = 0, acc_r = 0, half = 0;

    C352() {
        int j = 0;
        for (int i = 0; i < 128; i++) { mulaw[i] = (int16_t)(j << 5); j += i < 16 ? 1 : i < 24 ? 2 : i < 48 ? 4 : i < 100 ? 8 : 16; }
        for (int i = 0; i < 128; i++) mulaw[i + 128] = (int16_t)((~mulaw[i]) & 0xffe0);
        out.reserve(2048);
    }
    // costruisce l'immagine dei campioni (16 MB) dalle due ROM
    static bool build_wave(const std::string& dir, std::vector<uint8_t>& out) {
        out.assign(0x1000000, 0);
        const char* f[2] = {"pnp1wavel.2c", "pnp1waveh.2a"};
        for (int k = 0; k < 2; k++) {
            FILE* fp = fopen((dir + "/" + f[k]).c_str(), "rb"); if (!fp) { out.clear(); return false; }
            size_t n = fread(out.data() + k * 0x800000, 1, 0x800000, fp); fclose(fp); if (n != 0x800000) { out.clear(); return false; }
        }
        return true;
    }
    bool set_wave(const uint8_t* p, size_t n) { wave = (p && n >= 0x1000000) ? p : nullptr; return wave != nullptr; }
#ifdef __3DS__
    // 3DS: i campioni (16 MB) restano sulla SD, letti a pagine da 4 KB con una cache da 1 MB (15 MB di RAM in meno)
    FILE* wf = nullptr; uint8_t* wpage = nullptr; uint32_t wtag[256];
    bool set_wave_file(FILE* f) {
        if (!f) return false; fseek(f, 0, SEEK_END); if (ftell(f) < 0x1000000) { fclose(f); return false; }
        wpage = (uint8_t*)malloc(256 * 4096); if (!wpage) { fclose(f); return false; }
        memset(wtag, 0xff, sizeof wtag); wf = f; static const uint8_t one = 0; wave = &one; return true;
    }
    uint8_t wave_byte(uint32_t a) {
        if (!wf) return wave[a];
        const uint32_t page = a >> 12, slot = page & 255;
        if (wtag[slot] != page) { fseek(wf, (long)page << 12, SEEK_SET); if (fread(wpage + slot * 4096, 1, 4096, wf) != 4096) memset(wpage + slot * 4096, 0, 4096); wtag[slot] = page; }
        return wpage[slot * 4096 + (a & 4095)];
    }
#else
    uint8_t wave_byte(uint32_t a) const { return wave[a]; }
#endif
    bool ok() const { return wave != nullptr; }

    // ------------------------------------------------------------------ registri (offset in parole)
    uint16_t* reg_ptr(Voice& x, int r) {
        switch (r) { case 0: return &x.vol_f; case 1: return &x.vol_r; case 2: return &x.freq; case 3: return &x.flags;
                     case 4: return &x.wave_bank; case 5: return &x.wave_start; case 6: return &x.wave_end; default: return &x.wave_loop; }
    }
    uint16_t read(uint32_t off) {
        if (off < 0x100) return *reg_ptr(v[off / 8], off % 8);
        if (off == 0x200) return control;
        return 0;
    }
    void write(uint32_t off, uint16_t data, uint16_t mask) {
        if (off < 0x100) { uint16_t* p = reg_ptr(v[off / 8], off % 8); *p = (uint16_t)((*p & ~mask) | (data & mask)); }
        else if (off == 0x200) control = (uint16_t)((control & ~mask) | (data & mask));
        else if (off == 0x202 && mask == 0xffff) {                // esegue key on / key off
            for (auto& x : v) {
                if (x.flags & KEYON) {
                    x.pos = ((uint32_t)x.wave_bank << 16) | x.wave_start; x.sample = x.last_sample = 0; x.counter = 0xffff;
                    x.flags |= BUSY; x.flags &= ~(KEYON | LOOPHIST); memset(x.curr_vol, 0, 4);
                }
                if (x.flags & KEYOFF) { x.flags &= ~(BUSY | KEYOFF); x.counter = 0xffff; }
            }
        }
    }

    // ------------------------------------------------------------------ generazione
    void fetch(Voice& x) {
        x.last_sample = x.sample;
        if (x.flags & NOISE) { random = (uint16_t)((random >> 1) ^ ((-(random & 1)) & 0xfff6)); x.sample = (int16_t)random; return; }
        int8_t s = (int8_t)wave_byte(x.pos & 0xffffff);
        x.sample = (x.flags & MULAW) ? mulaw[(uint8_t)s] : (int16_t)(s * 256);
        uint16_t p = x.pos & 0xffff;
        if ((x.flags & LOOP) && (x.flags & REVERSE)) {
            if ((x.flags & LDIR) && p == x.wave_loop) x.flags &= ~LDIR;
            else if (!(x.flags & LDIR) && p == x.wave_end) x.flags |= LDIR;
            x.pos += (x.flags & LDIR) ? -1 : 1;
        } else if (p == x.wave_end) {
            if ((x.flags & LINK) && (x.flags & LOOP)) { x.pos = ((uint32_t)x.wave_start << 16) | x.wave_loop; x.flags |= LOOPHIST; }
            else if (x.flags & LOOP) { x.pos = (x.pos & 0xff0000) | x.wave_loop; x.flags |= LOOPHIST; }
            else { x.flags |= KEYOFF; x.flags &= ~BUSY; x.sample = 0; }
        } else x.pos += (x.flags & REVERSE) ? -1 : 1;
    }
    static void ramp(Voice& x, int ch, uint8_t val) { int d = x.curr_vol[ch] - val; if (d) x.curr_vol[ch] += d > 0 ? -1 : 1; }

    void one_sample() {
        int o[4] = {0, 0, 0, 0};
        for (auto& x : v) {
            if (!(x.flags & BUSY)) continue;                         // voce ferma: contributo nullo (i volumi correnti ripartono da 0 al key on)
            int s = 0;
            {
                int32_t next = (int32_t)(x.counter + x.freq);
                if (next & 0x10000) fetch(x);
                if ((next ^ (int32_t)x.counter) & 0x18000) { ramp(x, 0, x.vol_f >> 8); ramp(x, 1, x.vol_f & 0xff); ramp(x, 2, x.vol_r >> 8); ramp(x, 3, x.vol_r & 0xff); }
                x.counter = (uint32_t)next & 0xffff;
                s = x.sample;
                if (!(x.flags & FILTER)) s = x.last_sample + (int)(((int64_t)x.counter * (x.sample - x.last_sample)) >> 16);
            }
            o[0] += (((x.flags & PHASEFL) ? -s : s) * x.curr_vol[0]) >> 8;
            o[2] += (((x.flags & PHASERL) ? -s : s) * x.curr_vol[2]) >> 8;
            o[1] += (((x.flags & PHASEFR) ? -s : s) * x.curr_vol[1]) >> 8;
            o[3] += (((x.flags & PHASEFR) ? -s : s) * x.curr_vol[3]) >> 8;
        }
        // come MAME: uscite 0/2 a sinistra, 1/3 a destra, ognuna saturata a 16 bit dopo >>3
        auto sat = [](int a) { return a < -32768 ? -32768 : a > 32767 ? 32767 : a; };
        int l = sat(o[0] >> 3) + sat(o[2] >> 3), r = sat(o[1] >> 3) + sat(o[3] >> 3);
        acc_l += l; acc_r += r;
        if (++half == 2) {
            half = 0;
            out.push_back((int16_t)sat(acc_l / 2)); out.push_back((int16_t)sat(acc_r / 2)); acc_l = acc_r = 0;
            if (out.size() >= 1024) flush();
        }
    }
    void flush() { if (sink && !out.empty()) sink(out.data(), out.size() / 2); out.clear(); }

    // avanza fino al ciclo H8 indicato
    void run_to(uint64_t cycle) {
        if (!ok()) { last_cycle = cycle; return; }
        if (cycle < last_cycle) { last_cycle = cycle; return; }
        if (cycle - last_cycle > 16934400ull) last_cycle = cycle - 16934400ull;   // dopo una pausa lunga non recupera piu' di 1 s
        while (cycle - last_cycle >= CYCLES_PER_SAMPLE) { one_sample(); last_cycle += CYCLES_PER_SAMPLE; }
    }
    void reset() { for (auto& x : v) x = Voice(); control = 0; random = 0x1234; }
};
