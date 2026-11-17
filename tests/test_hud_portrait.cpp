// test_hud_portrait.cpp - the palettized HUD portraits (src/core/hud_art.h): which palette row a value of the colours layer stands for, and the
// tone / outline maths of the game's palettized shader.  Synthetic palettes only (no game files).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "art.h"
#include "colour_edit.h"
#include "hud_art.h"

using namespace zengine;
using namespace zengine::hudart;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(c)) {                                                    \
            ++g_fail;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
        }                                                              \
    } while (0)

namespace {
// A region of `n` keys that no smaller number of straight pieces reproduces (so the palette itself tells how many keys it has).
coledit::Region region(int n, int seed) {
    coledit::Region r;
    for (int k = 0; k < n; ++k) {
        const int a = (seed * 53 + k * 97 + (k % 2) * 61) % 200, b = (seed * 29 + k * 71 + (k % 3) * 43) % 200, c = (seed * 17 + k * 131) % 200;
        r.key.push_back({static_cast<uint8_t>(40 + a), static_cast<uint8_t>(40 + b), static_cast<uint8_t>(40 + c)});
    }
    return r;
}

Palette paletteOf(const std::vector<int>& counts) {
    coledit::Scheme s;
    s.character = "test";
    s.tones = 16;
    for (size_t i = 0; i < counts.size(); ++i) s.regions.push_back(region(counts[i], static_cast<int>(i) + 1));
    return coledit::bake(s);
}

Pcx layer(const std::vector<int>& values) {
    Pcx p;
    p.w = static_cast<int>(values.size());
    p.h = 1;
    for (int v : values) p.idx.push_back(static_cast<uint8_t>(v));
    return p;
}

// the first key of every region when row 0 has 3 keys and the regions have `counts` keys
std::vector<int> starts(const std::vector<int>& counts) {
    std::vector<int> out;
    int s = 3;
    for (int n : counts) {
        out.push_back(s);
        s += n;
    }
    return out;
}
}  // namespace

