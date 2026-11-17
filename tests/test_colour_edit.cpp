// test_colour_edit.cpp - the player's own colours for a fighter (src/core/colour_edit.*): palette <-> key colours, HSV changes that keep the
// shading, the baked palette texture and the favourite's text file.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "art.h"
#include "colour_edit.h"
#include "fav_colours.h"
#include "palmod.h"

using namespace zengine;
using namespace zengine::coledit;

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
int dist(const Rgb& a, const Rgb& b) {
    int d = 0;
    for (size_t i = 0; i < 3; ++i) d = std::max(d, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
    return d;
}

Scheme sample() {
    Scheme s;
    s.character = "filia";
    s.name = "Test";
    s.tones = 16;
    Region a;   // a red ramp of 5 keys, lightest first
    a.key = {{250, 190, 180}, {230, 110, 100}, {190, 40, 40}, {120, 20, 30}, {50, 10, 20}};
    Region b;   // a blue one of 3 keys
    b.key = {{200, 210, 255}, {80, 100, 220}, {20, 30, 90}};
    Region c;   // a flat grey
    c.key = {{128, 128, 128}};
    Region d;   // two keys
    d.key = {{255, 240, 200}, {120, 90, 60}};
    s.regions = {a, b, c, d};
    s.outline = {0, 0, 0};
    return s;
}

int worstRamp(const Region& a, const Region& b) {
    std::vector<Rgb> ra, rb;
    rampOf(a, 16, ra);
    rampOf(b, 16, rb);
    int w = 0;
    for (size_t t = 0; t < ra.size(); ++t) w = std::max(w, dist(ra[t], rb[t]));
    return w;
}

std::vector<uint8_t> readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
}  // namespace

