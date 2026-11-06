// Asset cache: the big ROMs (textures, 3D models, audio samples, data ROM) are converted ONCE
// in the format used in memory and saved in the assets folder (default "assets", env S23_ASSETS).
// On later launches the files are memory-mapped read-only (mmap / MapViewOfFile): the system loads
// only the pages actually used and can release them when needed, without swap. To regenerate: delete the folder.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <cstdlib>
#include <algorithm>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#elif defined(__WIIU__) || defined(__SWITCH__) || defined(__3DS__)
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace assets {
// the assets are in the machine's format: on a big-endian host (Wii U) a different folder is used
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define S23_ASSETS_DEFAULT "assets_be"
#else
#define S23_ASSETS_DEFAULT "assets"
#endif
inline std::string& dir() { static std::string d = getenv("S23_ASSETS") ? getenv("S23_ASSETS") : S23_ASSETS_DEFAULT; return d; }
inline std::string& last_error() { static std::string e; return e; }      // 3DS: reason why the last asset failed to load
inline bool& disabled() { static bool off = getenv("S23_NOASSETS") != nullptr; return off; }   // everything in RAM, as before

// maps a file read-only; nullptr if it doesn't exist or is empty
inline const uint8_t* map_file(const std::string& p, size_t& n) {
#ifdef _WIN32
    HANDLE f = CreateFileA(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER sz; if (!GetFileSizeEx(f, &sz) || sz.QuadPart == 0) { CloseHandle(f); return nullptr; }
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr); CloseHandle(f);
    if (!m) return nullptr;
    void* v = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0); CloseHandle(m);
    if (!v) return nullptr;
    n = (size_t)sz.QuadPart; return (const uint8_t*)v;
#elif defined(__WIIU__) || defined(__SWITCH__) || defined(__3DS__)
    // no mmap: the file is read entirely into memory (Wii U and Switch: ~85 MB in total)
    FILE* f = fopen(p.c_str(), "rb"); if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return nullptr; }
#ifdef __3DS__
    // 3DS: the normal heap is at most 96 MB; the remaining memory is "linear": the files that fit go there
    uint8_t* v = linearSpaceFree() > (u32)sz + (3u << 20) ? (uint8_t*)linearAlloc((size_t)sz) : nullptr;
    bool lin = v != nullptr;
    if (lin) printf("asset %s: in linear memory\n", p.c_str()); else v = (uint8_t*)malloc((size_t)sz);
    if (!v && linearSpaceFree() > (u32)sz) { v = (uint8_t*)linearAlloc((size_t)sz); lin = v != nullptr; }   // last try, without margin
    if (!v) { printf("asset %s: %ld bytes, no memory (heap and linear memory full)\n", p.c_str(), sz); fclose(f); return nullptr; }
#else
    uint8_t* v = (uint8_t*)malloc((size_t)sz); if (!v) { fclose(f); return nullptr; }
#endif
    static std::vector<char> buf(1 << 20); setvbuf(f, buf.data(), _IOFBF, buf.size());
    size_t k = 0;
    while (k < (size_t)sz) { size_t r = fread(v + k, 1, std::min<size_t>((size_t)sz - k, 1 << 20), f); if (!r) break; k += r; }
    fclose(f);
#ifdef __3DS__
    if (k != (size_t)sz) { printf("asset %s: read only %zu of %ld bytes (SD read error or damaged file: copy it again)\n", p.c_str(), k, sz);
                           if (lin) linearFree(v); else free(v); return nullptr; }
#else
    if (k != (size_t)sz) { free(v); return nullptr; }
#endif
    n = (size_t)sz; return v;
#else
    int fd = open(p.c_str(), O_RDONLY); if (fd < 0) return nullptr;
    struct stat st; if (fstat(fd, &st) != 0 || st.st_size == 0) { close(fd); return nullptr; }
    void* v = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0); close(fd);
    if (v == MAP_FAILED) return nullptr;
    n = (size_t)st.st_size; return (const uint8_t*)v;
#endif
}

// returns the asset `name`: maps it if it exists, otherwise builds it with `build`, saves it and maps it.
// If the folder isn't writable (or S23_NOASSETS) it stays in RAM. nullptr if `build` fails.
inline const uint8_t* get(const std::string& name, size_t& n, const std::function<bool(std::vector<uint8_t>&)>& build) {
    static std::vector<std::vector<uint8_t>*> keep;
    std::string p = dir() + "/" + name;
    if (!disabled()) if (const uint8_t* q = map_file(p, n)) {
#ifdef __3DS__
        printf("asset %s loaded: %zu KB\n", p.c_str(), n / 1024); fflush(stdout);
#endif
        return q; }
#ifdef __3DS__
    {   // 3DS: no conversion from the ROMs (it doesn't fit in memory): the assets must come ready-made from the PC
        FILE* f = fopen(p.c_str(), "rb"); const bool exists = f != nullptr; if (f) fclose(f);
        printf("asset %s: %s\n", p.c_str(), exists ? "could not be loaded (reason above)" : "MISSING (copy the assets folder from the PC)"); fflush(stdout);
        last_error() = name + (exists ? ": could not be loaded (see log.txt)" : ": file missing from assets/");
        (void)build; return nullptr;
    }
#endif
    auto* v = new std::vector<uint8_t>;
    if (!build(*v) || v->empty()) { delete v; return nullptr; }
    if (!disabled()) {
#ifdef _WIN32
        _mkdir(dir().c_str());
#else
        mkdir(dir().c_str(), 0755);
#endif
        std::string tmp = p + ".tmp";
        if (FILE* f = fopen(tmp.c_str(), "wb")) {
            bool ok = fwrite(v->data(), 1, v->size(), f) == v->size(); ok = (fclose(f) == 0) && ok;
            if (ok && rename(tmp.c_str(), p.c_str()) == 0) {
                if (const uint8_t* q = map_file(p, n)) { printf("asset extracted: %s (%zu KB)\n", p.c_str(), v->size() / 1024); delete v; return q; }
            } else remove(tmp.c_str());
        }
        printf("asset %s: folder not writable, stays in RAM\n", p.c_str());
    }
    n = v->size(); keep.push_back(v); return v->data();
}

inline bool read_all(const std::string& p, std::vector<uint8_t>& d) {
    FILE* f = fopen(p.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); if (n <= 0) { fclose(f); return false; }
    d.resize((size_t)n); size_t k = fread(d.data(), 1, (size_t)n, f); fclose(f); return k == (size_t)n;
}
}  // namespace assets
