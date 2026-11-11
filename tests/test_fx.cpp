// test_fx.cpp - PAFX container, ChaCha20-Poly1305, effect tuning extraction and the effect maths.
// Everything here runs on synthetic data; with ZDROID_TEST_DATA pointing at a folder that holds a real Core the last test also reads it.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "aead.h"
#include "art.h"
#include "fx_glsl.h"
#include "fx_math.h"
#include "fx_pack.h"
#include "fx_store.h"
#include "fx_tuning.h"
#include "pafx.h"

using namespace zengine;
using namespace zengine::fx;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) {                                                               \
            ++g_fail;                                                             \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);              \
        }                                                                         \
    } while (0)

static std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> v;
    for (; s[0] && s[1]; s += 2) {
        auto n = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
        v.push_back(static_cast<uint8_t>(n(s[0]) * 16 + n(s[1])));
    }
    return v;
}
static std::string toHex(const uint8_t* p, size_t n) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", p[i]);
        s += b;
    }
    return s;
}
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// ---------------------------------------------------------------- crypto
static void testAead() {
    // ChaCha20 block, RFC 8439 2.3.2 (counter 1)
    AeadKey k0;
    for (int i = 0; i < 32; ++i) k0[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    AeadNonce n0;
    const auto nb = hex("000000090000004a00000000");
    std::memcpy(n0.data(), nb.data(), 12);
    uint8_t blk[64];
    chacha20Block(k0, 1, n0, blk);
    CHECK(toHex(blk, 16) == "10f1e7e4d13b5915500fdd1fa32071c4");

    // Poly1305, RFC 8439 2.5.2
    const auto pk = hex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
    const char* msg = "Cryptographic Forum Research Group";
    uint8_t tag[16];
    poly1305(pk.data(), reinterpret_cast<const uint8_t*>(msg), std::strlen(msg), tag);
    CHECK(toHex(tag, 16) == "a8061dc1305136c6c22b8baf0c0127a9");

    // AEAD vectors produced with an independent implementation (python-cryptography)
    {
        AeadNonce n;
        const auto nv = hex("070000004041424344454647");
        std::memcpy(n.data(), nv.data(), 12);
        const auto aad = hex("50515253c0c1c2c3c4c5c6c7");
        const std::string pt = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
        std::vector<uint8_t> ct;
        aeadSeal(k0, n, aad.data(), aad.size(), reinterpret_cast<const uint8_t*>(pt.data()), pt.size(), ct);
        CHECK(ct.size() == pt.size() + 16);
        CHECK(toHex(ct.data(), 8) == "058d154b74324ca1");
        CHECK(toHex(ct.data() + pt.size(), 16) == "fffd3e18018d93709d02e916121b4bc3");
        std::vector<uint8_t> back;
        CHECK(aeadOpen(k0, n, aad.data(), aad.size(), ct.data(), ct.size(), back));
        CHECK(std::string(back.begin(), back.end()) == pt);
    }
    AeadKey k2;
    for (int i = 0; i < 32; ++i) k2[static_cast<size_t>(i)] = static_cast<uint8_t>((i * 7 + 3) & 255);
    AeadNonce n2;
    for (int i = 0; i < 12; ++i) n2[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    {
        std::vector<uint8_t> ct;
        aeadSeal(k2, n2, nullptr, 0, nullptr, 0, ct);
        CHECK(toHex(ct.data(), ct.size()) == "4edc818c662b0a3189cd47bac7ab80cd");
        std::vector<uint8_t> back;
        CHECK(aeadOpen(k2, n2, nullptr, 0, ct.data(), ct.size(), back) && back.empty());
    }
    {
        std::vector<uint8_t> p(300);
        for (size_t i = 0; i < p.size(); ++i) p[i] = static_cast<uint8_t>((i * 31 + 5) & 255);
        std::vector<uint8_t> ct;
        aeadSeal(k2, n2, reinterpret_cast<const uint8_t*>("hello"), 5, p.data(), p.size(), ct);
        CHECK(toHex(ct.data(), 8) == "b9d259268596d80d");
        CHECK(toHex(ct.data() + 290, 10) == "d6290a5b76bfdd938205");
        CHECK(toHex(ct.data() + 300, 16) == "c18e04fcae53a33e939f30c4a28aa709");
        // any change is caught: data, tag, aad, nonce, key
        std::vector<uint8_t> back;
        std::vector<uint8_t> bad = ct;
        bad[17] ^= 1;
        CHECK(!aeadOpen(k2, n2, reinterpret_cast<const uint8_t*>("hello"), 5, bad.data(), bad.size(), back) && back.empty());
        bad = ct;
        bad[ct.size() - 1] ^= 0x80;
        CHECK(!aeadOpen(k2, n2, reinterpret_cast<const uint8_t*>("hello"), 5, bad.data(), bad.size(), back));
        CHECK(!aeadOpen(k2, n2, reinterpret_cast<const uint8_t*>("hellp"), 5, ct.data(), ct.size(), back));
        AeadNonce n3 = n2;
        n3[0] ^= 1;
        CHECK(!aeadOpen(k2, n3, reinterpret_cast<const uint8_t*>("hello"), 5, ct.data(), ct.size(), back));
        CHECK(!aeadOpen(k0, n2, reinterpret_cast<const uint8_t*>("hello"), 5, ct.data(), ct.size(), back));
        CHECK(!aeadOpen(k2, n2, nullptr, 0, ct.data(), 15, back));  // shorter than a tag
        CHECK(aeadOpen(k2, n2, reinterpret_cast<const uint8_t*>("hello"), 5, ct.data(), ct.size(), back) && back == p);
    }
    // every length from 0 to 130 round-trips (block / Poly1305 boundaries)
    for (size_t n = 0; n <= 130; ++n) {
        std::vector<uint8_t> p(n), ct, back;
        for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(i * 13 + n);
        aeadSeal(k2, n2, p.data(), n % 7, p.data(), n, ct);
        CHECK(aeadOpen(k2, n2, p.data(), n % 7, ct.data(), ct.size(), back) && back == p);
    }
}

// ---------------------------------------------------------------- container
static AeadKey keyOf(uint8_t seed) {
    AeadKey k;
    for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(seed * 31 + i * 5);
    return k;
}
static std::array<uint8_t, 16> idOf(uint8_t seed) {
    std::array<uint8_t, 16> a;
    for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<uint8_t>(seed + i * 3);
    return a;
}

static void testPafx() {
    PafxWriter w;
    w.addText("fx/a.frag", PafxKind::GlslFragment, "void main() { gl_FragColor = vec4(SECRETMARKER); }");
    w.addText("fx/b.txt", PafxKind::Text, "second entry");
    w.add("fx/empty.bin", PafxKind::Raw, {});
    std::vector<uint8_t> blob(5000);
    for (size_t i = 0; i < blob.size(); ++i) blob[i] = static_cast<uint8_t>(i * 7);
    w.add("data/blob.bin", PafxKind::Raw, blob);

    std::vector<uint8_t> a, b, c;
    std::string err;
    CHECK(w.build(keyOf(1), idOf(1), a, &err));
    CHECK(w.build(keyOf(1), idOf(1), b));
    CHECK(a == b);                                      // deterministic for the same id
    CHECK(w.build(keyOf(1), idOf(2), c) && a != c);     // a new id seals everything differently
    CHECK(std::memcmp(a.data(), "PAFX", 4) == 0);

    // nothing readable inside
    const std::string text(a.begin(), a.end());
    CHECK(text.find("SECRETMARKER") == std::string::npos);
    CHECK(text.find("fx/a.frag") == std::string::npos);

    PafxArchive ar;
    CHECK(ar.open(a.data(), a.size(), keyOf(1), &err));
    CHECK(ar.entries().size() == 4);
    CHECK(ar.has("fx/b.txt") && !ar.has("fx/nope"));
    std::string t;
    CHECK(ar.readText("fx/b.txt", t) && t == "second entry");
    std::vector<uint8_t> out;
    CHECK(ar.read("data/blob.bin", out) && out == blob);
    CHECK(ar.read("fx/empty.bin", out) && out.empty());
    CHECK(!ar.read("fx/nope", out, &err) && err.find("no such entry") != std::string::npos);
    CHECK(ar.entries()[0].kind == PafxKind::GlslFragment && ar.entries()[3].size == 5000);

    // wrong key / other install
    PafxArchive wr;
    CHECK(!wr.open(a.data(), a.size(), keyOf(2), &err) && err.find("wrong key") != std::string::npos);
    CHECK(wr.entries().empty());

    // damage: header, table, entries, truncation, padding
    for (size_t pos : {size_t(0), size_t(4), size_t(8), size_t(24), size_t(40), size_t(70), a.size() / 2, a.size() - 1}) {
        std::vector<uint8_t> d = a;
        d[pos] ^= 0x10;
        PafxArchive x;
        bool ok = x.open(d.data(), d.size(), keyOf(1));
        if (ok) {   // the damage landed in an entry: opening may succeed but reading that entry must not
            bool allGood = true;
            for (const auto& e : x.entries()) {
                std::vector<uint8_t> o;
                allGood = allGood && x.read(e.name, o);
            }
            ok = allGood;
        }
        CHECK(!ok);
    }
    {
        PafxArchive x;
        CHECK(!x.open(a.data(), a.size() - 1, keyOf(1)));
        std::vector<uint8_t> d = a;
        d.push_back(0);
        CHECK(!x.open(d.data(), d.size(), keyOf(1)));
        CHECK(!x.open(a.data(), 10, keyOf(1)));
        CHECK(!x.open(nullptr, 0, keyOf(1)));
    }
    // entries cannot be swapped inside a pack: swap the sealed bytes of two same-sized entries
    {
        PafxWriter s;
        s.addText("one", PafxKind::Text, "AAAAAAAA");
        s.addText("two", PafxKind::Text, "BBBBBBBB");
        std::vector<uint8_t> p;
        CHECK(s.build(keyOf(3), idOf(3), p));
        const size_t sz = 8 + 16;
        std::vector<uint8_t> d = p;
        std::memcpy(&d[p.size() - 2 * sz], &p[p.size() - sz], sz);
        std::memcpy(&d[p.size() - sz], &p[p.size() - 2 * sz], sz);
        PafxArchive x;
        CHECK(x.open(d.data(), d.size(), keyOf(3)));
        std::string o;
        CHECK(!x.readText("one", o) && !x.readText("two", o));
    }
    // writer refuses bad input
    {
        PafxWriter bad;
        bad.addText("x", PafxKind::Text, "1");
        bad.addText("x", PafxKind::Text, "2");
        std::vector<uint8_t> o;
        CHECK(!bad.build(keyOf(1), idOf(1), o, &err) && err.find("duplicate") != std::string::npos);
        PafxWriter e2;
        e2.addText("", PafxKind::Text, "1");
        CHECK(!e2.build(keyOf(1), idOf(1), o));
    }
    // the key file: created once, then read back
    {
        const std::string path = (std::filesystem::temp_directory_path() / "zdroid_fx_test.key").string();
        std::filesystem::remove(path);
        AeadKey k1{}, k2{};
        CHECK(loadOrCreatePafxKey(path, k1, &err));
        CHECK(loadOrCreatePafxKey(path, k2, &err) && k1 == k2);
        AeadKey zero{};
        CHECK(k1 != zero);
        std::filesystem::remove(path);
        AeadKey k3{};
        CHECK(loadOrCreatePafxKey(path, k3) && k3 != k1);   // a different install gets a different key
        std::filesystem::remove(path);
    }
}

// ---------------------------------------------------------------- tuning extraction (synthetic HLSL-like text, invented numbers)
static const char* kTiled = R"(
// float3 gLowColors[4] = { float3(1,1,1), float3(1,1,1), float3(1,1,1), float3(1,1,1) };   <- a commented-out old table
static const float3 gLowColors[4] = { float3(10.f/255.f, 20.f/255.f, 30.f/255.f), float3(0.5f, 0.25f, 0.125f),
    float3(40.f/255.f, 50.f/255.f, 60.f/255.f), float3(70.f/255.f, 80.f/255.f, 90.f/255.f) };
static const float3 gHighColors[4] = { float3(250.f/255.f, 240.f/255.f, 230.f/255.f), float3(1,1,1), float3(0,0.5,1), float3(0.9, 0.8, 0.7) };
half3 Apply_SuperFlash(half3 pixel, float t, float3 lo, float3 hi)
{
	const float3 coeff = float3(0.25f, 0.5f, 0.25f);
	/* const float CONTRAST = 9.0f; */
	const float CONTRAST = 1.5f;
	const float BRIGHTNESS = 0.75f;
	const float SATURATION = -0.25f;
	return pixel;
}
half3 Apply_SuperShadowBG(half3 pixel, float t)
{
	const float3 gInSuperColor = float3(0.2f, 0.4f, 0.6f);
	const float3 gRGB2LUM = float3(0.3f, 0.6f, 0.1f);
	return pixel;
}
)";
static const char* kOtterVp = R"(
static const float3 gLowColors[4] = { float3(1/255.f, 2/255.f, 3/255.f), float3(4/255.f, 5/255.f, 6/255.f), float3(7/255.f, 8/255.f, 9/255.f), float3(10/255.f, 11/255.f, 12/255.f) };
static const float3 gHighColors[4] = { float3(100/255.f, 110/255.f, 120/255.f), float3(1,1,1), float3(1,1,1), float3(1,1,1) };
)";
static const char* kOtterFp = R"(
half3 Apply_SuperFlash(half3 pixel, float t, float3 lo, float3 hi)
{
	const float3 coeff = float3(0.3f, 0.3f, 0.4f);
	const float CONTRAST = 2.0f;
	const float BRIGHTNESS = 1.25f;
	const float SATURATION = -0.5f;
	return pixel;
}
half4 JojosDeathPortrait(float2 uv)
{
	const float3 strokeColor = {200.f/255.f, 100.f/255.f, 50.f/255.f};
	const float3 fillColor = {10.f/255.f, 20.f/255.f, 30.f/255.f};
	float2 texOffset = {-2.f/160.f, 0.f};
	return 0;
}
half4 JojosDeathBackground(float2 uv)
{
	const float3 colorA = {1.f, 0.f, 0.f}; const float percentA = 0.f/100.f;
	const float3 colorB = {0.f, 1.f, 0.f}; const float percentB = 0.5f;
	const float3 colorC = {0.f, 0.f, 1.f}; const float percentC = 1.0f;
	const float3 colorD = {1.f, 1.f, 0.f}; const float percentD = 1.5f;
	const float3 colorE = {1.f, 0.f, 1.f}; const float percentE = 2.5f;
	centerDists.x *= 2.f/1.f;
	return 0;
}
)";

