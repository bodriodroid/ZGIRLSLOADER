// duel.h - the move-driven fight runtime.
//
// fight.h keeps the match state and the GGPO-shaped session; this is the tick function that fills it with
// the real thing: every fighter plays the states of its own <char>.foit (moveset.h) at 60 Hz, attacks connect
// when the hit boxes of the frame on screen overlap the hurt boxes of the opponent's frame, damage / hitstop /
// hitstun / level come from the hit data of the move, and the walk / dash / jump numbers come from the
// character's scripts.
//
// Deterministic like the generic runtime: integer / fixed-point only inside the tick (the floats of the move
// set are converted to fixed point once per tick from constants), no allocation, no I/O.  The move sets are
// immutable and are NOT part of the snapshot - a snapshot only has to restore the Fighters.
//
// Not decoded yet and therefore approximated (see moveset.h): which reaction state a hit leads to, pushback
// decay, the knock-down / wake-up sequence, throws, special moves (needs the motion-input commands).
#pragma once
#include "fight.h"
#include "moveset.h"

namespace zengine {
namespace fight {

// Every number of the combat feel in one place.  These are design values written for ZDroid (not read from the
// game): tune them against the PC version by playing / public frame data, not by copying its code.  They are not part
// of the snapshot - both peers of an online match must use the same rules.
struct DuelRules {
    // ---- input
    int bufferTicks = 8;        // a button press waits this long for the fighter to be able to act (not aged in hitstop)
    int motionGap = 10;         // motions: max ticks spent between two directions of a motion ("236")
    int motionLast = 8;         // motions: the last direction must be at most this many ticks old
    // ---- chains (normals cancelling into normals once they touched the opponent, on hit or on block)
    bool chainLightsAnyOrder = true;  // a light may chain into a DIFFERENT light (punch/kick, either order)
    // every normal can be used once per chain; otherwise: stronger, or punch -> kick of the same strength
    // ---- block
    int blockstunLess[3] = {3, 4, 5};  // derived blockstun = hitstun - this (by strength L/M/H) when not decoded
    int minBlockstun = 6;
    int chipPct = 25;           // % of the damage a BLOCKED special / super still does (blocked normals do none)
    bool chipKills = true;      // false: chip damage never takes the last point of health
    // ---- combos
    int scaleFreeHits = 1;      // hits at full damage at the start of a combo
    int scaleStep = 10;         // then -scaleStep % per hit ...
    int scaleMin = 20;          // ... never below scaleMin %
    // undizzy (anti-infinite): every hit of a combo fills the defender's bar; when it is full the defender drops out
    // of the combo with a short strike invulnerability.  The bar drains while the defender is free.
    int undizzyPerHit[3] = {8, 12, 16};  // by strength L/M/H
    int undizzySpecial = 14;    // extra per hit of a special / super
    int undizzyMax = 240;
    int undizzyDrain = 2;       // per tick while not in a combo
    int escapeInvul = 30;       // ticks of invulnerability after the escape
    // ---- corner
    int wallMargin = 40;        // px: a defender this close to the wall in the push direction counts as cornered
    int recoilDecay256 = 218;   // attacker recoil x (recoilDecay256 / 256) per tick (~0.85)
};

struct DuelContent {
    const MoveSet* ms[2] = {nullptr, nullptr};
    int stageHalf = 1800;  // fighters are clamped to [-stageHalf, +stageHalf] (stage units = sprite pixels)
    DuelRules rules;
    bool valid() const { return ms[0] && ms[1] && ms[0]->valid() && ms[1]->valid(); }
};

// ---- input history / motions ---------------------------------------------------------------------------------
// Numpad notation: 5 neutral, 6 right, 4 left, 8 up, 2 down, 3 down-right ...  (screen terms: not facing-relative)
int numpadDir(uint16_t in);
// Mirrors a numpad direction for a fighter facing left (4 <-> 6, 1 <-> 3, 7 <-> 9), so "6" always means forward.
int relativeDir(int screenDir, Facing facing);
// Records one tick of input in the fighter's run-length history (duel.cpp does it every tick, hitstop included).
void pushHistory(Fighter& f, uint16_t in);
// True when `motion` - facing-relative numpad digits, e.g. "236", "623", "41236", "214" - is in the history: the
// last digit at most `lastWithin` ticks ago, at most `maxGap` ticks between consecutive digits.
bool motionDone(const Fighter& f, const char* motion, int maxGap, int lastWithin);

// One 1/60 s tick.  Falls back to the generic advanceFrame() when the content is incomplete.
void advanceDuel(MatchState& s, uint16_t p1, uint16_t p2, const DuelContent& c);

// Puts a fighter at the start of a round: full health, standing, intro delay.
// `introState` >= 0 = an entrance animation of the move set (MoveSet::introStates()): the fighter plays it from its first tick
// and the intro lasts exactly as long as that timeline (then it stands); -1 = the plain `introTicks` of standing still.
void duelStartRound(Fighter& f, const MoveSet& ms, fixed x, Facing facing, int introTicks, int introState = -1);
// Cuts the entrance short: the fighter stands and may act (a button press during the intro).
void duelSkipIntro(Fighter& f, const MoveSet& ms);

// GGPO callbacks wired to advanceDuel; `content` must outlive the session.
Callbacks duelCallbacks(const DuelContent* content);

// ---- geometry helpers (also used by the renderer's debug overlay)
struct WorldBox {
    int x0, x1;  // left .. right
    int y0, y1;  // bottom .. top, y grows upwards (0 = floor)
};
// Converts a frame box (relative to the pivot, y down, mirrored when the fighter faces left) to stage units.
WorldBox worldBox(const Fighter& f, const foit::Box& b);
inline bool overlaps(const WorldBox& a, const WorldBox& b) {
    return a.x0 < b.x1 && a.x1 > b.x0 && a.y0 < b.y1 && a.y1 > b.y0;
}

// Frame a fighter currently shows (nullptr when its move has none).
const foit::Frame* currentFrame(const Fighter& f, const MoveSet& ms);

}  // namespace fight
}  // namespace zengine
