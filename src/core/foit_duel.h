// foit_duel.h - the data-driven fight (foit_rt.h) plugged into the GGPO-shaped match layer (fight.h) and the renderer.
//
// foit_rt runs a character exactly as its own <char>.foit scripts say; fight.h / duel.h are the match structure the app was
// built around (MatchState snapshots, LocalSession / SyncTestSession / a future GGPO backend, HUD, AI, touch pad).  FoitDuel is the
// adapter between them:
//
//   input      the 14 button bits of fight.h -> one rt::Sample per fighter (direction + LP MP HP LK MK HK), macros expanded
//   sim        one 1/60 s tick = rt::tickAll: states, nodes, hit boxes, push, round flow (rt::Match) - all of it inside the world
//   snapshots  save / load = the whole rt::World (plus the previous input sample) - rollback restores the real fight, not a copy
//   checksum   the 32-bit fold of the world hash rides in MatchState::rng, so MatchState::checksum() (what GGPO compares between
//              peers, what SyncTestSession replays) covers the real state, not only the few numbers the HUD reads
//   projection health / meter / combo / round clock / round-over flag are copied into MatchState for the HUD and the AI
//   render     views(): what every object (the two bodies and the children they spawned) shows this tick
//
// The round flow lives in the world too (intro poses -> round card -> fight -> KO -> poses -> next round -> match over), so a peer
// that rolls back also rolls the round flow back; the app only reads `phase()` to decide which banner to show.
//
// Not part of the snapshot: the characters (immutable, shared), the configuration, `events()`.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fight.h"
#include "foit.h"
#include "foit_rt.h"

namespace zengine {
namespace fight {

namespace rt = ::zengine::foit::rt;

struct FoitDuelConfig {
    std::shared_ptr<const foit::Character> ch[2];
    float startX[2] = {-220.0f, 220.0f};   // stage units; fighter 0 starts facing right, fighter 1 facing left
    float stageHalf = 1700.0f;             // the walls: the fighters stay within [-stageHalf, +stageHalf]
    int roundSeconds = 99;                 // <= 0: no clock
    int roundsToWin = 2;
    bool training = false;                 // no clock, nobody dies
    bool introPoses = true;                // the first round opens with the fighters' entrance poses
    int readyTicks = 0;                    // hold between the poses and the fight (the "Take N" card)
    int endHold = 0;                       // minimum length of a round end (the K.O. banner)
    bool tieBreak = true;                  // both sides reaching the target together plays a deciding extra round (assumed rule)
    bool skipIntroOnButton = true;         // an attack button pressed during the entrance cuts it short
    int charId[2] = {-1, -1};              // roster id (engine op 50), -1 = unknown
    int palette[2] = {0, 0};
    std::string dizzyText;                 // contents of the player's own characters/_dizzy_values.ini (read at runtime, never shipped)
};

// What one object shows on this tick.
struct FoitView {
    const foit::Character* ch = nullptr;
    int side = 0;          // 0 / 1: the fighter that owns it
    bool body = false;     // the fighter's own body (false: a spawned child - projectile, effect, assist ...)
    int frame = -1;        // index into ch->frames; -1 = nothing to draw
    float x = 0, y = 0;    // stage units, y up (0 = floor)
    bool faceRight = true;
    float xscale = 1, yscale = 1;   // multipliers of the art (0 = invisible)
    float alpha = 1;                // 0..1
    bool superLit = false;          // raised SUPERFLASH / SUPERSHADOWS itself: stays bright while the rest of the picture goes dark
};

// What the picture has to do for a super move (presentation only; read by the Play screen, never by the simulation).
struct FoitSuper {
    int flash = 0;         // SUPERFLASH of the owner: 0 = off, else the 1-based flash look
    bool shadows = false;  // SUPERSHADOWS of the owner: the stage is dark around it
    int owner = -1;        // side (0 / 1) whose object raised either flag, -1 when none did
};

class FoitDuel {
public:
    bool init(const FoitDuelConfig& cfg);
    bool valid() const { return ready_; }
    const FoitDuelConfig& config() const { return cfg_; }

