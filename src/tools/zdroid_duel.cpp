// zdroid_duel - runs the move-driven fight runtime (duel.h) headless on the user's own <char>.foit files and
// prints what happens, tick by tick.  Used to check the numbers (walk speed, jump arc, hit timing, damage)
// without a window.
//
//   zdroid_duel <p1.foit> [<p2.foit>] [--p1 "30:R,20:,1:LP"] [--p2 "..."] [--ticks N] [--trace] [--gap 300] [--hp1 N] [--hp2 N]
//
// Input script: comma separated TICKS:BUTTONS, buttons joined with '+': U D L R LP MP HP LK MK HK
// (an empty button list = neutral).  P1 starts on the left facing right, P2 `--gap` units to the right.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "duel.h"
#include "foit.h"
#include "moveset.h"
#include "vfs.h"

using namespace zengine;

namespace {

uint16_t parseButtons(const std::string& s) {
    uint16_t b = 0;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, '+')) {
        if (tok == "U") b |= fight::kUp;
        else if (tok == "D") b |= fight::kDown;
        else if (tok == "L") b |= fight::kLeft;
        else if (tok == "R") b |= fight::kRight;
        else if (tok == "LP") b |= fight::kLP;
        else if (tok == "MP") b |= fight::kMP;
        else if (tok == "HP") b |= fight::kHP;
        else if (tok == "LK") b |= fight::kLK;
        else if (tok == "MK") b |= fight::kMK;
        else if (tok == "HK") b |= fight::kHK;
    }
    return b;
}

std::vector<uint16_t> parseScript(const std::string& s) {
    std::vector<uint16_t> out;
    std::stringstream ss(s);
    std::string seg;
    while (std::getline(ss, seg, ',')) {
        const size_t c = seg.find(':');
        const int n = std::atoi(seg.substr(0, c).c_str());
        const uint16_t b = c == std::string::npos ? 0 : parseButtons(seg.substr(c + 1));
        for (int i = 0; i < n; ++i) out.push_back(b);
    }
    return out;
}

std::shared_ptr<foit::Character> load(const std::string& path) {
    std::vector<uint8_t> d;
    if (!readFile(path, d)) {
        std::printf("cannot read %s\n", path.c_str());
        return nullptr;
    }
    auto ch = std::make_shared<foit::Character>();
    std::string err;
    if (!foit::parse(d.data(), d.size(), *ch, &err)) {
        std::printf("%s: %s\n", path.c_str(), err.c_str());
        return nullptr;
    }
    return ch;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    std::string s1, s2;
    int ticks = 0, gap = 300, hp[2] = {1000, 1000};
    bool trace = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--p1" && i + 1 < argc) s1 = argv[++i];
        else if (a == "--p2" && i + 1 < argc) s2 = argv[++i];
        else if (a == "--ticks" && i + 1 < argc) ticks = std::atoi(argv[++i]);
        else if (a == "--gap" && i + 1 < argc) gap = std::atoi(argv[++i]);
        else if (a == "--hp1" && i + 1 < argc) hp[0] = std::atoi(argv[++i]);
        else if (a == "--hp2" && i + 1 < argc) hp[1] = std::atoi(argv[++i]);
        else if (a == "--trace") trace = true;
        else files.push_back(a);
    }
    if (files.empty()) {
        std::printf("usage: zdroid_duel <p1.foit> [<p2.foit>] [--p1 script] [--p2 script] [--ticks N] [--trace] [--gap N]\n");
        return 1;
    }
    if (files.size() < 2) files.push_back(files[0]);
    auto c1 = load(files[0]);
    auto c2 = files[1] == files[0] ? c1 : load(files[1]);
    if (!c1 || !c2) return 1;
    fight::MoveSet m1, m2;
    std::string err;
    if (!m1.build(c1, &err) || !m2.build(c2, &err)) {
        std::printf("moveset: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s: walk %.2f/%.2f dash %.1f/%.1f jump vy %.1f vx %.1f gravity %.2f | %zu states, %zu hit records\n",
                m1.name().c_str(), m1.walkF, m1.walkB, m1.dashF, m1.dashB, m1.jumpVy, m1.jumpVx, m1.gravity,
                static_cast<size_t>(m1.stateCount()), m1.hitCount());

    const std::vector<uint16_t> a = parseScript(s1), b = parseScript(s2);
    if (ticks <= 0) ticks = static_cast<int>(std::max(a.size(), b.size()));

    fight::DuelContent content;
    content.ms[0] = &m1;
    content.ms[1] = &m2;
    fight::MatchState st{};
    fight::duelStartRound(st.p[0], m1, fight::toFixed(static_cast<float>(-gap / 2)), fight::Facing::kRight, 0);
    fight::duelStartRound(st.p[1], m2, fight::toFixed(static_cast<float>(gap / 2)), fight::Facing::kLeft, 0);
    st.p[0].state = fight::CharState::kIdle;
    st.p[1].state = fight::CharState::kIdle;
    st.p[0].health = hp[0];
    st.p[1].health = hp[1];
    st.roundTimer = 5940;

    // determinism check: a second copy driven through a snapshot round trip must agree every tick
    fight::MatchState twin = st;
    uint32_t mismatches = 0;
    float maxY[2] = {0, 0};
    for (int t = 0; t < ticks; ++t) {
        const uint16_t i1 = t < static_cast<int>(a.size()) ? a[static_cast<size_t>(t)] : 0;
        const uint16_t i2 = t < static_cast<int>(b.size()) ? b[static_cast<size_t>(t)] : 0;
        fight::advanceDuel(st, i1, i2, content);
        std::vector<uint8_t> snap = fight::saveState(twin);
        fight::MatchState copy{};
        fight::loadState(copy, snap);
        fight::advanceDuel(copy, i1, i2, content);
        twin = copy;
        if (copy.checksum() != st.checksum()) ++mismatches;
        for (int p = 0; p < 2; ++p) maxY[p] = std::max(maxY[p], fight::fromFixed(st.p[p].y));
        if (trace) {
            std::printf("t=%3d", t + 1);
            for (int p = 0; p < 2; ++p) {
                const fight::Fighter& f = st.p[p];
                const fight::MoveSet& m = p ? m2 : m1;
                std::printf(" | P%d %-12s %-22s +%-3u fr%-5d x=%7.1f y=%6.1f hp=%4d stop=%u", p + 1, fight::stateName(f.state),
                            m.stateName(f.mv).c_str(), f.mvTick, static_cast<int>(static_cast<int16_t>(m.frameAt(f.mv, f.mvTick))),
                            fight::fromFixed(f.x), fight::fromFixed(f.y), f.health, f.hitstop);
            }
            std::printf("\n");
        }
    }
    std::printf("after %d ticks: P1 x=%.1f hp=%d state=%s (%s) | P2 x=%.1f hp=%d state=%s (%s) | max height %.1f / %.1f | snapshot mismatches %u\n",
                ticks, fight::fromFixed(st.p[0].x), st.p[0].health, fight::stateName(st.p[0].state),
                m1.stateName(st.p[0].mv).c_str(), fight::fromFixed(st.p[1].x), st.p[1].health,
                fight::stateName(st.p[1].state), m2.stateName(st.p[1].mv).c_str(), maxY[0], maxY[1], mismatches);
    return mismatches ? 2 : 0;
}
