// moveset.h - a character's moves, ready for the fight runtime.
//
// Built from a parsed <char>.foit (foit.h) and nothing else.  For every state of the character body it holds
// a timeline - one entry per 1/60 s tick: which frame is on screen and which hit data is armed - plus the
// state the timeline continues into.  On top of that it resolves the handful of state names the runtime drives
// by hand ("Stand", "WalkFwd", "s.WP", "j.HK", "s.HitMid1", ...) and pulls the physics numbers the
// character's own scripts set (walk speed, dash speed, jump velocity, gravity).
//
// What is read straight from the data:
//   frames / durations / boxes      frame list + node durations (ticks at 60 Hz)
//   damage, hitstop, hitstun, level hit data record of the node (fc, f10, f8, f13 - see docs/RE_NOTES.md)
//   knockback / launch              the two literal chains of the hit data ([158] / [159] / [59])
//   walk / dash / jump              literal assignments in the state header and node scripts
// What is still an approximation: which state a hit puts the defender in (picked by level and posture), the
// pushback decay and the knock-down sequence - the transition tables that decide those are not decoded yet.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "foit.h"

namespace zengine {
namespace fight {

constexpr uint16_t kNoFrame = 0xFFFF;

// Posture index used by the move tables.
enum Posture { kPStand = 0, kPCrouch = 1, kPAir = 2, kPostureCount = 3 };
// Button index of the normals: LP MP HP LK MK HK  (the game calls them WP MP HP WK MK HK).
constexpr int kNormalCount = 6;

// One entry of a character's `Normals` hub state.  The engine reads that table top to bottom to decide which attack a button press
// starts, so it is the authoritative list of a character's 18 normals (plus the command / direction variants), whatever the states
// are called.  Throws need two buttons and are not listed here.
struct HubNormal {
    int state = -1;    // target state (index into the body's states)
    int posture = 0;   // Posture, from the name prefix s. / c. / j.
    int button = 0;    // 0..kNormalCount-1
    // Direction the command needs.  The engine's direction ids go round the compass: 0 forward, 1 down-forward, 2 down, 3 down-back,
    // 4 back, 5 up-back, 6 up, 7 up-forward, 9 = neutral, 10 = do not care.
    int dir = 0;       // 0 = none, 1 = forward, 2 = back, 3 = down in the air (j.D+HK ...), 4 = another direction (up, diagonals)
};
// Empty when the object has no `Normals` hub (BossMarie's scripted rig, HitSpark ...).
std::vector<HubNormal> hubNormals(const foit::Object& body);

struct HitInfo {
    int damage = 0;     // raw damage in the game's units (a light punch is 300 ... a heavy kick 950)
    int hitstop = 0;     // ticks both fighters freeze when it connects
    int hitstun = 0;     // ticks of hitstun on a grounded defender
    int blockstun = 0;   // ticks of blockstun (a copy of hitstun until the real value is decoded, see blockstunDecoded)
    bool blockstunDecoded = false;  // false: duel.cpp derives blockstun from hitstun with DuelRules
    int level = 3;       // 1 = high / overhead, 2 = low, 3 = mid
    int strength = 1;    // 1 light, 2 medium, 3 heavy
    bool launch = false; // the on-hit reaction puts the defender in the air
    float hitVx = 0;     // defender velocity on hit, px/tick, positive = away from the attacker
    float hitVy = 0;     // defender velocity on hit, positive = up
    float blockVx = 0;   // defender velocity on block, positive = away from the attacker
};

struct Timeline {
    std::vector<uint16_t> frame;  // per tick: index into Character::frames, or kNoFrame
    std::vector<int16_t> hit;     // per tick: index into MoveSet::hits(), -1 when no hit data is armed
    int next = -1;                // state entered when the timeline ends; -1 = none
    int ticks() const { return static_cast<int>(frame.size()); }
};

// Coarse behaviour class of a state; the runtime maps it to its own CharState.
enum class Cat : int8_t {
    kNone = -1,
    kIdle, kCrouch, kWalkF, kWalkB, kJumpStart, kRise, kFall, kLand, kDash, kAirDash, kAttackL, kAttackM, kAttackH
};

class MoveSet {
public:
    // Takes shared ownership: the frames outlive every Fighter that points into them.
    bool build(std::shared_ptr<const foit::Character> ch, std::string* err = nullptr);
    bool valid() const { return ch_ != nullptr && !tl_.empty(); }

    const foit::Character& character() const { return *ch_; }
    // Shared ownership of the parsed character: the data driven runtime (foit_duel.h) keeps its objects pointing into it.
    const std::shared_ptr<const foit::Character>& characterPtr() const { return ch_; }
    const std::string& name() const { return name_; }

