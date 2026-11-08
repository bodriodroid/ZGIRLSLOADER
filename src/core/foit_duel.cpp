// foit_duel.cpp - see foit_duel.h.
#include "foit_duel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace zengine {
namespace fight {
namespace {

// Slots of the engine's object record the adapter reads (see docs/RE_NOTES.md).
constexpr int kSlotPlayerNum = 25;
constexpr int kSlotFacing = 56;
constexpr int kSlotHitStun = 53;
constexpr int kSlotLife = 64;
constexpr int kSlotRedLife = 65;
constexpr int kSlotSuperBar = 66, kSlotSuperFlash = 71;
constexpr int kSlotXVel = 158, kSlotYVel = 159;
constexpr int kSlotXScale = 164, kSlotYScale = 165, kSlotAlpha = 166;
constexpr int kSlotComboHits = 11;   // hits the fighter has landed in the combo that is running

constexpr uint32_t kSnapMagic = 0x32554446u;  // "FDU2"
constexpr uint16_t kAttackBits = kLP | kMP | kHP | kLK | kMK | kHK;

constexpr float kMaxMeterSlot = 500.0f;  // SUPERBAR: 100 per level, 5 levels

template <typename T>
void put(std::vector<uint8_t>& o, const T& v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    o.insert(o.end(), p, p + sizeof(T));
}

template <typename T>
bool get(const uint8_t*& p, const uint8_t* end, T* v) {
    if (static_cast<size_t>(end - p) < sizeof(T)) return false;
    std::memcpy(v, p, sizeof(T));
    p += sizeof(T);
    return true;
}

// 16-bit unsigned clamp of a float.
uint16_t u16(float v) { return v <= 0 ? 0 : (v >= 65535.0f ? 65535 : static_cast<uint16_t>(v)); }

// 1 when the opponent could read the object as "in an attack" (what the AI wants), by the strength the node declared.
CharState attackState(const rt::Object& o) {
    switch (o.strength) {
        case 0:
        case 1: return CharState::kAttackLight;
        case 2: return CharState::kAttackMedium;
        default: return CharState::kAttackHeavy;
    }
}

// The coarse state the HUD / AI / debug overlay know (the real state is the one in the .foit; this is only a label).
CharState labelOf(const rt::Object& o, int phase) {
    if (phase == rt::Match::Intro) return CharState::kIntro;
    if (o.life() <= 0) return CharState::kDead;
    const bool air = !o.onGround;
    if ((o.fb & rt::fl::B_Blocking) != 0) return air ? CharState::kBlockAir : CharState::kBlockStand;
    if ((o.fb & rt::fl::B_GotHit) != 0 || o.slots[kSlotHitStun] > 0) return air ? CharState::kHitstunAir : CharState::kHitstunStand;
    if ((o.fb & rt::fl::B_Attack) != 0) return attackState(o);
    if (air) return CharState::kJumpFall;
    const float vx = o.slots[kSlotXVel];
    if (std::fabs(vx) > 0.25f) return (vx > 0) == (o.slots[kSlotFacing] == 1.0f) ? CharState::kWalkForward : CharState::kWalkBack;
    return CharState::kIdle;
}

}  // namespace

void FoitDuel::splitInput(uint16_t buttons, int* dir, uint32_t* mask) {
    const uint16_t b = expandMacros(buttons);
    int dx = 0, dy = 0;   // opposite directions cancel (neutral on that axis)
    if (b & kRight) ++dx;
    if (b & kLeft) --dx;
    if (b & kUp) ++dy;
    if (b & kDown) --dy;
    int d = rt::DirNeutral;
    if (dx > 0) d = dy > 0 ? rt::DirUpRight : (dy < 0 ? rt::DirDownRight : rt::DirRight);
    else if (dx < 0) d = dy > 0 ? rt::DirUpLeft : (dy < 0 ? rt::DirDownLeft : rt::DirLeft);
    else d = dy > 0 ? rt::DirUp : (dy < 0 ? rt::DirDown : rt::DirNeutral);
    *dir = d;
    *mask = static_cast<uint32_t>((b >> 4) & 0x3F);   // kLP..kHK = bits 4..9 -> sample buttons 0..5
}

bool FoitDuel::init(const FoitDuelConfig& cfg) {
    ready_ = false;
    if (!cfg.ch[0] || !cfg.ch[1]) return false;
    cfg_ = cfg;
    w_ = rt::World();
    events_.clear();
    lastHits_ = 0;
    w_.leftWall = -cfg_.stageHalf;
    w_.rightWall = cfg_.stageHalf;
    w_.floorY = 0;
    if (!cfg_.dizzyText.empty()) rt::parseStrengthTable(cfg_.dizzyText, w_.strengthTable);
    for (int i = 0; i < 2; ++i) {
        obj_[i] = std::make_unique<rt::Object>();
        const foit::Character& ch = *cfg_.ch[i];
        rt::init(*obj_[i], ch, ch.body, cfg_.startX[i], i == 0);
        obj_[i]->slots[kSlotPlayerNum] = i == 0 ? 1.0f : 2.0f;
        obj_[i]->charId = cfg_.charId[i];
        obj_[i]->palette = cfg_.palette[i];
    }
    obj_[0]->enemy = obj_[1].get();
    obj_[1]->enemy = obj_[0].get();
    for (int i = 0; i < 2; ++i) {
        const int stand = rt::findState(cfg_.ch[i]->body, "Stand");
        rt::enterState(*obj_[i], w_, stand < 0 ? 0 : stand);
        maxLife_[i] = std::max(1.0f, obj_[i]->life());
        w_.objects.push_back(obj_[i].get());
    }
    rt::MatchOptions mo;
    mo.roundSeconds = cfg_.training ? 0 : cfg_.roundSeconds;
    mo.roundsToWin = cfg_.roundsToWin;
    mo.introRounds = cfg_.introPoses ? 1 : 0;
    mo.readyTicks = cfg_.readyTicks;
    mo.endHold = cfg_.endHold;
    mo.endless = cfg_.training;
    mo.tieBreak = cfg_.tieBreak;
    rt::startMatch(w_, mo);
    w_.events.clear();
    prev_[0] = prev_[1] = rt::Sample();
    rawPrev_[0] = rawPrev_[1] = 0;
    ready_ = true;
    return true;
}

void FoitDuel::step(uint16_t in0, uint16_t in1) {
    if (!ready_) return;
    const uint16_t raw[2] = {expandMacros(in0), expandMacros(in1)};
    const int phase = w_.match.phase;
    // An attack button pressed (a new press, not one held since before) during the entrance cuts it short.  It is part of the
    // replicated input, so both peers agree.
    if (cfg_.skipIntroOnButton && phase == rt::Match::Intro && w_.match.phaseTicks > 1) {
        for (int i = 0; i < 2; ++i)
            if ((raw[i] & ~rawPrev_[i] & kAttackBits) != 0) {
                rt::matchSkipIntro(w_);
                break;
            }
    }
    rawPrev_[0] = raw[0];
    rawPrev_[1] = raw[1];
    // The pad only reaches the fighters while a fight is on: during the entrance, the round card and the round end nobody can act
    // (the shipped game cuts the control from outside the .foit scripts; here it is part of the world, so rollback sees it too).
    const int now = w_.match.phase;
    const bool live = now == rt::Match::Fight || now == rt::Match::Settle || now == rt::Match::Off;
    rt::Sample s[2];
    for (int i = 0; i < 2; ++i) {
        int dir = rt::DirNeutral;
        uint32_t mask = 0;
        if (live) splitInput(raw[i], &dir, &mask);
        s[i] = rt::composeSample(prev_[i], dir, mask);
        prev_[i] = s[i];
    }
    const uint64_t hitsBefore = w_.stats.hits + w_.stats.blocks;
    w_.events.clear();
    rt::tickAll(w_, s, 2);
    events_.swap(w_.events);
    w_.events.clear();
    lastHits_ = static_cast<int>(w_.stats.hits + w_.stats.blocks - hitsBefore);
}

void FoitDuel::save(std::vector<uint8_t>& out) const {
    out.clear();
    put(out, kSnapMagic);
    for (int i = 0; i < 2; ++i) put(out, prev_[i]);
    for (int i = 0; i < 2; ++i) put(out, rawPrev_[i]);
    std::vector<uint8_t> world;
    rt::saveWorld(w_, world);
    put(out, static_cast<uint32_t>(world.size()));
    out.insert(out.end(), world.begin(), world.end());
}

bool FoitDuel::load(const uint8_t* data, size_t size) {
    if (!ready_ || !data) return false;
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    uint32_t magic = 0, wsize = 0;
    if (!get(p, end, &magic) || magic != kSnapMagic) return false;
    rt::Sample prev[2];
    for (int i = 0; i < 2; ++i)
        if (!get(p, end, &prev[i])) return false;
    uint16_t rawPrev[2];
    for (int i = 0; i < 2; ++i)
        if (!get(p, end, &rawPrev[i])) return false;
    if (!get(p, end, &wsize) || static_cast<size_t>(end - p) < wsize) return false;
    std::vector<rt::Object*> chars = {obj_[0].get(), obj_[1].get()};
    if (!rt::loadWorld(w_, chars, p, wsize)) return false;
    prev_[0] = prev[0];
    prev_[1] = prev[1];
    rawPrev_[0] = rawPrev[0];
    rawPrev_[1] = rawPrev[1];
    events_.clear();
    lastHits_ = 0;
    return true;
}

uint64_t FoitDuel::hash() const {
    uint64_t h = rt::hashWorld(w_);
    for (int i = 0; i < 2; ++i) {   // the edge state of the pad decides the next tick as much as the world does
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&prev_[i]);
        for (size_t k = 0; k < sizeof(rt::Sample); ++k) h = (h ^ p[k]) * 0x100000001b3ull;
        h = (h ^ rawPrev_[i]) * 0x100000001b3ull;
    }
    return h;
}