static void testTuning() {
    FxTuning t = neutralTuning();
    CHECK(!t.fromCore);
    std::vector<std::string> notes;
    const int n = extractTuning(kTiled, kOtterVp, kOtterFp, t, &notes);
    CHECK(n == 4);
    CHECK(notes.empty());
    CHECK(t.fromCore);
    CHECK(near(t.sprite.look[0].low[0], 10.f / 255.f) && near(t.sprite.look[0].low[2], 30.f / 255.f));  // the commented table was ignored
    CHECK(near(t.sprite.look[1].low[1], 0.25f) && near(t.sprite.look[2].high[1], 0.5f) && near(t.sprite.look[3].low[2], 90.f / 255.f));
    CHECK(near(t.sprite.lumaCoeff[0], 0.25f) && near(t.sprite.lumaCoeff[1], 0.5f));
    CHECK(near(t.sprite.contrast, 1.5f));  // not the commented 9.0
    CHECK(near(t.sprite.brightness, 0.75f) && near(t.sprite.saturation, -0.25f));
    CHECK(near(t.ui.look[3].low[2], 12.f / 255.f) && near(t.ui.look[0].high[1], 110.f / 255.f));
    CHECK(near(t.ui.contrast, 2.0f) && near(t.ui.brightness, 1.25f) && near(t.ui.saturation, -0.5f) && near(t.ui.lumaCoeff[2], 0.4f));
    CHECK(near(t.shadowTint[2], 0.6f) && near(t.shadowLuma[1], 0.6f));
    CHECK(near(t.deathStroke[0], 200.f / 255.f) && near(t.deathFill[2], 30.f / 255.f) && near(t.deathStrokeShift, -2.f / 160.f));
    CHECK(near(t.deathBg[1][1], 1.0f) && near(t.deathBg[4][2], 1.0f) && near(t.deathBgStop[1], 0.5f) && near(t.deathBgStop[4], 2.5f));
    CHECK(near(t.deathBgAspect, 2.0f));

    // ini round trip (every number)
    FxTuning r;
    CHECK(tuningFromIni(tuningToIni(t), r));
    CHECK(r.fromCore && near(r.sprite.look[2].high[1], 0.5f) && near(r.ui.look[3].low[2], 12.f / 255.f) && near(r.deathBgAspect, 2.0f));
    CHECK(tuningToIni(r) == tuningToIni(t));
    CHECK(!tuningFromIni("fromCore=1\n", r));       // incomplete
    CHECK(!tuningFromIni("garbage", r));

    // missing sources: nothing found, neutral numbers stay, notes say what
    FxTuning m = neutralTuning();
    notes.clear();
    CHECK(extractTuning("", "", "", m, &notes) == 0 && !m.fromCore && notes.size() == 4);
    // a half-broken table must not leave half-applied numbers behind
    FxTuning h = neutralTuning();
    extractTuning("gLowColors[4] = { float3(1,2,3), float3(1,2,3) }; gHighColors[4] = {float3(1,1,1)};", "", "", h);
    CHECK(near(h.sprite.look[0].low[0], 0.0f) && !h.fromCore);
}

