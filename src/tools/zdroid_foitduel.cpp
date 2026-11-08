// zdroid_foitduel - runs the data-driven fight (foit_duel.h) through the same GGPO-shaped sessions the app uses and checks it.
//
//   zdroid_foitduel <p1.foit> [<p2.foit>] [--p1 "30:R,20:,1:LP"] [--p2 "..."] [--ticks N] [--seconds S] [--rounds R]
//                   [--intro] [--ready N] [--hold N] [--training] [--tiebreak] [--dizzy <_dizzy_values.ini>]
//                   [--synctest] [--rollback K,N] [--trace EVERY] [--gap D]
//
// Input script: comma separated TICKS:BUTTONS, buttons joined with '+': U D L R LP MP HP LK MK HK (empty = neutral).  After the
// scripts the fighters stand still until --ticks (default: until the match is over, at most 40000 ticks).
//   --synctest        run through SyncTestSession: every tick is replayed from a snapshot and the checksums must agree
//   --rollback K,N    at tick K save, run N ticks, load the save and run them again: every checksum must repeat (a deep rollback)
// Prints the phase changes of the round flow and, with --trace, one line every EVERY ticks.  Exit code 1 on any mismatch.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "fight.h"
#include "foit.h"
#include "foit_duel.h"
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

const char* phaseName(int p) {
    switch (p) {
        case foit::rt::Match::Intro: return "Intro";
        case foit::rt::Match::Ready: return "Ready";
        case foit::rt::Match::Fight: return "Fight";
        case foit::rt::Match::Settle: return "Settle";
        case foit::rt::Match::Ending: return "Ending";
        case foit::rt::Match::Over: return "Over";
        default: return "Off";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    std::string s1, s2, dizzyFile;
    int ticks = 0, seconds = 99, rounds = 2, ready = 0, hold = 0, trace = 0, gap = 440;
    int rbAt = -1, rbLen = 0;
    bool synctest = false, training = false, intro = false, tieBreak = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--p1") s1 = next();
        else if (a == "--p2") s2 = next();
        else if (a == "--ticks") ticks = std::atoi(next().c_str());
        else if (a == "--seconds") seconds = std::atoi(next().c_str());
        else if (a == "--rounds") rounds = std::atoi(next().c_str());
        else if (a == "--ready") ready = std::atoi(next().c_str());
        else if (a == "--hold") hold = std::atoi(next().c_str());
        else if (a == "--trace") trace = std::atoi(next().c_str());
        else if (a == "--gap") gap = std::atoi(next().c_str());
        else if (a == "--intro") intro = true;
        else if (a == "--training") training = true;
        else if (a == "--synctest") synctest = true;
        else if (a == "--tiebreak") tieBreak = true;
        else if (a == "--dizzy") dizzyFile = next();
        else if (a == "--rollback") {
            const std::string v = next();
            rbAt = std::atoi(v.c_str());
            const size_t c = v.find(',');
            rbLen = c == std::string::npos ? 8 : std::atoi(v.c_str() + c + 1);
        } else files.push_back(a);
    }
    if (files.empty()) {
        std::printf("usage: zdroid_foitduel <p1.foit> [<p2.foit>] [--p1 script] [--p2 script] [--ticks N] [--synctest] ...\n");
        return 2;
    }
    if (files.size() < 2) files.push_back(files[0]);
    fight::FoitDuelConfig cfg;
    for (int i = 0; i < 2; ++i) {
        cfg.ch[i] = load(files[static_cast<size_t>(i)]);
        if (!cfg.ch[i]) return 2;
    }
    cfg.startX[0] = -gap / 2.0f;
    cfg.startX[1] = gap / 2.0f;
    cfg.roundSeconds = seconds;
    cfg.roundsToWin = rounds;
    cfg.training = training;
    cfg.tieBreak = tieBreak;
    cfg.introPoses = intro;
    cfg.readyTicks = ready;
    cfg.endHold = hold;
    if (dizzyFile.empty()) {
        const size_t sl = files[0].find_last_of("/\\");
        dizzyFile = (sl == std::string::npos ? std::string() : files[0].substr(0, sl + 1)) + "_dizzy_values.ini";
    }
    {
        std::vector<uint8_t> d;
        if (readFile(dizzyFile, d)) cfg.dizzyText.assign(d.begin(), d.end());
    }
    fight::FoitDuel duel;
    if (!duel.init(cfg)) {
        std::printf("init failed\n");
        return 2;
    }
    const std::vector<uint16_t> sc1 = parseScript(s1), sc2 = parseScript(s2);

    fight::MatchState st;
    duel.project(st);
    std::unique_ptr<fight::Session> sess;
    fight::SyncTestSession* sync = nullptr;
    if (synctest) {
        auto s = std::make_unique<fight::SyncTestSession>(st, fight::foitDuelCallbacks(&duel), 1);
        sync = s.get();
        sess = std::move(s);
    } else {
        sess = std::make_unique<fight::LocalSession>(st, fight::foitDuelCallbacks(&duel));
    }

    int failures = 0;
    int lastPhase = -1, lastRound = -1;
    const int maxTicks = ticks > 0 ? ticks : 40000;
    std::vector<uint8_t> saved;
    std::vector<uint32_t> sums;
    std::vector<std::pair<uint16_t, uint16_t>> played;
    const auto t0 = std::chrono::steady_clock::now();
    int t = 0;
    for (; t < maxTicks; ++t) {
        const uint16_t in1 = static_cast<size_t>(t) < sc1.size() ? sc1[static_cast<size_t>(t)] : 0;
        const uint16_t in2 = static_cast<size_t>(t) < sc2.size() ? sc2[static_cast<size_t>(t)] : 0;
        if (t == rbAt) {
            fight::foitDuelCallbacks(&duel).save(st, saved, &duel);
            sums.clear();
            played.clear();
        }
        int sim = 0;
        sess->addLocalInput(in1, in2, &sim);
        if (t >= rbAt && rbAt >= 0 && static_cast<int>(sums.size()) < rbLen) {
            sums.push_back(st.checksum());
            played.push_back({in1, in2});
            if (static_cast<int>(sums.size()) == rbLen) {   // replay from the save: every checksum must come out the same
                std::vector<uint8_t> now;
                fight::foitDuelCallbacks(&duel).save(st, now, &duel);   // where the live run is
                fight::MatchState back;
                if (!fight::foitDuelCallbacks(&duel).load(back, saved, &duel)) {
                    std::printf("ROLLBACK load failed at tick %d\n", rbAt);
                    ++failures;
                } else {
                    for (size_t k = 0; k < played.size(); ++k) {
                        fight::foitDuelCallbacks(&duel).advance(back, played[k].first, played[k].second, &duel);
                        if (back.checksum() != sums[k]) {
                            std::printf("ROLLBACK MISMATCH: tick %d (+%zu) %08x != %08x\n", rbAt, k, back.checksum(), sums[k]);
                            ++failures;
                            break;
                        }
                    }
                    if (failures == 0) std::printf("rollback %d ticks from tick %d: all %zu checksums repeat\n", rbLen, rbAt, played.size());
                    // the replay left the world exactly where the live run was: continue from the live snapshot either way
                    fight::foitDuelCallbacks(&duel).load(st, now, &duel);
                }
            }
        }
        const int ph = duel.match().phase;
        if (ph != lastPhase || duel.round() != lastRound) {
            std::printf("t=%5d  round %d  %-6s  clock %3d  hp %4d/%4d  meter %4d/%4d  wins %d-%d", t, duel.round(), phaseName(ph),
                        st.roundTimer / 60, st.p[0].health, st.p[1].health, st.p[0].meter, st.p[1].meter, duel.wins(0), duel.wins(1));
            if (ph == foit::rt::Match::Ending) std::printf("  winner %d reason %d", duel.roundWinner(), duel.roundReason());
            if (ph == foit::rt::Match::Over) std::printf("  MATCH WINNER %d", duel.matchWinner());
            std::printf("\n");
            lastPhase = ph;
            lastRound = duel.round();
        }
        if (trace > 0 && t % trace == 0) {
            const auto& a = duel.body(0);
            const auto& b = duel.body(1);
            std::printf("  t=%5d  %-6s  P1 x %7.1f y %6.1f hp %4d st %-14s fr %4d | P2 x %7.1f y %6.1f hp %4d st %-14s fr %4d\n", t,
                        phaseName(ph), a.x, a.y, st.p[0].health, a.def->states[static_cast<size_t>(a.state)].name.c_str(), a.frame, b.x, b.y,
                        st.p[1].health, b.def->states[static_cast<size_t>(b.state)].name.c_str(), b.frame);
        }
        if (ticks == 0 && duel.over() && static_cast<size_t>(t) >= std::max(sc1.size(), sc2.size())) break;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%d ticks in %.1f ms (%.1f us/tick)%s\n", t, ms, ms * 1000.0 / std::max(1, t), synctest ? " incl. synctest replay" : "");
    if (sync) {
        std::printf("synctest: %u frames checked, %u mismatches\n", sync->framesChecked(), sync->mismatches());
        if (sync->mismatches() != 0) ++failures;
    }
    std::vector<uint8_t> snap;
    duel.save(snap);
    std::printf("snapshot %zu bytes, hash %016llx\n", snap.size(), static_cast<unsigned long long>(duel.hash()));
    return failures == 0 ? 0 : 1;
}
