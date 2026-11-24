// foit_rt.h - the data driven fight runtime: runs the states, nodes, transitions and scripts of a parsed .foit
// (see foit.h / foit_vm.h) tick by tick, the way the shipped engine does.  Everything a character does - standing,
// walking, dashing, jumping, attacking - is *data* in its .foit; this file only knows how to execute that data.
//
// What is here (all worked out from the shipped executable by static analysis, then written down as a clean model):
//   * input   a per character history of per-tick samples (direction 0..8 + 15 button states) and the command matcher
//             (motions, double taps, button windows, "default step" vetoes, hold requirements, facing mirror)
//   * object  one runtime instance of a foit::Object: 177 float slots, 96 flag bits, state / node cursor, transition bitset
//   * tick    input -> transition evaluation -> state step (transition / node timer / next state) -> node exec
//             (property records gated by commands, header + node scripts, hit data activation) -> physics
//
// Deterministic: no allocation while ticking, no wall clock, the script random generator is a caller owned seed.
// Not done yet (see PORT_QUEUE): hit resolution (damage / blockstun / knockdown), throws, child spawn from sub lists,
// camera, hitstop cancel buffering.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "foit.h"
#include "foit_vm.h"

namespace zengine {
namespace foit {
namespace rt {

// ---- input -------------------------------------------------------------------------------------------------------
constexpr int kButtons = 15;

// Direction of a sample, physical (screen) terms; a step names directions as if the character faced right.
enum Dir : uint8_t {
    DirRight = 0, DirDownRight = 1, DirDown = 2, DirDownLeft = 3, DirLeft = 4, DirUpLeft = 5, DirUp = 6, DirUpRight = 7,
    DirNeutral = 8,
    DirAny = 9,    // step only: matches every direction
    DirNever = 10  // step only: the step can never match
};

// State of one button in a sample, and the requests a step can make about it.
//   sample values: Pressed / Released (the edge happened this tick), Held (down since an earlier tick), Up (idle)
//   step requests: those four, Edge (pressed or released this tick), NotX (anything but X), Any
enum Btn : uint8_t {
    BtnPressed = 0,       // went down this tick
    BtnReleased = 1,      // went up this tick
    BtnEdge = 2,          // request: pressed or released this tick
    BtnHeld = 3,          // down since an earlier tick
    BtnUp = 4,            // up since an earlier tick (idle)
    BtnNotPressed = 5, BtnNotReleased = 6, BtnNotEdge = 7, BtnNotHeld = 8, BtnNotUp = 9,
    BtnAny = 10           // request: don't care
};

// The 15 button slots of a sample.  0..5 are the physical attack buttons (LP MP HP LK MK HK), 13 and 14 two extra physical
// buttons; the others are derived from the physical ones every tick (see composeSample).
enum ButtonSlot : uint8_t {
    SlotLP = 0, SlotMP = 1, SlotHP = 2, SlotLK = 3, SlotMK = 4, SlotHK = 5,
    SlotAnyKick = 6, SlotAnyPunch = 7,   // at least one kick / punch
    SlotKK = 8, SlotPP = 9,              // two or more kicks / punches at once
    SlotKKK = 10, SlotPPP = 11,          // three kicks / punches at once
    SlotChord = 12,                      // a punch+kick chord of three or more buttons
    SlotExtraA = 13, SlotExtraB = 14
};

struct Sample {
    uint8_t dir = DirNeutral;
    uint8_t btn[kButtons];
    Sample() {
        for (uint8_t& b : btn) b = BtnUp;
    }
};

// Newest-first history of samples; every entry exists from the start (filled with "nothing pressed").
class InputRing {
public:
    void reset(size_t capacity);
    void push(const Sample& s);
    const Sample* at(int age) const;  // 0 = newest; ages beyond the capacity wrap around like the original ring
    size_t capacity() const { return buf_.size(); }
    uint64_t pushes() const { return pushes_; }
    // raw access for snapshots: the buffer in storage order, the slot of the newest sample and the push counter
    const std::vector<Sample>& raw() const { return buf_; }
    size_t head() const { return head_; }
    void restore(std::vector<Sample> buf, size_t head, uint64_t pushes) {
        buf_ = std::move(buf);
        head_ = buf_.empty() ? 0 : head % buf_.size();
        pushes_ = pushes;
    }

private:
    std::vector<Sample> buf_;
    size_t head_ = 0;
    uint64_t pushes_ = 0;
};

// Builds the sample of this tick from the raw pad: `dir` (0..8) and the physical buttons down (bit i = button i, i in 0..5, 13, 14)
// given the previous sample: edges, the 1 tick leniency for chords and the derived buttons (any punch, PP, ...).
Sample composeSample(const Sample& prev, int dir, uint32_t rawDown);

// Does `step` accept `s` (direction mirrored when the character faces left)?  `cx` is only used for the step's condition chain.
bool stepMatches(const Step& step, const Sample& s, bool facingRight, vm::Context& cx);
// Does `cmd` match the history, its newest step anchored at ring age `anchor`?  `*ticks` = samples scanned past the anchor.
bool matchAt(const Command& cmd, const InputRing& ring, int anchor, bool facingRight, vm::Context& cx, int* ticks);
// Tries the anchors `offset .. offset + cmd.b`; `*age` = how many anchors past `offset` were needed, `*ticks` as above.
bool matchCommand(const Command& cmd, const InputRing& ring, int offset, bool facingRight, vm::Context& cx, int* age, int* ticks);

// ---- runtime object ------------------------------------------------------------------------------------------------
constexpr int kTransWords = 8;  // up to 256 transitions per state (the largest real state has 181)

// Runtime flag bytes (named after what the engine uses them for).
namespace fl {
// byte `a` (engine offset 0x364)
constexpr uint8_t A_Flag0 = 0x01;        // set/cleared by node records (b1 bit 1)
constexpr uint8_t A_FreezeFacing = 0x04; // the facing is not mirrored for input
constexpr uint8_t A_HitActive = 0x08;    // a hit data is armed
constexpr uint8_t A_HasHit = 0x40;       // the armed hit data already touched the enemy (one touch per arming)
// byte `b` (engine offset 0x366)
constexpr uint8_t B_GotHit = 0x01;
constexpr uint8_t B_Blocking = 0x02;
constexpr uint8_t B_Commit = 0x10;       // stop re-stepping this tick
constexpr uint8_t B_Attack = 0x80;       // this node is an attack (the enemy sees ENEMYATTACKING)
}  // namespace fl

constexpr float kStartHealth = 10000.0f;

struct World;

struct Object {
    const foit::Object* def = nullptr;
    const foit::Character* ch = nullptr;
    int id = 0;