void FoitDuel::project(MatchState& s) const {
    const int phase = w_.match.phase;
    for (int i = 0; i < 2; ++i) {
        const rt::Object& o = *obj_[i];
        Fighter& f = s.p[i];
        f.x = toFixed(o.x);
        f.y = toFixed(o.y);
        f.vx = toFixed(o.slots[kSlotXVel]);
        f.vy = toFixed(o.slots[kSlotYVel]);
        f.health = static_cast<int32_t>(std::clamp(o.life() * 1000.0f / maxLife_[i], 0.0f, 1000.0f));
        f.meter = static_cast<int32_t>(std::clamp(o.slots[kSlotSuperBar], 0.0f, kMaxMeterSlot) * 10.0f);
        f.comboCount = u16(o.slots[kSlotComboHits]);
        f.state = labelOf(o, phase);
        f.facing = o.slots[kSlotFacing] == 1.0f ? Facing::kRight : Facing::kLeft;
        f.airborne = o.onGround ? 0 : 1;
        f.stateTimer = u16(static_cast<float>(std::max(0, o.nodeDur - o.nodeTimer)));
        const bool blocking = (o.fb & rt::fl::B_Blocking) != 0;
        f.hitstun = blocking ? 0 : u16(o.slots[kSlotHitStun]);
        f.blockstun = blocking ? u16(o.slots[kSlotHitStun]) : 0;
        f.hitstop = u16(o.slots[52]);
    }
    s.roundTimer = u16(static_cast<float>(w_.match.clock));
    s.roundOver = (phase == rt::Match::Settle || phase == rt::Match::Ending || phase == rt::Match::Over) ? 1 : 0;
    // the checksum GGPO compares: the world hash folded into the shared PRNG field
    const uint64_t h = hash();
    s.rng = static_cast<uint32_t>(h) ^ static_cast<uint32_t>(h >> 32);
}

