// test_fighter_light.cpp - the stage's lights on the fighters: the sprite's normal plane (decoded from column differences), the palette's "takes light"
// flag, the maths of ambient / diffuse / rim / point lights, and the baking of all of it into coloured pixels (src/core/fighter_light.*, art.*).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "art.h"
#include "fighter_light.h"
#include "foit.h"
#include "level.h"

using namespace zengine;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(c)) {                                                    \
            ++g_fail;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
        }                                                              \
    } while (0)
#define NEAR(a, b, eps) (std::fabs((a) - (b)) <= (eps))

namespace {
LevelInfo levelOf(const char* lines) { return parseLevel(lines, "t"); }

// a 32-bit DDS palette: `bands` rows of `tones` colours; alpha(band, tone) gives the alpha byte
std::vector<uint8_t> makeDds(int tones, int bands, uint8_t (*alpha)(int, int)) {
    std::vector<uint8_t> d(128 + static_cast<size_t>(tones) * bands * 4, 0);
    auto w32 = [&](size_t o, uint32_t v) { std::memcpy(&d[o], &v, 4); };
    std::memcpy(&d[0], "DDS ", 4);
    w32(4, 124);
    w32(12, static_cast<uint32_t>(bands));
    w32(16, static_cast<uint32_t>(tones));
    w32(76, 32);       // pixel format size
    w32(80, 0x41);     // RGB | ALPHAPIXELS
    w32(88, 32);
    w32(92, 0x00FF0000);
    w32(96, 0x0000FF00);
    w32(100, 0x000000FF);
    w32(104, 0xFF000000);
    for (int b = 0; b < bands; ++b)
        for (int t = 0; t < tones; ++t) {
            const size_t o = 128 + (static_cast<size_t>(b) * tones + t) * 4;
            d[o + 0] = static_cast<uint8_t>(40 + b);   // B
            d[o + 1] = static_cast<uint8_t>(80 + b);   // G
            d[o + 2] = static_cast<uint8_t>(120 + b);  // R
            d[o + 3] = alpha(b, t);
        }
    return d;
}
}  // namespace

