// zdroid_sim - headless run of the data driven fight runtime (src/core/foit_rt) on the user's own .foit files.
//
//   zdroid_sim <p1.foit> [--p2 <p2.foit>] --input "n*30,f*10,n*3,f*10,n*60" [--p2-input "..."] [--p1x N] [--p2x N]
//              [--verbose] [--state NAME] [--max-ticks N] [--fuzz TICKS[,SEED]] [--audit]
//   --fuzz   both fighters get a random but repeatable input script (a direction and some buttons, held 3-12 ticks)
//   --audit  after the run: for every helper / projectile the largest horizontal and vertical distance it ever had from its owner,
//            and the ticks / states where a helper that is meant to stay beside its owner (it never left the 200 px around it in
//            its first 20 ticks) was farther than 200 px
//
// Input script: comma separated steps  <dir>[+<button>...][*<ticks>]
//   dir: n (neutral) f (right) b (left) u d uf ub df db       button: 0..14 (0 LP, 1 MP, 2 HP, 3 LK, 4 MK, 5 HK, ...)
// Player 1 starts at x = -100 facing right, player 2 at x = +100 facing left and does nothing.
// Prints every state change (tick, from -> to, position) and, at the end, the engine ops the scripts asked for that the runtime
// does not implement yet.  Nothing is written to disk.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <iterator>
#include <string>
#include <vector>

#include "foit.h"
#include "foit_names.h"
#include "foit_rt.h"

using namespace zengine;

namespace {

bool load(const std::string& path, foit::Character& ch) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string err;
    if (!foit::parse(d.data(), d.size(), ch, &err)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
        return false;
    }
    return true;
}

struct Frame {
    int dir = foit::rt::DirNeutral;
    unsigned mask = 0;
};

int dirOf(const std::string& d) {
    using namespace foit::rt;
    if (d == "n") return DirNeutral;
    if (d == "f") return DirRight;
    if (d == "b") return DirLeft;
    if (d == "u") return DirUp;
    if (d == "d") return DirDown;
    if (d == "uf") return DirUpRight;
    if (d == "ub") return DirUpLeft;
    if (d == "df") return DirDownRight;
    if (d == "db") return DirDownLeft;
    return -1;
}