    int stateCount() const { return static_cast<int>(tl_.size()); }
    int find(const std::string& n) const;                 // -1 when the character has no such state
    // The character's entrance animations ("Intro_<name>_ForReal": a wave, a snack, a sword twirl ...), in the file's order.  The
    // game's IntroPose_Choice script picks one at random; the runtime picks one the same way.
    std::vector<int> introStates() const;
    const std::string& stateName(int s) const;
    const Timeline& timeline(int s) const;
    int ticks(int s) const { return valid(s) ? tl_[static_cast<size_t>(s)].ticks() : 0; }
    int next(int s) const { return valid(s) ? tl_[static_cast<size_t>(s)].next : -1; }
    Cat cat(int s) const { return valid(s) ? cat_[static_cast<size_t>(s)] : Cat::kNone; }
    bool valid(int s) const { return s >= 0 && s < static_cast<int>(tl_.size()); }

    // Frame shown by state `s` at tick `t` (clamped to the timeline), kNoFrame when there is none.
    uint16_t frameAt(int s, int t) const;
    int hitAt(int s, int t) const;  // -1 when no hit data is armed at that tick
    const foit::Frame* frame(uint16_t idx) const {
        return (ch_ && idx < ch_->frames.size()) ? &ch_->frames[idx] : nullptr;
    }
    // ---- companion object: a few characters draw a second body next to the main one.  Umbrella's closed umbrella
    // is the child object "UMBRELLA_Hungern" (one locomotion state set per hunger level: Satiated, Starving,
    // Ravenous, Overstuffed); it shares the character's frame list and pivot, so it is drawn at the same feet point.
    // Attacks and hits carry the creature in the body's own frames, so only the locomotion states map to it.
    bool hasCompanion() const { return !ctl_.empty(); }
    // companion state shown while the body is in `bodyState` at hunger `level`; -1 = the body draws everything itself
    int companionFor(int bodyState, const char* level) const;
    int companionTicks(int cs) const { return (cs >= 0 && cs < static_cast<int>(ctl_.size())) ? ctl_[static_cast<size_t>(cs)].ticks() : 0; }
    uint16_t companionFrameAt(int cs, int t) const;

    const HitInfo& hit(int i) const { return hits_[static_cast<size_t>(i)]; }
    size_t hitCount() const { return hits_.size(); }

    // ---- physics, in px per tick (read from the character's scripts, with the shipped values as fallback)
    float walkF = 5.5f, walkB = 4.0f;
    float dashF = 14.0f, dashB = 11.0f;
    float jumpVy = 32.0f, jumpVx = 7.0f;
    float gravity = 1.5f;
    // default push box (top, bottom, left, right) used when a frame has none
    int pushDefault[4] = {0, 0, 0, 0};

    // ---- the states the runtime drives by hand (-1 = this character has none)
    struct Ids {
        int stand = -1, crouch = -1, crouching = -1, uncrouching = -1;
        int walkF = -1, walkFLoop = -1, walkFEnd = -1, walkB = -1, walkBLoop = -1, walkBEnd = -1;
        int dashF = -1, dashFLoop = -1, dashFEnd = -1, dashB = -1;
        int jumpStart[3] = {-1, -1, -1};   // neutral, forward, back
        int jumpRise[3] = {-1, -1, -1};
        int jumpApogee[3] = {-1, -1, -1};
        int jumpFall[3] = {-1, -1, -1};
        int land[3] = {-1, -1, -1};
        int normal[kPostureCount][kNormalCount];
        // The same button with forward / back held, when the character has a separate move for it (Annie's s.F+MP, Peacock's
        // j.MK_F / j.MK_B); -1 = the plain normal is used.  Read from the `Normals` hub, see hubNormals().
        int normalF[kPostureCount][kNormalCount];
        int normalB[kPostureCount][kNormalCount];
        int normalD[kPostureCount][kNormalCount];   // down held in the air (j.D+HK ...); only the air row is ever set
        int hit[kPostureCount][3];       // [posture][high, mid, low]
        int blockStun[kPostureCount][3];   // [posture][high, mid, low]
        int knockback = -1;                // launched, flying back
        int knockup[7];                    // by launch angle, steepest first
        int floorImpact = -1, floorLie = -1, floorLieLoop = -1, wakeup = -1;
        int deadPoint = -1, winPose = -1;
        Ids();
    } id;

private:
    std::shared_ptr<const foit::Character> ch_;
    std::string name_;
    std::vector<Timeline> tl_;
    std::vector<Cat> cat_;
    std::vector<HitInfo> hits_;
    std::unordered_map<std::string, int> byName_;
    std::vector<std::string> names_;
    std::vector<Timeline> ctl_;                       // companion object timelines
    std::unordered_map<std::string, int> cby_;
    std::vector<std::string> compBase_;               // per body state: companion state name prefix ("" = none)
};

}  // namespace fight
}  // namespace zengine
