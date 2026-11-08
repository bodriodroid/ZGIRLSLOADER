// arcade.h - the Arcade Mode ladder.
//
// The game's arcade script (core/arcade/random/Default.stm) is eight "random" fights (random.ini: Fighter Random, World Random, AI on)
// followed by one boss fight (vsBossArcade.ini: BossMarie on Falling Wreckage, one round, the hardest AI level).  The Lua AI the
// game runs cannot be executed here, so the ladder is our own: its bookkeeping is this class, which has no game files in it and no
// SDL, so it is unit tested.
//
//   * the eight opponents never repeat while the roster has enough fighters (the player's own character is left out first);
//   * the stage is random, never the boss's stage and never the same as the fight before;
//   * the AI gets harder as the ladder climbs (one level up every three fights, never past the hardest);
//   * losing lets the player try the same fight again ("continue"), winning moves on; the boss falling ends the run.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {
namespace arcade {

constexpr int kRandomFights = 8;          // before the boss
constexpr int kBossRounds = 1;            // vsBossArcade.ini: NumRounds 1

struct Fight {
    std::string fighter;      // character id ("filia")
    std::string stage;        // stage id ("new_meridian"); "" = the caller's choice
    int difficulty = 1;       // 0 easy, 1 normal, 2 hard
    int rounds = 0;           // rounds to play (best of); 0 = the player's setting
    bool boss = false;
};

class Ladder {
public:
    // `player`: the human's character.  `roster`: ids of every character that can be an opponent.  `stages`: ids of the stages random
    // fights may use.  `boss` / `bossStage`: the final fight's fighter and stage.  `baseDifficulty`: the menu's difficulty (0..2).
    void begin(const std::string& player, const std::vector<std::string>& roster, const std::vector<std::string>& stages,
               const std::string& boss, const std::string& bossStage, int baseDifficulty, uint32_t seed);

    bool active() const { return !fights_.empty(); }
    int total() const { return static_cast<int>(fights_.size()); }     // random fights + the boss
    int index() const { return idx_; }                                  // 0-based
    const Fight& current() const;
    bool atBoss() const { return active() && idx_ == total() - 1; }
    bool finished() const { return finished_; }
    int continuesUsed() const { return continues_; }

    // The current fight was won.  Returns true when that was the last one (the run is complete).
    bool won();
    // The current fight was lost and the player chose to go again.
    void retry() { ++continues_; }

    // The AI level of the fight at `index` for a menu difficulty: base - 1 + index / 3, clamped; the boss is always the hardest.
    static int difficultyFor(int baseDifficulty, int index, bool boss);

private:
    std::vector<Fight> fights_;
    int idx_ = 0, continues_ = 0;
    bool finished_ = false;
};

}  // namespace arcade
}  // namespace zengine
