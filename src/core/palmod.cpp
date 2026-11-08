#include "palmod.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace zengine {
namespace palmod {

namespace {

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
void wr32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF));
}

constexpr int kTones = 16;

const std::map<std::string, std::vector<int>>& table() {
    static const std::map<std::string, std::vector<int>> t = [] {
        std::map<std::string, std::vector<int>> m;
        static const struct { const char* name; std::vector<int> counts; } rows[] = {
#include "pal_regions.inc"
        };
        for (const auto& r : rows) m[normaliseName(r.name)] = r.counts;
        return m;
    }();
    return t;
}

std::string lowerExt(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

}  // namespace

std::string normaliseName(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o.push_back(c);
    }
    return o;
}

bool parse(const uint8_t* d, size_t size, PalFile& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (size < 22 + 12) return fail("file too short for a .pal");
    char name[17] = {};
    std::memcpy(name, d, 16);
    out = PalFile{};
    out.character = name;
    out.numColors = rd32(d + 16);
    out.hueShiftInc = d[20];
    out.hueShiftInt = d[21];
    if (out.numColors < 2 || out.numColors > 4096) return fail("implausible colour count");
    const size_t n = out.numColors - 1;
    if (size < 22 + n * 4 + 12) return fail("file shorter than its colour count says");
    out.colors.resize(n);
    for (size_t i = 0; i < n; ++i) out.colors[i] = rd32(d + 22 + i * 4);
    out.line = rd32(d + 22 + n * 4);
    out.shadow1 = rd32(d + 22 + n * 4 + 4);
    out.shadow2 = rd32(d + 22 + n * 4 + 8);
    return true;
}

std::vector<uint8_t> serialize(const PalFile& p) {
    std::vector<uint8_t> o;
    char name[16] = {};
    std::memcpy(name, p.character.data(), std::min<size_t>(15, p.character.size()));
    o.insert(o.end(), name, name + 16);
    wr32(o, static_cast<uint32_t>(p.colors.size() + 1));
    o.push_back(p.hueShiftInc);
    o.push_back(p.hueShiftInt);
    for (uint32_t c : p.colors) wr32(o, c);
    wr32(o, p.line);
    wr32(o, p.shadow1);
    wr32(o, p.shadow2);
    return o;
}

const std::vector<int>* regionCounts(const std::string& n) {
    const auto it = table().find(normaliseName(n));
    return it == table().end() ? nullptr : &it->second;
}

namespace {

// ARGB rows: row 0 = outline layer, then one row of 16 tones per region.
bool bake(const PalFile& pal, std::vector<uint32_t>& argb, int& rows, std::string* err) {
    const std::vector<int>* counts = regionCounts(pal.character);
    if (!counts) {
        if (err) *err = "unknown character '" + pal.character + "'";
        return false;
    }
    size_t total = 0;
    for (int c : *counts) total += static_cast<size_t>(c);
    if (total != pal.colors.size()) {
        if (err)
            *err = "'" + pal.character + "' has " + std::to_string(pal.colors.size()) + " colours but this game build's table expects " +
                   std::to_string(total) + " (palette made for another version)";
        return false;
    }
    rows = static_cast<int>(counts->size()) + 1;
    argb.assign(static_cast<size_t>(rows) * kTones, 0u);
    if ((pal.line >> 24) != 0)
        for (int t = 0; t < kTones; ++t) argb[static_cast<size_t>(t)] = pal.line;
    size_t at = 0;
    for (size_t r = 0; r < counts->size(); ++r) {
        const int n = (*counts)[r];
        const uint32_t* key = &pal.colors[at];  // key[0] = lightest ... key[n-1] = darkest
        at += static_cast<size_t>(n);
        for (int t = 0; t < kTones; ++t) {
            uint32_t px;
            if (n <= 1) {
                px = key[0];
            } else {
                const double x = t * (n - 1) / static_cast<double>(kTones - 1);  // 0 = darkest key
                const int i = std::min(static_cast<int>(x), n - 2);
                const double f = x - i;
                const uint32_t a = key[n - 1 - i], b = key[n - 2 - i];  // a darker, b lighter
                px = 0;
                for (int s = 0; s < 32; s += 8) {
                    const double va = (a >> s) & 0xFF, vb = (b >> s) & 0xFF;
                    px |= static_cast<uint32_t>(static_cast<int>(va + (vb - va) * f)) << s;
                }
            }
            argb[(r + 1) * kTones + static_cast<size_t>(t)] = px;
        }
    }
    return true;
}

}  // namespace

bool toPalette(const PalFile& pal, Palette& out, std::string* err) {
    std::vector<uint32_t> argb;
    int rows = 0;
    if (!bake(pal, argb, rows, err)) return false;
    out.tones = kTones;
    out.bands = rows;
    out.rgb.resize(argb.size() * 3);
    for (size_t i = 0; i < argb.size(); ++i) {
        out.rgb[i * 3 + 0] = static_cast<uint8_t>((argb[i] >> 16) & 0xFF);
        out.rgb[i * 3 + 1] = static_cast<uint8_t>((argb[i] >> 8) & 0xFF);
        out.rgb[i * 3 + 2] = static_cast<uint8_t>(argb[i] & 0xFF);
    }
    return true;
}