    float slots[kSlotCount];
    uint32_t flags[vm::kFlagWords];
    uint32_t rng = 1;

    // state machine
    int state = 0;
    int nodeIdx = 0, nodeTimer = 0, nodeDur = 1;
    bool nodeHasHit = false;
    bool fresh = false;   // spawned this tick: its first step runs node 0 instead of advancing past it
    uint32_t trans[kTransWords];
    uint8_t fa = 0, fb = 0, fc = 0;  // runtime flag bytes (see fl::); fc bit 6 = remember the visible previous state
    int hitState = -1, hitNode = -1, hitProp = -1;  // the armed hit data (state, node, property record)
    int moveUses = 0;
    int frame = 0;                         // 1-based id of the frame (boxes, pivot) the object shows right now; 0 = none yet
    // LIFEBAR (slot 64) is the health, in raw game units (a light punch takes 300, a character starts with kStartHealth).  The hit states of the
    // data take the damage off it themselves (LIFEBAR := LIFEBAR - DAMAGE); only throws and the engine's clamps write it from C++.
    float& life() { return slots[64]; }
    float life() const { return slots[64]; }
    // tags a script added to / removed from this object on top of the tags its current state carries (ops 118 / 119)
    static constexpr int kMaxDynTags = 8;
    uint32_t addedTags[kMaxDynTags] = {};
    uint32_t removedTags[kMaxDynTags] = {};
    int numAddedTags = 0, numRemovedTags = 0;
    int charId = -1;                       // roster id of the character (ops 50): set by whoever picks the fighters; -1 = unknown
    int palette = 0;                       // costume / colour index the player picked (op 54)
    int comboHits = 0;                     // hits this object took in the combo that is running (the damage scale decays from the 3rd on)
    int strength = 0;                      // attack strength index the current node declared (LP/MP/HP ...)
    const Chain* pendingChain = nullptr;   // script of the move that just connected; node records flagged "on connect" run it
    // where `pendingChain` lives (hit data of body / child `pendDef`, state / node / property / chain index), so a saved world can find it again
    const foit::Object* pendDef = nullptr;
    int pendState = -1, pendNode = -1, pendProp = -1, pendCi = -1;

