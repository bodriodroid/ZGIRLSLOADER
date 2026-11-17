// zdroid_hudart - writes a palettized HUD portrait (src/core/hud_portrait.cpp) as .png, for looking at it.
//   zdroid_hudart <outdir> --portrait <char> <pcxdir> <palette.dds>   one palettized HUD portrait
//   zdroid_hudart <outdir> --rows <char> <pcxdir> <palette.dds>       which palette row each used value of the colours layer got
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "art.h"
#include "hud_art.h"
#include "colour_edit.h"
#include "palmod.h"

using namespace zengine;

static bool readAll(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
static void writePng(const std::string& path, const hudart::Image& im) {
    const std::vector<uint8_t> png = encodePngRgba(im.rgba.data(), im.w, im.h);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    std::printf("%s  %dx%d\n", path.c_str(), im.w, im.h);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: zdroid_hudart <outdir> [--portrait <char> <pcxdir> <palette.dds>]\n");
        return 2;
    }
    const std::string out = argv[1];
    if (argc >= 6 && std::string(argv[2]) == "--portrait") {
        const std::string ch = argv[3], dir = argv[4];
        std::vector<uint8_t> a, b, c, pd;
        if (!readAll(dir + "/character_" + ch + "_colors.pcx", a) || !readAll(dir + "/character_" + ch + "_lines.pcx", b) ||
            !readAll(dir + "/character_" + ch + "_shade.pcx", c) || !readAll(argv[5], pd)) {
            std::fprintf(stderr, "cannot read the portrait layers or the palette\n");
            return 1;
        }
        hudart::Pcx pc, pl, ps;
        Palette pal;
        std::string e;
        if (!hudart::decodePcx(a.data(), a.size(), pc, &e) || !hudart::decodePcx(b.data(), b.size(), pl, &e) ||
            !hudart::decodePcx(c.data(), c.size(), ps, &e) || !readDds(pd.data(), pd.size(), pal, &e)) {
            std::fprintf(stderr, "%s\n", e.c_str());
            return 1;
        }
        hudart::Image im;
        if (!hudart::composePortrait(pc, pl, ps, pal, im, palmod::regionCounts(palmod::normaliseName(ch)))) return 1;
        writePng(out + "/portrait_" + ch + ".png", im);
        return 0;
    }
    if (argc >= 6 && std::string(argv[2]) == "--rows") {
        const std::string ch = argv[3], dir = argv[4];
        std::vector<uint8_t> a, pd;
        hudart::Pcx pc;
        Palette pal;
        std::string e;
        if (!readAll(dir + "/character_" + ch + "_colors.pcx", a) || !readAll(argv[5], pd) || !hudart::decodePcx(a.data(), a.size(), pc, &e) ||
            !readDds(pd.data(), pd.size(), pal, &e)) {
            std::fprintf(stderr, "cannot read the colours layer or the palette %s\n", e.c_str());
            return 1;
        }
        const std::vector<int>* table = palmod::regionCounts(palmod::normaliseName(ch));
        const std::vector<uint8_t> rows = hudart::portraitRows(pc, pal, table);
        std::vector<int> count(256, 0);
        for (uint8_t v : pc.idx) ++count[v];
        std::printf("%s: %d bands, table %s\n", ch.c_str(), pal.bands, table ? (static_cast<int>(table->size()) + 1 == pal.bands ? "matches" : "does not match the palette") : "none");
        for (int v = 0; v < 256; ++v)
            if (count[static_cast<size_t>(v)]) std::printf("  value %3d  %6d px  -> row %3d\n", v, count[static_cast<size_t>(v)], rows[static_cast<size_t>(v)]);
        if (argc >= 7 && std::string(argv[6]) == "--fits")
            for (int r = 1; r < pal.bands; ++r) {
                std::printf("  row %3d table %s  fits", r, table && static_cast<size_t>(r - 1) < table->size() ? std::to_string((*table)[static_cast<size_t>(r - 1)]).c_str() : "-");
                for (int n : coledit::keyCountCandidates(pal, r)) std::printf(" %d", n);
                std::printf("\n");
            }
        return 0;
    }
    std::fprintf(stderr, "usage: zdroid_hudart <outdir> --portrait|--rows <char> <pcxdir> <palette.dds>\n");
    return 2;
}