// ---------------------------------------------------------------- effect maths
static Palette randomPalette(int tones, int bands, uint32_t seed) {
    Palette p;
    p.tones = tones;
    p.bands = bands;
    p.rgb.resize(static_cast<size_t>(tones) * bands * 3);
    uint32_t s = seed;
    for (uint8_t& b : p.rgb) {
        s = s * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(s >> 24);
    }
    return p;
}

static void testMath() {
    // 1. the palette maths equals the engine's verified table path for every (band, grad)
    const Palette pal = randomPalette(16, 40, 7);
    const std::vector<uint8_t> tables = buildRgbaTables(pal);
    int worst = 0;
    SpriteParams p;
    for (int band = 0; band < 256; ++band)
        for (int g = 0; g < 256; ++g) {
            const Rgba c = spritePalette(pal, static_cast<uint8_t>(band), static_cast<uint8_t>(g), p);
            const float v[4] = {c.r, c.g, c.b, c.a};
            for (int ch = 0; ch < 4; ++ch) {
                const int ref = tables[(static_cast<size_t>(band) * 4 + ch) * 256 + g];
                const int mine = static_cast<int>(std::lround(std::fmax(0.f, std::fmin(1.f, v[ch])) * 255.f));
                worst = std::max(worst, std::abs(ref - mine));
            }
        }
    CHECK(worst <= 1);
    std::printf("  palette maths vs engine tables: worst difference %d step(s)\n", worst);

    // 2. identity at zero strength
    const FxTuning tun = neutralTuning();
    const ScreenFx off = ScreenFx::make(tun, false, 1, 0, 0);
    const Rgb c{0.3f, 0.6f, 0.9f};
    const Rgb o = screenFx(c, off);
    CHECK(near(o.r, c.r) && near(o.g, c.g) && near(o.b, c.b));
    const Rgb h0 = hueShift(c, std::cos(1.0f), std::sin(1.0f), 0.0f);
    CHECK(near(h0.r, c.r) && near(h0.g, c.g) && near(h0.b, c.b));

    // 3. hue shift: 120 degrees about the grey axis carries red to green; grey stays grey; length is kept
    const float a = 2.0f * 3.14159265f / 3.0f;
    const Rgb g = hueShift({1, 0, 0}, std::cos(a), std::sin(a), 1.0f);
    CHECK(near(g.r, 0.0f, 1e-4f) && near(g.g, 1.0f, 1e-4f) && near(g.b, 0.0f, 1e-4f));
    const Rgb grey = hueShift({0.4f, 0.4f, 0.4f}, std::cos(0.9f), std::sin(0.9f), 1.0f);
    CHECK(near(grey.r, 0.4f, 1e-5f) && near(grey.g, 0.4f, 1e-5f) && near(grey.b, 0.4f, 1e-5f));
    const Rgb v = hueShift({0.8f, 0.2f, 0.5f}, std::cos(0.7f), std::sin(0.7f), 1.0f);
    CHECK(near(v.r * v.r + v.g * v.g + v.b * v.b, 0.64f + 0.04f + 0.25f, 1e-4f));
    const Rgb half = hueShift({1, 0, 0}, std::cos(a), std::sin(a), 0.5f);
    CHECK(near(half.r, 0.5f, 1e-4f) && near(half.g, 0.5f, 1e-4f));

    // 4. super shadow: full strength = tint * luminance
    ScreenFx sh = off;
    sh.shadowT = 1.0f;
    const float lum = sh.shadowLuma[0] * c.r + sh.shadowLuma[1] * c.g + sh.shadowLuma[2] * c.b;
    const Rgb s1 = superShadow(c, sh);
    CHECK(near(s1.r, sh.shadowTint[0] * lum) && near(s1.b, sh.shadowTint[2] * lum));

    // 5. super flash: at full strength a black pixel maps to look.low * lift
    FxTuning ft = neutralTuning();
    ft.sprite.look[1] = {{0.2f, 0.1f, 0.0f}, {1.0f, 0.9f, 0.5f}};
    ft.sprite.contrast = 2.0f;
    ft.sprite.brightness = 0.5f;
    ft.sprite.saturation = 0.0f;
    const ScreenFx fl = ScreenFx::make(ft, false, 2, 0, 1.0f);
    const Rgb f0 = superFlash({0, 0, 0}, fl);
    const float lift = 0.5f * (1.2f + (0.5f + (0.0f - 1.0f) * 2.0f));   // lum 0: 1.2 + lerp(0.5, -0.5, 2) = 1.2 - 1.5
    CHECK(near(f0.r, 0.2f * lift) && near(f0.g, 0.1f * lift) && near(f0.b, 0.0f));
    // the flash number is 1-based and clamped
    CHECK(ScreenFx::make(ft, false, 0, 0, 1).look.low[0] == ft.sprite.look[0].low[0]);
    CHECK(ScreenFx::make(ft, false, 99, 0, 1).look.low[0] == ft.sprite.look[3].low[0]);

    // 6. outline layer and line darkening
    SpriteParams lp;
    lp.line[0] = 0.5f;
    const Rgba ol = spritePalette(pal, 0, 7u << 5, lp);   // band 0, full line
    CHECK(near(ol.a, 1.0f) && near(ol.r, 0.5f));
    const Rgba none = spritePalette(pal, 0, 0, lp);
    CHECK(near(none.a, 0.0f));
    SpriteParams ap;
    ap.alpha = 0.5f;
    CHECK(near(spritePalette(pal, 3, 0, ap).a, 0.5f));
    const float oa[3] = {1, 0, 0}, ob[3] = {0, 0, 1};
    const Rgba so = superOutline(pal, 3, 0, SpriteParams(), oa, ob, 0.25f);
    CHECK(near(so.r, 0.75f) && near(so.b, 0.25f) && near(so.a, 1.0f));

    // 7. floor shadow: fades with height, discards outside the distance and on the wrong side, and where nothing is drawn
    CHECK(near(floorShadowAlpha(5, 0, -100.0f, 400.0f, 0.75f), 0.75f * 0.75f));
    CHECK(floorShadowAlpha(5, 0, 100.0f, 400.0f, 0.75f) < 0);        // same sign as the fade distance: wrong side
    CHECK(near(floorShadowAlpha(5, 0, 100.0f, -400.0f, 0.75f), 0.75f * 0.75f));
    CHECK(floorShadowAlpha(5, 0, -401.0f, 400.0f, 0.75f) < 0);
    CHECK(floorShadowAlpha(0, 0, -10.0f, 400.0f, 0.75f) < 0);        // no colour and no line
    CHECK(floorShadowAlpha(0, 32, -10.0f, 400.0f, 0.75f) > 0);       // a line is enough
    CHECK(near(floorShadowAlpha(5, 0, 0.0f, -400.0f, 0.75f), 0.75f) || floorShadowAlpha(5, 0, 0.0f, -400.0f, 0.75f) < 0);

    // 8. death look
    FxTuning dt = neutralTuning();
    const Rgba dp = deathPortrait(dt, 1.0f, 0.0f, 0.5f);
    CHECK(near(dp.a, 0.5f) && near(dp.r, dt.deathStroke[0] * 0.0f + dt.deathFill[0]));   // r0 = 1 -> fill
    const Rgba dq = deathPortrait(dt, 0.0f, 1.0f, 1.0f);
    CHECK(near(dq.a, 1.0f) && near(dq.r, dt.deathStroke[0]));                              // r0 = 0, r1 = 1 -> the widened stroke
    const Rgb centre = deathBackground(dt, 0.5f, 0.5f);
    CHECK(near(centre.r, dt.deathBg[0][0]) && near(centre.g, dt.deathBg[0][1]));
    const Rgb corner = deathBackground(dt, 1.0f, 1.0f);
    CHECK(corner.r >= 0.0f && corner.r <= 1.0f);
    dt.deathBgStop[1] = dt.deathBgStop[0];                                                 // degenerate span must not divide by zero
    const Rgb dz = deathBackground(dt, 0.5f, 0.5f);
    CHECK(std::isfinite(dz.r));

    // 9. flat tint: texture x vertex colour, effect only on colour
    const ScreenFx none2 = ScreenFx::make(tun, false, 1, 0, 0);
    const Rgba ft2 = flatTint({0.5f, 1.0f, 0.2f, 0.5f}, {1.0f, 0.5f, 1.0f, 0.5f}, none2);
    CHECK(near(ft2.r, 0.5f) && near(ft2.g, 0.5f) && near(ft2.b, 0.2f) && near(ft2.a, 0.25f));
}

