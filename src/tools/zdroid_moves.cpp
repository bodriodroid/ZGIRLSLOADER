// zdroid_moves - what the fight runtime makes of a character's .foit.
//
//   zdroid_moves <a.foit> [<b.foit> ...]            key states, the 18 normals and timeline sanity for each file
//   zdroid_moves <a.foit> --states                  every state: ticks, frames shown, hit data armed, next state
//
// The runtime drives a character through a handful of state names ("Stand", "WalkFwd", "s.WP", "j.HK", ...).  A character that
// does not resolve one of them cannot do that action (a normal that maps to -1 simply never comes out), so this prints which
// ones are missing, and flags timelines that cannot end, point at nothing or show frames that do not exist.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "foit.h"
#include "moveset.h"

using namespace zengine;
using namespace zengine::fight;

static bool readAll(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

static const char* kBtn[kNormalCount] = {"LP", "MP", "HP", "LK", "MK", "HK"};
static const char* kPost[kPostureCount] = {"stand", "crouch", "air"};

int main(int argc, char** argv) {
    std::vector<std::string> files;
    bool allStates = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--states")) allStates = true;
        else files.push_back(argv[i]);
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: zdroid_moves <char.foit>... [--states]\n");
        return 2;
    }
    int rc = 0;
    for (const std::string& path : files) {
        std::vector<uint8_t> bytes;
        auto ch = std::make_shared<foit::Character>();
        std::string err;
        if (!readAll(path, bytes) || !foit::parse(bytes.data(), bytes.size(), *ch, &err)) {
            std::printf("== %s: cannot parse (%s)\n", path.c_str(), err.c_str());
            rc = 1;
            continue;
        }
        MoveSet ms;
        if (!ms.build(ch, &err)) {
            std::printf("== %s: MoveSet::build failed (%s)\n", path.c_str(), err.c_str());
            rc = 1;
            continue;
        }
        const size_t nFrames = ch->frames.size();
        std::printf("== %s  (%s): %d states, %zu frames, %zu hit records\n", path.c_str(), ms.name().c_str(), ms.stateCount(), nFrames, ms.hitCount());

        // ---- key states
        struct Key { const char* what; int id; };
        const MoveSet::Ids& d = ms.id;
        const Key keys[] = {
            {"stand", d.stand}, {"crouch", d.crouch}, {"crouching", d.crouching}, {"uncrouching", d.uncrouching},
            {"walkF", d.walkF}, {"walkFLoop", d.walkFLoop}, {"walkB", d.walkB}, {"walkBLoop", d.walkBLoop},
            {"dashF", d.dashF}, {"dashB", d.dashB},
            {"jumpStart[0]", d.jumpStart[0]}, {"jumpRise[0]", d.jumpRise[0]}, {"jumpFall[0]", d.jumpFall[0]}, {"land[0]", d.land[0]},
            {"jumpStart[1]", d.jumpStart[1]}, {"jumpStart[2]", d.jumpStart[2]},
            {"hit stand mid", d.hit[kPStand][1]}, {"hit crouch mid", d.hit[kPCrouch][1]}, {"hit air", d.hit[kPAir][1]},
            {"block stand mid", d.blockStun[kPStand][1]}, {"knockback", d.knockback}, {"floorImpact", d.floorImpact},
            {"floorLie", d.floorLie}, {"wakeup", d.wakeup}, {"winPose", d.winPose},
        };
        std::string miss;
        for (const Key& k : keys)
            if (k.id < 0) miss += std::string(miss.empty() ? "" : ", ") + k.what;
        std::printf("  key states missing: %s\n", miss.empty() ? "none" : miss.c_str());

        // ---- the 18 normals
        int missingNormals = 0;
        for (int p = 0; p < kPostureCount; ++p) {
            std::printf("  %-6s", kPost[p]);
            for (int b = 0; b < kNormalCount; ++b) {
                const int s = d.normal[p][b];
                if (s < 0) {
                    std::printf("  %s:MISSING", kBtn[b]);
                    ++missingNormals;
                    continue;
                }
                int firstHit = -1, hitTicks = 0;
                for (int t = 0; t < ms.ticks(s); ++t)
                    if (ms.hitAt(s, t) >= 0) {
                        if (firstHit < 0) firstHit = t;
                        ++hitTicks;
                    }
                std::printf("  %s:%dt", kBtn[b], ms.ticks(s));
                if (firstHit < 0) std::printf("(NO-HIT)");
                else std::printf("(hit@%d x%d)", firstHit, hitTicks);
            }
            std::printf("\n");
        }
        std::printf("  normals missing: %d of 18\n", missingNormals);
        {   // forward / back variants read from the Normals hub
            std::string var;
            for (int p = 0; p < kPostureCount; ++p)
                for (int b = 0; b < kNormalCount; ++b) {
                    if (d.normalF[p][b] >= 0) var += std::string(var.empty() ? "" : ", ") + kPost[p] + " " + kBtn[b] + "+fwd=" + ms.stateName(d.normalF[p][b]);
                    if (d.normalB[p][b] >= 0) var += std::string(var.empty() ? "" : ", ") + kPost[p] + " " + kBtn[b] + "+back=" + ms.stateName(d.normalB[p][b]);
                    if (d.normalD[p][b] >= 0) var += std::string(var.empty() ? "" : ", ") + kPost[p] + " " + kBtn[b] + "+down=" + ms.stateName(d.normalD[p][b]);
                }
            std::printf("  direction variants: %s\n", var.empty() ? "none" : var.c_str());
        }

        // ---- timeline sanity of every state
        int bad = 0;
        auto note = [&](int s, const char* msg) {
            if (bad < 40) std::printf("  ! state %d '%s': %s\n", s, ms.stateName(s).c_str(), msg);
            ++bad;
        };
        for (int s = 0; s < ms.stateCount(); ++s) {
            const Timeline& tl = ms.timeline(s);
            if (tl.ticks() == 0) { note(s, "empty timeline (0 ticks)"); continue; }
            if (tl.next >= ms.stateCount()) note(s, "next state out of range");
            int noFrame = 0, outOfRange = 0;
            for (uint16_t f : tl.frame) {
                if (f == kNoFrame) ++noFrame;
                else if (f >= nFrames) ++outOfRange;
            }
            if (outOfRange) note(s, "shows a frame index past the frame list");
            if (noFrame == tl.ticks()) note(s, "never shows a frame (all ticks empty)");
            else if (noFrame > 0 && allStates) note(s, "some ticks have no frame");
            if (tl.next == s && ms.cat(s) >= Cat::kAttackL) note(s, "attack state loops into itself");
        }
        std::printf("  timeline problems: %d\n", bad);

        if (allStates) {
            for (int s = 0; s < ms.stateCount(); ++s) {
                const Timeline& tl = ms.timeline(s);
                int hits = 0;
                for (int t = 0; t < tl.ticks(); ++t) hits += ms.hitAt(s, t) >= 0;
                std::printf("    [%3d] %-34s %4d ticks  hitTicks %-3d next %-4d cat %d\n", s, ms.stateName(s).c_str(), tl.ticks(), hits, tl.next, static_cast<int>(ms.cat(s)));
            }
        }
    }
    return rc;
}