bool parseScript(const std::string& s, std::vector<Frame>& out) {
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find(',', i);
        if (e == std::string::npos) e = s.size();
        std::string tok = s.substr(i, e - i);
        i = e + 1;
        if (tok.empty()) continue;
        int count = 1;
        const size_t star = tok.find('*');
        if (star != std::string::npos) {
            count = std::atoi(tok.c_str() + star + 1);
            tok = tok.substr(0, star);
        }
        Frame f;
        size_t p = tok.find('+');
        const int d = dirOf(tok.substr(0, p));
        if (d < 0) {
            std::fprintf(stderr, "bad direction in '%s'\n", tok.c_str());
            return false;
        }
        f.dir = d;
        while (p != std::string::npos) {
            const size_t q = tok.find('+', p + 1);
            f.mask |= 1u << std::atoi(tok.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1).c_str());
            p = q;
        }
        for (int k = 0; k < count; ++k) out.push_back(f);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string p1, p2, script = "n*60", script2, only;
    float x1 = -100, x2 = 100;
    bool verbose = false;
    int maxTicks = 100000, dumpChild = -1, dumpFrame = -1;
    uint32_t findPoint = 0, findSpan = 1;
    std::string dumpState;
    int rollbackAt = -1;  // --rollback-check K: save the world at tick K, finish the run, load it back and replay: every tick must hash the same
    float life1 = -1, life2 = -1, meter1 = -1, meter2 = -1;  // start values overriding the body defaults (--p1-life ...)
    std::vector<int> printSlots;
    int matchSecs = 0, matchRounds = 2;  // --match SECS[,ROUNDS]: run the match layer (intro -> fight -> KO / time over -> poses -> next round)
    int fuzzTicks = 0;
    unsigned fuzzSeed = 1;
    bool audit = false;
    std::string dizzyFile;  // --dizzy <_dizzy_values.ini>: stun per hit strength (default: the file next to p1.foit, when there is one)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--p2") p2 = next();
        else if (a == "--input") script = next();
        else if (a == "--p2-input") script2 = next();
        else if (a == "--p1x") x1 = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--p2x") x2 = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--verbose") verbose = true;
        else if (a == "--state") only = next();
        else if (a == "--max-ticks") maxTicks = std::atoi(next().c_str());
        else if (a == "--dump-child") dumpChild = std::atoi(next().c_str());
        else if (a == "--dump-frame") dumpFrame = std::atoi(next().c_str());
        else if (a == "--find-point") findPoint = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        else if (a == "--find-span") findSpan = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        else if (a == "--dump-state") dumpState = next();
        else if (a == "--rollback-check") rollbackAt = std::atoi(next().c_str());
        else if (a == "--dizzy") dizzyFile = next();
        else if (a == "--audit") audit = true;
        else if (a == "--fuzz") {
            const std::string v = next();
            fuzzTicks = std::atoi(v.c_str());
            const size_t cm = v.find(',');
            if (cm != std::string::npos) fuzzSeed = static_cast<unsigned>(std::atoi(v.c_str() + cm + 1));
        }
        else if (a == "--match") {
            const std::string v = next();
            matchSecs = std::atoi(v.c_str());
            const size_t cm = v.find(',');
            if (cm != std::string::npos) matchRounds = std::atoi(v.c_str() + cm + 1);
            if (matchSecs <= 0) matchSecs = 99;
        }
        else if (a == "--print-slots") {  // --print-slots 38,66,84: the final value of these slots of both fighters
            const std::string v = next();
            size_t q = 0;
            while (q < v.size()) {
                size_t e = v.find(',', q);
                if (e == std::string::npos) e = v.size();
                printSlots.push_back(std::atoi(v.substr(q, e - q).c_str()));
                q = e + 1;
            }
        }
        else if (a == "--p1-life") life1 = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--p2-life") life2 = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--p1-meter") meter1 = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--p2-meter") meter2 = static_cast<float>(std::atof(next().c_str()));
        else if (p1.empty()) p1 = a;
    }
    if (p1.empty()) {
        std::fprintf(stderr, "usage: zdroid_sim <p1.foit> [--p2 <p2.foit>] --input \"n*30,f*10,n*3,f*10\" [--verbose]\n");
        return 2;
    }
    if (p2.empty()) p2 = p1;
    static foit::Character c1, c2;
    if (!load(p1, c1) || !load(p2, c2)) return 1;
    if (dumpState == "?slots") {  // the slot defaults a body starts with (float values)
        for (int i : {5, 25, 26, 27, 53, 54, 55, 59, 62, 64, 65, 66, 67, 68, 83, 84, 86, 87, 88, 89, 90, 92, 117, 118, 133, 141}) {
            float f;
            std::memcpy(&f, &c1.body.slotDefaults[static_cast<size_t>(i)], sizeof f);
            std::printf("slot %3d default %g\n", i, f);
        }
        return 0;
    }
    if (dumpState == "?refs") {
        auto nm = [&](unsigned i) { return i < c1.body.states.size() ? c1.body.states[i].name.c_str() : "(out of range)"; };
        std::printf("refBe %u %s | refBf %u %s | refC0 %u%s %s\n", c1.body.refBe, nm(c1.body.refBe), c1.body.refBf, nm(c1.body.refBf), c1.body.refC0,
                    c1.body.hasRefC0 ? "" : " (absent)", nm(c1.body.refC0));
        return 0;
    }
    if (!dumpState.empty()) {
        const int si = c1.body.findState(dumpState);
        if (si < 0) {
            std::fprintf(stderr, "no state '%s'\n", dumpState.c_str());
            return 1;
        }
        const foit::State& st = c1.body.states[static_cast<size_t>(si)];
        std::printf("state %d %s: next=%u nodes=%zu trans=%zu hashes=%zu hdrChain=%zu\n", si, st.name.c_str(), st.next, st.hdr.nodes.size(),
                    st.transitions.size(), st.hashes.size(), st.hdr.chain.size());
        for (size_t ni = 0; ni < st.hdr.nodes.size(); ++ni) {
            const foit::Node& n = st.hdr.nodes[ni];
            for (size_t pi = 0; pi < n.props.size(); ++pi) {
                const foit::NodeProps& r = n.props[pi];
                std::printf("  n%zu.%zu frame %u dur %u b1=%02x b2=%02x b3=%02x chain=%zu sub=%zu%s\n", ni, pi, n.refs[1], r.dur, r.b1, r.b2, r.b3,
                            r.chain.size(), r.sub.size(), r.hasCmd ? " gated" : "");
                if (r.hasHit)
                    std::printf("     HIT fc=%u f8=%u f10=%u f12=%u f13=%u f18=%u codes g(hit,blk)=(%u,%u) a(hit,blk)=(%u,%u) chains=%d%d%d%d\n", r.hit.fc,
                                r.hit.f8, r.hit.f10, r.hit.f12, r.hit.f13, r.hit.f18, r.hit.f14, r.hit.f15, r.hit.f16, r.hit.f17,
                                r.hit.hasChain[0], r.hit.hasChain[1], r.hit.hasChain[2], r.hit.hasChain[3]);
            }
        }
        for (size_t ti = 0; ti < st.transitions.size(); ++ti)
            std::printf("  -> %s  (cmd steps %zu)\n", c1.body.states[st.transitions[ti].target].name.c_str(), st.transitions[ti].cmd.steps.size());
        return 0;
    }
    if (dumpFrame > 0) {
        if (static_cast<size_t>(dumpFrame) > c1.frames.size()) {
            std::fprintf(stderr, "no frame %d (%zu frames)\n", dumpFrame, c1.frames.size());
            return 1;
        }
        const foit::Frame& fr = c1.frames[static_cast<size_t>(dumpFrame - 1)];
        std::printf("frame %d pivot (%d,%d)\n", dumpFrame, fr.pivotX, fr.pivotY);
        auto list = [&](const char* nm, const std::vector<foit::Box>& v) {
            for (const foit::Box& b : v) std::printf("  %s top %d bottom %d left %d right %d\n", nm, b.top, b.bottom, b.left, b.right);
        };
        list("hit ", fr.hit);
        list("hurt", fr.hurt);
        list("push", fr.push);
        for (const auto& e : fr.entries)
            std::printf("  point hash %u at (%d,%d)  [as signed %d]\n", e[0], static_cast<int>(e[1]), static_cast<int>(e[2]), static_cast<int>(e[0]));
        return 0;
    }
    if (findPoint != 0) {  // --find-point HASH: which frames carry the point, and how often (a point of the frame table, ops 44/45)
        size_t n = 0;
        int first = 0;
        for (size_t i = 0; i < c1.frames.size(); ++i)
            for (const auto& e : c1.frames[i].entries)
                if (e[0] >= findPoint && e[0] < findPoint + findSpan) {
                    if (!n) first = static_cast<int>(i) + 1;
                    ++n;
                    break;
                }
        std::printf("point hash %u: in %zu of %zu frames (first: frame %d)\n", findPoint, n, c1.frames.size(), first);
        return 0;
    }
    if (dumpChild >= 0) {
        if (static_cast<size_t>(dumpChild) >= c1.body.children.size()) {
            std::fprintf(stderr, "no child %d (the character has %zu)\n", dumpChild, c1.body.children.size());
            return 1;
        }
        const foit::Object& k = c1.body.children[static_cast<size_t>(dumpChild)];
        std::printf("child %d '%s': %zu states, first state %d\n", dumpChild, k.name.c_str(), k.states.size(), k.refBe);
        for (size_t si = 0; si < k.states.size(); ++si) {
            const foit::State& st = k.states[si];
            std::printf("  state %zu %-28s next=%u nodes=%zu trans=%zu hdrChain=%zu\n", si, st.name.c_str(), st.next, st.hdr.nodes.size(),
                        st.transitions.size(), st.hdr.chain.size());
            for (size_t ni = 0; ni < st.hdr.nodes.size(); ++ni) {
                const foit::Node& n = st.hdr.nodes[ni];
                for (size_t pi = 0; pi < n.props.size(); ++pi) {
                    const foit::NodeProps& r = n.props[pi];
                    std::printf("    n%zu.%zu frame %u dur %u b1=%02x b2=%02x b3=%02x f2=%02x%s chain=%zu sub=%zu\n", ni, pi, n.refs[1], r.dur, r.b1,
                                r.b2, r.b3, r.f2, r.hasHit ? " HIT" : "", r.chain.size(), r.sub.size());
                }
            }
            for (const foit::Transition& tr : st.transitions)
                std::printf("    -> %s\n", tr.target < k.states.size() ? k.states[tr.target].name.c_str() : "?");
        }
        return 0;
    }
    std::vector<Frame> frames, frames2;
    if (fuzzTicks > 0) {
        // xorshift: the same seed always gives the same two scripts
        auto rnd = [state = 2463534242u + fuzzSeed * 7919u]() mutable {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        static const int dirs[] = {foit::rt::DirNeutral, foit::rt::DirRight, foit::rt::DirLeft, foit::rt::DirUp, foit::rt::DirDown,
                                   foit::rt::DirUpRight, foit::rt::DirUpLeft, foit::rt::DirDownRight, foit::rt::DirDownLeft};
        for (std::vector<Frame>* dst : {&frames, &frames2}) {
            while (static_cast<int>(dst->size()) < fuzzTicks) {
                Frame f;
                f.dir = dirs[rnd() % 9];
                if (rnd() % 3 == 0) f.mask |= 1u << (rnd() % 6);
                if (rnd() % 12 == 0) f.mask |= 1u << (rnd() % 6);
                const int hold = 3 + static_cast<int>(rnd() % 10);
                for (int k = 0; k < hold; ++k) dst->push_back(f);
            }
        }
    } else {
        if (!parseScript(script, frames)) return 2;
        if (!script2.empty() && !parseScript(script2, frames2)) return 2;
    }

    static foit::rt::Object o1, o2;
    foit::rt::World w;
    {   // stun per hit strength: explicit file, else `_dizzy_values.ini` in the folder of p1.foit
        std::string path = dizzyFile;
        if (path.empty()) {
            const size_t sl = p1.find_last_of("/\\");
            path = (sl == std::string::npos ? std::string() : p1.substr(0, sl + 1)) + "_dizzy_values.ini";
        }
        std::ifstream df(path);
        if (df) {
            const std::string text((std::istreambuf_iterator<char>(df)), std::istreambuf_iterator<char>());
            const int n = foit::rt::parseStrengthTable(text, w.strengthTable);
            if (verbose) std::fprintf(stderr, "stun table from %s: %d entries (L %g M %g H %g)\n", path.c_str(), n, w.strengthTable[1], w.strengthTable[2], w.strengthTable[3]);
        }
    }
    foit::rt::init(o1, c1, c1.body, x1, true);
    foit::rt::init(o2, c2, c2.body, x2, false);
    o2.slots[25] = 2;
    if (life1 >= 0) o1.slots[64] = o1.slots[65] = life1;
    if (life2 >= 0) o2.slots[64] = o2.slots[65] = life2;
    if (meter1 >= 0) o1.slots[66] = meter1;
    if (meter2 >= 0) o2.slots[66] = meter2;
    o1.enemy = &o2;
    o2.enemy = &o1;
    const int stand1 = c1.body.findState("Stand");
    const int stand2 = c2.body.findState("Stand");
    foit::rt::enterState(o1, w, stand1 < 0 ? 0 : stand1);
    foit::rt::enterState(o2, w, stand2 < 0 ? 0 : stand2);
    w.objects = {&o1, &o2};
    if (matchSecs > 0) foit::rt::startMatch(w, matchSecs, matchRounds);

    foit::rt::Sample prevSample, prevSample2;
    size_t eventsTotal = 0;
    std::vector<uint64_t> tickHash;
    std::vector<uint8_t> snap;
    foit::rt::Sample snapPrev1, snapPrev2;
    int lastState2 = o2.state;
    size_t lastObjects = 2;
    int lastState = o1.state;
    if (!audit) std::printf("tick  %-32s -> %-32s     x        y   xvel  yvel\n", "from", "to");
    struct Seen {
        float maxDx = 0, maxDy = 0;
        int born = -1;
        bool stays = true;   // within 200 px of the owner during its first 20 ticks
        int farTicks = 0, firstFar = -1;
        int tetherTicks = 0, tetherFar = 0;   // ticks spent tethered (TETHEREDTO 1 / 7 / 8 / 11) and how many of them far away while drawn
        std::string farState, farOwnerState;
        float farDx = 0;
    };
    std::map<std::string, Seen> seen;
    std::map<const foit::rt::Object*, int> bornAt;
    // with the match layer on the run goes on after the script ends (neutral inputs) until the match is over
    const int total = matchSecs > 0 ? maxTicks : std::min<int>(maxTicks, static_cast<int>(frames.size()));
    int lastPhase = -1, maxTicksHit = -1;
    for (int t = 0; t < total; ++t) {
        foit::rt::Sample in[2];
        const Frame neutralFrame;
        const Frame& f = static_cast<size_t>(t) < frames.size() ? frames[static_cast<size_t>(t)] : neutralFrame;
        in[0] = foit::rt::composeSample(prevSample, f.dir, f.mask);
        prevSample = in[0];
        if (static_cast<size_t>(t) < frames2.size()) {
            const Frame& g = frames2[static_cast<size_t>(t)];
            in[1] = foit::rt::composeSample(prevSample2, g.dir, g.mask);
            prevSample2 = in[1];
        } else {
            in[1] = foit::rt::composeSample(prevSample2, foit::rt::DirNeutral, 0);
            prevSample2 = in[1];
        }
        foit::rt::tickAll(w, in, 2);
        eventsTotal += w.events.size();
        if (rollbackAt >= 0) {
            tickHash.push_back(foit::rt::hashWorld(w));
            if (t == rollbackAt) {
                foit::rt::saveWorld(w, snap);
                snapPrev1 = prevSample;
                snapPrev2 = prevSample2;
            }
        }
        for (const foit::rt::Event& ev : w.events)
            if (ev.kind == foit::rt::Event::MatchPhase)
                std::printf("%4d  == MATCH phase %u (round %d) winner side %.0f   wins %d-%d  clock %d  ROUNDOVER P1 %g P2 %g DOPOSE P1 %g P2 %g\n", t, ev.hash,
                            ev.object, ev.x, w.match.wins[0], w.match.wins[1], w.match.clock, o1.slots[30], o2.slots[30], o1.slots[135], o2.slots[135]);
        w.events.clear();
        if (matchSecs > 0 && w.match.phase == foit::rt::Match::Over && lastPhase != w.match.phase) {
            lastPhase = w.match.phase;
            std::printf("%4d  == MATCH OVER: winner side %d, wins %d-%d, %d round(s), %d pose timeout(s)\n", t, w.match.matchWinner, w.match.wins[0],
                        w.match.wins[1], w.match.round, w.match.timeouts);
            // let the last poses play a little, then stop
            if (t + 120 < total) maxTicksHit = t + 120;
        }
        if (maxTicksHit >= 0 && t >= maxTicksHit) break;
        if (audit) {
            for (size_t k = 2; k < w.objects.size(); ++k) {
                const foit::rt::Object* c = w.objects[k];
                if (!c->leader || c->leader == c) continue;
                const foit::rt::Object* l = c->leader;
                const std::string key = (l == &o1 ? c1 : c2).body.children.empty() ? std::string("?") : c->def->name;
                Seen& sn = seen[key];
                if (!bornAt.count(c)) bornAt[c] = t;
                const float dx = c->x - l->x, dy = c->y - l->y;
                sn.maxDx = std::max(sn.maxDx, std::fabs(dx));
                sn.maxDy = std::max(sn.maxDy, std::fabs(dy));
                const int age = t - bornAt[c];
                if (age < 20 && std::fabs(dx) > 200) sn.stays = false;
            }
            // second pass: only for the helpers that stay beside their owner at the start
            for (size_t k = 2; k < w.objects.size(); ++k) {
                const foit::rt::Object* c = w.objects[k];
                if (!c->leader || c->leader == c) continue;
                const foit::rt::Object* l = c->leader;
                Seen& sn = seen[c->def->name];
                const float dx = c->x - l->x;
                const int tm = static_cast<int>(c->slots[57]);
                if (tm == 1 || tm == 7 || tm == 8 || tm == 11) {
                    ++sn.tetherTicks;
                    if (std::fabs(dx) > 200) ++sn.tetherFar;
                }
                {   // nothing is drawn (no frame, or one without pixels): where it is parked does not show
                    const foit::Frame* fr = c->frame > 0 && static_cast<size_t>(c->frame) <= c->ch->frames.size() ? &c->ch->frames[static_cast<size_t>(c->frame) - 1] : nullptr;
                    bool any = false;
                    if (fr) for (int q = 0; q < foit::kSlots; ++q) any = any || (!fr->img[q].placeholder() && static_cast<int>(fr->img[q].w) * fr->img[q].h > 1);
                    if (!any) continue;
                }
                if (sn.stays && std::fabs(dx) > 200) {
                    if (sn.firstFar < 0) {
                        sn.firstFar = t;
                        sn.farState = c->def->states[static_cast<size_t>(c->state)].name;
                        sn.farOwnerState = l->def->states[static_cast<size_t>(l->state)].name;
                        sn.farDx = dx;
                    }
                    ++sn.farTicks;
                }
            }
        }
        if (o1.state != lastState || verbose) {
            const std::string& from = c1.body.states[static_cast<size_t>(lastState)].name;
            const std::string& to = c1.body.states[static_cast<size_t>(o1.state)].name;
            if (only.empty() || to == only || from == only)
                std::printf("%4d  %-32s -> %-32s %7.1f %8.1f %6.2f %6.2f  node %d\n", t, from.c_str(), to.c_str(), o1.x, o1.y,
                            o1.slots[158], o1.slots[159], o1.nodeIdx);
            lastState = o1.state;
        }
        if (std::getenv("ZSIM_LEADER")) {  // what a helper's transitions read from its leader, every tick
            for (size_t k = 2; k < w.objects.size(); ++k) {
                const foit::rt::Object* c = w.objects[k];
                const foit::rt::Object* l = c->leader ? c->leader : c;
                uint32_t lr;
                std::memcpy(&lr, &l->slots[std::atoi(std::getenv("ZSIM_LEADER"))], 4);
                std::printf("%4d    helper#%zu %-30s leader %-18s slot=%08x\n", t, k, c->def->states[static_cast<size_t>(c->state)].name.c_str(),
                            l->def->states[static_cast<size_t>(l->state)].name.c_str(), lr);
            }
        }
        if (std::getenv("ZSIM_CHILDREN")) {
            for (size_t k = 2; k < w.objects.size(); ++k) {
                const foit::rt::Object* c = w.objects[k];
                std::printf("%4d    child#%zu %s x=%.0f y=%.0f (owner %.0f,%.0f) state %d (%s) node %d frame %d tether %g armed=%d hasHit=%d xvel %.1f yvel %.1f\n", t, k,
                            c->def->name.c_str(), c->x, c->y, c->leader ? c->leader->x : 0.0f, c->leader ? c->leader->y : 0.0f, c->state,
                            c->def->states[static_cast<size_t>(c->state)].name.c_str(), c->nodeIdx, c->frame, c->slots[57],
                            (c->fa & 8) ? 1 : 0, (c->fa & 0x40) ? 1 : 0, c->slots[158], c->slots[159]);
            }
        }
        if (w.objects.size() != lastObjects) {
            std::printf("%4d  objects in the world: %zu", t, w.objects.size());
            for (size_t k = 2; k < w.objects.size(); ++k) {
                const foit::rt::Object* c = w.objects[k];
                std::printf("  [child x=%.0f y=%.0f state %d]", c->x, c->y, c->state);
            }
            std::printf("\n");
            lastObjects = w.objects.size();
        }
        if (const char* p1t = std::getenv("ZSIM_P1")) {  // "A-B": print P1's state / node cursor / armed transitions / round slots every tick between those ticks
            int a = 0, b = 0;
            if (std::sscanf(p1t, "%d-%d", &a, &b) == 2 && t >= a && t <= b)
                std::printf("%4d    P1 %-26s node %3d timer %3d/%3d trans=%08x ROUNDOVER=%g DOPOSE=%g x=%.1f xvel=%.1f lr=%g\n", t, c1.body.states[static_cast<size_t>(o1.state)].name.c_str(),
                            o1.nodeIdx, o1.nodeTimer, o1.nodeDur, o1.trans[0], o1.slots[30], o1.slots[135], o1.x, o1.slots[158], o1.slots[13]);
        }
        if (const char* p2t = std::getenv("ZSIM_P2")) {  // "A-B": print P2 every tick between those ticks
            int a = 0, b = 0;
            if (std::sscanf(p2t, "%d-%d", &a, &b) == 2 && t >= a && t <= b)
                std::printf("%4d    P2 %-26s node %3d frame %3d x=%7.1f y=%6.1f face=%.0f thrown=%.0f | P1 %-22s x=%7.1f THROW=%.0f X=%.0f Y=%.0f\n", t,
                            c2.body.states[static_cast<size_t>(o2.state)].name.c_str(), o2.nodeIdx, o2.frame, o2.x, o2.y, o2.slots[56],
                            o2.slots[10], c1.body.states[static_cast<size_t>(o1.state)].name.c_str(), o1.x, o1.slots[111], o1.slots[112],
                            o1.slots[113]);
        }
        if (o2.state != lastState2) {
            const std::string& from = c2.body.states[static_cast<size_t>(lastState2)].name;
            const std::string& to = c2.body.states[static_cast<size_t>(o2.state)].name;
            std::printf("%4d  P2: %-28s -> %-28s %7.1f %8.1f  hp %d  hitstun %.0f\n", t, from.c_str(), to.c_str(), o2.x, o2.y, static_cast<int>(o2.life()),
                        o2.slots[53]);
            lastState2 = o2.state;
        }
    }
    if (audit) {
        std::printf("helper / projectile audit over %d ticks (largest |dx| |dy| from the owner; * = stays beside the owner at the start)\n", total);
        for (const auto& kv : seen) {
            const Seen& sn = kv.second;
            std::printf("  %c %-28s max dx %7.1f dy %7.1f", sn.stays ? '*' : ' ', kv.first.c_str(), sn.maxDx, sn.maxDy);
            if (sn.stays && sn.farTicks)
                std::printf("   FAR for %d ticks, first at %d (dx %.0f) helper state %s, owner state %s", sn.farTicks, sn.firstFar, sn.farDx,
                            sn.farState.c_str(), sn.farOwnerState.c_str());
            std::printf("\n");
        }
    }
    std::printf("\n%d ticks, %llu state changes, %llu transitions taken; hits %llu blocks %llu touches %llu; hp P1 %d P2 %d\n", total,
                static_cast<unsigned long long>(w.stats.stateChanges), static_cast<unsigned long long>(w.stats.transitions),
                static_cast<unsigned long long>(w.stats.hits), static_cast<unsigned long long>(w.stats.blocks),
                static_cast<unsigned long long>(w.stats.touches), static_cast<int>(o1.life()), static_cast<int>(o2.life()));
    if (rollbackAt >= 0 && !snap.empty()) {
        // load the snapshot taken at tick K back into the same objects and replay the rest of the inputs
        const uint64_t endHash = tickHash.empty() ? 0 : tickHash.back();
        if (!foit::rt::loadWorld(w, {&o1, &o2}, snap.data(), snap.size())) {
            std::printf("ROLLBACK FAIL: snapshot did not load\n");
            return 2;
        }
        foit::rt::Sample p1 = snapPrev1, p2 = snapPrev2;
        int firstBad = -1;
        // the first run may have stopped early (match over): replay exactly the ticks that were hashed
        const int replayEnd = std::min<int>(total, static_cast<int>(tickHash.size()));
        for (int t = rollbackAt + 1; t < replayEnd; ++t) {
            foit::rt::Sample in[2];
            const Frame neutralFrame;
            const Frame& f = static_cast<size_t>(t) < frames.size() ? frames[static_cast<size_t>(t)] : neutralFrame;
            in[0] = foit::rt::composeSample(p1, f.dir, f.mask);
            p1 = in[0];
            if (static_cast<size_t>(t) < frames2.size()) {
                const Frame& g = frames2[static_cast<size_t>(t)];
                in[1] = foit::rt::composeSample(p2, g.dir, g.mask);
            } else {
                in[1] = foit::rt::composeSample(p2, foit::rt::DirNeutral, 0);
            }
            p2 = in[1];
            foit::rt::tickAll(w, in, 2);
            w.events.clear();
            if (firstBad < 0 && foit::rt::hashWorld(w) != tickHash[static_cast<size_t>(t)]) firstBad = t;
        }
        std::printf("rollback check at tick %d (snapshot %zu bytes): %s%s\n", rollbackAt, snap.size(), firstBad < 0 ? "replay identical" : "DESYNC at tick ",
                    firstBad < 0 ? "" : std::to_string(firstBad).c_str());
        if (firstBad < 0 && foit::rt::hashWorld(w) != endHash) std::printf("ROLLBACK FAIL: final hash differs\n");
        if (firstBad >= 0) return 3;
    }
    std::printf("meter P1 %.1f P2 %.1f   red life P1 %.0f P2 %.0f\n", o1.slots[66], o2.slots[66], o1.slots[65], o2.slots[65]);
    for (int sl : printSlots)
        std::printf("slot %3d %-16s P1 %-10g P2 %-10g\n", sl, foit::slotName(sl), o1.slots[sl], o2.slots[sl]);
    std::printf("events raised by nodes (sound / effect hashes): %zu\n", eventsTotal);
    if (w.stats.loopCaps) std::printf("WARNING: %llu ticks hit the 500 re-step limit\n", static_cast<unsigned long long>(w.stats.loopCaps));
    std::printf("engine ops the scripts asked for (not implemented yet):\n");
    bool any = false;
    for (int i = 0; i < 256; ++i) {
        if (w.stats.hostOps[i]) {
            std::printf("  arith op %3d x %llu\n", i, static_cast<unsigned long long>(w.stats.hostOps[i]));
            any = true;
        }
        if (w.stats.hostTests[i]) {
            std::printf("  bool  op %3d x %llu\n", i, static_cast<unsigned long long>(w.stats.hostTests[i]));
            any = true;
        }
    }
    if (!any) std::printf("  (none)\n");
    bool soft = false;
    for (int i = 0; i < 256; ++i)
        if (w.stats.softOps[i]) {
            if (!soft) std::printf("engine ops answered with a neutral value (palette / art points / match globals):");
            std::printf(" %d x%llu", i, static_cast<unsigned long long>(w.stats.softOps[i]));
            soft = true;
        }
    if (soft) std::printf("\n");
    return 0;
}