// ---------------------------------------------------------------- programs and the pack
static void testPack() {
    const auto& progs = fxProgramSources();
    CHECK(progs.size() == 7);
    for (const FxProgramSource& p : progs) {
        CHECK(!p.vertex.empty() && p.fragment.find("void main") != std::string::npos);
        CHECK(p.vertex.find("gl_Position") != std::string::npos);
        CHECK(findFxProgram(p.name) == &p);
    }
    CHECK(findFxProgram("nope") == nullptr);

    // the program sources carry no tuning: no digit run that looks like a 0..255 colour literal over 1.0 except the structural ones
    for (const FxProgramSource& p : progs) CHECK(p.fragment.find("255.0 /") == std::string::npos || p.name == "sprite_palette" || p.name == "super_outline" || p.name == "floor_shadow");

    CoreShaderInputs in;
    in.tiledSpriteFp = kTiled;
    in.otterVp = kOtterVp;
    in.otterFp = kOtterFp;
    in.fileCount = 3;
    in.fingerprint = coreFingerprint({{"b.fx", 20}, {"a.fx", 10}});
    CHECK(in.fingerprint == coreFingerprint({{"a.fx", 10}, {"b.fx", 20}}));     // order independent
    CHECK(in.fingerprint != coreFingerprint({{"a.fx", 10}, {"b.fx", 21}}));
    CHECK(in.fingerprint.size() == 16);

    std::vector<uint8_t> bytes;
    FxBuildReport rep;
    std::string err;
    CHECK(buildFxPack(in, keyOf(9), idOf(9), bytes, &rep, &err));
    CHECK(rep.tuningGroups == 4 && rep.notes.empty());

    FxPack pk;
    CHECK(pk.load(bytes.data(), bytes.size(), keyOf(9), &err));
    CHECK(pk.programs().size() == progs.size());
    CHECK(pk.tuningFromCore() && pk.coreFingerprint() == in.fingerprint);
    CHECK(near(pk.tuning().sprite.contrast, 1.5f));
    for (const FxProgramSource& p : progs) {
        const FxProgramSource* q = pk.program(p.name);
        CHECK(q && q->vertex == p.vertex && q->fragment == p.fragment);
    }
    FxPack bad;
    CHECK(!bad.load(bytes.data(), bytes.size(), keyOf(8), &err));                // another install's key
    CHECK(bad.programs().empty());

    // a Core without the shader sources still packs (neutral numbers, flagged as such)
    CoreShaderInputs empty;
    CHECK(buildFxPack(empty, keyOf(9), idOf(9), bytes, &rep, &err) && rep.tuningGroups == 0);
    FxPack np;
    CHECK(np.load(bytes.data(), bytes.size(), keyOf(9)) && !np.tuningFromCore());
}

