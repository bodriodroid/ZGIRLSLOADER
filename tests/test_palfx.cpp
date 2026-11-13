// test_palfx.cpp - palette effects (src/core/palfx.*): the name hash, the table parser (every form of a frame line), playback by hash
// (restart, loop, end), resolving a frame into a tint, and the layers of a fighter.  Synthetic tables; a real table is checked only
// when ZDROID_TEST_PALFX names one.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "palfx.h"

using namespace zengine;
using namespace zengine::palfx;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_checks;                                                    \
        if (!(c)) {                                                    \
            ++g_fail;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
        }                                                              \
    } while (0)
static bool near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

static const char* kTable = R"(# NORMAL indicates to use the character's actual color in this spot
AllBlack
[0 0 0  0 0 0] 255
END

Fade   # a comment after the name
[255 0 0  0 0 255] 128
[255 0 0  0 0 255] 64 HiMult 250/100 LoMult -1/2
[NORMAL 0 255 0] 200
END

Pulse
NoLight
[PAL 203  PAL 205] 255 HiMult 2
[PAL 203  PAL 205] 40 HiMult 140/100 LoMult 0
[PAL 203  PAL 205] 240 Loop
[PAL 203  PAL 205] 100
END

Masked
MASK 25 26
[PAL 216 PAL 218] 100
END

Broken
[1 2]
[1 2 3 4 5 6 7] 5
END

Empty
END

Fade
[0 255 0 0 255 0] 255
END
)";

static void testHash() {
    CHECK(nameHash("") == 5381u);
    CHECK(nameHash("a") == (static_cast<uint32_t>('A') ^ (5381u * 33u)));
    CHECK(nameHash("CounterHit") == nameHash("counterhit"));
    CHECK(nameHash("CounterHit") == 0x5bcff524u);       // values read from a running fight, matched to the names of the table
    CHECK(nameHash("HatredGuard") == 0x2ec8226eu);
    CHECK(nameHash("AnnieStarCancel") == 0x138b331au);
    CHECK(nameHash("Beo_HypeLv1") == 0xc8efe39du);
    CHECK(nameHash("AnnieFlame") == 0xaecd29abu);
    CHECK(nameHash("Fade") != nameHash("Pulse"));
}

static void testParse() {
    Library lib;
    const size_t n = lib.parse(kTable);
    CHECK(n == 5);   // AllBlack, Fade, Pulse, Masked, Fade again (replaces); Broken keeps one frame? see below
    CHECK(lib.byName("allblack") != nullptr);
    const Effect* ab = lib.byName("AllBlack");
    CHECK(ab && ab->frames.size() == 1 && near(ab->frames[0].strength, 1.0f) && near(ab->frames[0].hi.rgb[0], 0));
    CHECK(ab && ab->loopFrom == -1 && !ab->noLight);

    // the later "Fade" replaced the earlier one
    const Effect* fd = lib.byName("Fade");
    CHECK(fd && fd->frames.size() == 1 && near(fd->frames[0].hi.rgb[1], 1.0f));

    const Effect* pu = lib.byName("Pulse");
    CHECK(pu && pu->noLight && pu->frames.size() == 4);
    if (pu && pu->frames.size() == 4) {
        CHECK(pu->frames[0].hi.pal == 203 && pu->frames[0].lo.pal == 205);
        CHECK(near(pu->frames[0].hiMult, 2.0f) && near(pu->frames[0].loMult, 1.0f));
        CHECK(near(pu->frames[1].hiMult, 1.4f) && near(pu->frames[1].loMult, 0.0f));
        CHECK(near(pu->frames[1].strength, 40.0f / 255.0f));
        CHECK(pu->loopFrom == 2);   // Loop marks the frame it is on
    }
    const Effect* ma = lib.byName("Masked");
    CHECK(ma && ma->mask.size() == 2 && ma->mask[0] == 25 && ma->mask[1] == 26);

    const Effect* br = lib.byName("Broken");
    CHECK(br == nullptr);   // both frame lines are bad: nothing left, so the effect is not added
    CHECK(lib.byName("Empty") == nullptr);

    // forms of a frame: fractions, negatives, NORMAL
    Library l2;
    l2.parse("X\n[NORMAL 0 255 0] 200\n[-100 -100 -100  PAL 194] 192 LoMult -1/2\nEND\n");
    const Effect* x = l2.byName("x");
    CHECK(x && x->frames.size() == 2);
    if (x && x->frames.size() == 2) {
        CHECK(x->frames[0].hi.actual && !x->frames[0].lo.actual && near(x->frames[0].lo.rgb[1], 1.0f));
        CHECK(near(x->frames[0].strength, 200.0f / 255.0f));
        CHECK(near(x->frames[1].hi.rgb[0], -100.0f / 255.0f) && x->frames[1].lo.pal == 194);
        CHECK(near(x->frames[1].loMult, -0.5f));
    }
    // NORMAL first with no strength: no tint by default
    Library l3;
    l3.parse("Y\n[NORMAL NORMAL]\nEND\n");
    CHECK(l3.byName("Y") && near(l3.byName("Y")->frames[0].strength, 0.0f));
    // no END at the end of the text, CRLF line ends, tabs
    Library l4;
    l4.parse("Z\r\n[\t255 255 255\t0 0 0 ] 255 Loop\r\nW\r\n[1 1 1 1 1 1] 1\r\n");
    CHECK(l4.byName("Z") && l4.byName("Z")->loopFrom == 0);
    CHECK(l4.byName("W") && l4.size() == 2);
}