int main() {
    // ---- the normal plane: column differences, 16 rows per cell
    {
        std::vector<uint8_t> strip(16 * 16, 0);   // one cell, strip width 16
        for (size_t x = 0; x < 16; ++x) strip[x] = 0x88;           // row 0: the value
        for (size_t x = 0; x < 16; ++x) strip[16 + x] = 0x10;      // row 1: +0x10 (x component one up)
        for (size_t x = 0; x < 16; ++x) strip[32 + x] = 0xFF;      // row 2: -1 (y component one down)
        integrateNormalColumns(strip, 16);
        CHECK(strip[0] == 0x88 && strip[15] == 0x88);
        CHECK(strip[16] == 0x98 && strip[31] == 0x98);
        CHECK(strip[32] == 0x97);
        CHECK(strip[48] == 0x97);   // zero differences keep the value
        // columns are independent, and a second cell next to the first one is too
        std::vector<uint8_t> two(32 * 16, 0);
        two[0] = 5;
        two[32] = 7;            // row 1, column 0
        two[16] = 100;          // cell 2, row 0
        two[32 + 16] = 1;       // cell 2, row 1
        integrateNormalColumns(two, 32);
        CHECK(two[32] == 12 && two[1] == 0 && two[32 + 1] == 0);
        CHECK(two[16] == 100 && two[32 + 16] == 101);
        std::vector<uint8_t> wrap(16 * 16, 0);
        wrap[0] = 250;
        wrap[16] = 10;
        integrateNormalColumns(wrap, 16);
        CHECK(wrap[16] == 4);   // modulo 256
        std::vector<uint8_t> tiny(3, 1);
        integrateNormalColumns(tiny, 16);   // too short: untouched, no crash
        CHECK(tiny[0] == 1);
    }
    // ---- normalOf
    {
        float nx, ny;
        normalOf(0x88, nx, ny);
        CHECK(NEAR(nx, 0, 1e-6) && NEAR(ny, 0, 1e-6));
        normalOf(0xF8, nx, ny);
        CHECK(NEAR(nx, 1.0, 1e-6) && NEAR(ny, 0, 1e-6));
        normalOf(0x8F, nx, ny);
        CHECK(NEAR(nx, 0, 1e-6) && NEAR(ny, 1.0, 1e-6));
        normalOf(0x00, nx, ny);
        CHECK(NEAR(nx, -1.0, 1e-6) && NEAR(ny, -1.0, 1e-6));   // clamped, not -8/7
        normalOf(0x18, nx, ny);
        CHECK(NEAR(nx, -1.0, 1e-6));
    }
    // ---- palette alpha: the "takes light" flag
    {
        auto bytes = makeDds(16, 4, [](int b, int t) -> uint8_t {
            if (b == 0) return 0;          // outline band: no
            if (b == 1) return 255;        // lit everywhere
            if (b == 2) return t < 8 ? 100 : 200;   // lit from tone 8
            return 127;                    // just under half: not lit
        });
        Palette pal;
        std::string err;
        CHECK(readDds(bytes.data(), bytes.size(), pal, &err));
        CHECK(pal.tones == 16 && pal.bands == 4);
        CHECK(pal.alpha.size() == 64);
        CHECK(!pal.litAt(0, 0) && pal.litAt(1, 5) && !pal.litAt(2, 3) && pal.litAt(2, 9) && !pal.litAt(3, 0));
        const std::vector<uint8_t> lit = buildLitTable(pal);
        CHECK(lit.size() == 256u * 256u);
        CHECK(lit[0 * 256 + 100] == 0);   // band 0 never
        CHECK(lit[1 * 256 + 0] == 255 && lit[1 * 256 + 31] == 255);
        CHECK(lit[3 * 256 + 17] == 0);
        // band 2 flips between tone 7 and tone 8: grad low bits (g & 31) / 2 is the tone, so g = 0..13 is below, g = 16.. is above
        CHECK(lit[2 * 256 + 0] == 0 && lit[2 * 256 + 20] == 255);
        CHECK(lit[2 * 256 + 14] == 0 || lit[2 * 256 + 14] == 255);   // between two tones: interpolated then cut, either is fine
        CHECK(lit[5 * 256 + 1] == lit[3 * 256 + 1]);                 // a band beyond the palette copies the last one
        // a palette with no alpha channel (24-bit, or a mask of 0) lights everything but the outline band
        Palette none = pal;
        none.alpha.clear();
        CHECK(none.litAt(0, 0) && none.litAt(3, 3));
        const std::vector<uint8_t> all = buildLitTable(none);
        CHECK(all[0 * 256 + 5] == 0 && all[1 * 256 + 5] == 255 && all[3 * 256 + 200] == 255);
    }
    // ---- a palette made by a tool borrows the character's own lit flags
    {
        Palette base;
        base.tones = 16;
        base.bands = 3;
        base.rgb.assign(48 * 3, 0);
        base.alpha.assign(48, 255);
        for (int t = 0; t < 16; ++t) base.alpha[2 * 16 + t] = 100;   // band 2: no light
        Palette made = base;                                          // same size, opaque everywhere (what palmod bakes)
        made.alpha.assign(48, 255);
        CHECK(inheritLitMask(made, base));
        CHECK(made.litAt(1, 4) && !made.litAt(2, 4));
        Palette noAlpha = base;
        noAlpha.alpha.clear();
        CHECK(inheritLitMask(noAlpha, base) && !noAlpha.litAt(2, 0));
        Palette own = base;                                           // a palette that already says something keeps it
        own.alpha.assign(48, 255);
        own.alpha[16] = 10;
        CHECK(!inheritLitMask(own, base) && !own.litAt(1, 0) && own.litAt(2, 0));
        Palette other = base;                                         // another size: nothing to borrow from
        other.bands = 4;
        other.rgb.assign(64 * 3, 0);
        other.alpha.assign(64, 255);
        CHECK(!inheritLitMask(other, base));
        Palette allLit = base;                                        // a base with no information gives none
        allLit.alpha.assign(48, 255);
        Palette m2 = base;
        m2.alpha.assign(48, 255);
        CHECK(!inheritLitMask(m2, allLit) && m2.litAt(2, 0));
    }
    // ---- the lights of a level
    {
        const LevelInfo lv = levelOf(
            "Light: Amb 85 96 122\n"
            "Light: VitaAmb 250 280 280\n"
            "Light: Dir 255 143 11 -0.634 -0.634 -0.6\n"
            "Light: Dir 10 10 10 0 0 0\n"           // no direction: ignored
            "Light: Pt 175 175 175 1000 1500 200 1000\n");
        const FighterLight f = FighterLight::fromLevel(lv);
        CHECK(NEAR(f.ambient[0], 85 / 255.0, 1e-5) && NEAR(f.ambient[2], 122 / 255.0, 1e-5));   // the PC ambient
        const FighterLight v = FighterLight::fromLevel(lv, FighterLight::Ambient::VitaAmb);
        CHECK(NEAR(v.ambient[0], 250 / 255.0, 1e-5) && v.ambient[1] > 1.0f);                  // may exceed 1
        CHECK(f.dirs.size() == 1 && f.pts.size() == 1);
        CHECK(NEAR(std::sqrt(f.dirs[0].dir[0] * f.dirs[0].dir[0] + f.dirs[0].dir[1] * f.dirs[0].dir[1] + f.dirs[0].dir[2] * f.dirs[0].dir[2]), 1.0, 1e-5));
        CHECK(f.active());
        // only the other ambient line present: it is used
        const FighterLight onlyVita = FighterLight::fromLevel(levelOf("Light: VitaAmb 100 100 100\n"));
        CHECK(NEAR(onlyVita.ambient[0], 100 / 255.0, 1e-5));
        const FighterLight onlyAmb = FighterLight::fromLevel(levelOf("Light: Amb 100 100 100\n"), FighterLight::Ambient::VitaAmb);
        CHECK(NEAR(onlyAmb.ambient[0], 100 / 255.0, 1e-5));
        // nothing: white, and nothing to do
        const FighterLight none = FighterLight::fromLevel(levelOf("Width: 3750\n"));
        CHECK(!none.active() && NEAR(none.ambient[0], 1.0, 1e-6));
        float m[3];
        none.baseMultiplier(m);
        CHECK(NEAR(m[0], 1, 1e-6) && NEAR(m[2], 1, 1e-6));
        // at most three directional lights
        const FighterLight many = FighterLight::fromLevel(
            levelOf("Light: Dir 1 1 1 0 0 -1\nLight: Dir 1 1 1 0 1 0\nLight: Dir 1 1 1 1 0 0\nLight: Dir 1 1 1 0 -1 0\n"));
        CHECK(many.dirs.size() == FighterLight::kMaxDir);
    }
    // ---- diffuse: a light that comes from the camera's side lights the whole sprite, one from behind or beside does not
    {
        const FighterLight f = FighterLight::fromLevel(levelOf("Light: Amb 0 0 0\nLight: Dir 255 0 0 0 0 -1\nLight: Dir 0 255 0 0 0 1\nLight: Dir 0 0 255 1 0 0\n"));
        float m[3];
        f.baseMultiplier(m);
        CHECK(NEAR(m[0], 1.0, 1e-5) && NEAR(m[1], 0.0, 1e-5) && NEAR(m[2], 0.0, 1e-5));
        const FighterLight half = FighterLight::fromLevel(levelOf("Light: Amb 51 51 51\nLight: Dir 255 255 255 0 0.8 -0.6\n"));
        half.baseMultiplier(m);
        CHECK(NEAR(m[0], 0.2 + 0.6, 1e-5));
    }
    // ---- rim: the edge that faces the light catches it
    {
        // light travelling toward -x and -y (and a little toward the camera's side: z < 0, which is the diffuse part and not the rim)
        const FighterLight f = FighterLight::fromLevel(levelOf("Light: Amb 0 0 0\nLight: Dir 255 255 255 -0.634 -0.634 -0.6\n"));
        float a[3], b[3], c[3];
        // a sprite facing right: with the stage's x axis the edge on its LEFT (normal pointing -x) is the lit one (measured against the original)
        f.rim(-1, 0, false, a);
        f.rim(1, 0, false, b);
        CHECK(a[0] > 0.6f && b[0] < 1e-6f);
        // flipped sprite: the lit edge is the one that was its right edge
        f.rim(1, 0, true, a);
        f.rim(-1, 0, true, b);
        CHECK(a[0] > 0.6f && b[0] < 1e-6f);
        // up-facing edge (+y) is lit, down-facing is not
        f.rim(0, 1, false, a);
        f.rim(0, -1, false, b);
        CHECK(a[0] > 0.6f && b[0] < 1e-6f);
        // straight-on pixel: the light has no sideways reach into it
        f.rim(0, 0, false, c);
        CHECK(c[0] < 1e-6f);
        // a light from behind (z > 0) reaches pixels facing the camera: the rim of a back light
        const FighterLight back = FighterLight::fromLevel(levelOf("Light: Amb 0 0 0\nLight: Dir 255 255 255 0 0 1\n"));
        back.rim(0, 0, false, c);
        CHECK(c[0] < 1e-6f);   // normal (0,0,1) . (0,0,1) is positive: -dot is negative -> clamped to 0: a back light does not light a camera-facing pixel
        // the rim is the same colour as the light, scaled
        const FighterLight col = FighterLight::fromLevel(levelOf("Light: Amb 0 0 0\nLight: Dir 255 128 0 1 0 0\n"));
        col.rim(1, 0, true, a);   // flipped: nx stays +, light travels +x: -dot = -1 -> 0 ... check the other edge
        col.rim(-1, 0, true, b);
        CHECK(a[0] < 1e-6f && NEAR(b[0], 1.0, 1e-3) && NEAR(b[1], 128 / 255.0, 1e-3) && NEAR(b[2], 0.0, 1e-6));
        // the table has one entry per texel and agrees with rim()
        const auto tbl = f.rimTable(false);
        float nx, ny, r[3];
        normalOf(0x08, nx, ny);
        f.rim(nx, ny, false, r);
        CHECK(NEAR(tbl[0x08][0], r[0], 1e-6));
        normalOf(0xA3, nx, ny);
        f.rim(nx, ny, false, r);
        CHECK(NEAR(tbl[0xA3][1], r[1], 1e-6));
    }
    // ---- point lights: reach, falloff, facing
    {
        const FighterLight f = FighterLight::fromLevel(levelOf("Light: Amb 0 0 0\nLight: Pt 255 255 255 0 0 100 1000\nLight: Pt 255 0 0 5000 0 100 200\n"));
        float m[3];
        f.pointDiffuse(0, 0, 0, m);   // right under the first light, in front of it
        CHECK(NEAR(m[0], 1.0 * (1.0 - 0.1), 1e-3) && NEAR(m[1], m[0], 1e-5));
        f.pointDiffuse(0, 0, 200, m);   // the light is behind the fighter (z): no diffuse
        CHECK(m[0] < 1e-6f);
        f.pointDiffuse(5000, 0, 0, m);   // under the second: red, half way out
        CHECK(NEAR(m[0], 0.5, 1e-3) && m[1] < 1e-6f);
        f.pointDiffuse(0, 0, 0, m);
        const float near0 = m[0];
        f.pointDiffuse(500, 0, 0, m);   // farther: less
        CHECK(m[0] < near0);
        f.pointDiffuse(2500, 0, 0, m);   // beyond both radii
        CHECK(m[0] == 0 && m[1] == 0 && m[2] == 0);
        // an anti-light darkens
        const FighterLight anti = FighterLight::fromLevel(levelOf("Light: Pt -100 -100 -100 0 0 100 1000\n"));
        anti.pointDiffuse(0, 0, 0, m);
        CHECK(m[0] < 0);
        // a level with five point lights gives a fighter the strongest four
        std::string five;
        for (int i = 0; i < 5; ++i) five += "Light: Pt " + std::to_string(50 * (i + 1)) + " 0 0 0 0 100 1000\n";
        const FighterLight many = FighterLight::fromLevel(levelOf(five.c_str()));
        many.pointDiffuse(0, 0, 0, m);
        CHECK(NEAR(m[0], 0.9 * (250 + 200 + 150 + 100) / 255.0, 1e-3));   // 250 + 200 + 150 + 100 (the weakest, 50, is left out)
    }
    // ---- baking into pixels
    {
        const int W = 2, H = 1;
        ArtPlanes pl;
        pl.width = W;
        pl.height = H;
        pl.band = {1, 1};
        pl.grad = {0, 0};
        pl.normal = {0xF8, 0x08};   // right-facing edge, left-facing edge
        std::vector<uint8_t> lit(256u * 256u, 0);
        for (int g = 0; g < 256; ++g) lit[1 * 256 + g] = 255;
        const FighterLight f = FighterLight::fromLevel(levelOf("Light: Amb 128 128 128\nLight: Dir 255 255 255 -1 0 0\n"));
        std::vector<uint8_t> px = {100, 100, 100, 255, 100, 100, 100, 255};
        std::vector<uint8_t> base = px;
        CHECK(applyLighting(px, pl, lit, f, false) == 2);
        // multiplier: ambient 0.5 + diffuse 0 (light has no z) ; rim: light travels -x, unflipped sprite lit on its -x edge (the second pixel)
        CHECK(px[0] == 50);                            // 100 * 0.502 = 50
        CHECK(px[4] > 200);                            // 50 + 255 = clamped at 255
        // flipped: the other edge is lit
        px = base;
        applyLighting(px, pl, lit, f, true);
        CHECK(px[0] > 200 && px[4] == 50);
        // transparent pixels and pixels that do not take light are left alone
        px = {100, 100, 100, 0, 100, 100, 100, 255};
        base = px;
        pl.band = {1, 0};
        CHECK(applyLighting(px, pl, lit, f, false) == 0);
        CHECK(px == base);
        // without a normal plane nothing happens (and nothing crashes)
        ArtPlanes nonormal = pl;
        nonormal.normal.clear();
        CHECK(applyLighting(px, nonormal, lit, f, false) == 0);
        // a line (grad high bits) darkens the rim along with the colour: line 7 = fully dark
        ArtPlanes ln;
        ln.width = 1;
        ln.height = 1;
        ln.band = {1};
        ln.grad = {static_cast<uint8_t>(7 << 5)};
        ln.normal = {0x08};
        std::vector<uint8_t> one = {0, 0, 0, 255};
        applyLighting(one, ln, lit, f, false);
        CHECK(one[0] == 0);
        // brighter than the palette: a stage ambient above 1
        const FighterLight bright = FighterLight::fromLevel(levelOf("Light: Amb 255 255 255\nLight: Dir 255 255 255 0 0 -1\n"));
        std::vector<uint8_t> p2 = {100, 100, 100, 255};
        ArtPlanes one1;
        one1.width = 1;
        one1.height = 1;
        one1.band = {1};
        one1.grad = {0};
        one1.normal = {0x88};
        applyLighting(p2, one1, lit, bright, false);
        CHECK(p2[0] == 200);
    }
    // ---- real data (ZDROID_TEST_CHARS = a folder with <name>.foit / .art / .art-pt): every visible pixel of a decoded frame has a normal, and the
    // edges point outward (left edges to -x, right edges to +x, bottom edges to -y)
    if (const char* dir = std::getenv("ZDROID_TEST_CHARS")) {
        auto slurp = [](const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        for (const char* name : {"peacock", "painwheel"}) {
            const std::string base = std::string(dir) + "/" + name;
            const std::vector<uint8_t> fb = slurp(base + ".foit"), art = slurp(base + ".art"), apt = slurp(base + ".art-pt");
            if (fb.empty() || (art.empty() && apt.empty())) continue;
            foit::Character c;
            std::string err;
            CHECK(foit::parse(fb.data(), fb.size(), c, &err));
            size_t frames = 0, opaque = 0, noNormal = 0;
            double left = 0, right = 0, bottom = 0, top = 0;
            long nl = 0, nr = 0, nb = 0, nt = 0;
            for (size_t fi = 0; fi < c.frames.size() && frames < 12; ++fi) {
                const foit::Frame& fr = c.frames[fi];
                bool any = false;
                for (int s = 0; s < foit::kSlots; ++s) {
                    if (fr.img[s].placeholder() || fr.img[s].silhouette()) continue;
                    const std::vector<uint8_t>& src = fr.src == 5 ? art : apt;
                    ArtPlanes plain, withN;
                    if (src.empty() || !foit::decodeImage(fr.img[s], src.data(), src.size(), withN, true)) continue;
                    CHECK(foit::decodeImage(fr.img[s], src.data(), src.size(), plain, false));
                    CHECK(plain.normal.empty() && plain.band == withN.band && plain.grad == withN.grad);   // asking for the normal changes nothing else
                    CHECK(withN.normal.size() == static_cast<size_t>(withN.width) * withN.height);
                    any = true;
                    auto vis = [&](int x, int y) {
                        if (x < 0 || y < 0 || x >= withN.width || y >= withN.height) return false;
                        const size_t i = static_cast<size_t>(y) * withN.width + x;
                        return withN.band[i] != 0 || (withN.grad[i] >> 5) != 0;
                    };
                    for (int y = 0; y < withN.height; ++y)
                        for (int x = 0; x < withN.width; ++x) {
                            if (!vis(x, y)) continue;
                            ++opaque;
                            const uint8_t n = withN.normal[static_cast<size_t>(y) * withN.width + x];
                            if (n == 0) ++noNormal;
                            float nx, ny;
                            normalOf(n, nx, ny);
                            if (!vis(x - 2, y)) { left += nx; ++nl; }
                            if (!vis(x + 2, y)) { right += nx; ++nr; }
                            if (!vis(x, y - 2)) { top += ny; ++nt; }
                            if (!vis(x, y + 2)) { bottom += ny; ++nb; }
                        }
                }
                if (any) ++frames;
            }
            CHECK(opaque > 1000);
            CHECK(noNormal * 100 < opaque);   // a visible pixel always has a direction
            CHECK(nl > 100 && nr > 100 && nt > 100 && nb > 100);
            if (nl && nr && nt && nb) {
                CHECK(left / nl < 0 && right / nr > 0);   // outward
                CHECK(bottom / nb < top / nt);            // the underside points down
            }
            std::printf("%s: %zu frames, %zu visible pixels, edge normals L %.2f R %.2f T %.2f B %.2f\n", name, frames, opaque,
                        nl ? left / nl : 0.0, nr ? right / nr : 0.0, nt ? top / nt : 0.0, nb ? bottom / nb : 0.0);
        }
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