    // ---- one tick (GGPO "advance_frame"): both fighters' raw buttons (fight.h bits).
    void step(uint16_t in0, uint16_t in1);

    // ---- snapshots (GGPO save / load): the whole world plus the input edge state.
    void save(std::vector<uint8_t>& out) const;
    bool load(const uint8_t* data, size_t size);
    uint64_t hash() const;

    // ---- views for the HUD / AI: fills the Fighter numbers the app reads and the round clock.
    void project(MatchState& s) const;

    // ---- round flow
    const rt::Match& match() const { return w_.match; }
    rt::Match::Phase phase() const { return static_cast<rt::Match::Phase>(w_.match.phase); }
    int round() const { return w_.match.round; }
    int wins(int side) const { return w_.match.wins[side & 1]; }
    int roundWinner() const { return w_.match.winner; }       // winner of the round that ended (-1 draw / still going)
    int roundReason() const { return w_.match.reason; }       // 1 KO, 2 time over, 3 double KO
    int matchWinner() const { return w_.match.matchWinner; }  // when phase() == Over (-1 draw)
    bool fighting() const { return phase() == rt::Match::Fight; }
    // The match is over once the last round has ended (phase() == Over).
    bool over() const { return phase() == rt::Match::Over; }

    // ---- rendering
    const rt::World& world() const { return w_; }
    const rt::Object& body(int side) const { return *obj_[side & 1]; }
    // Writable views for the lesson scripts (lua_host.h): a script reads and writes the object's slots like the shipped engine's did.
    // Not for the simulation of a networked match: anything written here has to be written by every peer on the same tick.
    rt::World& worldMut() { return w_; }
    rt::Object& bodyMut(int side) { return *obj_[side & 1]; }
    // Life of a fighter as a fraction of its full life (0..1).
    float lifeFraction(int side) const;
    // True while the fighter's own super flash (SUPERFLASH) is running: the clock stops and the HUD plays its filmstrip.
    bool superFlash(int side) const;
    // The super in progress, if any: which side owns it and whether the flash / the dark stage are on.  Scans every object, so a
    // helper that raises the flag counts for its fighter.
    FoitSuper superState() const;
    // What a fighter's own body keeps in its four PALFX values (status, self, self2, hit): 32-bit hashes of effect names from the
    // palette-effects table (0 = none).  Presentation only (palfx.h).
    std::array<uint32_t, 4> palFx(int side) const;
    // Tops up a fighter's super bar.  A capture / test aid (changes the simulation: never use it in a networked match).
    void fillMeter(int side);
    // Everything on screen, the two bodies first.  Fills `out` (cleared first).
    void views(std::vector<FoitView>& out) const;
    // Frame indexes the fighter will want soon (rest of its current state, then the state after it): for the texture prefetch.
    void lookahead(int side, std::vector<uint16_t>& out, int maxFrames) const;
    // Presentation events raised by the last step (hit sparks, node effects, phase changes); replaced by every step.
    const std::vector<rt::Event>& events() const { return events_; }
    // Hits the last step connected / blocked (for the hit flash / shake the HUD does).
    int lastHits() const { return lastHits_; }

    // ---- input conversion (public for the tests): fight.h bits -> direction (rt::Dir) and the six attack buttons (bit 0..5)
    static void splitInput(uint16_t buttons, int* dir, uint32_t* mask);

private:
    FoitDuelConfig cfg_;
    rt::World w_;
    std::unique_ptr<rt::Object> obj_[2];
    rt::Sample prev_[2];     // last sample each fighter was fed (edge state of the pad)
    uint16_t rawPrev_[2] = {0, 0};   // last raw buttons (for the "press during the entrance" edge)
    float maxLife_[2] = {rt::kStartHealth, rt::kStartHealth};
    std::vector<rt::Event> events_;
    int lastHits_ = 0;
    bool ready_ = false;
};

// GGPO callbacks over a FoitDuel (`user` = the FoitDuel, which must outlive the session).
Callbacks foitDuelCallbacks(FoitDuel* d);

}  // namespace fight
}  // namespace zengine
