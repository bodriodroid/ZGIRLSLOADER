// lesson_data.h - what the Tutorials, Trials and Challenges menus list and what a lesson fight needs, read from the player's own game data.
//
//   Tutorials   training/trainingTOC.ini  ->  Beginner.stm, Intermediate.stm ...  (chapters)  ->  StoryNode blocks (lessons): FileName = the fight .ini
//   Trials      training/trialsTOC.ini    ->  Trials_Filia.stm ...                (one chapter per character, a StoryNode per trial)
//   Challenges  ui/config/MainMenu/challenges.ini: one block per challenge (rules for a fight against the CPU)
//
// A lesson's fight .ini ("TrainingMode 1", "Player 1" / "Fighter Filia" / "Color 1", "Player 2" ..., "UseAI 1", "LuaFile training/...lua",
// "World Class_Notes") says who fights where and which Lua script runs the lesson.  Nothing here knows about SDL.
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace zengine {

class Vfs;

namespace lessons {

struct FightIni {
    std::string fighter[2];
    int color[2] = {1, 1};
    std::string world;
    std::string luaFile;
    bool training = false;
    bool useAI = false;
    int rounds = 1;
};
// false = fewer than two fighters (nothing to start).
bool parseFightIni(std::string_view text, FightIni& out);

struct Entry {
    std::string file;       // the fight .ini ("training/beginner/basicmovement.ini")
    std::string title;      // DisplayName: a localisation key
    std::string text;       // DisplayText: a localisation key
    std::string icon;       // NodeUnlockedImg / NodeCompletedImg: textures of the game's UI
    std::string doneIcon;
    int pos = 0;            // TimelinePos
};
struct Chapter {
    std::string stem;       // "Beginner" / "Trials_Filia" (the .stm's name)
    std::string name;       // CharName: "beginner" / "FiliaTrials"
    std::string icon;       // CharIcon
    std::vector<Entry> entries;   // by TimelinePos
    // A key that stays the same between chapters and between runs: what the progress file stores.
    std::string progressKey(const Entry& e) const;
};
// `toc` = "trainingTOC.ini" or "trialsTOC.ini": every `File:` it lists that exists, in the file's order.  Chapters without lessons are left out.
std::vector<Chapter> loadChapters(const Vfs& vfs, const std::string& toc);

// One challenge (challenges.ini).  Lists are what the game writes with commas ("Filia, Cerebella, Peacock"); "Any" for the player's team = free pick.
struct Challenge {
    std::string name;                   // "Challenge1": its localisation key
    int timeLimit = 99;                 // seconds
    std::string winCond;                // "_KO_" / "_TIME_"
    std::string world;
    std::vector<std::string> playerTeam, enemyTeam, playerAssist, enemyAssist;
    std::vector<int> playerColor, enemyColor;
    // "Health: 80, _NONE_, 100, _REGEN_": percent of the usual amount and a modifier (_NONE_ _REGEN_ _DRAIN_ _HOLD_), player then enemy
    struct Gauge {
        int playerPct = 100, enemyPct = 100;
        std::string playerMod = "_NONE_", enemyMod = "_NONE_";
    } health, meter, drama;
    // move kinds the side cannot use: Normal Throw Special Super Jump AssistCalled AlphaCounter.  They are the names of the tags the states carry
    // (the lessons' own addForbiddenTag lists use the same words), so they go to the fight as they are.
    std::vector<std::string> playerLocked, enemyLocked;
    std::vector<std::string> playerAllowDamage, enemyAllowDamage;       // what may damage: All Normal Special Super ComboOverN
    std::string aiFile;                 // "AI/ArcadeMode/Random.lua"
    int aiLevel = 5;
    bool freePick() const { return playerTeam.empty(); }
};
std::vector<Challenge> parseChallenges(std::string_view text);

// A lesson text ready for the screen.  {1} / {2} = the objective's progress / maximum; the game draws its own pictures for the other
// placeholders ({lp} {hk} {qcf} {df} {seqs} ...), here they become plain text: buttons in capitals, directions as letters, {seqs} as ">".
std::string formatText(const std::string& text, int progress = 0, int maxProgress = 0);

}  // namespace lessons
}  // namespace zengine
