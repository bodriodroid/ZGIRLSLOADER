// lessons_screen.h - ZGirls Loader: Training -> Tutorials / Trials / Challenges, listed from the user's own game data.
//
//   Tutorials   training/trainingTOC.ini -> <Chapter>.stm -> one lesson (a fight .ini + a Lua script) per StoryNode
//   Trials      training/trialsTOC.ini   -> Trials_<Char>.stm -> one combo trial per StoryNode
//   Challenges  ui/config/MainMenu/challenges.ini -> rules for a fight against the CPU
// (core/lesson_data.h reads them, core/lesson.h + lua_host.h run the scripts, PlayScreen::enterLesson fights.)
//
// This screen only chooses: chapter -> lesson -> START.  It hands a PlayScreen::LessonRun to the viewer (Out::Kind::Start) and gets the
// result back through lessonResult().  A lesson that was completed is remembered in the progress file (zg_progress, "lesson.<ini>").
//
// Challenges are team fights in the game (up to three fighters a side, assists); the duel engine is one against one, so a challenge here is
// a row of single fights against the enemy team in order: the player's life carries over from one fight to the next, the challenge is won
// when the last enemy falls.  Assists, "AllowDamage" limits and the Drama gauge are not applied.
#pragma once
#include <SDL.h>

#include <string>
#include <vector>

#include "gamedata.h"
#include "lesson_data.h"
#include "menu_data.h"
#include "music_player.h"
#include "play.h"
#include "ui.h"
#include "zg_progress.h"

namespace zengine {
namespace lessons {

struct Out {
    enum class Kind { None, Start, Back } kind = Kind::None;
    play::PlayScreen::LessonRun run;   // Kind::Start
};

class LessonsScreen {
public:
    enum class Kind { Tutorials, Trials, Challenges };

    // Call after every (re)scan of the data folder.  `fighters` = the characters the data has (the free pick of a challenge).
    void setGameData(const GameData* gd, const std::vector<std::string>& fighters);
    void setPrefsDir(const std::string& dir) { prefsDir_ = dir; }
    void setMusic(MusicPlayer* m) { music_ = m; }   // the lists play the menu's theme
    void begin(Kind kind, const std::string& lang);
    Out frame(Ui& ui, SDL_Renderer* r, int dtMs);
    // The Play screen ended the lesson Out::Kind::Start asked for.  `done` = completed (false = given up / lost);
    // `playerLife` = the player's life left, 0..1 (a challenge carries it into the next fight).  The result may start the next fight of a challenge.
    Out lessonResult(bool done, float playerLife);
    // Android back / Escape: one level up (true) or nothing left to close (false = leave to the menu).
    bool onBack();
    Kind kind() const { return kind_; }
    // Opens chapter `i` of the list (smoke runs / tests); false = no such chapter.
    bool openChapter(int i);

    // ---- what the list shows (public for the tests)
    // The rules of challenge `c` for a fight against its enemy number `enemy` (0-based) with the given player fighter; `carry` < 0 = full life.
    static play::PlayScreen::LessonRun challengeRun(const Challenge& c, const std::string& player, int enemy, float carry);

private:
    enum class State { Chapters, Lessons, Challenges };
    void load();
    std::string tr(const std::string& key, const std::string& fallback = std::string()) const;
    std::string chapterTitle(const Chapter& ch) const;
    std::string lessonTitle(const Chapter& ch, size_t i) const;
    bool lessonDone(const Chapter& ch, size_t i) const;
    int doneIn(const Chapter& ch) const;
    void drawChapters(Ui& ui, Out& out);
    void drawLessons(Ui& ui, Out& out);
    void drawChallenges(Ui& ui, Out& out);
    bool startLesson(size_t chapter, size_t entry, Out& out);
    std::string playerFor(const Challenge& c) const;

    const GameData* gd_ = nullptr;
    MusicPlayer* music_ = nullptr;
    std::vector<std::string> fighters_;
    std::string prefsDir_, lang_ = "en";
    Kind kind_ = Kind::Tutorials;
    State state_ = State::Chapters;
    Localization loc_;
    ZgProgress prog_;
    bool loaded_ = false;
    std::vector<Chapter> chapters_;
    std::vector<Challenge> challenges_;
    int chapter_ = -1;
    int sel_ = 0;            // selected lesson / challenge
    float scroll_ = 0;
    int pick_ = 0;           // free pick of a challenge: index into fighters_
    // what Out::Kind::Start asked for last
    int runChapter_ = -1, runEntry_ = -1;
    int runChallenge_ = -1, runEnemy_ = 0;
    std::string runPlayer_;
    std::string note_;       // line under the list ("not done yet", "challenge complete")
};

}  // namespace lessons
}  // namespace zengine
