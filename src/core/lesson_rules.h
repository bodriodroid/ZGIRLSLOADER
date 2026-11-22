// lesson_rules.h - what a challenge asks of the fight besides its script (lesson_data.h Challenge, one fighter per side here).  No dependencies.
#pragma once
#include <string>
#include <vector>

namespace zengine {
namespace script {

// What a challenge asks of the fight besides its script (lesson_data.h Challenge, one fighter per side here).
struct Rules {
    std::vector<std::string> locked[2];    // move kinds the side cannot use: the names of the tags the states carry (Normal Throw Special Super Jump ...)
    int lifePct[2] = {100, 100};           // full life as a percentage of the usual
    int meterBars[2] = {-1, -1};           // super meter at the start, in bars (-1 = as it comes)
    float lifeStart[2] = {-1, -1};         // life at the start as a fraction of the full life above (-1 = full): a challenge carries the player's life over
    enum class Mod { None, Regen, Drain, Hold };
    Mod lifeMod[2] = {Mod::None, Mod::None};    // what happens to the life while the fight runs
    Mod meterMod[2] = {Mod::None, Mod::None};   // ... and to the meter
    // "_REGEN_" / "_DRAIN_" / "_HOLD_" (anything else = None)
    static Mod modOf(const std::string& word) {
        return word == "_REGEN_" ? Mod::Regen : word == "_DRAIN_" ? Mod::Drain : word == "_HOLD_" ? Mod::Hold : Mod::None;
    }
};

}  // namespace script
}  // namespace zengine
