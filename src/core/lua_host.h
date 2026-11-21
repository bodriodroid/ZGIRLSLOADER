// lua_host.h - runs the game's own Lua scripts (training / tutorial / trial lessons: `LuaFile` of a fight .ini) against a running fight.
//
// The scripts are written for the shipped engine's embedded Lua 5.1 and its native bindings.  This host rebuilds those bindings from what the
// scripts themselves use (LuaLib.lua / TutorialLib.lua and the lessons are the player's own game data; nothing of them is bundled):
//
//   * sprites     getChar(0..5) = P1, P1 assist 1 / 2, P2, P2 assist 1 / 2.  A sprite reads and writes the object's script slots by name
//                 (`p1.numhits`, `p2.hitstun`, `p1.xpos`, ... case-insensitive; `slotN` = slot id N, `flagN` = flag bit N) and has the methods
//                 hasTag(tag, ...) (ALL the tags), getChildren(), getLastAttacker(), getHitInfo(), addForbiddenTag(...), clearForbiddenTags(),
//                 blockingAllowed(bool).
//   * callbacks   setInit(fn) (the fight starts), setUpdate(fn) (every simulated tick); LuaLib.lua builds coroutines, timers and wait() on them.
//   * input       addInput(dir, button, ...) and doMotion("D,DF,F+HP") feed the second player (the script's dummy): see InputFrame / takeInput.
//   * state       saveState() returns a function that puts the fight back as it was (a snapshot of the rollback bytes).
//   * ui          ui.* drives the lesson HUD (objective list, explanation pages, fades): UiSink, implemented by the app; the default sink does
//                 nothing, so a lesson also runs headless (tests).
//   * misc        hash(name), random(n), print, require("File") (through the `ReadText` callback), getDifficulty, floorHeight, levelDimensions.
//
// Nothing here knows about SDL or the screen: the host only touches the foit::rt::World it is given.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "foit_rt.h"

struct lua_State;

namespace zengine {
namespace script {

// One tick of the dummy's pad.  dir is relative to the way the dummy faces (the same numbers LuaLib's `d` table uses):
// 0 forward, 1 down-forward, 2 down, 3 down-back, 4 back, 5 up-back, 6 up, 7 up-forward, 8 neutral.  Button bit i: 0 LP, 1 MP, 2 HP, 3 LK, 4 MK, 5 HK.
struct InputFrame {
    int dir = 8;
    uint8_t buttons = 0;
    bool operator==(const InputFrame& o) const { return dir == o.dir && buttons == o.buttons; }
};

// "D,DF,F+HP" -> pad ticks (2 per step, 3 on the last one).  Steps: a direction (N F B D U DF DB UF UB, X = any, `!D` = not D, `D(L)` = D), then
// `+button` names (Jab LP WP, Strong MP, Fierce HP, Short LK WK, Fwd Forward MK, RH Roundhouse HK, 2P 3P 2K 3K PP KK); `~name` / `^name` = that
// button is let go; `VALID=n` (the display window of the input prompts) is ignored.  An empty / unknown text gives no ticks.
std::vector<InputFrame> parseMotion(const std::string& text);

// A recorded dummy sequence (the game's `.seq` files, played by playSequence): 5 header bytes (version 1, then the number of records minus one
// as a little-endian u32), then 18 bytes per pad tick: the direction (the same 0..8 numbers as InputFrame::dir), 0x0a 0x0a, three zero bytes and
// 12 button states (LP MP HP LK MK HK first; the rest are the any-punch / any-kick macro buttons).  A state of 0 (just pressed) or 3 (held) is a
// button that is down, 1 (just released) and 4 (up) one that is not.  One record = one tick.  false = not a sequence (too short, bad version).
bool parseSequence(const std::string& bytes, std::vector<InputFrame>& out);

// What the lesson HUD needs from the app (every call is optional: the base class ignores them).
struct UiSink {
    virtual ~UiSink() = default;
    virtual void loadCatalog(const std::string& /*name*/) {}                   // ui.loadcatalog: a text catalog the lesson's keys come from
    virtual void clearAll() {}                                                 // empties the objective list
    virtual void setMax(int /*rows*/) {}                                       // rows the list shows at once
    virtual void add(const std::string& /*key*/, int /*progress*/, int /*maxProgress*/) {}
    virtual void update(int /*index*/, const std::string& /*key*/, int /*progress*/, int /*maxProgress*/) {}
    virtual void complete(int /*index*/, bool /*done*/) {}                     // 1-based
    virtual void scrollTo(int /*index*/) {}
    virtual void setExplanation(const std::string& /*key*/) {}                 // a page of the "press any button" panel
    virtual void showExplanation(bool /*on*/) {}
    virtual bool explanationActive() { return false; }                         // ui.getexplanationstatus: true while the page waits for the player
    virtual void setDetails(const std::string& /*key*/) {}                     // the hint line under the list
    virtual void showDetails(bool /*on*/) {}
    virtual void displayPlayerInputs(bool /*p1*/, bool /*p2*/) {}              // the input display
    virtual void setSlowMoPct(float /*pct*/) {}
    virtual void screenFade(float /*amount*/, int /*dir*/, float /*alpha*/) {}
    virtual void p1Disable(bool /*off*/) {}                                    // the player's own pad is ignored (explanations, demonstrations)
    virtual void endMission() {}                                               // the lesson is complete
    virtual void setInputPrompts(const std::vector<std::string>& /*motions*/, const std::vector<int>& /*frames*/) {}
};

// The running fight as the scripts reach it.
struct FightAccess {
    virtual ~FightAccess() = default;
    virtual foit::rt::World* world() = 0;
    // getChar(i): 0 P1, 1 P1 assist 1, 2 P1 assist 2, 3 P2, 4 P2 assist 1, 5 P2 assist 2 (null = nobody there)
    virtual foit::rt::Object* character(int index) = 0;
    virtual bool saveState(std::vector<uint8_t>& out) = 0;
    virtual bool loadState(const std::vector<uint8_t>& in) = 0;
    virtual int difficulty() { return 3; }
    virtual float levelWidth() { return 3750.0f; }                             // levelDimensions(): the stage in the units of the XPOS / YPOS slots
    virtual float levelHeight() { return 1000.0f; }
};

// A file of the game data by the name the script asked for (`require("LuaLib")` -> LuaLib.lua); false = not found.
using ReadText = std::function<bool(const std::string& name, std::string& out)>;

class LuaHost {
public:
    LuaHost();
    ~LuaHost();
    LuaHost(const LuaHost&) = delete;
    LuaHost& operator=(const LuaHost&) = delete;

