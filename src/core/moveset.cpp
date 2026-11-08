#include "moveset.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace zengine {
namespace fight {

namespace {

// ---------------------------------------------------------------------------------------------------------
// Literal evaluation of script chains.  Only what is needed to read numbers out of the scripts: `set`, `add`,
// `sub` and `mul` on literals and on slots that were assigned a literal earlier in the same walk.  Everything
// else leaves the destination "unknown" (conditional sets are skipped on purpose: they depend on live state).
// ---------------------------------------------------------------------------------------------------------
constexpr size_t kS = foit::kSlotCount;

struct Vals {
    float v[kS];
    bool k[kS];
    float peak[kS];  // value with the largest magnitude ever assigned
    bool seen[kS];
    Vals() {
        std::fill(v, v + kS, 0.0f);
        std::fill(k, k + kS, false);
        std::fill(peak, peak + kS, 0.0f);
        std::fill(seen, seen + kS, false);
    }
};

float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

bool operandValue(const foit::Operand& o, const Vals& s, float& out) {
    if (o.desc == 0) {
        out = bitsToFloat(o.raw);
        return std::isfinite(out);
    }
    if (o.desc == 1 && o.raw < kS && s.k[o.raw]) {
        out = s.v[o.raw];
        return true;
    }
    return false;
}

void walkChain(const foit::Chain& c, Vals& s) {
    for (const foit::ChainNode& n : c) {
        if (n.boolean()) continue;
        if (n.op == 0xE || n.op == 0xF) continue;  // conditional set: skipped
        if (n.c.desc != 1 || n.c.raw >= kS) continue;
        const size_t dst = n.c.raw;
        float a = 0, b = 0, r = 0;
        const bool oka = operandValue(n.a, s, a);
        const bool okb = operandValue(n.b, s, b);
        bool ok = false;
        switch (n.op) {
            case 1: ok = oka; r = a; break;
            case 2: ok = oka && okb; r = a + b; break;
            case 3: ok = oka && okb; r = a - b; break;
            case 4: ok = oka && okb; r = a * b; break;
            default: break;
        }
        if (ok) {
            s.v[dst] = r;
            s.k[dst] = true;
            if (!s.seen[dst] || std::fabs(r) > std::fabs(s.peak[dst])) s.peak[dst] = r;
            s.seen[dst] = true;
        } else {
            s.k[dst] = false;
        }
    }
}

void walkState(const foit::State& st, Vals& s) {
    if (st.hdr.hasChain) walkChain(st.hdr.chain, s);
    for (const foit::Node& n : st.hdr.nodes)
        for (const foit::NodeProps& p : n.props) walkChain(p.chain, s);
}

constexpr int kSlotVx = 158, kSlotVy = 159, kSlotAirborne = 59, kSlotGravity = 54;

// ---- hit data -> HitInfo -----------------------------------------------------------------------------
HitInfo makeHit(const foit::HitData& h) {
    HitInfo r;
    r.damage = h.fc;
    r.hitstop = h.f10;
    r.hitstun = h.f8;
    r.level = (h.f13 >= 1 && h.f13 <= 3) ? h.f13 : 3;
    r.strength = (h.f18 >= 1 && h.f18 <= 3) ? h.f18 : 1;
    r.blockstun = r.hitstun;
    Vals onHit, onBlock;
    if (h.hasChain[0]) walkChain(h.chains[0], onHit);
    if (h.hasChain[1]) walkChain(h.chains[1], onBlock);
    if (onHit.k[kSlotVx]) r.hitVx = -onHit.v[kSlotVx];
    if (onHit.k[kSlotVy]) r.hitVy = onHit.v[kSlotVy];
    if (onBlock.k[kSlotVx]) r.blockVx = -onBlock.v[kSlotVx];
    r.launch = onHit.k[kSlotAirborne] && onHit.v[kSlotAirborne] == 2.0f;
    return r;
}

// The 6 buttons as the game's three generations of characters name their normals: Annie, Umbrella ... "s.WP s.MP s.HP s.WK s.MK s.HK",
// the older ones (Filia ...) "s.Jab s.Strong s.Fierce s.Short s.Fwd s.RH", and BlackDahlia / Marie "s.LP s.MP s.HP s.LK s.MK s.HK".
// Order = LP MP HP LK MK HK.
constexpr int kButtonSets = 3;
const char* const kButtonNames[kButtonSets][kNormalCount] = {{"WP", "MP", "HP", "WK", "MK", "HK"},
                                                             {"Jab", "Strong", "Fierce", "Short", "Fwd", "RH"},
                                                             {"LP", "MP", "HP", "LK", "MK", "HK"}};

// "s.WP" / "s.Jab" -> (posture 0, button 0) ...; false for anything that is not exactly one of the 18 normals ("s.Jab_RapidFire" is not).
bool parseNormal(const std::string& n, int* posture, int* button) {
    if (n.size() < 4 || n[1] != '.') return false;
    const char p = n[0];
    *posture = p == 's' ? kPStand : (p == 'c' ? kPCrouch : (p == 'j' ? kPAir : -1));
    if (*posture < 0) return false;
    for (const auto& set : kButtonNames)
        for (int i = 0; i < kNormalCount; ++i)
            if (n.compare(2, std::string::npos, set[i]) == 0) {
                *button = i;
                return true;
            }
    return false;
}

HitInfo syntheticHit(int posture, int button) {
    // Some normals (Annie's 5LK / 5MP kick object ...) carry their hit data in a spawned object that is not
    // wired up yet: they get the values of a normal of the same strength.  Flagged in docs/RE_NOTES.md.
    const int strength = (button % 3) + 1;
    HitInfo h;
    static const int dmg[3] = {300, 500, 800}, stop[3] = {7, 9, 12}, stun[3] = {16, 22, 30};
    h.damage = dmg[strength - 1];
    h.hitstop = stop[strength - 1];
    h.hitstun = stun[strength - 1];
    h.blockstun = h.hitstun;
    h.strength = strength;
    h.level = posture == kPAir ? 1 : ((posture == kPCrouch && button >= 3) ? 2 : 3);
    h.hitVx = 4.0f + 2.5f * (strength - 1);
    h.blockVx = 7.0f + 2.5f * (strength - 1);
    return h;
}

}  // namespace

// Name prefix of the companion state that goes with a body state ("" = the body draws it all).
static std::string companionBase(const std::string& n) {
    auto has = [&](const char* p) { return n.rfind(p, 0) == 0; };
    if (n == "Stand" || n == "Uncrouching" || n == "TurnStanding" || n == "WalkFwd_End" || n == "WalkBack_End")
        return "Hungern_Stand_Idle_";
    if (n == "Crouch" || n == "Crouching" || n == "TurnCrouching") return "Hungern_Crouch_Idle_";
    if (n == "WalkFwd" || n == "WalkFwd_Loop") return "Hungern_Walk_Forward_";
    if (n == "WalkBack" || n == "WalkBack_Loop") return "Hungern_Walk_Backward_";
    if (has("DashFwd")) return "Hungern_Dash_Forward_";
    if (has("DashBack")) return "Hungern_Dash_Backward_";
    if (has("Jump_Start_") || has("JumpRise") || has("JumpApogee") || has("JumpFall") || has("JumpLand")) {
        std::string k = n;
        const size_t lp = k.rfind("_Loop");
        if (lp != std::string::npos && lp + 5 == k.size()) k.resize(lp);
        const bool f = k.size() > 2 && k.compare(k.size() - 2, 2, "_F") == 0;
        const bool b = k.size() > 2 && k.compare(k.size() - 2, 2, "_B") == 0;
        return f ? "Hungern_Jump_Forward_And_Prejump_" : (b ? "Hungern_Jump_Backward_And_Prejump_" : "Hungern_Jump_Up_And_Prejump_");
    }
    return {};
}

std::vector<HubNormal> hubNormals(const foit::Object& body) {
    std::vector<HubNormal> out;
    const int hub = body.findState("Normals");
    if (hub < 0) return out;
    for (const foit::Transition& tr : body.states[static_cast<size_t>(hub)].transitions) {
        if (tr.target >= body.states.size()) continue;
        const std::string& nm = body.states[tr.target].name;
        if (nm.size() < 3 || nm[1] != '.' || nm.find("Throw") != std::string::npos) continue;
        HubNormal h;
        h.posture = nm[0] == 's' ? kPStand : (nm[0] == 'c' ? kPCrouch : (nm[0] == 'j' ? kPAir : -1));
        if (h.posture < 0) continue;
        // a plain attack is one step; motions (specials) live in other hubs
        const foit::Step* st = nullptr;
        if (tr.cmd.steps.size() == 1 && !tr.cmd.hasDefault) st = &tr.cmd.steps[0];
        else if (tr.cmd.steps.empty() && tr.cmd.hasDefault) st = &tr.cmd.def;
        if (!st) continue;
        // value 0 = "pressed": exactly one of the six attack buttons, otherwise it is a throw / button combo
        int pressed = 0;
        for (int i = 0; i < kNormalCount; ++i)
            if (st->t[i] == 0) {
                ++pressed;
                h.button = i;
            }
        if (pressed != 1) continue;
        // 0 forward, 4 back, 2 down (a plain crouching attack on the ground, a separate move in the air), 9 / 10 = none; the rest is
        // up / diagonals (BossMarie's s.U+WP ...)
        switch (st->b0) {
            case 0: h.dir = 1; break;
            case 4: h.dir = 2; break;
            case 2: h.dir = h.posture == kPAir ? 3 : (h.posture == kPCrouch ? 0 : 4); break;
            case 9:
            case 10: h.dir = 0; break;
            default: h.dir = 4; break;
        }
        h.state = static_cast<int>(tr.target);
        out.push_back(h);
    }
    return out;
}

MoveSet::Ids::Ids() {
    for (int p = 0; p < kPostureCount; ++p) {
        for (int b = 0; b < kNormalCount; ++b) normal[p][b] = normalF[p][b] = normalB[p][b] = normalD[p][b] = -1;
        for (int l = 0; l < 3; ++l) hit[p][l] = blockStun[p][l] = -1;
    }
    for (int& k : knockup) k = -1;
}

int MoveSet::find(const std::string& n) const {
    auto it = byName_.find(n);
    return it == byName_.end() ? -1 : it->second;
}

std::vector<int> MoveSet::introStates() const {
    std::vector<int> out;
    for (size_t s = 0; s < names_.size(); ++s) {
        const std::string& n = names_[s];
        if (n.size() > 14 && n.compare(0, 6, "Intro_") == 0 && n.compare(n.size() - 8, 8, "_ForReal") == 0 && tl_[s].ticks() > 0)
            out.push_back(static_cast<int>(s));
    }
    return out;
}

const std::string& MoveSet::stateName(int s) const {
    static const std::string none;
    return valid(s) ? names_[static_cast<size_t>(s)] : none;
}

const Timeline& MoveSet::timeline(int s) const {
    static const Timeline none;
    return valid(s) ? tl_[static_cast<size_t>(s)] : none;
}

uint16_t MoveSet::frameAt(int s, int t) const {
    if (!valid(s)) return kNoFrame;
    const Timeline& tl = tl_[static_cast<size_t>(s)];
    if (tl.frame.empty()) return kNoFrame;
    t = std::clamp(t, 0, tl.ticks() - 1);
    return tl.frame[static_cast<size_t>(t)];
}

int MoveSet::companionFor(int bodyState, const char* level) const {
    if (ctl_.empty() || !valid(bodyState) || static_cast<size_t>(bodyState) >= compBase_.size()) return -1;
    const std::string& base = compBase_[static_cast<size_t>(bodyState)];
    if (base.empty()) return -1;
    auto it = cby_.find(base + level);
    return it == cby_.end() ? -1 : it->second;
}

uint16_t MoveSet::companionFrameAt(int cs, int t) const {
    if (cs < 0 || cs >= static_cast<int>(ctl_.size())) return kNoFrame;
    const Timeline& tl = ctl_[static_cast<size_t>(cs)];
    if (tl.frame.empty()) return kNoFrame;
    t = std::clamp(t, 0, tl.ticks() - 1);
    return tl.frame[static_cast<size_t>(t)];
}

int MoveSet::hitAt(int s, int t) const {
    if (!valid(s)) return -1;
    const Timeline& tl = tl_[static_cast<size_t>(s)];
    if (t < 0 || t >= tl.ticks()) return -1;
    return tl.hit[static_cast<size_t>(t)];
}

bool MoveSet::build(std::shared_ptr<const foit::Character> ch, std::string* err) {
    tl_.clear();
    ctl_.clear();
    cby_.clear();
    compBase_.clear();
    cat_.clear();
    hits_.clear();
    byName_.clear();
    names_.clear();
    ch_ = std::move(ch);
    id = Ids();
    if (!ch_ || ch_->body.states.empty()) {
        if (err) *err = "character has no states";
        ch_.reset();
        return false;
    }
    const foit::Object& body = ch_->body;
    name_ = body.name;
    const size_t n = body.states.size();
    tl_.resize(n);
    cat_.assign(n, Cat::kNone);
    names_.resize(n);

    for (size_t s = 0; s < n; ++s) {
        names_[s] = body.states[s].name;
        byName_.emplace(names_[s], static_cast<int>(s));  // first one wins
    }

    // ---- the character's own normals table (see hubNormals): state -> posture * kNormalCount + button
    const std::vector<HubNormal> hub = hubNormals(body);
    std::vector<int> hubKey(n, -1);
    for (const HubNormal& h : hub)
        if (hubKey[static_cast<size_t>(h.state)] < 0) hubKey[static_cast<size_t>(h.state)] = h.posture * kNormalCount + h.button;

    // ---- timelines
    std::vector<char> hasBox(n, 0), armed(n, 0);
    // normals with hit boxes but no armed hit data: see syntheticHit()
    auto synth = [&](size_t s, int posture, int button) {
        if (!hasBox[s] || armed[s]) return;
        Timeline& tl = tl_[s];
        hits_.push_back(syntheticHit(posture, button));
        const int16_t h = static_cast<int16_t>(hits_.size() - 1);
        for (size_t t = 0; t < tl.frame.size(); ++t)
            if (tl.frame[t] != kNoFrame && !ch_->frames[tl.frame[t]].hit.empty()) tl.hit[t] = h;
        armed[s] = 1;
    };
    for (size_t s = 0; s < n; ++s) {
        const foit::State& st = body.states[s];
        Timeline& tl = tl_[s];
        tl.next = st.next < n ? static_cast<int>(st.next) : -1;
        int curHit = -1;
        bool anyBox = false, anyArmed = false;
        for (const foit::Node& node : st.hdr.nodes) {
            for (const foit::NodeProps& p : node.props)
                if (p.hasHit) {
                    hits_.push_back(makeHit(p.hit));
                    curHit = static_cast<int>(hits_.size()) - 1;
                }
            if (node.isScript()) continue;
            const uint16_t ref = node.refs[1] ? node.refs[1] : (node.refs[0] ? node.refs[0] : node.refs[2]);
            const uint16_t fr = (ref != 0 && ref <= ch_->frames.size()) ? static_cast<uint16_t>(ref - 1) : kNoFrame;
            const int ticks = std::max(1, node.ticks());
            for (int t = 0; t < ticks; ++t) {
                tl.frame.push_back(fr);
                tl.hit.push_back(static_cast<int16_t>(curHit));
            }
            if (fr != kNoFrame && !ch_->frames[fr].hit.empty()) anyBox = true;
            if (curHit >= 0) anyArmed = true;
        }
        hasBox[s] = anyBox;
        armed[s] = anyArmed;
        int posture = 0, button = 0;
        bool isNormal = parseNormal(st.name, &posture, &button);
        if (!isNormal && hubKey[s] >= 0) {  // a normal the name does not give away (j.MK_N, s.F+MP ...)
            posture = hubKey[s] / kNormalCount;
            button = hubKey[s] % kNormalCount;
            isNormal = true;
        }
        if (isNormal) synth(s, posture, button);
    }
    // A normal that is only a router (Peacock's j.MK_N: one tick, then j.MK_ForReal; Beowulf's s.HP -> s.HP_NoChair ...) lends its button
    // to the states it hands over to, so the move that really swings gets the same stand-in hit data as the others.
    for (size_t s = 0; s < n; ++s) {
        if (hubKey[s] < 0 || tl_[s].ticks() > 2) continue;
        size_t cur = s;
        for (int hop = 0; hop < 3; ++hop) {
            const int nx = tl_[cur].next;
            if (nx < 0 || static_cast<size_t>(nx) == cur) break;
            synth(static_cast<size_t>(nx), hubKey[s] / kNormalCount, hubKey[s] % kNormalCount);
            cur = static_cast<size_t>(nx);
            if (tl_[cur].ticks() > 2) break;
        }
    }

    // ---- companion object (see moveset.h): frames only, no hit data
    for (const foit::Object& child : body.children) {
        if (child.name != "UMBRELLA_Hungern") continue;
        ctl_.resize(child.states.size());
        for (size_t s = 0; s < child.states.size(); ++s) {
            const foit::State& st = child.states[s];
            Timeline& tl = ctl_[s];
            tl.next = st.next < child.states.size() ? static_cast<int>(st.next) : -1;
            cby_.emplace(st.name, static_cast<int>(s));
            for (const foit::Node& node : st.hdr.nodes) {
                if (node.isScript()) continue;
                const uint16_t ref = node.refs[1] ? node.refs[1] : (node.refs[0] ? node.refs[0] : node.refs[2]);
                const uint16_t fr = (ref != 0 && ref <= ch_->frames.size()) ? static_cast<uint16_t>(ref - 1) : kNoFrame;
                const int ticks = std::max(1, node.ticks());
                for (int t = 0; t < ticks; ++t) {
                    tl.frame.push_back(fr);
                    tl.hit.push_back(-1);
                }
            }
        }
        compBase_.resize(n);
        for (size_t s = 0; s < n; ++s) compBase_[s] = companionBase(names_[s]);
        break;
    }

    // ---- named states
    auto F = [&](const char* nm) { return find(nm); };
    bool sharedJumpStart = false, sharedJumpLand = false;
    id.stand = F("Stand");
    id.crouch = F("Crouch");
    id.crouching = F("Crouching");
    id.uncrouching = F("Uncrouching");
    id.walkF = F("WalkFwd");
    id.walkFLoop = F("WalkFwd_Loop");
    id.walkFEnd = F("WalkFwd_End");
    id.walkB = F("WalkBack");
    id.walkBLoop = F("WalkBack_Loop");
    id.walkBEnd = F("WalkBack_End");
    id.dashF = F("DashFwd");
    id.dashFLoop = F("DashFwd_Loop");
    id.dashFEnd = F("DashFwd_End");
    id.dashB = F("DashBack");
    static const char* sfx[3] = {"N", "F", "B"};
    for (int d = 0; d < 3; ++d) {
        const std::string x = sfx[d];
        id.jumpStart[d] = F(("Jump_Start_" + x).c_str());
        id.jumpRise[d] = F(("JumpRise_" + x).c_str());
        id.jumpApogee[d] = F(("JumpApogee_" + x).c_str());
        id.jumpFall[d] = F(d == 0 ? "JumpFall" : ("JumpFall_" + x).c_str());
        id.land[d] = F(("JumpLand_" + x).c_str());
    }
    // The older characters (Filia ...) have one jump start and one landing for every direction instead of _N / _F / _B: the rise /
    // apogee / fall states are still per direction, so only these two need a fallback.
    sharedJumpStart = id.jumpStart[0] < 0 && F("Jump_Start") >= 0;
    sharedJumpLand = id.land[0] < 0 && F("JumpLand") >= 0;
    for (int d = 0; d < 3; ++d) {
        if (sharedJumpStart) id.jumpStart[d] = F("Jump_Start");
        if (sharedJumpLand) id.land[d] = F("JumpLand");
    }
    static const char* pre[3] = {"s.", "c.", "j."};
    for (int p = 0; p < kPostureCount; ++p)
        for (int b = 0; b < kNormalCount; ++b)
            for (int set = 0; set < kButtonSets && id.normal[p][b] < 0; ++set)
                id.normal[p][b] = F((std::string(pre[p]) + kButtonNames[set][b]).c_str());
    // What the names did not give, from the character's own table: a missing neutral normal (Peacock's air MK is j.MK_N) and the
    // forward / back variants.  A name match always wins over the table.
    for (const HubNormal& h : hub) {
        int& base = id.normal[h.posture][h.button];
        if (h.dir == 0) {
            if (base < 0) base = h.state;
        } else if (h.dir <= 3) {
            int& v = (h.dir == 1 ? id.normalF : (h.dir == 2 ? id.normalB : id.normalD))[h.posture][h.button];
            if (v < 0 && h.state != base) v = h.state;
        }   // dir 4 (up / diagonals): no input for it in the duel yet
    }
    static const char* lv[3] = {"High", "Mid", "Low"};
    for (int l = 0; l < 3; ++l) {
        id.hit[kPStand][l] = F((std::string("s.Hit") + lv[l] + "1").c_str());
        id.hit[kPCrouch][l] = F((std::string("c.Hit") + lv[l] + "1").c_str());
        id.hit[kPAir][l] = F("j.HitMid1");
    }
    // crouching has no "mid" hit pose: it reuses the high one
    if (id.hit[kPCrouch][1] < 0) id.hit[kPCrouch][1] = id.hit[kPCrouch][0];
    id.blockStun[kPStand][0] = F("s.BlockStun_High");
    id.blockStun[kPStand][1] = F("s.BlockStun_Mid");
    id.blockStun[kPStand][2] = F("s.BlockStun_Mid");
    id.blockStun[kPCrouch][0] = F("c.BlockStun_Mid");
    id.blockStun[kPCrouch][1] = F("c.BlockStun_Mid");
    id.blockStun[kPCrouch][2] = F("c.BlockStun_Low");
    for (int l = 0; l < 3; ++l) id.blockStun[kPAir][l] = F("j.BlockStun");
    id.knockback = F("Hit_Knockback");
    static const char* ku[7] = {"Knockup_+90", "Knockup_+45", "Knockup_+0", "Knockup_-0", "Knockup_-45", "Knockup_-60", "Knockup_-90"};
    for (int i = 0; i < 7; ++i) id.knockup[i] = F(ku[i]);
    id.floorImpact = F("OnFloor_Back_Impact_ForReal");
    id.floorLie = F("OnFloor_Back_LieThere_ForReal");
    id.floorLieLoop = F("OnFloor_Back_LieThere_Loop");
    id.wakeup = F("Wakeup_Faceup");
    id.deadPoint = F("OnFloor_Back_Dead_Point");
    id.winPose = F("WinPose");

    // ---- behaviour classes
    auto setCat = [&](const char* nm, Cat c) {
        const int s = find(nm);
        if (s >= 0) cat_[static_cast<size_t>(s)] = c;
    };
    for (const char* nm : {"Stand", "Uncrouching", "TurnStanding", "WalkFwd_End", "WalkBack_End"}) setCat(nm, Cat::kIdle);
    for (const char* nm : {"Crouch", "Crouching", "TurnCrouching"}) setCat(nm, Cat::kCrouch);
    for (const char* nm : {"WalkFwd", "WalkFwd_Loop"}) setCat(nm, Cat::kWalkF);
    for (const char* nm : {"WalkBack", "WalkBack_Loop"}) setCat(nm, Cat::kWalkB);
    for (const char* nm : {"DashFwd", "DashFwd_Loop", "DashFwd_End", "DashBack", "DashBack_End"}) setCat(nm, Cat::kDash);
    for (const char* nm : {"AirDash_ForReal", "AirBackdash_ForReal"}) setCat(nm, Cat::kAirDash);
    for (int d = 0; d < 3; ++d) {
        const std::string x = sfx[d];
        setCat(("Jump_Start_" + x).c_str(), Cat::kJumpStart);
        setCat(("JumpRise_" + x).c_str(), Cat::kRise);
        setCat(("JumpRise_Loop_" + x).c_str(), Cat::kRise);
        setCat(("JumpApogee_" + x).c_str(), Cat::kRise);
        setCat(d == 0 ? "JumpFall" : ("JumpFall_" + x).c_str(), Cat::kFall);
        setCat(d == 0 ? "JumpFall_Loop" : ("JumpFall_" + x + "_Loop").c_str(), Cat::kFall);
        setCat(("JumpLand_" + x).c_str(), Cat::kLand);
        setCat(("JumpLand_Recovery_" + x).c_str(), Cat::kLand);
    }
    if (sharedJumpStart) setCat("Jump_Start", Cat::kJumpStart);
    if (sharedJumpLand) {
        setCat("JumpLand", Cat::kLand);
        setCat("JumpLand_Recovery", Cat::kLand);
    }
    for (int p = 0; p < kPostureCount; ++p)
        for (int b = 0; b < kNormalCount; ++b) {
            const Cat c = (b % 3 == 0) ? Cat::kAttackL : (b % 3 == 1 ? Cat::kAttackM : Cat::kAttackH);
            for (const int s : {id.normal[p][b], id.normalF[p][b], id.normalB[p][b], id.normalD[p][b]})
                if (s >= 0) cat_[static_cast<size_t>(s)] = c;
        }

    // ---- physics from the character's own scripts
    auto stateVals = [&](std::initializer_list<const char*> names, Vals& v) {
        for (const char* nm : names) {
            const int s = find(nm);
            if (s >= 0) walkState(body.states[static_cast<size_t>(s)], v);
        }
    };
    {
        Vals v;
        stateVals({"WalkFwd"}, v);
        if (v.k[kSlotVx] && v.v[kSlotVx] > 0.5f) walkF = v.v[kSlotVx];
    }
    {
        Vals v;
        stateVals({"WalkBack"}, v);
        if (v.k[kSlotVx] && v.v[kSlotVx] < -0.5f) walkB = -v.v[kSlotVx];
    }
    {
        Vals v;
        stateVals({"DashFwd", "DashFwd_Loop"}, v);
        if (v.seen[kSlotVx] && v.peak[kSlotVx] > 1.0f) dashF = v.peak[kSlotVx];
    }
    {
        Vals v;
        stateVals({"DashBack"}, v);
        if (v.seen[kSlotVx] && v.peak[kSlotVx] < -1.0f) dashB = -v.peak[kSlotVx];
    }
    {
        Vals v;
        stateVals({"Jump_Start_N"}, v);
        if (v.seen[kSlotVy] && v.peak[kSlotVy] > 4.0f) jumpVy = v.peak[kSlotVy];
        Vals f;
        stateVals({"Jump_Start_F"}, f);
        if (f.seen[kSlotVx] && std::fabs(f.peak[kSlotVx]) > 1.0f) jumpVx = std::fabs(f.peak[kSlotVx]);
    }
    if (body.slotDefaultsValid) {
        const float g = bitsToFloat(body.slotDefaults[kSlotGravity]);
        if (g > 0.2f && g < 6.0f) gravity = g;
    }
    for (int i = 0; i < 4; ++i) pushDefault[i] = body.box[i];

    if (id.stand < 0) {
        if (err) *err = "character has no \"Stand\" state";
        ch_.reset();
        tl_.clear();
        return false;
    }
    return true;
}

}  // namespace fight
}  // namespace zengine