bool FoitDuel::superFlash(int side) const { return obj_[side & 1]->slots[kSlotSuperFlash] != 0; }

float FoitDuel::lifeFraction(int side) const {
    const int i = side & 1;
    return std::clamp(obj_[i]->life() / maxLife_[i], 0.0f, 1.0f);
}

void FoitDuel::views(std::vector<FoitView>& out) const {
    out.clear();
    for (const rt::Object* o : w_.objects) {
        if (!o->alive || !o->ch) continue;
        FoitView v;
        v.ch = o->ch;
        v.side = o->leader == obj_[1].get() ? 1 : 0;
        v.body = o == obj_[0].get() || o == obj_[1].get();
        v.frame = (o->frame > 0 && static_cast<size_t>(o->frame) <= o->ch->frames.size()) ? o->frame - 1 : -1;
        v.x = o->x;
        v.y = o->y;
        v.faceRight = o->slots[kSlotFacing] == 1.0f;
        v.xscale = o->slots[kSlotXScale];
        v.yscale = o->slots[kSlotYScale];
        v.alpha = o->slots[kSlotAlpha];
        out.push_back(v);
    }
}

void FoitDuel::lookahead(int side, std::vector<uint16_t>& out, int maxFrames) const {
    out.clear();
    const rt::Object& o = *obj_[side & 1];
    int state = o.state;
    int node = o.nodeIdx;
    for (int hops = 0; hops < 3 && static_cast<int>(out.size()) < maxFrames; ++hops) {
        if (state < 0 || state >= static_cast<int>(o.def->states.size())) break;
        const foit::State& st = o.def->states[static_cast<size_t>(state)];
        for (size_t n = static_cast<size_t>(std::max(0, node)); n < st.hdr.nodes.size() && static_cast<int>(out.size()) < maxFrames; ++n) {
            const uint16_t f = st.hdr.nodes[n].refs[1];
            if (f != 0 && (out.empty() || out.back() != f - 1)) out.push_back(static_cast<uint16_t>(f - 1));
        }
        state = st.next;
        node = 0;
    }
}