int main() {
    // ---- every region has 5 keys: the values are 3, 8, 13 ... and the old "value / 5 + 1" is only a coincidence of that ----------------
    {
        const std::vector<int> counts(8, 5);
        const Palette pal = paletteOf(counts);
        std::vector<int> vals = {0};
        for (int s : starts(counts)) vals.push_back(s);
        const Pcx c = layer(vals);
        for (const std::vector<int>* t : {static_cast<const std::vector<int>*>(nullptr), &counts}) {   // with the table and from the palette alone
            const std::vector<uint8_t> rows = portraitRows(c, pal, t);
            CHECK(rows[0] == 0);
            for (size_t r = 0; r < counts.size(); ++r) CHECK(rows[static_cast<size_t>(starts(counts)[r])] == r + 1);
            CHECK(rows[9] == 2);    // inside the span of region 2 (keys 8..12)
            CHECK(rows[12] == 2);
            CHECK(rows[13] == 3);
        }
    }
    // ---- regions of different sizes ---------------------------------------------------------------------------------------------------
    {
        const std::vector<int> counts = {2, 5, 3, 8, 1, 5, 16, 4};
        const Palette pal = paletteOf(counts);
        const std::vector<int> st = starts(counts);
        const Pcx c = layer(std::vector<int>(st.begin(), st.end()));
        const std::vector<uint8_t> rows = portraitRows(c, pal, &counts);
        for (size_t r = 0; r < counts.size(); ++r) CHECK(rows[static_cast<size_t>(st[r])] == r + 1);
    }
    // ---- a table of another build: region 2 and 3 really have 8 keys, the table says 5 -------------------------------------------------
    {
        const std::vector<int> real = {5, 8, 8, 5, 5};
        const std::vector<int> wrongTable = {5, 5, 5, 5, 5};
        const Palette pal = paletteOf(real);
        const std::vector<int> st = starts(real);   // 3, 8, 16, 24, 29
        const Pcx c = layer(std::vector<int>(st.begin(), st.end()));
        const std::vector<uint8_t> rows = portraitRows(c, pal, &wrongTable);
        for (size_t r = 0; r < real.size(); ++r) CHECK(rows[static_cast<size_t>(st[r])] == r + 1);   // the table alone would say row 5 for 24
        // a table that does not even have the palette's size is ignored
        const std::vector<int> shortTable = {5, 8, 8};
        const std::vector<uint8_t> rows2 = portraitRows(c, pal, &shortTable);
        for (size_t r = 0; r < real.size(); ++r) CHECK(rows2[static_cast<size_t>(st[r])] == r + 1);
    }
    // ---- a recoloured palette (flat rows: any key count fits) takes its rows from the stock palette ------------------------------------
    {
        const std::vector<int> real = {5, 8, 8, 5, 5};
        const std::vector<int> wrongTable = {5, 5, 5, 5, 5};
        const Palette stock = paletteOf(real);
        Palette flat = stock;
        for (int b = 0; b < flat.bands; ++b)
            for (int t = 0; t < flat.tones; ++t) {
                uint8_t* c = const_cast<uint8_t*>(flat.at(b, t));
                c[0] = static_cast<uint8_t>(20 * b + 20);
                c[1] = c[2] = 0;
            }
        Pcx colors = layer({24, 24}), lines = layer({255, 255}), shade = layer({255, 255});
        Image withShape, without;
        CHECK(composePortrait(colors, lines, shade, flat, withShape, &wrongTable, &stock));
        CHECK(composePortrait(colors, lines, shade, flat, without, &wrongTable));
        CHECK(withShape.rgba[0] == 20 * 4 + 20);   // value 24 is region 4 in the stock palette
        CHECK(without.rgba[0] == 20 * 5 + 20);     // the table alone, nothing in the flat rows to argue with it
    }
    // ---- flat regions (one key each) ---------------------------------------------------------------------------------------------------
    {
        const std::vector<int> counts(30, 1);
        const Palette pal = paletteOf(counts);
        std::vector<int> vals;
        for (int v = 3; v < 33; ++v) vals.push_back(v);
        const std::vector<uint8_t> rows = portraitRows(layer(vals), pal, nullptr);
        for (int r = 0; r < 30; ++r) CHECK(rows[static_cast<size_t>(3 + r)] == r + 1);
    }
    // ---- more keys than the 8-bit layer can number: the values are region numbers ----------------------------------------------------
    {
        const std::vector<int> counts(40, 16);
        const Palette pal = paletteOf(counts);
        const std::vector<uint8_t> rows = portraitRows(layer({0, 1, 2, 17, 40}), pal, nullptr);
        CHECK(rows[1] == 1 && rows[2] == 2 && rows[17] == 17 && rows[40] == 40);
    }
    // ---- the shader's tone and outline ------------------------------------------------------------------------------------------------
    {
        const std::vector<int> counts = {5, 5};
        Palette pal = paletteOf(counts);
        pal.rgb[0] = 10;   // row 0: the outline colour
        pal.rgb[1] = 20;
        pal.rgb[2] = 30;
        Pcx colors, lines, shade;
        colors.w = lines.w = shade.w = 5;
        colors.h = lines.h = shade.h = 1;
        //                     background  outline 1/2   region 1, shade 100   region 1, shade 255   region 1, shade 255 with a strong line
        colors.idx = {0, 0, 3, 3, 3};
        lines.idx = {255, 128, 255, 255, 0};
        shade.idx = {255, 255, 100, 255, 255};
        Image img;
        CHECK(composePortrait(colors, lines, shade, pal, img));
        CHECK(img.w == 5 && img.h == 1 && img.rgba.size() == 20);
        CHECK(img.rgba[3] == 0);                                      // background: transparent
        CHECK(img.rgba[4 + 3] > 100 && img.rgba[4 + 3] < 160);        // the outline layer: as opaque as the line is strong (8/16)
        CHECK(img.rgba[4] == 10 && img.rgba[5] == 20 && img.rgba[6] == 30);
        // shade 100 is tone 0.5: the middle of tones 0 and 1 (the darkest two), shade 255 is tone 14.5 (the lightest is never reached)
        const uint8_t* t0 = pal.at(1, 0);
        const uint8_t* t1 = pal.at(1, 1);
        for (int ch = 0; ch < 3; ++ch) CHECK(std::abs(img.rgba[8 + ch] - (t0[ch] + t1[ch]) / 2.0) <= 1.5);
        CHECK(img.rgba[8 + 3] == 255);
        const uint8_t* t14 = pal.at(1, 14);
        const uint8_t* t15 = pal.at(1, 15);
        for (int ch = 0; ch < 3; ++ch) CHECK(std::abs(img.rgba[12 + ch] - (t14[ch] + t15[ch]) / 2.0) <= 1.5);
        // a full outline mixes the outline colour all the way in
        CHECK(img.rgba[16] == 10 && img.rgba[17] == 20 && img.rgba[18] == 30 && img.rgba[19] == 255);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