static void testPlayback() {
    Library lib;
    lib.parse(kTable);
    const Effect* pu = lib.byName("Pulse");
    CHECK(pu != nullptr);
    if (!pu) return;
    // frames: 0 1 | loop from 2: 2 3 2 3 2 3 ...
    CHECK(pu->frameAt(0) == 0 && pu->frameAt(3) == 3);
    CHECK(pu->frameAt(4) == 2 && pu->frameAt(5) == 3 && pu->frameAt(6) == 2 && pu->frameAt(1001) == 3);
    CHECK(pu->frameAt(-1) == -1);
    const Effect* ab = lib.byName("AllBlack");
    CHECK(ab && ab->frameAt(0) == 0 && ab->frameAt(1) == -1 && ab->frameAt(50) == -1);   // no Loop: it ends

    Layer ly;
    CHECK(!ly.active());
    ly.update(nameHash("Pulse"), lib);
    CHECK(ly.active() && ly.frame() == 0);
    ly.update(nameHash("Pulse"), lib);
    CHECK(ly.frame() == 1);
    ly.update(nameHash("AllBlack"), lib);   // a new hash restarts
    CHECK(ly.frame() == 0);
    ly.update(nameHash("AllBlack"), lib);
    CHECK(!ly.active());                    // over, although the slot still holds the hash
    ly.update(nameHash("AllBlack"), lib);
    CHECK(!ly.active());
    ly.update(0, lib);                      // cleared ...
    ly.update(nameHash("AllBlack"), lib);   // ... and raised again: plays again
    CHECK(ly.active() && ly.frame() == 0);
    ly.update(0xDEADBEEFu, lib);            // unknown hash: nothing
    CHECK(!ly.active());
}

static void testResolve() {
    Library lib;
    lib.parse(kTable);
    const PalLookup pal = [](int i, float o[3]) {
        if (i == 203) { o[0] = 1; o[1] = 0; o[2] = 0; return true; }
        if (i == 205) { o[0] = 0; o[1] = 0; o[2] = 1; return true; }
        return false;
    };
    const Effect* pu = lib.byName("Pulse");
    Tint t = resolve(*pu, pu->frames[1], pal);
    CHECK(t.noLight && near(t.hi[0], 1) && near(t.lo[2], 1));
    CHECK(near(t.aHi, 40.0f / 255.0f * 1.4f) && near(t.aLo, 0.0f));
    // an unknown palette entry is mid grey, not garbage
    const Effect* ma = lib.byName("Masked");
    t = resolve(*ma, ma->frames[0], pal);
    CHECK(near(t.hi[0], 0.5f) && near(t.lo[1], 0.5f) && t.maskCount == 2 && t.mask[0] == 25 && t.mask[1] == 26);
    // NORMAL at one end: no strength there
    Library l2;
    l2.parse("N\n[NORMAL 255 0 0] 255\nEND\n");
    const Effect* n = l2.byName("N");
    t = resolve(*n, n->frames[0], pal);
    CHECK(near(t.aHi, 0.0f) && near(t.aLo, 1.0f));
    const Flat f = flatten(t);
    CHECK(f.a > 0.45f && f.a < 0.55f && near(f.rgb[0], 1.0f) && near(f.rgb[1], 0.0f));
    // overshoot and negatives clamp
    Tint big;
    big.aLo = 3.0f;
    big.aHi = -1.0f;
    big.lo[0] = 2.0f;
    const Flat fb = flatten(big);
    CHECK(fb.a >= 0 && fb.a <= 1 && fb.rgb[0] <= 1.0f);
    CHECK(flatten(Tint()).a == 0.0f);
}

