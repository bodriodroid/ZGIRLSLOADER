// lesson.h - a lesson (tutorial / trial / challenge script) running on the real data-driven fight.
//
// LuaHost (lua_host.h) only knows bare rt::Objects; this file plugs it into a FoitDuel:
//   * DuelAccess   FightAccess over the duel: getChar(0) = the player's body, getChar(3) = the dummy's, the assist slots are empty;
//                  saveState / loadState are the duel's own snapshot, so a script's "put it back as it was" is a real rollback.
//   * Lesson       owns the host, starts the script when the fight begins and, every tick, turns the pads into duel input:
//                  the player's pad (ignored while the lesson disabled it) and the dummy's pad (what the script typed with addInput / doMotion).
//
// No SDL, no screen: the Play screen asks `step()` for the tick and reads the UI sink the lesson HUD was given.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "foit_duel.h"
#include "lesson_rules.h"
#include "lua_host.h"

namespace zengine {
namespace script {

class DuelAccess : public FightAccess {
public:
    explicit DuelAccess(fight::FoitDuel* d, int difficulty = 3) : d_(d), difficulty_(difficulty) {}
    int difficulty() override { return difficulty_; }
    foit::rt::World* world() override { return &d_->worldMut(); }
    foit::rt::Object* character(int index) override {
        if (index == 0) return &d_->bodyMut(0);
        if (index == 3) return &d_->bodyMut(1);
        return nullptr;
    }
    bool saveState(std::vector<uint8_t>& out) override {
        d_->save(out);
        return true;
    }
    bool loadState(const std::vector<uint8_t>& in) override { return !in.empty() && d_->load(in.data(), in.size()); }

private:
    fight::FoitDuel* d_;
    int difficulty_;
};

// One pad tick of the dummy (relative to the way it faces) -> the fight.h bits.
uint16_t padFromFrame(const InputFrame& f, bool facingRight);

class Lesson {
public:
    Lesson() = default;
    ~Lesson() { end(); }
    Lesson(const Lesson&) = delete;
    Lesson& operator=(const Lesson&) = delete;

    // `scriptName` = the lesson's `LuaFile`.  false = the script did not load (host().lastError()).  The lesson hooks into the duel (every
    // step of it runs the script first) until end() or the destructor; the duel must outlive the lesson or be released with end().
    // `difficulty` is what getDifficulty() answers (the AI scripts' level; a challenge's AILevel).
    bool begin(fight::FoitDuel* duel, UiSink* ui, ReadText read, const std::string& scriptName, int difficulty = 3);
    void end();
    bool active() const { return duel_ != nullptr && host_.loaded(); }

    // One tick of the fight with the pads of the people playing: the duel's step runs the script (once the fight has begun) and swaps in
    // what it decides - the dummy's pad when the script types, no pad for the player while the lesson disabled it, the forbidden tags and
    // the blocking switch for both sides.  (The Play screen just steps its duel as usual; this is the same thing for tests and the probe.)
    void step(uint16_t playerPad, uint16_t dummyPad = 0) {
        if (duel_) duel_->step(playerPad, dummyPad);
    }

    // A challenge's rules (call right after begin(): the life and meter set-up is applied at once, the rest every tick).
    void setRules(const Rules& r);

    LuaHost& host() { return host_; }
    const LuaHost& host() const { return host_; }
    fight::FoitDuel* duel() { return duel_; }

private:
    void onPads(uint16_t& p1, uint16_t& p2);

    fight::FoitDuel* duel_ = nullptr;
    std::unique_ptr<DuelAccess> access_;
    LuaHost host_;
    bool started_ = false;
    Rules rules_;
    std::vector<uint32_t> locked_[2];   // rules_.locked as tag hashes
    float lifeHold_[2] = {0, 0}, meterHold_[2] = {0, 0};
};

}  // namespace script
}  // namespace zengine
