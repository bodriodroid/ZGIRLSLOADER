// test_chord_assist.cpp - fight::ChordAssist: two attack buttons a few ticks apart reach the fight as one chord, nothing else changes.
// The last block (only with ZDROID_TEST_CHARS = the player's own characters folder) drives a REAL character's super through the real fight
// with the two punches landing 0..5 ticks apart, with and without the assist.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "chord_assist.h"
#include "fight.h"
#include "foit.h"
#include "foit_duel.h"
#include "vfs.h"

using namespace zengine;
using namespace zengine::fight;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                     \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(c)) {                                                                  \
            ++g_fail;                                                                \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c);                       \
        }                                                                            \
    } while (0)

// runs `in` through the assist, returns what the fight sees
static std::vector<uint16_t> runAssist(const std::vector<uint16_t>& in, bool active = true, int window = 3) {
    ChordAssist a(window);
    std::vector<uint16_t> out;
    for (uint16_t b : in) out.push_back(a.filter(b, active));
    return out;
}
// the tick (index) at which `bit` first shows, -1 = never
static int firstTick(const std::vector<uint16_t>& v, uint16_t bit) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] & bit) return static_cast<int>(i);
    return -1;
}
// two buttons: a at tick 2, b at tick 2 + spread, both held until tick 20
static std::vector<uint16_t> twoPress(uint16_t a, uint16_t b, int spread) {
    std::vector<uint16_t> in(30, 0);
    for (int t = 2; t < 20; ++t) in[static_cast<size_t>(t)] |= a;
    for (int t = 2 + spread; t < 20; ++t) in[static_cast<size_t>(t)] |= b;
    return in;
}

static std::shared_ptr<foit::Character> loadChar(const std::string& path) {
    std::vector<uint8_t> d;
    if (!readFile(path, d)) return nullptr;
    auto ch = std::make_shared<foit::Character>();
    std::string err;
    if (!foit::parse(d.data(), d.size(), *ch, &err)) return nullptr;
    return ch;
}
static std::string stateName(const FoitDuel& d, int side) {
    const auto& b = d.body(side);
    if (!b.def || b.state < 0 || b.state >= static_cast<int>(b.def->states.size())) return "?";
    return b.def->states[static_cast<size_t>(b.state)].name;
}
// R, D, DR + two punches `spread` ticks apart (Filia's Fenrir Drive); true when `target` was entered.
static bool superFires(const std::shared_ptr<foit::Character>& ch, const std::string& target, int spread, bool assist, int bars) {
    FoitDuelConfig cfg;
    cfg.ch[0] = cfg.ch[1] = ch;
    cfg.introPoses = false;
    cfg.roundSeconds = 0;
    cfg.training = true;
    FoitDuel d;
    if (!d.init(cfg)) return false;
    d.setStart(0, 100, bars);
    for (int i = 0; i < 90; ++i) d.step(0, 0);
    std::vector<uint16_t> in(6, 0);
    for (int i = 0; i < 3; ++i) in.push_back(kRight);
    for (int i = 0; i < 3; ++i) in.push_back(kDown);
    const size_t land = in.size();
    for (int i = 0; i < 14; ++i) in.push_back(static_cast<uint16_t>(kDown | kRight));
    for (size_t i = land; i < in.size(); ++i) {
        in[i] |= kLP;
        if (static_cast<int>(i - land) >= spread) in[i] |= kMP;
    }
    for (int i = 0; i < 20; ++i) in.push_back(0);
    ChordAssist a(assist ? 3 : 0);
    bool fired = false;
    for (uint16_t raw : in) {
        d.step(a.filter(raw, bars >= 1), 0);
        if (stateName(d, 0) == target) fired = true;
    }
    return fired;
}

