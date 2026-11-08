#include "duel.h"

#include <algorithm>
#include <cmath>

namespace zengine {
namespace fight {

namespace {

constexpr int kMaxHops = 8;
constexpr uint16_t kAtkButtons = kLP | kMP | kHP | kLK | kMK | kHK;

enum Flag : uint8_t { kFConnected = 1, kFBlocked = 2 };

// Knock-down sequence phases (Fighter::seq while state == kKnockdown).
enum Floor : uint8_t { kFloorImpact = 0, kFloorLie = 1, kFloorWait = 2 };

inline fixed F(float v) { return static_cast<fixed>(std::lround(v * static_cast<float>(kFixedOne))); }
inline int whole(fixed v) { return v >> kFixedShift; }
inline int sgn(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
inline int faceSign(const Fighter& f) { return f.facing == Facing::kRight ? 1 : -1; }

uint32_t nextRng(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

bool isAttackState(CharState s) {
    return s == CharState::kAttackLight || s == CharState::kAttackMedium || s == CharState::kAttackHeavy ||
           s == CharState::kAttackSpecial || s == CharState::kAttackBlockbuster || s == CharState::kThrow;
}

// States from which the player may start something new on the ground.
bool freeGround(CharState s) {
    return s == CharState::kIdle || s == CharState::kCrouch || s == CharState::kWalkForward ||
           s == CharState::kWalkBack || s == CharState::kJumpLand;
}

bool canBeHit(const Fighter& f) {
    if (f.invul) return false;  // combo escape (undizzy)
    switch (f.state) {
        case CharState::kKnockdown:
        case CharState::kWakeup:
        case CharState::kDead:
        case CharState::kIntro:
        case CharState::kWin: return false;
        default: return true;
    }
}

CharState stateFor(Cat c, CharState keep) {
    switch (c) {
        case Cat::kIdle: return CharState::kIdle;
        case Cat::kCrouch: return CharState::kCrouch;
        case Cat::kWalkF: return CharState::kWalkForward;
        case Cat::kWalkB: return CharState::kWalkBack;
        case Cat::kJumpStart: return CharState::kJumpStart;
        case Cat::kRise: return CharState::kJumpRise;
        case Cat::kFall: return CharState::kJumpFall;
        case Cat::kLand: return CharState::kJumpLand;
        case Cat::kDash: return CharState::kDash;
        case Cat::kAirDash: return CharState::kAirDash;
        case Cat::kAttackL: return CharState::kAttackLight;
        case Cat::kAttackM: return CharState::kAttackMedium;
        case Cat::kAttackH: return CharState::kAttackHeavy;
        default: return keep;
    }
}

// Switches the fighter to move-set state `st` (zero-length states are followed to their successor).
void enter(Fighter& f, const MoveSet& ms, int st) {
    int hops = 0;
    while (ms.valid(st) && ms.ticks(st) == 0 && ms.next(st) >= 0 && ms.next(st) != st && hops < kMaxHops) {
        st = ms.next(st);
        ++hops;
    }
    if (!ms.valid(st)) st = ms.id.stand;
    f.mv = static_cast<uint16_t>(st);
    f.mvTick = 0;
    f.hitId = -1;
    f.flags = static_cast<uint8_t>(f.flags & ~(kFConnected | kFBlocked));
    f.state = stateFor(ms.cat(st), f.state);
}

// Advances the timeline by one tick.  Returns true when it reached its end (looped, moved on or held).
bool stepAnim(Fighter& f, const MoveSet& ms) {
    const int total = ms.ticks(f.mv);
    ++f.mvTick;
    if (f.mvTick < total) return false;
    const int nx = ms.next(f.mv);
    if (nx == f.mv) {
        f.mvTick = 0;
        f.hitId = -1;  // a looping state re-arms its hit data
        return true;
    }
    if (nx < 0 || !ms.valid(nx)) {
        f.mvTick = static_cast<uint16_t>(std::max(0, total - 1));
        return true;
    }
    enter(f, ms, nx);
    return true;
}

bool lastTick(const Fighter& f, const MoveSet& ms) { return f.mvTick + 1 >= ms.ticks(f.mv); }

int pickButton(uint16_t b) {
    // strongest first; punch before kick at equal strength
    if (b & kHP) return 2;
    if (b & kHK) return 5;
    if (b & kMP) return 1;
    if (b & kMK) return 4;
    if (b & kLP) return 0;
    if (b & kLK) return 3;
    return -1;
}

void decay(Fighter& f) {
    f.vx = fixedMul(f.vx, 225);  // x 0.88 per tick
    if (f.vx > -F(0.25f) && f.vx < F(0.25f)) f.vx = 0;
}

int levelIndex(int level) { return level == 1 ? 0 : (level == 2 ? 2 : 1); }

// Chain rule between two normals (button index 0..5 = LP MP HP LK MK HK): stronger always; at the same strength a
// punch may go into the kick; lights may go into a different light when the rules allow it.  "Each normal once per
// chain" is checked by the caller with Fighter::chainUsed.
bool chainAllowed(int cur, int nw, const DuelRules& r) {
    const int cs = cur % 3, ns = nw % 3;
    if (ns > cs) return true;
    if (ns < cs) return false;
    if (cur < 3 && nw >= 3) return true;  // punch -> kick of the same strength
    return ns == 0 && r.chainLightsAnyOrder && nw != cur;
}

bool isSpecialAttack(CharState s) { return s == CharState::kAttackSpecial || s == CharState::kAttackBlockbuster; }

// A defender pushed into the wall cannot move: the attacker takes the push-back instead (ground attacks only).
void cornerRecoil(Fighter& a, const Fighter& d, int away, const DuelContent& c) {
    if (a.airborne || d.vx == 0) return;
    const fixed lim = static_cast<fixed>(c.stageHalf) << kFixedShift;
    const fixed margin = static_cast<fixed>(c.rules.wallMargin) << kFixedShift;
    const bool cornered = away > 0 ? d.x >= lim - margin : d.x <= -lim + margin;
    if (cornered) a.recoil = -d.vx;
}

int derivedBlockstun(const HitInfo& h, const DuelRules& r) {
    if (h.blockstunDecoded) return std::max(1, h.blockstun);
    const int s = std::clamp(h.strength, 1, 3) - 1;
    return std::max(r.minBlockstun, h.hitstun - r.blockstunLess[s]);
}

// ---- knock-down / recovery ----------------------------------------------------------------------------------
void startKnockdown(Fighter& f, const MoveSet& ms) {
    f.airborne = 0;
    f.y = 0;
    f.vy = 0;
    f.vx = f.vx / 2;
    f.state = f.health <= 0 ? CharState::kDead : CharState::kKnockdown;
    f.seq = kFloorImpact;
    f.hitstun = 0;
    f.blockstun = 0;
    f.stateTimer = 0;
    enter(f, ms, ms.id.floorImpact >= 0 ? ms.id.floorImpact : ms.id.stand);
    f.state = f.health <= 0 ? CharState::kDead : CharState::kKnockdown;
}

void recoverTo(Fighter& f, const MoveSet& ms, bool crouch) {
    f.hitstun = f.blockstun = 0;
    f.vx = 0;
    f.comboCount = 0;
    enter(f, ms, crouch ? ms.id.crouch : ms.id.stand);
    f.state = crouch ? CharState::kCrouch : CharState::kIdle;
}

void land(Fighter& f, const MoveSet& ms) {
    f.airborne = 0;
    f.y = 0;
    f.vy = 0;
    f.vx = 0;
    const int v = f.pad2 < 3 ? f.pad2 : 0;  // pad2 = jump direction of the jump in progress (0 N, 1 F, 2 B)
    enter(f, ms, ms.id.land[v] >= 0 ? ms.id.land[v] : ms.id.stand);
    f.state = CharState::kJumpLand;
    f.pad2 = 0;
}

// ---- one fighter, one tick ----------------------------------------------------------------------------------
void tick(MatchState& s, int i, uint16_t in, const DuelContent& c) {
    Fighter& f = s.p[i];
    const Fighter& o = s.p[1 - i];
    const MoveSet& ms = *c.ms[i];
    const DuelRules& R = c.rules;
    const uint16_t pressed = static_cast<uint16_t>(in & ~f.prevIn);
    f.prevIn = in;
    pushHistory(f, in);  // directions are recorded during hitstop too: motions can be input while frozen

    if (pressed & kAtkButtons) {
        f.buffer = pressed & kAtkButtons;
        f.bufferAge = 0;
    } else if (f.buffer && f.hitstop == 0 && ++f.bufferAge > R.bufferTicks) {
        f.buffer = 0;  // the buffer does not age while frozen: a press during hitstop comes out right after it
    }
    if (f.tap) --f.tap;

    if (f.hitstop > 0) {  // frozen: nothing moves, nothing animates
        --f.hitstop;
        return;
    }

    if (f.invul) --f.invul;
    if (f.recoil) {  // corner push-back taken by the attacker
        f.x += f.recoil;
        f.recoil = fixedMul(f.recoil, R.recoilDecay256);
        if (f.recoil > -F(0.25f) && f.recoil < F(0.25f)) f.recoil = 0;
    }
    if (f.comboCount == 0 && f.undizzy > 0) f.undizzy = std::max(0, f.undizzy - R.undizzyDrain);

    const fixed grav = F(ms.gravity);

    // ---- states that run by themselves -------------------------------------------------------------------
    switch (f.state) {
        case CharState::kIntro: {
            if (f.stateTimer > 0) --f.stateTimer;
            stepAnim(f, ms);
            f.state = CharState::kIntro;
            if (f.stateTimer == 0) {
                if (f.mv != ms.id.stand) enter(f, ms, ms.id.stand);   // an entrance animation ends on its feet (no restart of the plain loop)
                f.state = CharState::kIdle;
            }
            return;
        }
        case CharState::kHitstunStand:
        case CharState::kHitstunCrouch: {
            f.x += f.vx;
            decay(f);
            stepAnim(f, ms);
            if (f.hitstun > 0 && --f.hitstun == 0) recoverTo(f, ms, f.state == CharState::kHitstunCrouch);
            return;
        }
        case CharState::kBlockStand:
        case CharState::kBlockCrouch: {
            f.x += f.vx;
            decay(f);
            stepAnim(f, ms);
            if (f.blockstun > 0 && --f.blockstun == 0) recoverTo(f, ms, f.state == CharState::kBlockCrouch);
            return;
        }
        case CharState::kBlockAir:
        case CharState::kHitstunAir:
        case CharState::kLauncher: {
            f.x += f.vx;
            f.vy -= grav;
            f.y += f.vy;
            stepAnim(f, ms);
            if (f.blockstun > 0 && f.state == CharState::kBlockAir && --f.blockstun == 0) {
                f.state = CharState::kJumpFall;
                enter(f, ms, ms.id.jumpFall[0]);
            }
            if (f.y <= 0 && f.vy < 0) {
                if (f.state == CharState::kBlockAir) land(f, ms);
                else startKnockdown(f, ms);
            }
            return;
        }
        case CharState::kDead:
            if (f.airborne) {
                f.x += f.vx;
                f.vy -= grav;
                f.y += f.vy;
                stepAnim(f, ms);
                f.state = CharState::kDead;
                if (f.y <= 0 && f.vy < 0) startKnockdown(f, ms);
                return;
            }
            [[fallthrough]];
        case CharState::kKnockdown: {
            f.x += f.vx;
            decay(f);
            const bool dead = f.state == CharState::kDead;
            const CharState keep = f.state;
            if (f.seq == kFloorImpact) {
                if (lastTick(f, ms)) {
                    f.seq = kFloorLie;
                    enter(f, ms, dead && ms.id.deadPoint >= 0 ? ms.id.deadPoint : (ms.id.floorLie >= 0 ? ms.id.floorLie : ms.id.stand));
                } else {
                    stepAnim(f, ms);
                }
            } else if (f.seq == kFloorLie) {
                if (dead) {
                    stepAnim(f, ms);
                } else if (lastTick(f, ms)) {
                    f.seq = kFloorWait;
                    f.stateTimer = 16;
                    enter(f, ms, ms.id.floorLieLoop >= 0 ? ms.id.floorLieLoop : ms.id.floorLie);
                } else {
                    stepAnim(f, ms);
                }
            } else {
                stepAnim(f, ms);
                if (f.stateTimer > 0 && --f.stateTimer == 0) {
                    f.state = CharState::kWakeup;
                    enter(f, ms, ms.id.wakeup >= 0 ? ms.id.wakeup : ms.id.stand);
                    f.state = CharState::kWakeup;
                    f.vx = 0;
                    return;
                }
            }
            f.state = keep;
            return;
        }
        case CharState::kWakeup: {
            stepAnim(f, ms);
            if (f.state == CharState::kIdle) f.comboCount = 0;  // the move set walked on to "Stand"
            if (f.state != CharState::kIdle) f.state = CharState::kWakeup;
            return;
        }
        case CharState::kWin: {
            stepAnim(f, ms);
            f.state = CharState::kWin;
            return;
        }
        default: break;
    }

    // ---- round is over: survivors celebrate ---------------------------------------------------------------
    if (s.roundOver && f.health > 0 && !f.airborne && freeGround(f.state) && ms.id.winPose >= 0) {
        f.vx = 0;
        enter(f, ms, ms.id.winPose);
        f.state = CharState::kWin;
        return;
    }

    // ---- facing: turn to the opponent while nothing is committed -------------------------------------------
    if (!f.airborne && freeGround(f.state)) f.facing = (o.x >= f.x) ? Facing::kRight : Facing::kLeft;
    const int face = faceSign(f);

    const int dirIn = ((in & kRight) ? 1 : 0) - ((in & kLeft) ? 1 : 0);
    const int rel = dirIn * face;  // +1 forward, -1 back
    const bool down = (in & kDown) != 0;
    const bool attacking = isAttackState(f.state);

    // ---- attacks (new ones, and cancels once the move connected) -----------------------------------------
    bool canAttack = false;
    if (!attacking) {
        canAttack = (!f.airborne && freeGround(f.state)) ||
                    (f.airborne && (f.state == CharState::kJumpRise || f.state == CharState::kJumpFall));
    } else if (f.flags & kFConnected) {
        canAttack = true;
    }
    bool started = false;
    if (f.buffer && canAttack) {
        const int btn = pickButton(f.buffer);
        if (btn >= 0) {
            const int posture = f.airborne ? kPAir : ((f.state == CharState::kCrouch || down) ? kPCrouch : kPStand);
            int mvId = ms.id.normal[posture][btn];
            // forward / back held: the character's own command normal for it, when the table has one (s.F+MP, j.MK_F ...)
            // (down in the air is a command too: j.D+HK, j.D+MP ...; on the ground down is the crouching posture)
            int dirMv = rel > 0 ? ms.id.normalF[posture][btn] : (rel < 0 ? ms.id.normalB[posture][btn] : -1);
            if (posture == kPAir && down && ms.id.normalD[posture][btn] >= 0) dirMv = ms.id.normalD[posture][btn];
            if (dirMv >= 0) mvId = dirMv;
            if (mvId < 0) mvId = ms.id.normal[kPStand][btn];
            bool ok = mvId >= 0;
            const uint32_t bit = 1u << (posture * kNormalCount + btn);
            if (ok && attacking) {  // chain: only out of a normal, each normal once, see chainAllowed()
                ok = !isSpecialAttack(f.state) && f.state != CharState::kThrow && (f.chainUsed & bit) == 0 &&
                     chainAllowed(f.seq, btn, R);
            }
            if (ok) {
                f.chainUsed = attacking ? (f.chainUsed | bit) : bit;
                f.invul = 0;  // acting ends the escape invulnerability
                enter(f, ms, mvId);
                f.state = (btn % 3 == 0) ? CharState::kAttackLight : (btn % 3 == 1 ? CharState::kAttackMedium : CharState::kAttackHeavy);
                f.seq = static_cast<uint8_t>(btn);
                f.buffer = 0;
                if (!f.airborne) f.vx = 0;
                started = true;
            }
        }
    }

    if (isAttackState(f.state)) {
        if (!started) stepAnim(f, ms);
        f.stateTimer = static_cast<uint16_t>(std::max(0, ms.ticks(f.mv) - f.mvTick));
        if (f.airborne) {
            f.x += f.vx;
            f.vy -= grav;
            f.y += f.vy;
            if (f.y <= 0 && f.vy < 0) land(f, ms);
        } else {
            f.x += f.vx;
            f.vx = 0;
            if (!isAttackState(f.state)) f.stateTimer = 0;
        }
        return;
    }
    f.stateTimer = 0;

    // ---- air ----------------------------------------------------------------------------------------------
    if (f.airborne) {
        f.x += f.vx;
        f.vy -= grav;
        f.y += f.vy;
        stepAnim(f, ms);
        const int jv = f.pad2 < 3 ? f.pad2 : 0;
        if (f.state == CharState::kJumpRise && f.vy <= 0 && ms.id.jumpApogee[jv] >= 0 && f.mv != ms.id.jumpApogee[jv]) {
            enter(f, ms, ms.id.jumpApogee[jv]);
            f.state = CharState::kJumpRise;
        }
        if (f.y <= 0 && f.vy < 0) land(f, ms);
        return;
    }

    // ---- ground: jump start / dash run their own timelines -----------------------------------------------
    if (f.state == CharState::kJumpStart) {
        const bool launch = lastTick(f, ms);
        if (!launch) {
            stepAnim(f, ms);
        } else {
            f.airborne = 1;
            f.vy = F(ms.jumpVy);
            const int dir = (f.pad2 == 1) ? face : (f.pad2 == 2 ? -face : 0);
            f.vx = F(ms.jumpVx) * dir;
            enter(f, ms, ms.id.jumpRise[f.pad2 < 3 ? f.pad2 : 0]);
            f.state = CharState::kJumpRise;
            f.x += f.vx;
            f.vy -= grav;
            f.y += f.vy;
        }
        return;
    }
    if (f.state == CharState::kDash) {
        const bool back = f.seq == 1;
        const int sign = back ? -face : face;
        if (back) {
            // the back-dash is one long hop: it moves while the legs are in motion
            const int t = f.mvTick;
            f.vx = (t >= 4 && t < 30) ? F(ms.dashB) * sign : ((t >= 30 && t < 40) ? F(ms.dashB * 0.7f) * sign : 0);
        } else if (f.mv == ms.id.dashF || f.mv == ms.id.dashFLoop) {
            f.vx = F(ms.dashF) * sign;
        } else {
            f.vx = fixedMul(f.vx, 192);  // DashFwd_End: slide to a stop
        }
        f.x += f.vx;
        stepAnim(f, ms);
        if (!back) {
            if (f.pad2 < 255) ++f.pad2;  // pad2 counts the ticks since the dash started (no jump is in progress)
            if (f.mv == ms.id.dashFLoop && f.pad2 >= 18 && ms.id.dashFEnd >= 0) {
                enter(f, ms, ms.id.dashFEnd);
                f.state = CharState::kDash;
            }
        }
        if (f.state != CharState::kDash) {
            f.vx = 0;
            f.pad2 = 0;
        }
        return;
    }

    // ---- ground: free ------------------------------------------------------------------------------------
    const uint8_t dirPress = (pressed & kLeft) ? 1 : ((pressed & kRight) ? 2 : 0);
    if (dirPress) {
        if (f.tap > 0 && f.tapDir == dirPress && freeGround(f.state) && !down) {
            const bool fwd = ((dirPress == 2) == (face > 0));
            const int d = fwd ? ms.id.dashF : ms.id.dashB;
            if (d >= 0) {
                enter(f, ms, d);
                f.state = CharState::kDash;
                f.seq = fwd ? 0 : 1;
                f.pad2 = 0;
                f.tap = 0;
                return;
            }
        }
        f.tap = 14;
        f.tapDir = dirPress;
    }

    if ((in & kUp) && ms.id.jumpStart[0] >= 0) {
        const int variant = rel > 0 ? 1 : (rel < 0 ? 2 : 0);
        const int st = ms.id.jumpStart[variant] >= 0 ? ms.id.jumpStart[variant] : ms.id.jumpStart[0];
        enter(f, ms, st);
        f.state = CharState::kJumpStart;
        f.pad2 = static_cast<uint8_t>(variant);
        f.vx = 0;
        return;
    }

    if (down) {
        f.vx = 0;
        if (f.state != CharState::kCrouch) {
            const int cs = ms.id.crouching >= 0 ? ms.id.crouching : ms.id.crouch;
            if (cs >= 0) enter(f, ms, cs);
            f.state = CharState::kCrouch;
        }
        stepAnim(f, ms);
        f.state = CharState::kCrouch;
        return;
    }
    if (f.state == CharState::kCrouch) {  // released down: stand up
        enter(f, ms, ms.id.uncrouching >= 0 ? ms.id.uncrouching : ms.id.stand);
        f.state = CharState::kIdle;
        return;
    }

    if (dirIn != 0) {
        const bool fwd = rel > 0;
        const int target = fwd ? ms.id.walkF : ms.id.walkB;
        const CharState want = fwd ? CharState::kWalkForward : CharState::kWalkBack;
        if (f.state != want && target >= 0) {
            enter(f, ms, target);
            f.state = want;
        }
        f.vx = F(fwd ? ms.walkF : ms.walkB) * dirIn;
        f.x += f.vx;
        stepAnim(f, ms);
        if (f.state != want && f.state != CharState::kIdle) f.state = want;
        return;
    }

    // no input
    f.vx = 0;
    if (f.state == CharState::kWalkForward || f.state == CharState::kWalkBack) {
        const int e = f.state == CharState::kWalkForward ? ms.id.walkFEnd : ms.id.walkBEnd;
        enter(f, ms, e >= 0 ? e : ms.id.stand);
        f.state = CharState::kIdle;
    } else if (f.state == CharState::kJumpLand) {
        stepAnim(f, ms);
        // the landing animation is cancellable: after its first tick the fighter counts as standing
        if (f.state == CharState::kJumpLand && f.mvTick >= 1) f.state = CharState::kIdle;
    } else {
        stepAnim(f, ms);
    }
    if (f.state == CharState::kIdle) f.comboCount = 0;
}

// ---- pushboxes ------------------------------------------------------------------------------------------------
bool pushBox(const Fighter& f, const MoveSet& ms, WorldBox& out) {
    const foit::Frame* fr = currentFrame(f, ms);
    foit::Box b{ms.pushDefault[0], ms.pushDefault[1], ms.pushDefault[2], ms.pushDefault[3]};
    if (fr && !fr->push.empty()) b = fr->push[0];
    if (b.left == 0 && b.right == 0) {
        b = foit::Box{-160, 0, -45, 45};
    }
    out = worldBox(f, b);
    return true;
}

void separate(MatchState& s, const DuelContent& c) {
    Fighter& a = s.p[0];
    Fighter& b = s.p[1];
    if (a.state == CharState::kKnockdown || b.state == CharState::kKnockdown || a.state == CharState::kDead ||
        b.state == CharState::kDead)
        return;
    WorldBox wa, wb;
    pushBox(a, *c.ms[0], wa);
    pushBox(b, *c.ms[1], wb);
    if (!overlaps(wa, wb)) return;
    const bool aLeft = a.x < b.x || (a.x == b.x && a.facing == Facing::kRight);
    const int ov = aLeft ? (wa.x1 - wb.x0) : (wb.x1 - wa.x0);
    if (ov <= 0) return;
    const int h1 = ov / 2, h2 = ov - ov / 2;
    const fixed da = static_cast<fixed>(h1) << kFixedShift, db = static_cast<fixed>(h2) << kFixedShift;
    const fixed lim = static_cast<fixed>(c.stageHalf) << kFixedShift;
    if (aLeft) {
        a.x -= da;
        b.x += db;
    } else {
        a.x += da;
        b.x -= db;
    }
    // at a wall the other fighter takes the whole push
    if (a.x < -lim) { b.x += (-lim - a.x); a.x = -lim; }
    if (a.x > lim) { b.x -= (a.x - lim); a.x = lim; }
    if (b.x < -lim) { a.x += (-lim - b.x); b.x = -lim; }
    if (b.x > lim) { a.x -= (b.x - lim); b.x = lim; }
}

// ---- hits -----------------------------------------------------------------------------------------------------
bool boxesTouch(const Fighter& a, const MoveSet& am, const Fighter& d, const MoveSet& dm) {
    const foit::Frame* af = currentFrame(a, am);
    const foit::Frame* df = currentFrame(d, dm);
    if (!af || !df || af->hit.empty() || df->hurt.empty()) return false;
    for (const foit::Box& hb : af->hit) {
        const WorldBox w = worldBox(a, hb);
        for (const foit::Box& ub : df->hurt)
            if (overlaps(w, worldBox(d, ub))) return true;
    }
    return false;
}

void applyHit(MatchState& s, int ai, uint16_t defIn, const HitInfo& h, const DuelContent& c) {
    Fighter& a = s.p[ai];
    Fighter& d = s.p[1 - ai];
    const MoveSet& dm = *c.ms[1 - ai];
    const int away = d.x >= a.x ? 1 : -1;

    const bool holdBack = (away > 0 && (defIn & kRight)) || (away < 0 && (defIn & kLeft));
    const bool actionable = freeGround(d.state) || d.state == CharState::kBlockStand ||
                            d.state == CharState::kBlockCrouch || d.state == CharState::kBlockAir ||
                            d.state == CharState::kDash ||
                            (d.airborne && (d.state == CharState::kJumpRise || d.state == CharState::kJumpFall));
    const bool crouchBlock = !d.airborne && (defIn & kDown) != 0;
    bool blocked = false;
    if (holdBack && actionable) {
        if (d.airborne) blocked = true;
        else if (crouchBlock) blocked = h.level != 1;
        else blocked = h.level != 2;
    }

    a.hitstop = static_cast<uint16_t>(std::max(1, h.hitstop));
    d.hitstop = a.hitstop;
    a.flags |= kFConnected;

    if (blocked) {
        a.flags |= kFBlocked;
        const int posture = d.airborne ? kPAir : (crouchBlock ? kPCrouch : kPStand);
        d.blockstun = static_cast<uint16_t>(derivedBlockstun(h, c.rules));
        d.state = d.airborne ? CharState::kBlockAir : (crouchBlock ? CharState::kBlockCrouch : CharState::kBlockStand);
        d.vx = F(h.blockVx > 0.5f ? h.blockVx : 6.0f) * away;
        const int bs = dm.id.blockStun[posture][levelIndex(h.level)];
        enter(d, dm, bs >= 0 ? bs : dm.id.stand);
        d.state = d.airborne ? CharState::kBlockAir : (crouchBlock ? CharState::kBlockCrouch : CharState::kBlockStand);
        d.hitstop = a.hitstop;
        d.meter = std::min(5000, d.meter + h.damage / 10);
        a.meter = std::min(5000, a.meter + h.damage / 12);
        if (isSpecialAttack(a.state) && c.rules.chipPct > 0) {  // chip damage: specials and supers only
            const int chip = std::max(1, h.damage * c.rules.chipPct / 100 / 10);
            d.health -= chip;
            if (d.health <= 0) {
                if (c.rules.chipKills) {
                    d.health = 0;
                    s.roundOver = 1;
                } else {
                    d.health = 1;
                }
            }
        }
        if (!d.airborne) cornerRecoil(a, d, away, c);
        return;
    }

    // ---- a clean hit
    const DuelRules& R = c.rules;
    d.comboCount = static_cast<uint16_t>(d.comboCount + 1);
    const int n = static_cast<int>(d.comboCount);
    const int pct = n <= R.scaleFreeHits ? 100 : std::max(R.scaleMin, 100 - R.scaleStep * (n - R.scaleFreeHits));
    const int dmg = std::max(1, h.damage * pct / 100 / 10);
    d.health -= dmg;
    d.undizzy += R.undizzyPerHit[std::clamp(h.strength, 1, 3) - 1] + (isSpecialAttack(a.state) ? R.undizzySpecial : 0);
    a.meter = std::min(5000, a.meter + h.damage * 3 / 10);
    d.meter = std::min(5000, d.meter + h.damage / 10);

    const bool ko = d.health <= 0;
    if (ko) {
        d.health = 0;
        s.roundOver = 1;
    }
    const bool escape = !ko && d.undizzy >= R.undizzyMax;  // the undizzy bar is full: this hit ends the combo
    const bool toAir = ko || h.launch || d.airborne;
    d.hitstun = 0;
    d.blockstun = 0;
    if (toAir) {
        float vy = h.hitVy > 0 ? h.hitVy : 9.0f;
        float vx = h.hitVx > 2.0f ? h.hitVx : 2.5f;
        if (ko) {
            vy = std::max(vy, 16.0f);
            vx = std::max(vx, 8.0f);
        }
        d.airborne = 1;
        d.vy = F(std::min(vy, 40.0f));
        d.vx = F(vx) * away;
        d.state = ko ? CharState::kDead : CharState::kHitstunAir;
        int pose = dm.id.hit[kPAir][1];
        if (h.launch || ko) {
            // steep launches use the knock-up poses, the rest flies back
            if (vy >= 24.0f && dm.id.knockup[0] >= 0) pose = dm.id.knockup[0];
            else if (vy >= 14.0f && dm.id.knockup[1] >= 0) pose = dm.id.knockup[1];
            else if (dm.id.knockback >= 0) pose = dm.id.knockback;
        }
        enter(d, dm, pose >= 0 ? pose : dm.id.stand);
        d.state = ko ? CharState::kDead : CharState::kHitstunAir;
        d.hitstop = a.hitstop;
        if (escape) {  // flips out of the combo: falls under control, invulnerable for a moment
            d.invul = static_cast<uint8_t>(std::clamp(R.escapeInvul, 0, 255));
            d.undizzy = 0;
            d.vx = d.vx / 2;
            enter(d, dm, dm.id.jumpFall[0] >= 0 ? dm.id.jumpFall[0] : dm.id.stand);
            d.state = CharState::kJumpFall;
            d.pad2 = 0;
        }
        return;
    }
    const bool crouched = d.state == CharState::kCrouch || d.state == CharState::kHitstunCrouch ||
                          d.state == CharState::kBlockCrouch || (defIn & kDown) != 0;
    const int posture = crouched ? kPCrouch : kPStand;
    d.hitstun = static_cast<uint16_t>(std::max(6, h.hitstun));
    d.state = crouched ? CharState::kHitstunCrouch : CharState::kHitstunStand;
    d.vx = F(h.hitVx > 0.5f ? h.hitVx : 4.0f) * away;
    const int pose = dm.id.hit[posture][levelIndex(h.level)];
    enter(d, dm, pose >= 0 ? pose : dm.id.stand);
    d.state = crouched ? CharState::kHitstunCrouch : CharState::kHitstunStand;
    d.hitstop = a.hitstop;
    cornerRecoil(a, d, away, c);
    if (escape) {  // recovers on the next tick, invulnerable for a moment
        d.invul = static_cast<uint8_t>(std::clamp(R.escapeInvul, 0, 255));
        d.undizzy = 0;
        d.hitstun = 1;
    }
}

void resolveHits(MatchState& s, const uint16_t in[2], const DuelContent& c) {
    int hid[2] = {-1, -1};
    for (int i = 0; i < 2; ++i) {
        const Fighter& a = s.p[i];
        const Fighter& d = s.p[1 - i];
        const MoveSet& am = *c.ms[i];
        const int h = am.hitAt(a.mv, a.mvTick);
        if (h < 0 || h == a.hitId || !canBeHit(d)) continue;
        if (boxesTouch(a, am, d, *c.ms[1 - i])) hid[i] = h;
    }
    for (int i = 0; i < 2; ++i) {
        if (hid[i] < 0) continue;
        s.p[i].hitId = static_cast<int16_t>(hid[i]);
        applyHit(s, i, in[1 - i], c.ms[i]->hit(hid[i]), c);
    }
}

}  // namespace

// ---- input history / motions -----------------------------------------------------------------------------------
int numpadDir(uint16_t in) {
    const int h = ((in & kRight) ? 1 : 0) - ((in & kLeft) ? 1 : 0);
    const int v = ((in & kUp) ? 1 : 0) - ((in & kDown) ? 1 : 0);
    return 5 + h + 3 * v;
}

int relativeDir(int d, Facing facing) {
    if (facing == Facing::kRight || d < 1 || d > 9) return d;
    const int col = (d - 1) % 3;  // 0 left, 1 centre, 2 right
    return d - col + (2 - col);
}

void pushHistory(Fighter& f, uint16_t in) {
    const uint8_t d = static_cast<uint8_t>(numpadDir(in));
    if (f.histLen[0] > 0 && f.histDir[0] == d) {
        if (f.histLen[0] < 255) ++f.histLen[0];
        return;
    }
    for (int k = 7; k > 0; --k) {
        f.histDir[k] = f.histDir[k - 1];
        f.histLen[k] = f.histLen[k - 1];
    }
    f.histDir[0] = d;
    f.histLen[0] = 1;
}

bool motionDone(const Fighter& f, const char* motion, int maxGap, int lastWithin) {
    if (!motion || !*motion) return false;
    int want[16];
    int n = 0;
    for (const char* p = motion; *p && n < 16; ++p)
        if (*p >= '1' && *p <= '9') want[n++] = relativeDir(*p - '0', f.facing);  // to screen terms
    if (n == 0) return false;
    // endAge[e]: ticks since history entry e ended (0 for the newest one, which is still held)
    int endAge[8];
    int age = 0;
    for (int e = 0; e < 8; ++e) {
        endAge[e] = age;
        age += f.histLen[e];
    }
    int k = n - 1;    // digit being matched, newest first
    int prevStart = -1;  // age at which the previously matched (newer) entry started
    for (int e = 0; e < 8 && k >= 0; ++e) {
        if (f.histLen[e] == 0) break;
        if (k == n - 1) {
            // the last direction must have been REACHED recently (holding 6 for a second after 23 is not a 236)
            if (endAge[e] + f.histLen[e] - 1 > lastWithin) return false;
        } else if (endAge[e] - prevStart > maxGap) {
            return false;
        }
        if (f.histDir[e] == want[k]) {
            prevStart = endAge[e] + f.histLen[e];
            --k;
        }
    }
    return k < 0;
}

// ---------------------------------------------------------------------------------------------------------------
WorldBox worldBox(const Fighter& f, const foit::Box& b) {
    const int px = whole(f.x), py = whole(f.y);
    WorldBox w;
    if (f.facing == Facing::kRight) {
        w.x0 = px + b.left;
        w.x1 = px + b.right;
    } else {
        w.x0 = px - b.right;
        w.x1 = px - b.left;
    }
    w.y0 = py - b.bottom;
    w.y1 = py - b.top;
    return w;
}

const foit::Frame* currentFrame(const Fighter& f, const MoveSet& ms) {
    return ms.frame(ms.frameAt(f.mv, f.mvTick));
}

void duelStartRound(Fighter& f, const MoveSet& ms, fixed x, Facing facing, int introTicks, int introState) {
    f = Fighter{};
    f.x = x;
    f.facing = facing;
    f.health = 1000;
    f.state = CharState::kIntro;
    const bool anim = introState >= 0 && ms.valid(introState) && ms.ticks(introState) > 0;
    f.stateTimer = static_cast<uint16_t>(anim ? std::min(ms.ticks(introState), 0xFFFF) : introTicks);
    enter(f, ms, anim ? introState : ms.id.stand);
    f.state = CharState::kIntro;
}

void duelSkipIntro(Fighter& f, const MoveSet& ms) {
    if (f.state != CharState::kIntro) return;
    f.stateTimer = 0;
    if (f.mv != ms.id.stand) enter(f, ms, ms.id.stand);
    f.state = CharState::kIdle;
}

void advanceDuel(MatchState& s, uint16_t p1, uint16_t p2, const DuelContent& c) {
    if (!c.valid()) {
        advanceFrame(s, p1, p2);
        return;
    }
    const uint16_t in[2] = {expandMacros(p1), expandMacros(p2)};
    ++s.frame;
    if (s.roundTimer > 0 && !s.roundOver) --s.roundTimer;

    for (int i = 0; i < 2; ++i) tick(s, i, in[i], c);

    const fixed lim = static_cast<fixed>(c.stageHalf) << kFixedShift;
    for (Fighter& f : s.p) f.x = std::clamp(f.x, -lim, lim);
    separate(s, c);
    resolveHits(s, in, c);
    nextRng(s.rng);
}

namespace {
void cbAdvance(MatchState& s, uint16_t a, uint16_t b, void* user) {
    const DuelContent* c = static_cast<const DuelContent*>(user);
    if (c) advanceDuel(s, a, b, *c);
    else advanceFrame(s, a, b);
}
}  // namespace

Callbacks duelCallbacks(const DuelContent* content) {
    Callbacks cb = defaultCallbacks();
    cb.advance = cbAdvance;
    cb.user = const_cast<DuelContent*>(content);
    return cb;
}

}  // namespace fight
}  // namespace zengine