    // Opens a fresh Lua state, installs the bindings and runs `scriptName` (it registers its callbacks).  false = it failed (see `lastError`).
    bool load(FightAccess* fight, UiSink* ui, ReadText read, const std::string& scriptName);
    bool loaded() const { return L_ != nullptr && error_.empty(); }
    // The fight starts: runs the setInit callbacks (once).
    void start();
    // One simulated tick, before the pads are read: runs the setUpdate callbacks.
    void tick();
    // The dummy's pad for this tick (the script's addInput / doMotion); `scripted` tells whether the script asked for anything.
    InputFrame takeInput(bool* scripted = nullptr);
    bool p1Disabled() const { return p1Disabled_; }
    bool missionComplete() const { return missionComplete_; }
    // What the lesson asked of a character (index as in getChar: 0 P1, 3 P2, ...): the tags it may not use (addForbiddenTag, name hashes) and
    // whether it may block (blockingAllowed).  The app applies them to the fight.
    static constexpr int kChars = 6;
    const std::vector<uint32_t>& forbiddenTags(int index = 0) const { return forbidden_[index >= 0 && index < kChars ? index : 0]; }
    bool blockingAllowed(int index = 0) const { return blocking_[index >= 0 && index < kChars ? index : 0]; }
    bool slowMotion(float* pct) const { if (pct) *pct = slowMo_; return slowMo_ > 0; }
    const std::string& lastError() const { return error_; }
    const std::vector<std::string>& log() const { return log_; }
    uint64_t ticks() const { return ticks_; }

    struct Impl;   // the C functions of the bindings reach it through an upvalue

private:
    friend struct Impl;
    void closeState();
    void logLine(const std::string& s);
    void failure(const std::string& s);

    lua_State* L_ = nullptr;
    std::unique_ptr<Impl> impl_;
    FightAccess* fight_ = nullptr;
    UiSink* ui_ = nullptr;
    ReadText read_;
    std::string error_;
    std::vector<std::string> log_;
    std::vector<uint32_t> forbidden_[kChars];
    bool blocking_[kChars] = {true, true, true, true, true, true};
    bool p1Disabled_ = false;
    bool missionComplete_ = false;
    bool started_ = false;
    float slowMo_ = 0;
    uint64_t ticks_ = 0;
    uint32_t rng_ = 0x2545F491u;
    // the dummy's pad
    InputFrame added_;
    bool addedSet_ = false;
    std::deque<InputFrame> motion_;
    // recorded sequences (loadSequence / playSequence): by the name the script used
    std::map<std::string, std::vector<InputFrame>> sequences_;
    const std::vector<InputFrame>* seq_ = nullptr;   // the one playing (points into sequences_)
    size_t seqPos_ = 0;
};

}  // namespace script
}  // namespace zengine
