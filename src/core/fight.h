// fight.h - deterministic match runtime: the layer GGPO expects to sit on top of.
//
// SkullGirls.exe statically links GGPO and exports the full 19-entry API
// (sym.sg3e.exe_ggpo_*).  GGPO only works if the game below it is a deterministic
// state machine that can:
//
//   1. serialise its whole state into an opaque buffer  (save_game_state)
//   2. restore it byte-for-byte                          (load_game_state)
//   3. advance exactly one tick given both players' bits (advance_frame)
//   4. produce a checksum of the state                   (log_game_state)
//
// This file provides exactly those four operations plus the input encoding that
// Skullgirls uses.  The button names come straight out of the executable's string
// table ("Light Punch", "Medium Kick", "Macro 1", ...).
//
// It is deliberately self-contained: no rendering, no I/O, no allocation of
// variable-size state, no floating point that depends on the platform.  Velocities
// and positions are fixed-point (1/256 subpixel) so the checksum is stable across
// arm64 and x86, which is what makes the Android port able to play against a PC
// build.

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace zengine {
namespace fight {

// ---------------------------------------------------------------------------
// Input encoding
// ---------------------------------------------------------------------------

// Bit layout of one player's input for a single frame.  Mirrors the button list in
// SkullGirls.exe ("Light Punch" ... "Macro 2").  Two bytes on the wire, which is
// what GGPO's GGPOGameInput provides.
enum Button : uint16_t {
    kUp = 1u << 0,
    kDown = 1u << 1,
    kLeft = 1u << 2,
    kRight = 1u << 3,
    kLP = 1u << 4,  // Light Punch
    kMP = 1u << 5,  // Medium Punch
    kHP = 1u << 6,  // Hard Punch
    kLK = 1u << 7,  // Light Kick
    kMK = 1u << 8,  // Medium Kick
    kHK = 1u << 9,  // Hard Kick
    kStart = 1u << 10,
    kSelect = 1u << 11,
    kMacro1 = 1u << 12,  // LP+MP+HP
    kMacro2 = 1u << 13,  // LK+MK+HK
};

// Expand the two "macro" buttons into the three attacks they stand for.
uint16_t expandMacros(uint16_t buttons);

// Human-readable form used by the input display ("Training Mode" overlay shows
// exactly this: 6 + arrow glyphs).
std::string describeButtons(uint16_t buttons);

// ---------------------------------------------------------------------------
// Fixed-point math
// ---------------------------------------------------------------------------

// 24.8 signed fixed point.  Skullgirls runs its stage coordinates in roughly the
// +/-4096 range with sub-pixel motion, which fits comfortably here.
using fixed = int32_t;
constexpr int kFixedShift = 8;
constexpr fixed kFixedOne = 1 << kFixedShift;

constexpr fixed toFixed(float v) { return static_cast<fixed>(v * kFixedOne); }
constexpr float fromFixed(fixed v) { return static_cast<float>(v) / kFixedOne; }
constexpr fixed fixedMul(fixed a, fixed b) {
    return static_cast<fixed>((static_cast<int64_t>(a) * b) >> kFixedShift);
}

// ---------------------------------------------------------------------------
// Match state - the thing GGPO snapshots and rolls back
// ---------------------------------------------------------------------------

enum class Facing : uint8_t { kRight = 0, kLeft = 1 };

// Skullgirls state names observed in the executable's string table.  Kept as an
// enum so the serialised state stays stable and the viewer can label them.
enum class CharState : uint16_t {
    kIdle = 0,
    kWalkForward,
    kWalkBack,
    kCrouch,
    kJumpStart,
    kJumpRise,
    kJumpFall,
    kJumpLand,
    kDash,
    kAirDash,
    kBlockStand,
    kBlockCrouch,
    kBlockAir,
    kHitstunStand,
    kHitstunCrouch,
    kHitstunAir,
    kLauncher,
    kKnockdown,
    kWakeup,
    kAttackLight,
    kAttackMedium,
    kAttackHeavy,
    kAttackSpecial,
    kAttackBlockbuster,
    kThrow,
    kThrowTech,
    kBurst,
    kTaunt,
    kIntro,
    kWin,
    kDead,
    kStateCount
};

const char* stateName(CharState s);

struct Fighter {
    fixed x = 0;          // stage X, 24.8
    fixed y = 0;          // stage Y (0 = floor), 24.8
    fixed vx = 0;
    fixed vy = 0;
    int32_t health = 1000;   // 1000 = full, matches the "%d%%" HUD strings
    int32_t meter = 0;       // blockbuster meter, 0..5000
    int32_t undizzy = 0;     // drama / "undizzy" combo limiter
    uint16_t stateTimer = 0; // ticks remaining in the current state
    uint16_t hitstun = 0;
    uint16_t blockstun = 0;
    uint16_t comboCount = 0;
    CharState state = CharState::kIdle;
    Facing facing = Facing::kRight;
    uint8_t airborne = 0;
    uint8_t pad = 0;
    // ---- move-driven runtime (duel.h).  The generic runtime above leaves these at zero.
    uint16_t mv = 0;          // state id inside the character's move set (what is playing)
    uint16_t mvTick = 0;      // ticks into that state's timeline
    uint16_t hitstop = 0;     // ticks both fighters stay frozen after a hit
    int16_t hitId = -1;       // hit data of the current move that already connected
    uint16_t buffer = 0;      // buffered attack buttons (Button bits)
    uint16_t prevIn = 0;      // inputs of the previous tick (edge detection)
    uint8_t bufferAge = 0;
    uint8_t seq = 0;          // sub-phase of a scripted sequence / button of the current attack / jump direction
    uint8_t tap = 0;          // double-tap window
    uint8_t tapDir = 0;       // 1 = left, 2 = right
    uint8_t flags = 0;        // duel.cpp: bit0 attack connected, bit1 blocked, ...
    uint8_t pad2 = 0;     // jump direction of the jump in progress (0 neutral, 1 forward, 2 back); tick counter of a forward dash
    // ---- phase 1 of the combat polish (duel.cpp).  All snapshot state, all fixed size.
    uint8_t histDir[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // run-length direction history, numpad notation in SCREEN terms
                                                    // (6 = right, 4 = left); [0] = newest.  Motions are read from here.
    uint8_t histLen[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // ticks each entry was held (saturates at 255)
    fixed recoil = 0;         // push-back the attacker takes when the defender is against the wall (decays per tick)
    uint32_t chainUsed = 0;   // bit (posture * 6 + button): normals already used in the current chain
    uint8_t invul = 0;        // ticks of strike invulnerability left (combo escape when the undizzy bar fills)
    uint8_t pad3[3] = {0, 0, 0};
};

struct MatchState {
    uint32_t frame = 0;
    Fighter p[2];
    uint32_t rng = 0x12345678u;  // shared deterministic PRNG
    uint16_t roundTimer = 0;     // ticks, 99s at 60Hz is 5940
    uint8_t roundOver = 0;
    uint8_t pad = 0;

    // FNV-1a over the serialised bytes.  This is what GGPO compares between the
    // two peers every frame ("Checksum for frame %d does not match saved").
    uint32_t checksum() const;
};

// Sizes are asserted so nobody adds a std::string/vector to the snapshot by accident.
// 88 = 41 bytes of the generic runtime + the move-driven fields + the phase-1 fields (input history, recoil,
// chain bits, invulnerability) + alignment padding.
static_assert(sizeof(Fighter) == 88, "Fighter must stay POD-sized for GGPO snapshots");
static_assert(sizeof(MatchState) == 188, "MatchState must stay POD-sized for GGPO snapshots");
static_assert(std::is_trivially_copyable_v<Fighter>, "Fighter must be memcpy-able");
static_assert(std::is_trivially_copyable_v<MatchState>, "MatchState must be memcpy-able");
static_assert(std::is_standard_layout_v<Fighter>, "Fighter layout must not depend on the compiler");
static_assert(std::is_standard_layout_v<MatchState>, "MatchState layout must not depend on the compiler");

// Serialise / restore.  Fixed layout, byte-stable, no padding surprises.
std::vector<uint8_t> saveState(const MatchState& s);
bool loadState(MatchState& s, const std::vector<uint8_t>& buf);

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

// One 1/60s tick.  Identical inputs + identical state => identical result on every
// platform.  This is the "advance_frame" GGPO callback.
void advanceFrame(MatchState& s, uint16_t p1, uint16_t p2);

// ---------------------------------------------------------------------------
// GGPO-shaped session interface
// ---------------------------------------------------------------------------

enum class NetResult { kOk, kNotSynchronized, kPredictionBarrier, kWouldRollback };

// The four callbacks GGPO requires, plus the event sink.  A local (offline) session
// implements this trivially; a real UDP/Steam session would wrap the same interface.
struct Callbacks {
    // Return a serialised snapshot of *state* into buf.
    void (*save)(const MatchState& state, std::vector<uint8_t>& buf, void* user);
    bool (*load)(MatchState& state, const std::vector<uint8_t>& buf, void* user);
    void (*advance)(MatchState& state, uint16_t p1, uint16_t p2, void* user);
    void (*logState)(const MatchState& state, const char* tag, void* user);
    void* user = nullptr;
};

class Session {
  public:
    virtual ~Session() = default;
    virtual const char* kind() const = 0;

    // Feed this player's input for the current frame, run the prediction/rollback
    // dance, and step the simulation.  Returns the number of frames actually
    // simulated (0 when GGPO says "wait").
    virtual NetResult addLocalInput(uint16_t buttons, int* simulatedFrames) = 0;
    // Both players' input for the current frame (what a real two-player local match needs;
    // the single-argument form above is the one-input-per-frame convenience wrapper).
    virtual NetResult addLocalInput(uint16_t p1, uint16_t p2, int* simulatedFrames) = 0;
    virtual int currentFrame() const = 0;
    virtual int lastConfirmedFrame() const = 0;
    virtual bool isRollingBack() const = 0;
    virtual void idle(int ms) = 0;
};

// Offline session: no network, no prediction.  Useful on its own (training mode,
// local versus, replay playback) and as the reference implementation that a real
// GGPO backend must match frame-for-frame.
class LocalSession : public Session {
  public:
    explicit LocalSession(MatchState& state, Callbacks cb);
    const char* kind() const override { return "local"; }
    NetResult addLocalInput(uint16_t buttons, int* simulatedFrames) override;
    NetResult addLocalInput(uint16_t p1, uint16_t p2, int* simulatedFrames) override;
    int currentFrame() const override { return static_cast<int>(state_.frame); }
    int lastConfirmedFrame() const override { return static_cast<int>(state_.frame); }
    bool isRollingBack() const override { return false; }
    void idle(int) override {}

  private:
    MatchState& state_;
    Callbacks cb_;
    uint16_t pending_[2] = {0, 0};
};

// Synctest session: GGPO's own determinism harness.  It runs the simulation twice -
// once live, once from a reloaded snapshot - and fails loudly if the checksums
// diverge.  That is exactly the check the shipped game logs as
// "Checksum for frame %d does not match saved (%d != %d)".
class SyncTestSession : public Session {
  public:
    SyncTestSession(MatchState& state, Callbacks cb, int checkDistance = 1);
    const char* kind() const override { return "synctest"; }
    NetResult addLocalInput(uint16_t buttons, int* simulatedFrames) override;
    // Two-player form: the synctest replays the frame from a snapshot and compares checksums.
    NetResult addLocalInput(uint16_t p1, uint16_t p2, int* simulatedFrames) override;
    int currentFrame() const override { return static_cast<int>(state_.frame); }
    int lastConfirmedFrame() const override { return static_cast<int>(state_.frame); }
    bool isRollingBack() const override { return false; }
    void idle(int) override {}

    uint32_t mismatches() const { return mismatches_; }
    uint32_t framesChecked() const { return framesChecked_; }

  private:
    MatchState& state_;
    Callbacks cb_;
    int checkDistance_;
    std::vector<uint8_t> snapshot_;
    uint32_t mismatches_ = 0;
    uint32_t framesChecked_ = 0;
};

// Default callbacks wired to saveState/loadState/advanceFrame above.
Callbacks defaultCallbacks();

}  // namespace fight
}  // namespace zengine
