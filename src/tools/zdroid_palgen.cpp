// zdroid_palgen - makes themed character palettes (*.pal) from the palette textures of YOUR game data.
//
//   zdroid_palgen --sprites <folder with <name>_1p.dds> --out <folder> [--theme claude|dark|all] [--only Name[,Name]] [--swatch file.png]
//
// For every character of the region table (src/core/pal_regions.inc) it reads <name>_1p.dds, recolours the regions with the theme
// (src/core/palgen.cpp: black ink, greys, white, orange eyes) and writes <out>/<Character>/<Theme>.pal - the folder layout of mods/pal, so the result can be copied
// straight into <game folder>/mods/pal.  The files are derived from game data: keep them for yourself, do not publish them.
// A character whose texture does not have the rows of the region table (Squigly and Marie in some builds) is skipped with a line saying why.
// --swatch writes a picture of the original tones (left) and the recoloured ones (right) of every region of every character.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "art.h"
#include "palgen.h"
#include "palmod.h"
#include "vfs.h"

using namespace zengine;
namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            if (!cur.empty()) v.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

// <dir>/<name>_1p.dds, also inside a "sprites" folder of it
std::string findTexture(const std::string& dir, const std::string& name) {
    const std::string file = lower(name) + "_1p.dds";
    for (const fs::path& p : {fs::path(dir) / file, fs::path(dir) / "sprites" / file, fs::path(dir) / "data" / "sprites" / file}) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return p.string();
    }
    return {};
}

struct Strip {
    std::string name;
    Palette orig, themed;
};

}  // namespace

int main(int argc, char** argv) {
    std::string sprites, outDir = "pal_out", swatch, themeArg = "all";
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 < argc) dst = argv[++i];
        };
        if (a == "--sprites") next(sprites);
        else if (a == "--out") next(outDir);
        else if (a == "--theme") next(themeArg);
        else if (a == "--swatch") next(swatch);
        else if (a == "--only") {
            std::string v;
            next(v);
            only = split(v, ',');
        } else {
            std::printf("unknown argument '%s'\n", a.c_str());
            return 2;
        }
    }
    if (sprites.empty()) {
        std::printf(
            "usage: zdroid_palgen --sprites <folder with <name>_1p.dds> --out <folder> [--theme claude|dark|all] [--only Name,Name] "
            "[--swatch file.png]\n");
        return 2;
    }
    std::vector<palgen::Theme> themes;
    if (themeArg == "claude" || themeArg == "all") themes.push_back(palgen::Theme::Claude);
    if (themeArg == "dark" || themeArg == "all") themes.push_back(palgen::Theme::ClaudeDark);
    if (themes.empty()) {
        std::printf("--theme must be claude, dark or all\n");
        return 2;
    }

    int made = 0, skipped = 0;
    std::vector<Strip> strips;
    for (const std::string& name : palmod::rosterFolders()) {
        if (!palmod::regionCounts(palmod::normaliseName(name))) continue;  // e.g. BossMarie: no table of her own
        if (!only.empty() && std::find_if(only.begin(), only.end(), [&](const std::string& o) {
                                 return palmod::normaliseName(o) == palmod::normaliseName(name);
                             }) == only.end())
            continue;
        const std::string tex = findTexture(sprites, name);
        if (tex.empty()) {
            std::printf("%-12s no %s_1p.dds in %s\n", name.c_str(), lower(name).c_str(), sprites.c_str());
            ++skipped;
            continue;
        }
        std::vector<uint8_t> bytes;
        Palette src;
        std::string err;
        if (!readFile(tex, bytes) || !readDds(bytes.data(), bytes.size(), src, &err)) {
            std::printf("%-12s cannot read %s (%s)\n", name.c_str(), tex.c_str(), err.c_str());
            ++skipped;
            continue;
        }
        for (palgen::Theme th : themes) {
            palmod::PalFile pal;
            palgen::Report rep;
            if (!palgen::generate(name, src, th, pal, &rep, &err)) {
                std::printf("%-12s %-10s skipped: %s\n", name.c_str(), palgen::themeLabel(th), err.c_str());
                ++skipped;
                continue;
            }
            const fs::path dir = fs::path(outDir) / name;
            std::error_code ec;
            fs::create_directories(dir, ec);
            const fs::path file = dir / (std::string(palgen::themeLabel(th)) + ".pal");
            const std::vector<uint8_t> out = palmod::serialize(pal);
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
            if (!f) {
                std::printf("%-12s cannot write %s\n", name.c_str(), file.string().c_str());
                ++skipped;
                continue;
            }
            std::printf("%-12s %-10s %3d regions (black %d, cloth %d, skin %d, white %d, eyes %d, ties %d)  %zu colours  bake error max %.0f mean %.2f\n",
                        name.c_str(), palgen::themeLabel(th), rep.regions, rep.perRole[static_cast<int>(palgen::Role::Black)],
                        rep.perRole[static_cast<int>(palgen::Role::Cloth)], rep.perRole[static_cast<int>(palgen::Role::Skin)],
                        rep.perRole[static_cast<int>(palgen::Role::White)], rep.perRole[static_cast<int>(palgen::Role::Eye)], rep.ties,
                        pal.colors.size(), rep.maxErr, rep.meanErr);
            ++made;
            if (!swatch.empty() && th == themes.front()) {
                Strip s;
                s.name = name;
                s.orig = src;
                if (palmod::toPalette(pal, s.themed, nullptr)) strips.push_back(std::move(s));
            }
        }
    }

    if (!swatch.empty() && !strips.empty()) {
        const int cell = 6, rowH = 3, gap = 12;
        int maxRows = 0;
        for (const Strip& s : strips) maxRows = std::max(maxRows, s.orig.bands);
        const int colW = 16 * cell;
        const int width = static_cast<int>(strips.size()) * (2 * colW + gap) + gap;
        const int height = maxRows * rowH + 2 * gap;
        std::vector<uint8_t> img(static_cast<size_t>(width) * height * 4, 0);
        for (size_t i = 0; i < img.size(); i += 4) {
            img[i] = 0xFA; img[i + 1] = 0xF9; img[i + 2] = 0xF5; img[i + 3] = 0xFF;
        }
        for (size_t si = 0; si < strips.size(); ++si) {
            const Strip& s = strips[si];
            for (int band = 1; band < s.orig.bands; ++band)
                for (int side = 0; side < 2; ++side) {
                    const Palette& p = side == 0 ? s.orig : s.themed;
                    for (int t = 0; t < 16; ++t) {
                        const uint8_t* c = p.at(band, t);
                        for (int y = 0; y < rowH; ++y)
                            for (int x = 0; x < cell; ++x) {
                                const int px = gap + static_cast<int>(si) * (2 * colW + gap) + side * colW + t * cell + x;
                                const int py = gap + (band - 1) * rowH + y;
                                uint8_t* d = &img[(static_cast<size_t>(py) * width + px) * 4];
                                d[0] = c[0]; d[1] = c[1]; d[2] = c[2]; d[3] = 0xFF;
                            }
                    }
                }
        }
        const std::vector<uint8_t> png = encodePngRgba(img.data(), width, height);
        std::ofstream f(swatch, std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        std::printf("swatch: %s (%dx%d)\n", swatch.c_str(), width, height);
    }

    std::printf("%d palette(s) written to %s, %d skipped\n", made, outDir.c_str(), skipped);
    return made > 0 ? 0 : 1;
}