int main() {
    const uint16_t P = kLP | kMP | kHP;
    // ---- inactive / window 0: nothing changes
    {
        const auto in = twoPress(kLP, kMP, 2);
        CHECK(runAssist(in, false) == in);
        CHECK(runAssist(in, true, 0) == in);
    }
    // ---- a lone press waits exactly `window` ticks, then stays down until released
    {
        std::vector<uint16_t> in(20, 0);
        for (int t = 2; t < 10; ++t) in[static_cast<size_t>(t)] = kLP;
        const auto out = runAssist(in);
        CHECK(firstTick(out, kLP) == 5);   // pressed at 2, out at 2 + 3
        for (int t = 5; t < 10; ++t) CHECK(out[static_cast<size_t>(t)] & kLP);
        CHECK(!(out[10] & kLP));
        for (int w : {1, 2, 4}) CHECK(firstTick(runAssist(in, true, w), kLP) == 2 + w);
    }
    // ---- a chord: the partner within the window goes out together with the first, on the partner's tick
    for (int spread = 0; spread <= 3; ++spread) {
        const auto out = runAssist(twoPress(kLP, kMP, spread));
        CHECK(firstTick(out, kLP) == 2 + spread);
        CHECK(firstTick(out, kMP) == 2 + spread);
        const auto outK = runAssist(twoPress(kLK, kHK, spread));
        CHECK(firstTick(outK, kLK) == 2 + spread && firstTick(outK, kHK) == 2 + spread);
    }
    // ...too late: the first is out alone, the second follows on its own
    {
        const auto out = runAssist(twoPress(kLP, kMP, 4));
        CHECK(firstTick(out, kLP) == 5);
        CHECK(firstTick(out, kMP) == 6);
    }
    // ---- a tap shorter than the window is still delivered (one tick of press), never lost
    {
        std::vector<uint16_t> in(20, 0);
        in[4] = kHP;
        in[5] = kHP;   // two ticks down, then up
        const auto out = runAssist(in);
        int pressed = 0;
        for (uint16_t b : out)
            if (b & kHP) ++pressed;
        CHECK(pressed >= 1);
        CHECK(firstTick(out, kHP) >= 4);
    }
    // ---- groups are independent: a punch and a kick are NOT merged
    {
        std::vector<uint16_t> in(20, 0);
        for (int t = 2; t < 12; ++t) in[static_cast<size_t>(t)] |= kLP;
        for (int t = 3; t < 12; ++t) in[static_cast<size_t>(t)] |= kLK;
        const auto out = runAssist(in);
        CHECK(firstTick(out, kLP) == 5 && firstTick(out, kLK) == 6);
    }
    // ---- a second button while the first is already out is not delayed
    {
        std::vector<uint16_t> in(30, 0);
        for (int t = 2; t < 25; ++t) in[static_cast<size_t>(t)] |= kLP;
        for (int t = 12; t < 25; ++t) in[static_cast<size_t>(t)] |= kMP;
        const auto out = runAssist(in);
        CHECK(firstTick(out, kLP) == 5);
        CHECK(firstTick(out, kMP) == 12);
    }
    // ---- three punches: LP + MP together, HP late and on its own tick
    {
        std::vector<uint16_t> in(30, 0);
        for (int t = 2; t < 25; ++t) in[static_cast<size_t>(t)] |= kLP;
        for (int t = 3; t < 25; ++t) in[static_cast<size_t>(t)] |= kMP;
        for (int t = 5; t < 25; ++t) in[static_cast<size_t>(t)] |= kHP;
        const auto out = runAssist(in);
        CHECK(firstTick(out, kLP) == 3 && firstTick(out, kMP) == 3);
        CHECK(firstTick(out, kHP) == 5);
        const auto in3 = [&] {
            std::vector<uint16_t> v(30, 0);
            for (int t = 2; t < 25; ++t) v[static_cast<size_t>(t)] |= kLP;
            for (int t = 3; t < 25; ++t) v[static_cast<size_t>(t)] |= kMP;
            for (int t = 4; t < 25; ++t) v[static_cast<size_t>(t)] |= kHP;
            return v;
        }();
        const auto o3 = runAssist(in3);
        CHECK(firstTick(o3, kHP) == 4 && (o3[4] & P) == P - 0);   // the third lands right after the pair, in the same tick it arrives
    }
    // ---- directions / Start are never touched, and the super bar running out lets a held-back press go at once
    {
        std::vector<uint16_t> in(20, 0);
        for (int t = 0; t < 20; ++t) in[static_cast<size_t>(t)] = kRight | kDown | kStart;
        for (uint16_t b : runAssist(in)) CHECK((b & (kRight | kDown | kStart)) == (kRight | kDown | kStart));
        ChordAssist a(3);
        CHECK((a.filter(kLP, true) & kLP) == 0);    // held back
        CHECK((a.filter(kLP, false) & kLP) != 0);   // the bar is gone: out now
    }
    // ---- the same raw input twice gives the same output (no hidden state beyond reset)
    {
        const auto in = twoPress(kLP, kMP, 2);
        CHECK(runAssist(in) == runAssist(in));
        ChordAssist a(3);
        std::vector<uint16_t> o1, o2;
        for (uint16_t b : in) o1.push_back(a.filter(b, true));
        a.reset();
        for (uint16_t b : in) o2.push_back(a.filter(b, true));
        CHECK(o1 == o2);
    }

    // ---- a real character's super through the real fight (skipped without the data)
    if (const char* dir = std::getenv("ZDROID_TEST_CHARS")) {
        const auto ch = loadChar(std::string(dir) + "/filia.foit");
        if (ch) {
            const std::string sup = "s.Fenrir_Drive";
            for (int spread = 0; spread <= 1; ++spread) CHECK(superFires(ch, sup, spread, false, 5));   // the game's own leniency
            for (int spread = 2; spread <= 5; ++spread) CHECK(!superFires(ch, sup, spread, false, 5));  // ...ends at 1 tick: the bug on a phone
            for (int spread = 0; spread <= 3; ++spread) CHECK(superFires(ch, sup, spread, true, 5));    // with the assist
            CHECK(!superFires(ch, sup, 5, true, 5));
            for (int spread = 0; spread <= 3; ++spread) CHECK(!superFires(ch, sup, spread, true, 0));   // no bar: still no super
            CHECK(superFires(ch, sup, 0, true, 1));
            std::printf("  (real data: filia %s)\n", sup.c_str());
        } else {
            std::printf("  (ZDROID_TEST_CHARS set but filia.foit could not be read: real-data block skipped)\n");
        }
    }
    std::printf("chord assist tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
