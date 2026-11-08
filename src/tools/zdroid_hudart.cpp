// zdroid_hudart - writes a palettized HUD portrait (src/core/hud_portrait.cpp) as .png, for looking at it.
//   zdroid_hudart <outdir> --portrait <char> <pcxdir> <palette.dds>   one palettized HUD portrait
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "art.h"
#include "hud_art.h"

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
        if (!hudart::composePortrait(pc, pl, ps, pal, im)) return 1;
        writePng(out + "/portrait_" + ch + ".png", im);
        return 0;
    }
    std::fprintf(stderr, "usage: zdroid_hudart <outdir> --portrait <char> <pcxdir> <palette.dds>\n");
    return 2;
}