// the bilinear sprite: texel centres give the nearest look, the middle of two texels their average, empty texels lend no colour
static void testSmoothMath() {
    Palette pal;
    pal.tones = 2;
    pal.bands = 3;
    pal.rgb = {0, 0, 0, 255, 255, 255,      // band 0 (never read)
               200, 0, 0, 200, 0, 0,        // band 1: red
               0, 0, 200, 0, 0, 200};       // band 2: blue
    // 2x2 picture: red, blue / empty outline-less texel (band 0, line 0), red with a half-strength outline
    const uint8_t band[4] = {1, 2, 0, 1};
    const uint8_t grad[4] = {0, 0, 0, 0};
    SpriteParams p;
    auto at = [&](float u, float v) { return spritePaletteSmooth(pal, band, grad, 2, 2, u, v, p); };
    const Rgba red = at(0.25f, 0.25f), blue = at(0.75f, 0.25f);
    CHECK(near(red.r, 200 / 255.0f, 1e-4f) && near(red.a, 1.0f) && near(blue.b, 200 / 255.0f, 1e-4f));   // texel centres
    const Rgba mid = at(0.5f, 0.25f);   // halfway between red and blue, top row
    CHECK(near(mid.r, 100 / 255.0f, 1e-4f) && near(mid.b, 100 / 255.0f, 1e-4f) && near(mid.a, 1.0f));
    const Rgba edge = at(0.25f, 0.5f);  // red above an empty texel: half coverage, but the colour stays red (the empty texel lends none)
    CHECK(near(edge.a, 0.5f) && near(edge.r, 200 / 255.0f, 1e-4f) && near(edge.b, 0.0f, 1e-4f));
    const Rgba out = at(-1.0f, -1.0f);  // beyond the picture: the corner texel again (clamped)
    CHECK(near(out.r, red.r) && near(out.a, red.a));
    // alpha and the screen effect act after the filtering
    p.alpha = 0.5f;
    CHECK(near(at(0.25f, 0.25f).a, 0.5f));
    // a fully empty neighbourhood: no coverage
    const uint8_t b0[4] = {0, 0, 0, 0};
    CHECK(near(spritePaletteSmooth(pal, b0, grad, 2, 2, 0.5f, 0.5f, SpriteParams{}).a, 0.0f));
}

