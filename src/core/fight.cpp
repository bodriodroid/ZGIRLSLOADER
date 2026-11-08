#include "fight.h"

#include <cstring>

namespace zengine {
namespace fight {

uint16_t expandMacros(uint16_t b) {
    if (b & kMacro1) b |= kLP | kMP | kHP;
    if (b & kMacro2) b |= kLK | kMK | kHK;
    return b;
}

std::string describeButtons(uint16_t b) {
    b = expandMacros(b);
    std::string s;
    auto add = [&](const char* n) {
        if (!s.empty()) s += '+';
        s += n;
    };
    if (b & kUp) add("U");
    if (b & kDown) add("D");
    if (b & kLeft) add("L");
    if (b & kRight) add("R");
    if (b & kLP) add("LP");
    if (b & kMP) add("MP");
    if (b & kHP) add("HP");
    if (b & kLK) add("LK");
    if (b & kMK) add("MK");
    if (b & kHK) add("HK");
    if (b & kStart) add("ST");
    if (b & kSelect) add("SE");
    return s.empty() ? "-" : s;
}

const char* stateName(CharState s) {
    switch (s) {
        case CharState::kIdle: return "Idle";
        case CharState::kWalkForward: return "WalkForward";
        case CharState::kWalkBack: return "WalkBack";
        case CharState::kCrouch: return "Crouch";
        case CharState::kJumpStart: return "JumpStart";
        case CharState::kJumpRise: return "JumpRise";
        case CharState::kJumpFall: return "JumpFall";
        case CharState::kJumpLand: return "JumpLand";
        case CharState::kDash: return "Dash";
        case CharState::kAirDash: return "AirDash";
        case CharState::kBlockStand: return "BlockStand";
        case CharState::kBlockCrouch: return "BlockCrouch";
        case CharState::kBlockAir: return "BlockAir";
        case CharState::kHitstunStand: return "HitstunStand";
        case CharState::kHitstunCrouch: return "HitstunCrouch";
        case CharState::kHitstunAir: return "HitstunAir";
        case CharState::kLauncher: return "Launcher";
        case CharState::kKnockdown: return "Knockdown";
        case CharState::kWakeup: return "Wakeup";
        case CharState::kAttackLight: return "AttackLight";
        case CharState::kAttackMedium: return "AttackMedium";
        case CharState::kAttackHeavy: return "AttackHeavy";
        case CharState::kAttackSpecial: return "AttackSpecial";
        case CharState::kAttackBlockbuster: return "AttackBlockbuster";
        case CharState::kThrow: return "Throw";
        case CharState::kThrowTech: return "ThrowTech";
        case CharState::kBurst: return "Burst";
        case CharState::kTaunt: return "Taunt";
        case CharState::kIntro: return "Intro";
        case CharState::kWin: return "Win";
        case CharState::kDead: return "Dead";
        default: return "?";
    }
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

namespace {
// Write/read each field explicitly so the byte layout is identical everywhere.
struct Packer {
    std::vector<uint8_t>& out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
};

struct Unpacker {
    const std::vector<uint8_t>& in;
    std::size_t p = 0;
    bool ok = true;
    uint8_t u8() {
        if (p + 1 > in.size()) { ok = false; return 0; }
        return in[p++];
    }
    uint16_t u16() {
        uint16_t v = u8();
        v |= static_cast<uint16_t>(u8()) << 8;
        return v;
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(u8()) << (8 * i);
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
};

uint32_t fnv1a(const std::vector<uint8_t>& b) {
    uint32_t h = 0x811C9DC5u;
    for (uint8_t c : b) {
        h ^= c;
        h *= 0x01000193u;
    }
    return h;
}
}  // namespace

std::vector<uint8_t> saveState(const MatchState& s) {
    std::vector<uint8_t> out;
    out.reserve(240);
    Packer pk{out};
    pk.u32(s.frame);
    for (int i = 0; i < 2; ++i) {
        const Fighter& f = s.p[i];
        pk.i32(f.x);
        pk.i32(f.y);
        pk.i32(f.vx);
        pk.i32(f.vy);
        pk.i32(f.health);
        pk.i32(f.meter);
        pk.i32(f.undizzy);
        pk.u16(f.stateTimer);
        pk.u16(f.hitstun);
        pk.u16(f.blockstun);
        pk.u16(f.comboCount);
        pk.u16(static_cast<uint16_t>(f.state));
        pk.u8(static_cast<uint8_t>(f.facing));
        pk.u8(f.airborne);
        pk.u8(f.pad);
        pk.u16(f.mv);
        pk.u16(f.mvTick);
        pk.u16(f.hitstop);
        pk.u16(static_cast<uint16_t>(f.hitId));
        pk.u16(f.buffer);
        pk.u16(f.prevIn);
        pk.u8(f.bufferAge);
        pk.u8(f.seq);
        pk.u8(f.tap);
        pk.u8(f.tapDir);
        pk.u8(f.flags);
        pk.u8(f.pad2);
        for (int k = 0; k < 8; ++k) pk.u8(f.histDir[k]);
        for (int k = 0; k < 8; ++k) pk.u8(f.histLen[k]);
        pk.i32(f.recoil);
        pk.u32(f.chainUsed);
        pk.u8(f.invul);
    }
    pk.u32(s.rng);
    pk.u16(s.roundTimer);
    pk.u8(s.roundOver);
    pk.u8(s.pad);
    return out;
}

bool loadState(MatchState& s, const std::vector<uint8_t>& buf) {
    Unpacker up{buf};
    s.frame = up.u32();
    for (int i = 0; i < 2; ++i) {
        Fighter& f = s.p[i];
        f.x = up.i32();
        f.y = up.i32();
        f.vx = up.i32();
        f.vy = up.i32();
        f.health = up.i32();
        f.meter = up.i32();
        f.undizzy = up.i32();
        f.stateTimer = up.u16();
        f.hitstun = up.u16();
        f.blockstun = up.u16();
        f.comboCount = up.u16();
        f.state = static_cast<CharState>(up.u16());
        f.facing = static_cast<Facing>(up.u8());
        f.airborne = up.u8();
        f.pad = up.u8();
        f.mv = up.u16();
        f.mvTick = up.u16();
        f.hitstop = up.u16();
        f.hitId = static_cast<int16_t>(up.u16());
        f.buffer = up.u16();
        f.prevIn = up.u16();
        f.bufferAge = up.u8();
        f.seq = up.u8();
        f.tap = up.u8();
        f.tapDir = up.u8();
        f.flags = up.u8();
        f.pad2 = up.u8();
        for (int k = 0; k < 8; ++k) f.histDir[k] = up.u8();
        for (int k = 0; k < 8; ++k) f.histLen[k] = up.u8();
        f.recoil = up.i32();
        f.chainUsed = up.u32();
        f.invul = up.u8();
    }
    s.rng = up.u32();
    s.roundTimer = up.u16();
    s.roundOver = up.u8();
    s.pad = up.u8();
    return up.ok && up.p == buf.size();
}

uint32_t MatchState::checksum() const { return fnv1a(saveState(*this)); }

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

namespace {
// xorshift32 - small, fast, and identical on every platform.
uint32_t nextRng(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

constexpr fixed kGravity = toFixed(-0.55f);
constexpr fixed kWalkSpeed = toFixed(3.2f);
constexpr fixed kBackSpeed = toFixed(2.6f);
constexpr fixed kJumpVelocity = toFixed(11.5f);
constexpr fixed kStageHalfWidth = toFixed(1400.0f);

bool isAttacking(CharState s) {
    return s == CharState::kAttackLight || s == CharState::kAttackMedium ||
           s == CharState::kAttackHeavy || s == CharState::kAttackSpecial ||
           s == CharState::kAttackBlockbuster || s == CharState::kThrow;
}
}  // namespace

void advanceFrame(MatchState& s, uint16_t p1Buttons, uint16_t p2Buttons) {
    const uint16_t in[2] = {expandMacros(p1Buttons), expandMacros(p2Buttons)};

    ++s.frame;
    if (s.roundTimer > 0 && !s.roundOver) --s.roundTimer;

    // Facing: characters turn to look at each other when neither is committed.
    if (!isAttacking(s.p[0].state) && s.p[0].stateTimer == 0 && !isAttacking(s.p[1].state) &&
        s.p[1].stateTimer == 0) {
        s.p[0].facing = (s.p[0].x <= s.p[1].x) ? Facing::kRight : Facing::kLeft;
        s.p[1].facing = (s.p[1].x <= s.p[0].x) ? Facing::kRight : Facing::kLeft;
    }

    for (int i = 0; i < 2; ++i) {
        Fighter& f = s.p[i];
        const uint16_t b = in[i];
        const Fighter& other = s.p[1 - i];

        if (f.stateTimer > 0) {
            if (--f.stateTimer == 0 && !isAttacking(f.state)) {
                f.state = f.airborne ? CharState::kJumpFall : CharState::kIdle;
            }
        }
        if (f.hitstun > 0) {
            if (--f.hitstun == 0) {
                f.state = f.airborne ? CharState::kHitstunAir : CharState::kIdle;
                f.comboCount = 0;
            }
        }
        if (f.blockstun > 0) {
            if (--f.blockstun == 0 && !isAttacking(f.state)) f.state = CharState::kIdle;
        }

        const bool busy = f.stateTimer > 0 && isAttacking(f.state);
        const bool stunned = f.hitstun > 0 || f.blockstun > 0;

        // Horizontal movement.  Attacks lock movement; hitstun keeps the pushback.
        if (!busy && !stunned && !f.airborne) {
            int dir = 0;
            if (b & kLeft) --dir;
            if (b & kRight) ++dir;
            if (f.state == CharState::kCrouch) dir = 0;
            if (dir != 0) {
                const bool forward = (f.facing == Facing::kRight) ? (dir > 0) : (dir < 0);
                const fixed speed = forward ? kWalkSpeed : kBackSpeed;
                f.x += (dir > 0) ? speed : -speed;
                f.state = forward ? CharState::kWalkForward : CharState::kWalkBack;
            } else if (b & kDown) {
                f.state = CharState::kCrouch;
            } else if (!isAttacking(f.state)) {
                f.state = CharState::kIdle;
            }
        }

        // Jump / air physics.
        if (!busy && !stunned && !f.airborne && (b & kUp)) {
            f.vy = kJumpVelocity;
            f.airborne = 1;
            f.state = CharState::kJumpRise;
            f.stateTimer = 4;
        }
        if (f.airborne) {
            f.vy += kGravity;
            f.y += f.vy;
            if (f.y <= 0) {
                f.y = 0;
                f.vy = 0;
                f.airborne = 0;
                if (f.state != CharState::kHitstunAir) {
                    f.state = CharState::kJumpLand;
                    f.stateTimer = 3;
                } else {
                    f.state = CharState::kKnockdown;
                    f.stateTimer = 12;
                }
            }
        }

        // Attack startup.  Three buttons -> light / medium / heavy; the executable
        // labels these exactly as "s.LP", "s.MP", "s.HP", ... with j. variants
        // while airborne.
        if (!busy && !stunned) {
            CharState atk = CharState::kStateCount;
            uint16_t dur = 0;
            if (b & kHP) {
                atk = CharState::kAttackHeavy;
                dur = 22;
            } else if (b & kMP) {
                atk = CharState::kAttackMedium;
                dur = 17;
            } else if (b & kLP) {
                atk = CharState::kAttackLight;
                dur = 11;
            } else if (b & kHK) {
                atk = CharState::kAttackHeavy;
                dur = 26;
            } else if (b & kMK) {
                atk = CharState::kAttackMedium;
                dur = 19;
            } else if (b & kLK) {
                atk = CharState::kAttackLight;
                dur = 13;
            }
            if (atk != CharState::kStateCount) {
                f.state = atk;
                f.stateTimer = dur;
            }
        }

        // Clamp to the stage.
        if (f.x < -kStageHalfWidth) f.x = -kStageHalfWidth;
        if (f.x > kStageHalfWidth) f.x = kStageHalfWidth;
        (void)other;
    }

    // Minimal pushbox separation: fighters cannot occupy the same space.
    constexpr fixed kPushMin = toFixed(60.0f);
    const fixed dx = s.p[1].x - s.p[0].x;
    if (dx < kPushMin && dx > -kPushMin) {
        const fixed half = (kPushMin - (dx < 0 ? -dx : dx)) / 2;
        if (dx == 0) {
            s.p[0].x -= half;
            s.p[1].x += half;
        } else if (dx > 0) {
            s.p[0].x -= half;
            s.p[1].x += half;
        } else {
            s.p[0].x += half;
            s.p[1].x -= half;
        }
    }

    // Resolve attacks whose active window just opened.
    for (int i = 0; i < 2; ++i) {
        Fighter& atk = s.p[i];
        Fighter& def = s.p[1 - i];
        if (!isAttacking(atk.state) || atk.stateTimer == 0) continue;

        const bool heavy = atk.state == CharState::kAttackHeavy;
        const uint16_t reach = heavy ? 96 : (atk.state == CharState::kAttackMedium ? 84 : 72);
        const fixed gap = (def.x - atk.x) < 0 ? -(def.x - atk.x) : (def.x - atk.x);
        if (gap > toFixed(static_cast<float>(reach))) continue;
        const bool facingOpponent = ((def.x - atk.x) > 0) == (atk.facing == Facing::kRight);
        if (!facingOpponent && gap > toFixed(40.0f)) continue;

        const int damage = heavy ? 120 : (atk.state == CharState::kAttackMedium ? 80 : 45);
        const uint16_t stun = heavy ? 24 : (atk.state == CharState::kAttackMedium ? 18 : 12);

        // Blocking: holding away from the opponent while grounded.
        const bool holdingAway = ((atk.facing == Facing::kRight) && (in[1 - i] & kRight)) ||
                                 ((atk.facing == Facing::kLeft) && (in[1 - i] & kLeft));
        const bool blocking = holdingAway && !def.airborne && !isAttacking(def.state) &&
                              def.hitstun == 0;

        if (blocking) {
            def.blockstun = static_cast<uint16_t>(stun / 2);
            def.health -= damage / 8;  // chip damage
            def.meter += damage / 4;
        } else {
            def.health -= damage;
            def.hitstun = stun;
            def.state = def.airborne ? CharState::kHitstunAir : CharState::kHitstunStand;
            def.comboCount = static_cast<uint16_t>(def.comboCount + 1);
            def.undizzy += damage;
            if (heavy && !def.airborne) {
                def.airborne = 1;
                def.vy = toFixed(7.0f);
            }
            atk.meter += damage;
        }

        if (def.health < 0) def.health = 0;
        if (atk.meter > 5000) atk.meter = 5000;
        if (def.meter > 5000) def.meter = 5000;
        if (def.health == 0) {
            def.state = CharState::kDead;
            s.roundOver = 1;
        }
    }

    // Advance the shared PRNG once per tick so that any randomised system (stage
    // hazards, AI) consumes randomness in a fixed order.
    nextRng(s.rng);
}

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------

namespace {
void cbSave(const MatchState& s, std::vector<uint8_t>& buf, void*) { buf = saveState(s); }
bool cbLoad(MatchState& s, const std::vector<uint8_t>& buf, void*) { return loadState(s, buf); }
void cbAdvance(MatchState& s, uint16_t a, uint16_t b, void*) { advanceFrame(s, a, b); }
void cbLog(const MatchState& s, const char* tag, void*) {
    std::printf("[synctest] %s frame=%u checksum=%08u\n", tag ? tag : "", s.frame, s.checksum());
}
}  // namespace

Callbacks defaultCallbacks() { return Callbacks{cbSave, cbLoad, cbAdvance, cbLog, nullptr}; }

LocalSession::LocalSession(MatchState& state, Callbacks cb) : state_(state), cb_(cb) {}

NetResult LocalSession::addLocalInput(uint16_t buttons, int* simulatedFrames) {
    pending_[1] = pending_[0];
    pending_[0] = buttons;
    cb_.advance(state_, pending_[0], pending_[1], cb_.user);
    if (simulatedFrames) *simulatedFrames = 1;
    return NetResult::kOk;
}

NetResult LocalSession::addLocalInput(uint16_t p1, uint16_t p2, int* simulatedFrames) {
    pending_[0] = p1;
    pending_[1] = p2;
    cb_.advance(state_, pending_[0], pending_[1], cb_.user);
    if (simulatedFrames) *simulatedFrames = 1;
    return NetResult::kOk;
}

SyncTestSession::SyncTestSession(MatchState& state, Callbacks cb, int checkDistance)
    : state_(state), cb_(cb), checkDistance_(checkDistance < 1 ? 1 : checkDistance) {}

NetResult SyncTestSession::addLocalInput(uint16_t buttons, int* simulatedFrames) {
    // Snapshot before stepping.
    if (cb_.save) cb_.save(state_, snapshot_, cb_.user);
    else snapshot_ = saveState(state_);

    const uint32_t before = state_.checksum();
    (void)before;

    cb_.advance(state_, buttons, 0, cb_.user);

    // Every checkDistance frames, replay from the snapshot and compare.
    if (state_.frame % static_cast<uint32_t>(checkDistance_) == 0) {
        MatchState replay{};
        if (cb_.load ? cb_.load(replay, snapshot_, cb_.user) : loadState(replay, snapshot_)) {
            cb_.advance(replay, buttons, 0, cb_.user);
            ++framesChecked_;
            const uint32_t a = state_.checksum(), b = replay.checksum();
            if (a != b) {
                ++mismatches_;
                if (cb_.logState) {
                    cb_.logState(state_, "MISMATCH live", cb_.user);
                    cb_.logState(replay, "MISMATCH replay", cb_.user);
                }
            }
        }
    }
    if (simulatedFrames) *simulatedFrames = 1;
    return NetResult::kOk;
}

NetResult SyncTestSession::addLocalInput(uint16_t p1, uint16_t p2, int* simulatedFrames) {
    if (cb_.save) cb_.save(state_, snapshot_, cb_.user);
    else snapshot_ = saveState(state_);

    cb_.advance(state_, p1, p2, cb_.user);

    if (state_.frame % static_cast<uint32_t>(checkDistance_) == 0) {
        MatchState replay{};
        if (cb_.load ? cb_.load(replay, snapshot_, cb_.user) : loadState(replay, snapshot_)) {
            cb_.advance(replay, p1, p2, cb_.user);
            ++framesChecked_;
            const uint32_t a = state_.checksum(), b = replay.checksum();
            if (a != b) {
                ++mismatches_;
                if (cb_.logState) {
                    cb_.logState(state_, "MISMATCH live", cb_.user);
                    cb_.logState(replay, "MISMATCH replay", cb_.user);
                }
            }
        }
    }
    if (simulatedFrames) *simulatedFrames = 1;
    return NetResult::kOk;
}

}  // namespace fight
}  // namespace zengine
