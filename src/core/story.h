// story.h - Story Mode data: the cutscene scripts (core/arcade/story/*.ini) and the fight scripts, plus the timeline nodes of a *.stm.
//
// A cutscene script is a list of "Key: value" lines run top to bottom (see docs/RE_NOTES.md):
//   Scene: / View:   which .gbs and which of its views draws the pictures (CutsceneCommon / ShotHandling / <Char>_Legacy_Intro ...)
//   Anim: [_LEGACY|_PERSIST n] <name>   starts an animation of the view   AnimSpeed: n   ResetAnim: <name>
//   ShotTex: <node> <dir> <name>        puts the texture story_art/<dir>/<name> on a sprite of the view
//   Text: <localisation key>            a line of dialogue (waits for the player)     Header: Left|Right <name>   the speaker's name tag
//   Portrait: <side> <char> <expression> [out]   Msg: DIALOGUE_ON|OFF|CINE_ON|CINE_OFF|FADE_ALL|CLEAR|LEGACY_PLAY|MUSIC_x|P_END n
//   Choice: / Decision: / Voice: / File: / AltVO:  (rare)
// Lines without a colon (notes) and '#' comments are skipped.
#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "text_formats.h"

namespace zengine {

struct CutCmd {
    std::string key;   // "Text", "Msg", "Anim" ... exactly as written (case kept)
    std::string arg;   // the rest of the line, trimmed, inline comment removed
    int line = 0;      // 1-based, for messages
};

struct CutScript {
    std::string scene, view;        // from the Scene: / View: lines (empty when absent)
    std::vector<CutCmd> cmds;       // everything else, in order (Scene / View lines are kept here too)
};
CutScript parseCutscene(std::string_view text);

// Whitespace separated words of a command argument.
std::vector<std::string> splitWords(const std::string& arg);

// Story fight script ("Player 2", "Fighter Parasoul", "World New_Meridian", "UseAI 1", "LuaFile ...", "NumRounds 1" ...): first word -> rest.
struct FightScript {
    std::map<std::string, std::string> fields;
    std::string get(const std::string& key, const std::string& fallback = {}) const;
    std::string fighter() const { return get("Fighter"); }
    std::string world() const { return get("World"); }
};
FightScript parseFightScript(std::string_view text);

// One step of a character's story (*.stm).
struct StoryNode {
    enum class Type { Scene, Fight, Credits, Other } type = Type::Other;
    std::string file;               // "arcade/story/Filia_Legacy_Intro.ini" ("" for credits)
    int pos = 0;                    // TimelinePos
    std::string unlockChar;         // UnlockCondition <Char> <n>: needs node n of that story completed
    int unlockPos = 0;
    std::string progressionUnlock;  // "StoryFilia" on the last fight
};
std::vector<StoryNode> storyNodes(const StmFile& stm);

// Story voice-over files (rules read from SkullGirls.exe, see docs/RE_NOTES.md):
//   story_vo/lang_<en|jp>/<tokens of the Text key joined>_<EN|JP>.wav
//   "Filia Intro 100" -> story_vo/lang_en/FiliaIntro100_EN.wav
//   a line voiced in several files (Cine, at most 3) swaps the "_" before the language for "_1_", "_2_", "_3_" (`part` 1..3; 0 = single file)
//   a key starting with "_RAW_" has no voice.
// Returns the candidates in the order to try (the joined tokens as written, then without '-' and '_'); empty = no voice for this key.
std::vector<std::string> storyVoiceFiles(const std::string& textKey, const std::string& lang, int part = 0);

}  // namespace zengine
