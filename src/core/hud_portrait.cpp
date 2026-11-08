// hud_portrait.cpp - palettized HUD portraits (see hud_art.h)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "hud_art.h"

namespace zengine {
namespace hudart {

bool decodePcx(const uint8_t* d, size_t size, Pcx& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (size < 128 || d[0] != 0x0A) return fail("not a .pcx file");
    if (d[2] != 1) return fail("uncompressed .pcx is not supported");
    const int bpp = d[3], planes = d[65];
    if (bpp != 8 || planes != 1) return fail("only 8-bit single-plane .pcx is supported");
    const int x0 = d[4] | d[5] << 8, y0 = d[6] | d[7] << 8, x1 = d[8] | d[9] << 8, y1 = d[10] | d[11] << 8;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    const int bpl = d[66] | d[67] << 8;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || bpl < w) return fail("bad .pcx size");
    out.w = w;
    out.h = h;
    out.idx.assign(static_cast<size_t>(w) * h, 0);
    size_t p = 128;
    std::vector<uint8_t> line(static_cast<size_t>(bpl));
    for (int y = 0; y < h; ++y) {
        int n = 0;
        while (n < bpl) {
            if (p >= size) return fail("truncated .pcx");
            uint8_t b = d[p++];
            int run = 1;
            if (b >= 0xC0) {
                run = b & 0x3F;
                if (p >= size) return fail("truncated .pcx");
                b = d[p++];
            }
            for (int k = 0; k < run && n < bpl; ++k) line[static_cast<size_t>(n++)] = b;
        }
        std::memcpy(&out.idx[static_cast<size_t>(y) * w], line.data(), static_cast<size_t>(w));
    }
    return true;
}

bool composePortrait(const Pcx& colors, const Pcx& lines, const Pcx& shade, const Palette& pal, Image& out) {
    if (colors.w <= 0 || colors.w != lines.w || colors.w != shade.w || colors.h != lines.h || colors.h != shade.h) return false;
    if (pal.tones <= 0 || pal.bands <= 0) return false;
    out.w = colors.w;
    out.h = colors.h;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    const int tones = pal.tones;
    for (size_t i = 0; i < colors.idx.size(); ++i) {
        const int idx = colors.idx[i], sh = shade.idx[i], ln = lines.idx[i];
        if (idx == 0 && sh == 255 && ln == 255) continue;   // background
        const int band = std::min(idx / 5 + 1, pal.bands - 1);
        const double t = static_cast<double>(sh) / 255.0 * (tones - 1);
        const int t0 = std::min(static_cast<int>(t), tones - 1);
        const int t1 = std::min(t0 + 1, tones - 1);
        const double fr = t - t0;
        const uint8_t* a = pal.at(band, t0);
        const uint8_t* b = pal.at(band, t1);
        const double k = static_cast<double>(ln) / 255.0;   // the outline darkens what it covers
        uint8_t* o = &out.rgba[i * 4];
        for (int c = 0; c < 3; ++c) o[c] = static_cast<uint8_t>(std::clamp((a[c] * (1 - fr) + b[c] * fr) * k + 0.5, 0.0, 255.0));
        o[3] = 255;
    }
    return true;
}

std::string displayName(const std::string& keyIn) {
    std::string key;
    for (char c : keyIn)
        if (std::isalnum(static_cast<unsigned char>(c))) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    struct Pair { const char* key; const char* name; };
    static const Pair names[] = {
        {"bossmarie", "BLOODY MARIE"}, {"blackdahlia", "BLACK DAHLIA"}, {"msfortune", "MS. FORTUNE"}, {"robofortune", "ROBO-FORTUNE"},
        {"bigband", "BIG BAND"},       {"painwheel", "PAINWHEEL"},      {"parasoul", "PARASOUL"},      {"squigly", "SQUIGLY"},
        {"cerebella", "CEREBELLA"},    {"beowulf", "BEOWULF"},          {"eliza", "ELIZA"},            {"fukua", "FUKUA"},
        {"double", "DOUBLE"},          {"valentine", "VALENTINE"},      {"umbrella", "UMBRELLA"},      {"peacock", "PEACOCK"},
        {"filia", "FILIA"},            {"annie", "ANNIE"},              {"marie", "MARIE"},
    };
    for (const Pair& p : names)
        if (key == p.key) return p.name;
    std::string up = keyIn;
    for (char& c : up) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        if (c == '_') c = ' ';
    }
    return up;
}

}  // namespace hudart
}  // namespace zengine