    // body
    float x = 0, y = 0;        // world position; y grows upwards, the floor is y = 0
    float accX = 0, accY = 0;  // movement gathered this tick, applied after the scripts ran
    bool onGround = true;
    // TETHEREDTO mode 1 ("ride along with the parent"): where the tether target stood the last time the object followed it
    float tethX = 0, tethY = 0;
    bool tethOk = false;

    InputRing ring;

    Object* leader = nullptr;    // owning character (itself for a character)
    Object* enemy = nullptr;
    Object* target = nullptr;
    Object* attacker = nullptr;
    Object* parent = nullptr;
    bool alive = true;

    float facing() const { return slots[56]; }
    bool facingRight() const { return (fa & fl::A_FreezeFacing) != 0 || slots[56] == 1.0f; }
    const State& st() const { return def->states[static_cast<size_t>(state)]; }
};

struct Stats {
    uint64_t ticks = 0;
    uint64_t transitions = 0;
    uint64_t stateChanges = 0;
    uint64_t hits = 0, blocks = 0, touches = 0;
    uint64_t loopCaps = 0;           // ticks that hit the 500 re-step limit (a script loop that never settles)
    uint64_t hostOps[256] = {};      // unimplemented engine ops hit
    uint64_t hostTests[256] = {};
    uint64_t softOps[256] = {};      // engine ops answered with a neutral value (palette / art-point / match globals not modelled)
};

// Something a node asked the presentation layer for (a sound or an effect, named by hash): the host drains `World::events` every tick.
struct Event {
    // MatchPhase: the match layer moved on (`hash` = Match::Phase it entered, `object` = round number, `x` = winning side of the round, -1 none)
    // TagIn: a team swapped its point character (`object` = id of the character that came in, `hash` = side 0 / 1, `x` = TAGINFO)
    enum Kind : uint8_t { NodeEvent = 0, HitSpark = 1, BlockSpark = 2, MatchPhase = 3, TagIn = 4 };
    uint64_t tick = 0;     // Stats::ticks when it was raised
    int object = 0;        // Object::id of the object whose node raised it (HitSpark / BlockSpark: the attacker)
    uint32_t hash = 0;     // NodeEvent: name hash from the node's event list; HitSpark / BlockSpark: the hit data's effect index
    float x = 0, y = 0;    // where the object stood (HitSpark / BlockSpark: where the defender stood)
    uint8_t kind = NodeEvent;
    // HitSpark / BlockSpark only: what the presentation layer needs to draw the spark.
    //   spark   = the hit data's spark class (HitData::f12: 1 light .. 5 grab, 201.. the character-specific families; 0 = none) for a
    //             hit, -1 (a block on the ground) / -5 (a block in the air) for a block: the codes the hitspark character's `None`
    //             state dispatches on (its slot 168)
    //   hitX/hitY = the centre of the first hit box / hurt box pair that touched (world coordinates, y up)
    //   faceRight = the attacker was facing right (the spark is mirrored with it)
    int16_t spark = 0;
    float hitX = 0, hitY = 0;
    bool faceRight = true;
};

// Round flow.  The shipped fight engine runs it purely through two slots of every character (the .foit scripts do the rest):
//   ROUNDOVER (slot 30): < 0 the round intro is playing, 0 the fight is on, > 0 the round is over (1 = KO, 2 = time over)
//   DOPOSE    (slot 135): the match layer asks for a pose: > 0 win pose, < 0 lose pose; the pose states write 0 back when they are done.
// `Match` is the part that sits above the characters: it sets those two slots, waits for the poses, runs the clock, counts rounds.
struct Match {
    // Settle: a point character ran out of life; the round is not over (ROUNDOVER stays 0, so the killing blow can still wall-bounce)
    // until the fallen fighter has landed and stopped
    // Ready: the poses are over (or skipped) but the round card is still up: ROUNDOVER stays -1, nobody can act, the clock waits
    // (`readyTicks` long; 0 = the phase is skipped)
    enum Phase : int32_t { Off = 0, Intro = 1, Fight = 2, Ending = 3, Over = 4, Settle = 5, Ready = 6 };
    int32_t phase = Off;
    int32_t round = 1;
    int32_t roundTicks = 99 * 60;   // length of the round clock
    int32_t clock = 0;              // ticks left on it (it only runs while the fight is on and no super flash is playing)
    int32_t roundsToWin = 2;
    int32_t wins[2] = {0, 0};       // rounds won by side 0 / 1 (PLAYERNUM 1 / 2)
    int32_t winner = -1;            // winner of the round that just ended (0 / 1; -1 = draw or still fighting)
    int32_t matchWinner = -1;       // set when phase == Over (-1 = draw)
    int32_t reason = 0;             // how the round ended: 1 KO, 2 time over, 3 double KO
    int32_t phaseTicks = 0;         // ticks spent in the current phase
    int32_t settleTicks = 20;       // how long the fallen fighter must lie still before the round counts as over
    int32_t still = 0;              // Settle: consecutive ticks the fallen fighter has been on the ground and not moving
    int32_t introGoTicks = 90;      // ticks after the round starts at which the intro poses are told to go (DOPOSE 1 -> 2)
    int32_t timeouts = 0;           // phases that were cut short because a pose never signalled "done" (diagnostic)
    int32_t introRounds = 1000;     // the first N rounds open with the fighters' entrance poses (a normal match: 1; the default: every round)
    int32_t readyTicks = 0;         // hold between the poses and the fight (the "Take N" card); the clock and the fighters wait
    int32_t endHold = 0;            // a finished round lasts at least this long (the K.O. banner), even when the poses are quicker
    int32_t endless = 0;            // 1 = training: no clock, and nobody can die (life is topped up before every tick)
    int32_t tieBreak = 0;           // 1 = both sides reaching the target together (a double KO / time over in the deciding round) plays one more
                                    // round that decides it (the target goes up by one); 0 = the match ends as a draw
    static constexpr int kMaxFighters = 8;
    int32_t numFighters = 0;
    float startX[kMaxFighters] = {};
    int32_t startRight[kMaxFighters] = {};
    float maxLife[kMaxFighters] = {};        // LIFEBAR every fighter started the match with (what training tops it up to)
    float startTeamPos[kMaxFighters] = {};   // TEAMPOS / ISPOINT every fighter had when the match began (a new round starts from them)
    float startIsPoint[kMaxFighters] = {};
};

struct World {
    std::vector<Object*> objects;    // characters first
    std::vector<Event> events;       // raised since the host last cleared it
    float globals[256];
    uint32_t rng = 12345;
    float floorY = 0;
    float leftWall = -400, rightWall = 400;  // pixels
    Stats stats;
    // stun the engine hands out per hit strength (HitData::f18 = 0..7); the shipped game reads it from characters/_dizzy_values.ini
    // (Light / Medium / Heavy -> entries 1 / 2 / 3).  All zero = no dizzy gauge.  Configuration, not match state: it is not saved or hashed.
    float strengthTable[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<std::unique_ptr<Object>> pool;  // children spawned during the match (owned here, also listed in `objects`)
    int nextId = 1;                  // id of the next spawned child (ids are never reused)
    Match match;                     // round flow (Phase::Off = the host runs the fight without a match layer)
    // Teams.  With `teams` off (the default) the world is a plain duel: two characters, each fed `inputs[i]` in the order they are listed.
    // With it on, every character is a member of the side its PLAYERNUM (slot 25: 1 / 2) names, TEAMPOS (26) is its place in the team
    // (0 = on point, 1 / 2 = the others) and ISPOINT (27) says who is on screen; all members of a side are fed that side's input (the
    // offscreen ones read it to call an assist or tag in) and the engine keeps their POINTX / POINTY / POINTFACING / POINTASSISTLOCK / ASSISTOK
    // slots up to date, links every object's `enemy` to the opposing point character, and answers the TAGTO the scripts write.
    bool teams = false;
    float anchor[2][3] = {};         // per side: where the last visible point character stood (x, y, facing): what POINTX / POINTY / POINTFACING report
    // Lesson rules (the tutorial / trial scripts' addForbiddenTag / blockingAllowed on a side's point character).  The host sets them before every
    // tick; they are configuration, not match state: not saved, not hashed.  `forbidden`: name hashes of tags; a transition into a state that
    // carries one of them is refused.  `noBlock`: the character never blocks (every hit lands).
    std::vector<uint32_t> forbidden[2];
    bool noBlock[2] = {false, false};
    // A challenge's "AllowDamage" for each side's attacks (see script::Rules): a hit whose move is not one of the allowed kinds lands with
    // its stun and pushback but takes no life.  Configuration, like `forbidden`.  The hashes are the tag names the states carry.
    struct DamageGate {
        bool active = false;
        bool normal = false, special = false, super = false;
        int comboOver = -1;                                   // >= 0: anything damages once the combo has this many hits
        uint32_t hNormal = 0, hSpecial = 0, hSuper = 0;
    } gate[2];
    World() {
        for (float& g : globals) g = 0;
    }
};

// Creates `o` from a character body (or one of its children) at `x`.
void init(Object& o, const foit::Character& ch, const foit::Object& def, float x, bool facingRight, int inputRing = -1);
// Spawns child `childIdx` of the parent's character (projectiles, effects, helpers) at an offset from the parent
// (`ox` forward, `oy` up), runs `initScript` on it and lists it in the world.  Returns null when the index is out of range.
Object* spawn(Object& parent, World& w, int childIdx, float ox, float oy, const Chain* initScript);
// Moves the object into state `s` (node cursor on the first node, per-state slot resets).
void enterState(Object& o, World& w, int s);
// One whole tick of one object: input sample (optional), transitions, state step(s), node execution, physics.
void tick(Object& o, World& w, const Sample* input);
// Reads the text of _dizzy_values.ini ("# comment" lines are skipped, then pairs "Light 15", "Medium 20", "Heavy 30": the first letter
// picks the entry, the number is its stun) into `table` (8 floats).  Returns how many entries were set.
int parseStrengthTable(const std::string& text, float* table);
// Hit boxes of every armed attack against the hurt boxes of its enemy: damage, hitstop, the reaction the defender's states pick up.
void resolveHits(World& w);
// Pushes overlapping fighters apart (push boxes of the current frames) and keeps them inside the stage.
void resolvePush(World& w);
// Starts a match with the characters that are in `w.objects` right now: remembers where they stand, asks for the intro poses
// (ROUNDOVER = -1, DOPOSE = 1).  `tickAll` then runs the rest: intro -> fight -> KO / time over -> poses -> next round -> Over.
void startMatch(World& w, int roundSeconds = 99, int roundsToWin = 2);
// The same with every knob of the flow.  Everything the flow needs is in the world (and so in its snapshots): the app only reads the phase.
struct MatchOptions {
    int roundSeconds = 99;   // <= 0 = no clock
    int roundsToWin = 2;
    int introRounds = 1000;  // rounds that open with the entrance poses (a normal match: 1)
    int readyTicks = 0;      // card hold between the poses and the fight
    int endHold = 0;         // minimum length of the round end
    bool endless = false;    // training: no clock, nobody dies
    bool tieBreak = false;   // a drawn match plays a deciding extra round instead of ending as a draw (assumed rule)
};
void startMatch(World& w, const MatchOptions& opt);
// Cuts the entrance poses short: everybody stands, the flow goes on with Ready / Fight.
void matchSkipIntro(World& w);
// Convenience: tick every object once, characters first.
void tickAll(World& w, const Sample* inputs /* one per character, may be null */, int numInputs);

int findState(const foit::Object& def, const char* name);

// Does the object carry the tag (name hash) right now: the tags its current state lists, plus the ones a script added, minus the ones it removed.
bool objectHasTag(const Object& o, uint32_t hash);

// ---- rollback support ------------------------------------------------------------------------------------------------
// Everything that decides the future of a fight (objects, slots, cursors, links, input history, world globals) as bytes, and back.
// The characters of the match are owned by the caller (`World::objects[0 .. n)`, created with init()); children are rebuilt from the
// character bodies.  The byte stream carries no pointers, so a snapshot loads into any process that has the same .foit files.
// Events and Stats (diagnostics) are not part of it.  loadWorld returns false (world untouched or cleared) on a malformed stream.
void saveWorld(const World& w, std::vector<uint8_t>& out);
bool loadWorld(World& w, const std::vector<Object*>& characters, const uint8_t* data, size_t size);
// 64 bit FNV-1a over the saved bytes: two worlds with the same hash played the same fight (desync detection).
uint64_t hashWorld(const World& w);

}  // namespace rt
}  // namespace foit
}  // namespace zengine