// ---- GGPO callbacks ------------------------------------------------------------------------------------------------------
namespace {

void cbSave(const MatchState& s, std::vector<uint8_t>& buf, void* user) {
    auto* d = static_cast<FoitDuel*>(user);
    const std::vector<uint8_t> st = saveState(s);
    std::vector<uint8_t> world;
    d->save(world);
    buf.clear();
    put(buf, static_cast<uint32_t>(st.size()));
    buf.insert(buf.end(), st.begin(), st.end());
    buf.insert(buf.end(), world.begin(), world.end());
}

bool cbLoad(MatchState& s, const std::vector<uint8_t>& buf, void* user) {
    auto* d = static_cast<FoitDuel*>(user);
    const uint8_t* p = buf.data();
    const uint8_t* end = p + buf.size();
    uint32_t n = 0;
    if (!get(p, end, &n) || static_cast<size_t>(end - p) < n) return false;
    const std::vector<uint8_t> st(p, p + n);
    p += n;
    if (!loadState(s, st)) return false;
    return d->load(p, static_cast<size_t>(end - p));
}

void cbAdvance(MatchState& s, uint16_t p1, uint16_t p2, void* user) {
    auto* d = static_cast<FoitDuel*>(user);
    d->step(p1, p2);
    ++s.frame;
    d->project(s);
}

void cbLog(const MatchState&, const char*, void*) {}

}  // namespace

Callbacks foitDuelCallbacks(FoitDuel* d) {
    Callbacks cb{};
    cb.save = cbSave;
    cb.load = cbLoad;
    cb.advance = cbAdvance;
    cb.logState = cbLog;
    cb.user = d;
    return cb;
}

}  // namespace fight
}  // namespace zengine
