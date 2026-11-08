// story_mode.h - ZGirls Loader: Story Mode (Modo Historia), played from the user's own game data.
//
// What it reads (nothing is bundled):
//   arcade/story/<Char>.stm          the timeline of a character: scenes, fights, credits, unlock conditions
//   arcade/story/*.ini               cutscene scripts (Scene / View / ShotTex / Anim / Text / Header / Portrait / Msg ...)
//                                    and fight scripts (Fighter / World / NumRounds / UseAI)
//   CutsceneCommon.gbs               the cutscene views (ShotHandling, <Char>_Legacy_Intro ...) and the dialogue overlay
//                                    (CutsceneGeneral2013), played with the engine's GameUi + GbsAnimPlayer like attract.cpp
//   story_art/Cinematics/<dir>/...   the shot pictures (ShotTex), story_art/THs/<dir>/... the talking heads (Portrait)
//   localization/<lang>_loc.json     every line of dialogue (Text keys) and the names (Header)
//   stages.ini                       World id -> level file of a story fight
//   ProgressionInit.txt              which stories start locked (StoryDouble / StoryValentine = L)
//
// Flow: character (Character Select of the menu, or the own list of this screen) -> timeline -> each node in order:
//   NewScene -> the cutscene runner below; Fight -> handed to the Play screen (Out::Kind::Fight), the result comes back
//   through fightResult(); Credits -> a short credits card.  Progress / unlocks live in zg_progress (pref dir).
//
// When pictures, the scene file or its textures are missing, the same script still runs with plain drawing
// (black screen, dialogue box drawn with the built-in font), so the story can always be followed.
#pragma once
#include <SDL.h>

#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "audio.h"
#include "music_player.h"
#include "credits.h"
#include "game_ui.h"
#include "gamedata.h"
#include "gbs_anim.h"
#include "menu_data.h"
#include "story.h"
#include "ui.h"
#include "zg_progress.h"

namespace zengine {
namespace story {

// One fight of a story, as the Play screen needs it.
struct FightRequest {
    std::string p1;      // the story's character ("Filia")
    std::string p2;      // "Parasoul", "BossMarie", "Random" ...
    std::string world;   // stages.ini id as written ("New_Meridian")
    std::string level;   // level stem from stages.ini ("new_meridian_3d"); "" = keep the current stage
    int rounds = 1;
    bool ai = true;
};

struct Out {
    enum class Kind { None, Fight, Back } kind = Kind::None;
    FightRequest fight;  // Kind::Fight
};

class StoryScreen {
public:
    // Call after every (re)scan of the data folder.
    void setGameData(const GameData* gd);
    void setPrefsDir(const std::string& dir) { prefsDir_ = dir; }
    // Walks every node of every story without playing it: scripts, scenes / views / animations, pictures, text keys, portraits,
    // voices and fight set-ups.  Prints what is wrong and writes <prefs>/story_audit.txt.  Returns the number of problems.
    int audit(SDL_Renderer* r, const std::string& lang = std::string());
    const std::string& auditMessage() const { return auditMsg_; }
    void setAudio(Audio* a) { audio_ = a; }
    // Story music: the scripts' MUSIC_xx cues (see MusicLibrary::forCue); the story map plays the menu theme.
    void setMusic(MusicPlayer* m) { music_ = m; }
    // From the menu: `who` = character id picked on the Character Select ("filia"), "random", or "" (this screen's own list).
    void begin(const std::string& who, const std::string& lang);
    Out frame(Ui& ui, SDL_Renderer* r, int dtMs);
    // The Play screen finished the fight that frame() asked for: 1 won, 0 lost / gave up, -1 left with Back.
    void fightResult(int result);
    // Android back / Escape.  false = nothing left to close here (the caller returns to the menu).
    bool onBack();

private:
    enum class State { Pick, Timeline, Cutscene, Credits };
    enum class Wait { None, Tap, Anim, Legacy, Choice, End };

    struct Entry {
        std::string charName;             // CharName of the .stm ("Filia")
        std::string key;                  // zgKey(charName) ("filia")
        std::vector<StoryNode> nodes;
        std::vector<std::string> labels;  // one line per node for the timeline
    };

    // ---- data
    void scanStories();
    void loadLocalization(const std::string& lang);
    int findStory(const std::string& who) const;
    bool storyLocked(const Entry& e) const;
    bool nodeUnlocked(const Entry& e, size_t i) const;
    size_t nextNode(const Entry& e) const;
    void openStory(int idx);
    void playNode(size_t i, Out& out);
    void nodeDone();
    void finishStory();
    FightRequest fightFor(const Entry& e, const StoryNode& n) const;
    std::string levelForWorld(const std::string& world) const;
    std::string nodeLabel(const StoryNode& n) const;
    std::string tr(const std::string& key, const std::string& fallback) const { return loc_.trOr(key, fallback); }

    // ---- screens
    void ensureGfx(SDL_Renderer* r);
    void drawPick(Ui& ui, Out& out);
    void drawTimeline(Ui& ui, Out& out);
    void drawCredits(Ui& ui, SDL_Renderer* r, int dtMs);
    void drawNotice(Ui& ui);
    void drawCharArt(const std::string& key, int x, int y, int w, int h);