// ---------------------------------------------------------------- the pack on the device
static void put(const std::filesystem::path& p, const std::string& text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static void testStore() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("zd_fx_store_" + std::to_string(static_cast<long>(std::rand())) + "_" + std::to_string(static_cast<long>(std::time(nullptr))));
    fs::remove_all(root);
    const fs::path core = root / "core", store = root / "private";
    put(core / "shaders" / "TiledSprite_fp.fx", kTiled);
    put(core / "shaders" / "Otter_vp.fx", kOtterVp);
    put(core / "shaders" / "Otter_fp.fx", kOtterFp);
    put(core / "shaders" / "pssl" / "other.fx", "x");

    CoreShaderInputs in;
    std::string err;
    CHECK(gatherCoreShaders(core.string(), in, &err));                       // the Core folder ...
    CoreShaderInputs in2;
    CHECK(gatherCoreShaders((core / "shaders").string(), in2, &err));        // ... or its shaders folder
    CHECK(in.fingerprint == in2.fingerprint && in.fileCount == 4 && in.fingerprint.size() == 16);
    CHECK(!in.tiledSpriteFp.empty() && !in.otterVp.empty() && !in.otterFp.empty());
    CoreShaderInputs none;
    CHECK(!gatherCoreShaders((root / "nothing").string(), none, &err) && !err.empty());
    put(root / "emptycore" / "shaders" / "readme.txt", "no shaders here");
    CHECK(!gatherCoreShaders((root / "emptycore").string(), none, &err));

    // first use: created from the Core, written, and the key next to it
    FxPack pk;
    FxStoreInfo inf;
    CHECK(loadOrBuildFxPack(store.string(), core.string(), pk, &inf, &err));
    CHECK(inf.built && inf.persisted && inf.fromCore && inf.why == "created" && inf.fingerprint == in.fingerprint);
    CHECK(fs::exists(store / kFxPackFile) && fs::exists(store / kFxKeyFile) && fs::file_size(store / kFxKeyFile) == 32);
    CHECK(near(pk.tuning().sprite.contrast, 1.5f));
    CHECK(pk.programs().size() == fxProgramSources().size());

    // second use: loaded as it is
    const auto packTime = fs::last_write_time(store / kFxPackFile);
    FxPack again;
    CHECK(loadOrBuildFxPack(store.string(), core.string(), again, &inf, &err));
    CHECK(!inf.built && inf.persisted && inf.fromCore && inf.why.empty());
    CHECK(fs::last_write_time(store / kFxPackFile) == packTime);
    CHECK(again.coreFingerprint() == pk.coreFingerprint());

    // the Core is not found right now: the pack made from it stays in use
    FxPack gone;
    CHECK(loadOrBuildFxPack(store.string(), "", gone, &inf, &err));
    CHECK(!inf.built && inf.fromCore);
    CHECK(loadOrBuildFxPack(store.string(), (root / "nothing").string(), gone, &inf, &err) && !inf.built && inf.fromCore);

    // the Core changed: rebuilt
    put(core / "shaders" / "pssl" / "other.fx", "a longer file than before");
    FxPack changed;
    CHECK(loadOrBuildFxPack(store.string(), core.string(), changed, &inf, &err));
    CHECK(inf.built && inf.why == "the Core changed" && changed.coreFingerprint() != pk.coreFingerprint());

    // damaged, or sealed with another key: rebuilt
    {
        std::fstream f(store / kFxPackFile, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(200);
        f.put('\x5a');
    }
    FxPack fixed;
    CHECK(loadOrBuildFxPack(store.string(), core.string(), fixed, &inf, &err));
    CHECK(inf.built && inf.why == "unreadable" && fixed.programs().size() == fxProgramSources().size());
    fs::remove(store / kFxKeyFile);   // a new install key cannot open the old pack
    FxPack rekeyed;
    CHECK(loadOrBuildFxPack(store.string(), core.string(), rekeyed, &inf, &err));
    CHECK(inf.built && inf.why == "unreadable");

    // a pack made before a program existed is rebuilt (here: a pack without "sprite_palette_smooth")
    {
        AeadKey key{};
        CHECK(loadOrCreatePafxKey((store / kFxKeyFile).string(), key, &err));
        PafxWriter w;
        std::string man = "format=1\ncoreFingerprint=" + in.fingerprint + "\ncoreShaderFiles=4\ntuningGroups=0\ntuningFromCore=0\nprograms=";
        bool first = true;
        for (const FxProgramSource& p : fxProgramSources()) {
            if (p.name == "sprite_palette_smooth") continue;
            man += (first ? "" : ",") + p.name;
            first = false;
            w.addText("fx/" + p.name + ".vert", PafxKind::GlslVertex, p.vertex);
            w.addText("fx/" + p.name + ".frag", PafxKind::GlslFragment, p.fragment);
        }
        w.addText("fx/manifest.ini", PafxKind::Text, man + "\n");
        w.addText("fx/tuning.ini", PafxKind::Text, tuningToIni(neutralTuning()));
        std::vector<uint8_t> bytes;
        CHECK(w.build(key, idOf(3), bytes, &err));
        put(store / kFxPackFile, std::string(bytes.begin(), bytes.end()));
    }
    FxPack old;
    CHECK(loadOrBuildFxPack(store.string(), core.string(), old, &inf, &err));
    CHECK(inf.built && inf.why == "built before this version" && old.program("sprite_palette_smooth") != nullptr);

    // no private folder: built in memory, nothing written
    FxPack mem;
    CHECK(loadOrBuildFxPack("", core.string(), mem, &inf, &err));
    CHECK(inf.built && !inf.persisted && inf.fromCore && mem.programs().size() == fxProgramSources().size());
    CHECK(loadOrBuildFxPack("", "", mem, &inf, &err) && !inf.fromCore);   // no Core either: neutral numbers of our own

    // a store folder that cannot be created is an error, not a crash
    put(root / "afile", "x");
    CHECK(!loadOrBuildFxPack((root / "afile" / "sub").string(), core.string(), mem, &inf, &err) && !err.empty());

    fs::remove_all(root);
}

// ---------------------------------------------------------------- a real Core, when one is around
static std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testRealCore() {
    const char* d = std::getenv("ZDROID_TEST_DATA");
    if (!d || !*d) {
        std::printf("  (no ZDROID_TEST_DATA: skipping the real-Core extraction)\n");
        return;
    }
    std::stringstream ss(d);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        for (const char* sub : {"shaders", "core/shaders", "core/core/shaders", "core/core/core/shaders"}) {
            const std::filesystem::path sd = std::filesystem::path(dir) / sub;
            if (!std::filesystem::exists(sd / "TiledSprite_fp.fx")) continue;
            FxTuning t = neutralTuning();
            std::vector<std::string> notes;
            const int n = extractTuning(slurp(sd / "TiledSprite_fp.fx"), slurp(sd / "Otter_vp.fx"), slurp(sd / "Otter_fp.fx"), t, &notes);
            for (const std::string& m : notes) std::printf("  note: %s\n", m.c_str());
            CHECK(n == 4);
            // sanity only: colours are in range, the "high" end of every look is lighter than its "low" end
            for (const FlashDomain* fd : {&t.sprite, &t.ui})
                for (int i = 0; i < 4; ++i) {
                    float lo = 0, hi = 0;
                    for (int c = 0; c < 3; ++c) {
                        CHECK(fd->look[i].low[c] >= -0.01f && fd->look[i].low[c] <= 1.01f);
                        CHECK(fd->look[i].high[c] >= -0.01f && fd->look[i].high[c] <= 1.01f);
                        lo += fd->look[i].low[c];
                        hi += fd->look[i].high[c];
                    }
                    CHECK(hi > lo);
                }
            CHECK(t.sprite.contrast > 0 && t.ui.contrast > 0 && t.deathBgAspect > 1.0f);
            std::printf("  real Core: %d tuning group(s) read from %s\n", n, sd.string().c_str());
            return;
        }
    }
    std::printf("  (ZDROID_TEST_DATA has no shaders/TiledSprite_fp.fx: skipping the real-Core extraction)\n");
}

int main() {
    std::printf("[fx] crypto\n");
    testAead();
    std::printf("[fx] container\n");
    testPafx();
    std::printf("[fx] tuning\n");
    testTuning();
    std::printf("[fx] maths\n");
    testMath();
    std::printf("[fx] pack\n");
    testPack();
    testSmoothMath();
    std::printf("[fx] the pack on the device\n");
    testStore();
    std::printf("[fx] real Core\n");
    testRealCore();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