bool toDds(const PalFile& pal, std::vector<uint8_t>& out, std::string* err) {
    std::vector<uint32_t> argb;
    int rows = 0;
    if (!bake(pal, argb, rows, err)) return false;
    out.clear();
    out.insert(out.end(), {'D', 'D', 'S', ' '});
    wr32(out, 124);
    wr32(out, 0x100F);                         // caps | height | width | pixel format | pitch
    wr32(out, static_cast<uint32_t>(rows));
    wr32(out, kTones);
    wr32(out, kTones * 4);                     // pitch
    wr32(out, 0);                              // depth
    wr32(out, 1);                              // mip levels
    for (int i = 0; i < 11; ++i) wr32(out, 0); // reserved
    wr32(out, 32);                             // pixel format: size
    wr32(out, 0x41);                           // RGB | ALPHAPIXELS
    wr32(out, 0);                              // fourcc
    wr32(out, 32);                             // bits
    wr32(out, 0x00FF0000);
    wr32(out, 0x0000FF00);
    wr32(out, 0x000000FF);
    wr32(out, 0xFF000000);
    wr32(out, 0x1000);                         // caps: texture
    for (int i = 0; i < 4; ++i) wr32(out, 0);  // caps2..4, reserved
    for (uint32_t px : argb) wr32(out, px);
    return true;
}

std::vector<const Entry*> Library::forCharacter(const std::string& n) const {
    std::vector<const Entry*> v;
    const std::string key = normaliseName(n);
    for (const Entry& e : entries)
        if (e.character == key) v.push_back(&e);
    return v;
}

const std::vector<std::string>& rosterFolders() {
    static const std::vector<std::string> v = {"Annie",   "Beowulf",  "BigBand",     "BlackDahlia", "BossMarie", "Cerebella", "Double",
                                               "Eliza",   "Filia",    "Fukua",       "Marie",       "MsFortune", "Painwheel", "Parasoul",
                                               "Peacock", "RoboFortune", "Squigly", "Umbrella",    "Valentine"};
    return v;
}

int organise(const std::string& palDir, std::vector<std::string>* log) {
    std::error_code ec;
    if (palDir.empty() || !fs::is_directory(palDir, ec)) return 0;
    std::vector<fs::path> loose;
    for (fs::directory_iterator it(palDir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && lowerExt(it->path()) == ".pal") loose.push_back(it->path());
    }
    std::sort(loose.begin(), loose.end());
    int moved = 0;
    for (const fs::path& p : loose) {
        std::vector<uint8_t> bytes;
        if (std::FILE* f = std::fopen(p.string().c_str(), "rb")) {
            uint8_t buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
            std::fclose(f);
        }
        PalFile pf;
        if (bytes.empty() || !parse(bytes.data(), bytes.size(), pf)) continue;
        const std::string want = normaliseName(pf.character);
        const std::string* folder = nullptr;
        for (const std::string& r : rosterFolders())
            if (normaliseName(r) == want) folder = &r;
        if (!folder) {
            if (log) log->push_back("pal: " + p.filename().string() + ": character '" + pf.character + "' is not in the roster, left in place");
            continue;
        }
        const fs::path dir = fs::path(palDir) / *folder;
        fs::create_directories(dir, ec);
        const fs::path dest = dir / p.filename();
        if (fs::exists(dest, ec)) {
            if (log) log->push_back("pal: " + p.filename().string() + ": " + *folder + "/ already has a file with that name, left in place");
            continue;
        }
        fs::rename(p, dest, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(p, dest, ec);
            if (!ec) fs::remove(p, ec);
        }
        if (!ec) {
            ++moved;
            if (log) log->push_back("pal: " + p.filename().string() + " -> " + *folder + "/");
        }
    }
    return moved;
}

Library scan(const std::string& palDir, const std::string& cacheDir) {
    Library lib;
    std::error_code ec;
    if (palDir.empty() || !fs::is_directory(palDir, ec)) return lib;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(palDir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && lowerExt(it->path()) == ".pal") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    if (!cacheDir.empty()) fs::create_directories(cacheDir, ec);
    for (const fs::path& p : files) {
        const std::string label = p.stem().string();
        std::vector<uint8_t> bytes;
        std::FILE* f = std::fopen(p.string().c_str(), "rb");
        if (f) {
            uint8_t buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
            std::fclose(f);
        }
        PalFile pf;
        std::string err;
        if (bytes.empty()) {
            lib.warnings.push_back(p.filename().string() + ": cannot read");
            continue;
        }
        if (!parse(bytes.data(), bytes.size(), pf, &err)) {
            lib.warnings.push_back(p.filename().string() + ": " + err);
            continue;
        }
        std::vector<uint8_t> dds;
        if (!toDds(pf, dds, &err)) {
            lib.warnings.push_back(p.filename().string() + ": " + err);
            continue;
        }
        Entry e;
        e.character = normaliseName(pf.character);
        e.label = label;
        e.file = p.string();
        e.line = pf.line;
        e.shadow1 = pf.shadow1;
        e.shadow2 = pf.shadow2;
        if (cacheDir.empty()) {
            lib.warnings.push_back(p.filename().string() + ": no cache folder to bake the palette into");
            continue;
        }
        std::string safe;
        for (char c : label) safe.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_');
        e.dds = (fs::path(cacheDir) / (e.character + "__" + safe + ".dds")).string();
        std::FILE* o = std::fopen(e.dds.c_str(), "wb");
        if (!o || std::fwrite(dds.data(), 1, dds.size(), o) != dds.size()) {
            if (o) std::fclose(o);
            lib.warnings.push_back(p.filename().string() + ": cannot write " + e.dds);
            continue;
        }
        std::fclose(o);
        lib.entries.push_back(std::move(e));
    }
    std::sort(lib.entries.begin(), lib.entries.end(), [](const Entry& a, const Entry& b) {
        return a.character != b.character ? a.character < b.character : a.label < b.label;
    });
    return lib;
}

}  // namespace palmod
}  // namespace zengine
