// Benchmark of the 3DS renderer (platform/pica3d.h) on PC, with 3D lists saved by the PC version:
//   S23_FRAMES=frames S23_PICADUMP=dumps ./panicprk ...      (saves dumps/list_NNNN.bin)
//   g++ -O2 -std=c++17 -I. tools/bench_pica.cpp -o bench_pica && ./bench_pica assets dumps/list_*.bin   (REPS=n repeats)
// It times g3d::prepare ("geo") and pica::Renderer::build ("cells") with a backend that does the same work as the
// 3DS one (RGBA5551, tiled), and prints a checksum of vertices and textures to compare two versions.
#include "platform/assets.h"
#include "platform/gpu3d.h"
#include "platform/pica3d.h"
#include <chrono>
static const uint8_t MORTON_Y[8] = {0, 2, 8, 10, 32, 34, 40, 42};
struct Bk : pica::Backend {
    static const int SIDE = 1024, PER = 64, ROW = 8; std::vector<uint16_t> tex[3];
    Bk() { for (auto& t : tex) t.assign(SIDE * SIDE, 0); }
    bool flip_t() const override { return true; }
    int tex_of(int s) const override { return s / PER; }
    int slots_per_tex() const override { return PER; }
    pica::SlotMap slot_map(int s) const override { const int i = s % PER; const float C = (float)pica::CELL / SIDE; return {(i % ROW) * C, C, 1.f - (i / ROW) * C, -C}; }
    void upload_slot(int s, const uint16_t* p) override {           // same as C3DBackend::upload_slot (platform_3ds.h)
        uint16_t* d = tex[s / PER].data(); const int C = pica::CELL, i = s % PER, ox = (i % ROW) * C, oy = (i / ROW) * C;
        for (int y = 0; y < C; y++) {
            const uint16_t* r = p + y * C; uint16_t* row = d + ((oy + y) & ~7u) * SIDE + ox * 8; const uint8_t* yl = MORTON_Y + ((oy + y) & 7);
            for (int x = 0; x < C; x += 8) { uint16_t* t = row + x * 8; const uint16_t* q = r + x;
                t[0 + *yl] = q[0]; t[1 + *yl] = q[1]; t[4 + *yl] = q[2]; t[5 + *yl] = q[3]; t[16 + *yl] = q[4]; t[17 + *yl] = q[5]; t[20 + *yl] = q[6]; t[21 + *yl] = q[7]; }
        }
    }
};
static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: bench_pica assets_dir list.bin...\n"); return 1; }
    setenv("S23_ASSETS", argv[1], 1);
    if (!g3d::load_roms(".")) { printf("assets not found\n"); return 1; }
    struct Fr { std::vector<g3d::Entry> l; std::vector<uint32_t> pal; std::vector<uint16_t> sten; };
    std::vector<Fr> frs;
    for (int i = 2; i < argc; i++) {
        FILE* f = fopen(argv[i], "rb"); if (!f) continue; Fr fr; uint32_t ne = 0, np = 0;
        if (fread(&ne, 4, 1, f) != 1) { fclose(f); continue; } fr.l.resize(ne); if (fread(fr.l.data(), sizeof(g3d::Entry), ne, f) != ne) { fclose(f); continue; }
        if (fread(&np, 4, 1, f) != 1) { fclose(f); continue; } fr.pal.resize(np); if (fread(fr.pal.data(), 4, np, f) != np) { fclose(f); continue; }
        fr.sten.resize(0x20000); if (fread(fr.sten.data(), 2, 0x20000, f) != 0x20000) { fclose(f); continue; } fclose(f);
        frs.push_back(std::move(fr));
    }
    static Bk bk; static pica::Renderer pr; pr.init(&bk, 192);
    g3d::Frame F; std::vector<int> order; double tg = 0, tb = 0; uint64_t sum = 1469598103934665603ull; unsigned polys = 0, nslots = 0, cells = 0, rel = 0, ev = 0;
    const int reps = getenv("REPS") ? atoi(getenv("REPS")) : 3;
    for (int r = 0; r < reps; r++) for (auto& fr : frs) {
        memcpy(g3d::C.c412_sram, fr.sten.data(), 0x40000);
        double a = now(); F.polys.clear(); F.pal.clear(); order.clear(); g3d::prepare(fr.l, F, order);
        double b = now(); pr.build(F, order, fr.pal.data(), 0.5f, 40.f, 0.f, 360.f, 240.f); double c = now();
        tg += b - a; tb += c - b; polys += pr.st.polys; nslots += pr.st.new_slots; cells += pr.st.cells; rel += pr.st.reloads; ev += pr.st.evicted;
        if (r == 0) { auto mix = [&](const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; for (size_t i = 0; i < n; i++) sum = (sum ^ q[i]) * 1099511628211ull; };
            for (auto& v : pr.verts) { int32_t q[6] = {(int32_t)std::lround(v.x * 16), (int32_t)std::lround(v.y * 16), (int32_t)std::lround(v.w * 4096), (int32_t)std::lround(v.s * 65536), (int32_t)std::lround(v.t * 65536), (int32_t)(v.r | v.g << 8 | v.b << 16 | v.a << 24)}; mix(q, sizeof q); }
            for (auto& bt : pr.batches) { int q[5] = {bt.slot, bt.count, bt.ca, bt.mode, (int)bt.fade}; mix(q, sizeof q); } }
    }
    uint64_t ts = 0; for (auto& t : bk.tex) for (auto x : t) ts = ts * 31 + x;
    const double n = (double)frs.size() * reps;
    printf("%zu frames x %d: geo %.3f ms  cells %.3f ms per frame | polygons %.0f cells %.0f new slots %.1f (palette reloads %.1f, evicted %.1f) | checksum %016llx tex %016llx\n",
           frs.size(), reps, tg / n, tb / n, polys / n, cells / n, nslots / n, rel / n, ev / n, (unsigned long long)sum, (unsigned long long)ts);
}