static void testFighter() {
    Library lib;
    lib.parse(kTable);
    const PalLookup none;
    FighterFx fx;
    CHECK(!fx.any());
    Tint t;
    CHECK(!fx.primary(none, t) && !fx.secondary(none, t) && fx.flat(none).a == 0.0f);

    std::array<uint32_t, kLayers> h{};
    h[kHit] = nameHash("AllBlack");
    fx.update(h, lib);
    CHECK(fx.any() && fx.primary(none, t) && near(t.aHi, 1.0f));
    Flat f = fx.flat(none);
    CHECK(near(f.a, 1.0f) && near(f.rgb[0], 0.0f));

    // the hit layer is on top of self: self is red 50%, hit is black 100% -> black
    Library l2;
    l2.parse("R\n[255 0 0 255 0 0] 128\nEND\nK\n[0 0 0 0 0 0] 255\nEND\n");
    FighterFx g;
    h = {};
    h[kSelf] = nameHash("R");
    g.update(h, l2);
    f = g.flat(none);
    CHECK(near(f.a, 128.0f / 255.0f, 1e-3f) && near(f.rgb[0], 1.0f));
    h[kHit] = nameHash("K");
    g.update(h, l2);
    f = g.flat(none);
    CHECK(near(f.a, 1.0f) && near(f.rgb[0], 0.0f, 1e-3f));
    // a masked self2 never paints the whole sprite; it is the shader's second pair
    FighterFx m;
    h = {};
    h[kSelf2] = nameHash("Masked");
    m.update(h, lib);
    CHECK(m.any() && m.flat(none).a == 0.0f && !m.primary(none, t) && m.secondary(none, t) && t.maskCount == 2);
    // an unmasked self2 is just another wash
    FighterFx u;
    h = {};
    h[kSelf2] = nameHash("R");
    u.update(h, l2);
    CHECK(u.flat(none).a > 0.4f && !u.secondary(none, t));
    u.reset();
    CHECK(!u.any());
}

static void testRealTable() {
    const char* p = std::getenv("ZDROID_TEST_PALFX");
    if (!p || !*p) {
        std::printf("  (no ZDROID_TEST_PALFX: skipping the real table)\n");
        return;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        std::printf("  (cannot read %s: skipping the real table)\n", p);
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    Library lib;
    const size_t n = lib.parse(ss.str());
    std::printf("  real table: %zu effects, %zu name collisions\n", n, lib.collisions());
    CHECK(n > 50);
    CHECK(lib.collisions() == 0);
    // hashes a running fight raised, found by name
    for (const char* name : {"CounterHit", "HatredGuard", "AnnieStarCancel", "Beo_HypeLv1", "AnnieFlame"}) {
        const Effect* e = lib.find(nameHash(name));
        CHECK(e != nullptr);
        if (e) CHECK(!e->frames.empty());
    }
    // every effect plays through: each frame resolves, a looping one never ends, a non-looping one ends exactly after its last frame
    const PalLookup none;
    for (const char* name : {"CounterHit", "HatredGuard", "AnnieStarCancel", "AnnieFlame", "AnnieStarCharge"}) {
        const Effect* e = lib.byName(name);
        if (!e) continue;
        const int cnt = static_cast<int>(e->frames.size());
        CHECK(e->frameAt(cnt - 1) == cnt - 1);
        if (e->loopFrom >= 0) CHECK(e->frameAt(cnt * 50) >= e->loopFrom);
        else CHECK(e->frameAt(cnt) == -1);
        for (int i = 0; i < cnt; ++i) {
            const Tint t = resolve(*e, e->frames[static_cast<size_t>(i)], none);
            CHECK(std::isfinite(t.aHi) && std::isfinite(t.aLo));
        }
    }
}

int main() {
    std::printf("[palfx] name hash\n");
    testHash();
    std::printf("[palfx] table parser\n");
    testParse();
    std::printf("[palfx] playback\n");
    testPlayback();
    std::printf("[palfx] resolving frames\n");
    testResolve();
    std::printf("[palfx] a fighter's layers\n");
    testFighter();
    std::printf("[palfx] real table\n");
    testRealTable();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