int main() {
    // ---- HSV
    {
        const Rgb cs[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {128, 64, 200}, {12, 200, 99}, {250, 250, 10}};
        for (const Rgb& c : cs) CHECK(dist(fromHsv(toHsv(c)), c) <= 1);
        CHECK(std::fabs(toHsv({255, 0, 0}).h - 0.0f) < 0.01f);
        CHECK(std::fabs(toHsv({0, 255, 0}).h - 120.0f) < 0.01f);
        CHECK(std::fabs(toHsv({0, 0, 255}).h - 240.0f) < 0.01f);
        CHECK(toHsv({90, 90, 90}).s == 0.0f);
        CHECK(dist(fromHsv({-120.0f, 1.0f, 1.0f}), {0, 0, 255}) <= 1);   // a negative hue wraps
    }
    // ---- the tones of a region: tone 0 is the darkest key, the last tone the lightest
    {
        const Scheme s = sample();
        std::vector<Rgb> r;
        rampOf(s.regions[0], 16, r);
        CHECK(r.size() == 16);
        CHECK(dist(r[0], {50, 10, 20}) == 0);
        CHECK(dist(r[15], {250, 190, 180}) <= 1);
        rampOf(s.regions[2], 16, r);
        CHECK(dist(r[0], {128, 128, 128}) == 0 && dist(r[9], {128, 128, 128}) == 0);
        rampOf(Region{}, 16, r);
        CHECK(r.size() == 16 && dist(r[3], {0, 0, 0}) == 0);   // no keys: black, no crash
    }
    // ---- a palette baked from keys gives the same keys back (with the counts, and without)
    {
        Scheme s = sample();
        s.litAlpha.assign(5 * 16, 255);
        for (int t = 0; t < 16; ++t) s.litAlpha[2 * 16 + t] = 0;   // region 2 (row 2... the blue one) takes no light
        const Palette p = bake(s);
        CHECK(p.bands == 5 && p.tones == 16 && p.rgb.size() == 5u * 16 * 3 && p.alpha.size() == 80);
        CHECK(p.at(0, 3)[0] == 0 && p.at(1, 0)[0] == 50 && p.at(1, 0)[2] == 20);
        CHECK(!p.litAt(2, 4) && p.litAt(1, 4));
        const std::vector<int> counts = {5, 3, 1, 2};
        Scheme back;
        CHECK(fromPalette(p, &counts, back));
        CHECK(back.regions.size() == 4 && back.tones == 16);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(back.regions[i].key.size() == s.regions[i].key.size());
            CHECK(worstRamp(back.regions[i], s.regions[i]) <= 2);
        }
        CHECK(back.litAlpha == s.litAlpha);
        Scheme free;
        CHECK(fromPalette(p, nullptr, free));   // no table: the smallest key count that reproduces each row
        CHECK(free.regions[2].key.size() == 1);
        CHECK(free.regions[3].key.size() == 2);
        CHECK(free.regions[1].key.size() <= 3 && free.regions[0].key.size() <= 5);
        for (size_t i = 0; i < 4; ++i) CHECK(worstRamp(free.regions[i], s.regions[i]) <= 4);
        const std::vector<int> wrong = {5, 3};   // a table of another size is ignored
        Scheme w;
        CHECK(fromPalette(p, &wrong, w) && w.regions.size() == 4 && worstRamp(w.regions[0], s.regions[0]) <= 4);
        Palette tiny;
        CHECK(!fromPalette(tiny, nullptr, w));
    }
    // ---- the same maths as palmod's baked .pal rows
    {
        palmod::PalFile pal;
        pal.character = "Filia";
        const std::vector<int>* counts = palmod::regionCounts("filia");
        CHECK(counts && counts->size() == 24);
        if (counts) {
            for (size_t r = 0; r < counts->size(); ++r)
                for (int k = 0; k < (*counts)[r]; ++k)
                    pal.colors.push_back(0xFF000000u | static_cast<uint32_t>((r * 9 + k * 40) & 255) << 16 | static_cast<uint32_t>((r * 31 + k * 7) & 255) << 8 |
                                         static_cast<uint32_t>((200 - r * 5 - k * 20) & 255));
            pal.numColors = static_cast<uint32_t>(pal.colors.size() + 1);
            Palette ref;
            CHECK(palmod::toPalette(pal, ref));
            Scheme s;
            CHECK(fromPalette(ref, counts, s));
            const Palette again = bake(s);
            CHECK(again.bands == ref.bands && again.tones == ref.tones);
            int worst = 0;
            for (size_t i = 0; i < ref.rgb.size(); ++i) worst = std::max(worst, std::abs(static_cast<int>(ref.rgb[i]) - static_cast<int>(again.rgb[i])));
            CHECK(worst <= 2);
        }
    }
    // ---- the .dds written for a palette reads back the same
    {
        Scheme s = sample();
        s.litAlpha.assign(80, 255);
        for (int t = 0; t < 16; ++t) s.litAlpha[3 * 16 + t] = 7;
        const Palette p = bake(s);
        const std::vector<uint8_t> dds = toDds(p);
        Palette q;
        std::string err;
        CHECK(readDds(dds.data(), dds.size(), q, &err));
        CHECK(q.tones == p.tones && q.bands == p.bands && q.rgb == p.rgb);
        CHECK(q.alpha.size() == p.alpha.size() && !q.litAt(3, 0) && q.litAt(1, 0));
        Palette noAlpha = p;
        noAlpha.alpha.clear();
        const std::vector<uint8_t> d2 = toDds(noAlpha);
        Palette q2;
        CHECK(readDds(d2.data(), d2.size(), q2, &err) && q2.rgb == p.rgb && q2.litAt(0, 0) && q2.litAt(4, 15));
    }
    // ---- changes keep the shading
    {
        Scheme s = sample();
        Region r = s.regions[0];
        adjust(r, 120.0f, 1.0f, 1.0f);   // red -> green family
        const Hsv m = toHsv(mainKey(r));
        CHECK(std::fabs(m.h - 120.0f) < 4.0f);
        CHECK(toHsv(r.key.front()).v >= toHsv(r.key.back()).v);   // still light -> dark
        Region g = s.regions[0];
        adjust(g, 0, 0.0f, 1.0f);   // no saturation: greys of the same lightness
        for (const Rgb& k : g.key) CHECK(k[0] == k[1] && k[1] == k[2]);
        Region v = s.regions[0];
        adjust(v, 0, 1.0f, 0.5f);
        CHECK(toHsv(mainKey(v)).v < toHsv(mainKey(s.regions[0])).v * 0.55f);
        // setMain: the main key becomes the chosen colour exactly, the others follow
        Region t = s.regions[1];
        const Rgb want{40, 200, 90};
        const Rgb before = t.key.front();
        setMain(t, want);
        CHECK(dist(mainKey(t), want) == 0);
        CHECK(dist(t.key.front(), before) > 20);
        CHECK(toHsv(t.key.front()).v >= toHsv(t.key.back()).v);
        CHECK(std::fabs(toHsv(t.key.front()).h - toHsv(want).h) < 40.0f);
        // a grey region taking a colour gets that hue
        Region gr = s.regions[2];
        setMain(gr, {200, 60, 60});
        CHECK(dist(mainKey(gr), {200, 60, 60}) == 0);
        Region gr2;
        gr2.key = {{240, 240, 240}, {128, 128, 128}, {20, 20, 20}};
        setMain(gr2, {60, 90, 220});
        CHECK(std::fabs(toHsv(gr2.key.front()).h - toHsv({60, 90, 220}).h) < 3.0f && toHsv(gr2.key.front()).v > toHsv(gr2.key.back()).v);
        Region none;
        setMain(none, {1, 2, 3});   // nothing to change, no crash
        adjust(none, 10, 1, 1);
        CHECK(none.key.empty());
        // the whole character
        Scheme all = sample();
        const Rgb outlineBefore = all.outline;
        adjustAll(all, 90.0f, 1.0f, 1.0f);
        CHECK(all.outline == outlineBefore);
        CHECK(std::fabs(toHsv(mainKey(all.regions[0])).h - std::fmod(toHsv(mainKey(sample().regions[0])).h + 90.0f, 360.0f)) < 5.0f);
        CHECK(dist(mainKey(all.regions[2]), {128, 128, 128}) <= 1);   // a grey stays grey
    }
    // ---- random colours: same seed same colours, other seeds other ones, greys stay
    {
        Scheme a = sample(), b = sample(), c = sample();
        randomise(a, 7);
        randomise(b, 7);
        randomise(c, 8);
        CHECK(serialize(a) == serialize(b));
        CHECK(serialize(a) != serialize(c));
        CHECK(serialize(a) != serialize(sample()));
        CHECK(dist(mainKey(a.regions[2]), {128, 128, 128}) == 0);
        for (const Region& r : a.regions)
            for (const Rgb& k : r.key) CHECK(k.size() == 3);
    }
    // ---- the favourite's file
    {
        Scheme s = sample();
        s.name = "Mi color  favorito";
        s.outline = {10, 20, 30};
        const std::string text = serialize(s);
        Scheme t;
        std::string err;
        CHECK(parse(text, t, &err));
        CHECK(t.character == "filia" && t.name == "Mi color  favorito" && t.tones == 16 && t.outline == s.outline);
        CHECK(t.regions.size() == s.regions.size());
        for (size_t i = 0; i < t.regions.size(); ++i) CHECK(t.regions[i].key == s.regions[i].key);
        CHECK(parse("# comment\n\nZGCOL 1\ncharacter peacock\nr 2 ff0000 00ff00\n", t, &err) && t.character == "peacock" && t.regions.size() == 1);
        CHECK(!parse("", t, &err));
        CHECK(!parse("ZGCOL 2\ncharacter a\nr 1 ffffff\n", t, &err));
        CHECK(!parse("ZGCOL 1\nr 1 ffffff\n", t, &err));          // no character
        CHECK(!parse("ZGCOL 1\ncharacter a\n", t, &err));         // no regions
        CHECK(!parse("ZGCOL 1\ncharacter a\nr 2 ffffff\n", t, &err));   // fewer keys than it says
        CHECK(!parse("ZGCOL 1\ncharacter a\nr 1 gg0000\n", t, &err));
        CHECK(!parse("ZGCOL 1\ncharacter a\nr 99 ffffff\n", t, &err));
        CHECK(!parse("ZGCOL 1\ncharacter a\ntones 1\nr 1 ffffff\n", t, &err));
        CHECK(slug("Mi color 1") == "mi_color_1");
        CHECK(slug("  --Rojo!! ") == "rojo");
        CHECK(slug("***") == "colour");
        CHECK(slug("") == "colour");
    }
    // ---- favourites on disk
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("zg_fav_test_" + std::to_string(std::rand()));
        fs::remove_all(dir);
        Favourites fav(dir.string());
        CHECK(fav.enabled() && !Favourites().enabled());
        Scheme s = sample();
        s.litAlpha.assign(80, 255);
        for (int t = 0; t < 16; ++t) s.litAlpha[16 * 2 + t] = 0;
        s.name = "Mi Rojo";
        const Palette baked = bake(s);
        Fav a, b, c;
        std::string err;
        CHECK(fav.save(s, baked, "", a, &err));
        CHECK(fs::exists(a.file) && fs::exists(a.dds) && a.name == "Mi Rojo");
        CHECK(fav.save(s, baked, "", b, &err) && b.file != a.file);   // the same name does not overwrite
        s.name = "Azul";
        CHECK(fav.save(s, baked, "", c, &err) && fs::path(c.file).stem() == "azul");
        s.name = "Mi Rojo (v2)";
        Fav again;
        CHECK(fav.save(s, baked, a.file, again, &err) && again.file == a.file);   // over the same file
        const Palette lit = baked;
        std::vector<Fav> l = fav.list("filia", &lit);
        CHECK(l.size() == 3);
        CHECK(l.size() == 3 && l[0].name == "Azul" && l[1].name == "Mi Rojo (v2)");
        CHECK(fav.list("peacock", &lit).empty());
        CHECK(fav.list("", &lit).empty());
        // the texture is read back by the game's palette reader, with its lit flags
        {
            const std::vector<uint8_t> b2 = readAll(a.dds);
            Palette q;
            CHECK(readDds(b2.data(), b2.size(), q) && q.bands == 5 && !q.litAt(2, 3) && q.litAt(1, 3));
        }
        // a lost texture is baked again from the keys with the character's own lit flags
        fs::remove(l[0].dds);
        l = fav.list("filia", &lit);
        CHECK(l.size() == 3 && fs::exists(l[0].dds));
        fs::remove(l[0].dds);
        CHECK(fav.list("filia", nullptr).size() == 2);   // no way to bake it: skipped, not a crash
        Palette other = lit;
        other.bands = 9;
        CHECK(fav.list("filia", &other).size() == 2);      // data of another build: skipped
        // a file that is not a favourite is ignored
        {
            std::ofstream junk(dir / "filia" / "junk.zgcol");
            junk << "hello\n";
        }
        CHECK(fav.list("filia", &lit).size() == 3);
        Scheme back;
        CHECK(Favourites::load(c.file, back) && back.name == "Azul" && back.regions.size() == 4);
        CHECK(!Favourites::load((dir / "nope.zgcol").string(), back, &err) && !err.empty());
        CHECK(fav.remove(c) && !fs::exists(c.file) && !fs::exists(c.dds));
        CHECK(fav.list("filia", &lit).size() == 2);
        Favourites off;
        CHECK(!off.save(s, baked, "", c, &err));
        fs::remove_all(dir);
    }
    // ---- real palettes (ZDROID_TEST_PALS = a folder with <name>_1p.dds): every region fits its row
    if (const char* dir = std::getenv("ZDROID_TEST_PALS")) {
        const char* names[] = {"annie", "beowulf", "bigband", "blackdahlia", "cerebella", "double", "eliza", "filia", "fukua",
                               "marie", "msfortune", "painwheel", "parasoul", "peacock", "robofortune", "squigly", "umbrella", "valentine"};
        for (const char* n : names) {
            const std::vector<uint8_t> b = readAll(std::string(dir) + "/" + n + "_1p.dds");
            Palette p;
            if (b.empty() || !readDds(b.data(), b.size(), p)) continue;
            Scheme with, without;
            CHECK(fromPalette(p, palmod::regionCounts(n), with));
            CHECK(fromPalette(p, nullptr, without));
            const Palette back = bake(with), back2 = bake(without);
            int w1 = 0, w2 = 0;
            for (size_t i = 0; i < p.rgb.size() && i < back.rgb.size(); ++i) w1 = std::max(w1, std::abs(static_cast<int>(p.rgb[i]) - static_cast<int>(back.rgb[i])));
            for (size_t i = 0; i < p.rgb.size() && i < back2.rgb.size(); ++i) w2 = std::max(w2, std::abs(static_cast<int>(p.rgb[i]) - static_cast<int>(back2.rgb[i])));
            std::printf("%s: %d regions, worst error with table %d, without %d\n", n, static_cast<int>(with.regions.size()), w1, w2);
            CHECK(back2.bands == p.bands && back2.alpha == p.alpha);
            CHECK(w2 <= 24);
            CHECK(w1 <= 4);   // a table that does not fit a region falls back to the search for that region
        }
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