    // ---- cutscene runner
    bool startCutscene(const std::string& file);
    void endCutscene();
    void cutTick();
    void cutRun();
    void cutExec(const CutCmd& c);
    void cutMsg(const std::string& arg);
    void showText(const std::string& text);
    // ---- voices: story_vo/lang_<en|jp>/<key>_<EN|JP>.wav (core/story.h storyVoiceFiles); nothing mounted = silent
    void playVoice(const std::string& textKey, const std::string& explicitName);
    int loadVoice(const std::vector<std::string>& candidates);
    // Last resort when the exact path is not in the Vfs: matches the line by its letters and digits only, any folder, any case, with or
    // without the _EN / _JP tail (a pack that spells the name a little differently still plays).  `part` 0 = the whole line.
    std::string findVoiceFuzzy(const std::string& textKey, int part, const std::string& lang, const std::string& explicitName);
    std::string voiceFileFor(const std::string& textKey, const std::string& explicitName, const std::string& lang);   // "" = none
    int loadVoiceFuzzy(const std::string& textKey, int part, const std::string& lang, const std::string& explicitName);
    void voTick();
    void voStop() { voQueue_.clear(); voWait_ = 0; }
    void applyHeader(const std::string& arg);
    void applyPortrait(const std::string& arg);
    void startLegacy(const std::string& name);
    void releaseLegacy(bool toEnd);
    void dlgPlay(const char* name);
    void cutInput(Ui& ui);
    void drawCutscene(Ui& ui);
    void drawFlatDialogue(Ui& ui);
    void playCues(GbsAnimPlayer& p, const GbsScene* sc);
    std::string revealed() const;
    bool fullyRevealed() const;

    // ---- wiring
    const GameData* gd_ = nullptr;
    Audio* audio_ = nullptr;
    MusicPlayer* music_ = nullptr;
    int cueIdx_ = -1;                 // theme of the last MUSIC_xx cue of the cutscene (-1 = none / MUSIC_STOP)
    std::set<std::string> cueWarned_;  // cues with no file, reported once
    std::string prefsDir_, lang_ = "en";
    Localization loc_;
    ZgProgress prog_;
    GameUi gfx_;
    SDL_Renderer* gfxRen_ = nullptr;
    std::set<std::string> initLocked_;          // ProgressionInit.txt "StoryX = L" -> "x"
    std::vector<Entry> stories_;
    std::vector<StageEntry> stages_;
    std::unordered_map<std::string, int> sfx_;  // sound name -> Audio id (-2 = not found)
    std::unordered_map<std::string, std::string> voIndex_;   // letters+digits of a voice file's name -> its Vfs path
    bool voIndexBuilt_ = false;
    std::string auditMsg_;
    std::unordered_map<std::string, int> voCache_;   // voice file -> Audio id (-2 = not found)
    std::vector<int> voQueue_;                       // the next parts of a multi-file line, with the wait before the first of them
    int voWait_ = 0, voMisses_ = 0;
    bool scanned_ = false;
    bool storyAll_ = false;

    // ---- flow
    State state_ = State::Pick;
    bool fromPicker_ = false;   // the timeline was opened from this screen's own list (Back returns to it)
    int cur_ = -1;              // index into stories_
    size_t node_ = 0;
    bool pendingPlay_ = false;  // play node_ on the next frame
    bool fightPending_ = false;
    float scroll_ = 0;
    std::vector<std::string> notices_;
    int noticeMs_ = 0;
    int timeMs_ = 0;
    int creditsMs_ = 0;
    credits::CreditsScreen credits_;   // the roll + Thanks for playing at the end of a story (credits.cpp)

    // ---- cutscene state
    CutScript cut_;
    size_t pc_ = 0;
    bool cutDone_ = false;
    std::string shotScene_, dlgScene_;
    const GbsScene* shotSc_ = nullptr;
    const GbsScene* dlgSc_ = nullptr;
    const GbsSceneView* shotView_ = nullptr;
    const GbsSceneView* dlgView_ = nullptr;
    GbsAnimPlayer shotAnim_, dlgAnim_;
    bool dlgUi_ = false;        // the real dialogue overlay can be drawn (its textures are on disk)
    bool portraitsOk_ = false;  // Portrait lines name THs pictures (CutsceneCommon scripts; not the old <Char>ArcadeMode ones)
    std::unordered_map<std::string, std::string> shotTex_;  // sprite node -> picture
    std::string lastShot_;
    std::string portrait_[2];
    std::string header_[2];
    int speaker_ = -1;
    bool dlgOn_ = false, cineOn_ = false;
    std::string text_;
    int textTicks_ = 0;
    Wait wait_ = Wait::None;
    std::string waitAnim_;
    int waitTicks_ = 0;
    std::string legacyAnim_;
    std::vector<int> legacyCues_;
    size_t legacyNext_ = 0;
    bool legacyHeld_ = false;
    std::vector<std::pair<std::string, size_t>> choices_;  // decision text, index of its Decision line
    double acc_ = 0;
};

}  // namespace story
}  // namespace zengine
