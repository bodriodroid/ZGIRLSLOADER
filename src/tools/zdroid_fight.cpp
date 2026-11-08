// zdroid_fight - headless harness for the deterministic match runtime.
//
//   zdroid_fight                 run 600 ticks of both players walking into each other
//   zdroid_fight --synctest N    GGPO-style determinism check over N frames
//   zdroid_fight --replay <bin>  replay a recorded input file and print the checksum
//   zdroid_fight --record <bin>  write the scripted inputs out for later replay
//
// The point of this tool is the same as GGPO's own synctest: run the simulation,
// roll a frame back to a saved snapshot, re-run it, and confirm the checksums
// match.  Without that property rollback netcode cannot work, and a port that
// disagrees with the PC build frame-for-frame is not "playable" online.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "fight.h"
#include "vfs.h"

using namespace zengine;
using namespace zengine::fight;

namespace {

// A short scripted exchange: approach, jab, block, jump in.
uint16_t scriptP1(uint32_t f) {
    uint16_t b = 0;
    if (f < 90) b |= kRight;
    if (f == 90 || f == 140 || f == 200) b |= kLP;
    if (f == 160 || f == 210) b |= kMP;
    if (f == 240) b |= kHP;
    if (f == 300) b |= kUp;
    if (f > 300 && f < 320) b |= kRight;
    if (f == 320) b |= kHK;
    return b;
}

uint16_t scriptP2(uint32_t f) {
    uint16_t b = 0;
    if (f < 60) b |= kLeft;
    if (f >= 100 && f < 160) b |= kRight;  // hold "away" => block
    if (f == 165 || f == 215) b |= kLK;
    if (f >= 250 && f < 300) b |= kDown;
    if (f == 330) b |= kMK;
    return b;
}

void dumpFighter(const char* tag, const Fighter& f) {
    std::printf("  %s %-16s hp=%-5d meter=%-5d undizzy=%-5d x=%+8.2f y=%6.2f combo=%u\n", tag,
                stateName(f.state), f.health, f.meter, f.undizzy, fromFixed(f.x), fromFixed(f.y),
                f.comboCount);
}

int cmdRun(int frames, bool verbose) {
    MatchState s;
    s.p[0].x = toFixed(-220.0f);
    s.p[1].x = toFixed(220.0f);
    s.p[0].facing = Facing::kRight;
    s.p[1].facing = Facing::kLeft;
    s.roundTimer = 5940;  // 99 seconds at 60Hz

    LocalSession sess(s, defaultCallbacks());
    for (int i = 0; i < frames; ++i) {
        const uint16_t a = scriptP1(s.frame), b = scriptP2(s.frame);
        int sim = 0;
        sess.addLocalInput(a, &sim);
        if (sim) advanceFrame(s, a, b);
        if (verbose && i % 30 == 0) {
            std::printf("frame %4u  p1:{%s}  p2:{%s}\n", s.frame, describeButtons(a).c_str(),
                        describeButtons(b).c_str());
            dumpFighter("P1", s.p[0]);
            dumpFighter("P2", s.p[1]);
        }
        if (s.roundOver) {
            std::printf("round over at frame %u\n", s.frame);
            break;
        }
    }
    std::printf("final frame=%u checksum=%08u\n", s.frame, s.checksum());
    dumpFighter("P1", s.p[0]);
    dumpFighter("P2", s.p[1]);
    return 0;
}

int cmdSynctest(int frames) {
    MatchState s;
    s.p[0].x = toFixed(-220.0f);
    s.p[1].x = toFixed(220.0f);
    s.p[0].facing = Facing::kRight;
    s.p[1].facing = Facing::kLeft;
    s.roundTimer = 5940;

    SyncTestSession sess(s, defaultCallbacks(), 1);
    for (int i = 0; i < frames; ++i) {
        const uint16_t a = scriptP1(s.frame), b = scriptP2(s.frame);
        int sim = 0;
        sess.addLocalInput(a, &sim);
        if (sim) advanceFrame(s, a, b);
        if (s.roundOver) break;
    }
    std::printf("synctest: %u frame(s) checked, %u mismatch(es)\n", sess.framesChecked(),
                sess.mismatches());
    std::printf("final frame=%u checksum=%08u\n", s.frame, s.checksum());
    return sess.mismatches() ? 1 : 0;
}

int cmdRecord(const std::string& path, int frames) {
    std::vector<uint8_t> out;
    auto push16 = [&](uint16_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    };
    for (int f = 0; f < frames; ++f) {
        push16(scriptP1(f));
        push16(scriptP2(f));
    }
    {
        std::ofstream f(path, std::ios::binary);
        if (!f || !f.write(reinterpret_cast<const char*>(out.data()),
                           static_cast<std::streamsize>(out.size()))) {
            std::printf("cannot write %s\n", path.c_str());
            return 1;
        }
    }
    std::printf("recorded %d frames -> %s (%zu bytes)\n", frames, path.c_str(), out.size());
    return 0;
}

int cmdReplay(const std::string& path, int frames) {
    std::vector<uint8_t> in;
    if (!readFile(path, in)) {
        std::printf("cannot read %s\n", path.c_str());
        return 1;
    }
    const std::size_t n = in.size() / 4;
    MatchState s;
    s.p[0].x = toFixed(-220.0f);
    s.p[1].x = toFixed(220.0f);
    s.p[0].facing = Facing::kRight;
    s.p[1].facing = Facing::kLeft;
    s.roundTimer = 5940;

    const std::size_t limit = (frames > 0 && static_cast<std::size_t>(frames) < n)
                                  ? static_cast<std::size_t>(frames)
                                  : n;
    for (std::size_t f = 0; f < limit; ++f) {
        const uint16_t a = static_cast<uint16_t>(in[f * 4] | (in[f * 4 + 1] << 8));
        const uint16_t b = static_cast<uint16_t>(in[f * 4 + 2] | (in[f * 4 + 3] << 8));
        advanceFrame(s, a, b);
        if (s.roundOver) break;
    }
    std::printf("replayed %zu frames -> checksum=%08u\n", limit, s.checksum());
    dumpFighter("P1", s.p[0]);
    dumpFighter("P2", s.p[1]);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    int frames = 600;
    bool synctest = false, verbose = false;
    std::string record, replay;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--synctest") {
            synctest = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') frames = std::atoi(argv[++i]);
        } else if (a == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a == "--verbose" || a == "-v") {
            verbose = true;
        } else if (a == "--record" && i + 1 < argc) {
            record = argv[++i];
        } else if (a == "--replay" && i + 1 < argc) {
            replay = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: zdroid_fight [--frames N] [--verbose] [--synctest N]\n"
                "                 [--record file] [--replay file]\n");
            return 0;
        }
    }

    if (!replay.empty()) return cmdReplay(replay, frames);
    if (!record.empty()) return cmdRecord(record, frames > 0 ? frames : 600);
    if (synctest) return cmdSynctest(frames);
    return cmdRun(frames, verbose);
}
