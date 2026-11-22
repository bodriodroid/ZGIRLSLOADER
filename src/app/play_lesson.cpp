// play_lesson.cpp - Tutorials, Trials and Challenges on the Play screen (see play.h "Lessons").
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "play.h"

namespace zengine {
namespace play {

void PlayScreen::enterLesson(SDL_Renderer* r, const LessonRun& run) {
    if (r) ren_ = r;
    lesson_.reset();
    lessonHud_.reset();
    lessonMode_ = true;
    lessonRun_ = run;
    lessonOutcome_ = LessonOutcome::None;
    lessonDoneTicks_ = 0;
    lessonPadPrev_ = 0;
    storyMode_ = false;
    arcadeMode_ = false;
    storyOutcome_ = StoryOutcome::None;
    exitToMenu_ = false;
    lastWinner_ = -1;
    training_ = run.training;
    cpu_[0] = false;
    cpu_[1] = true;   // the second pad is the script's (update() cuts the built-in CPU while a lesson runs)
    difficulty_ = 1;
    roundsToWin_ = 1;
    timeLimitSec_ = std::max(0, run.timeLimitSec);
    const int a = findFighter(run.p1);
    if (a >= 0) pick_[0] = a;
    const int b = findFighter(run.p2);
    if (b >= 0) pick_[1] = b;
    if (a < 0) std::printf("[lesson] fighter '%s' is not in the mounted data\n", run.p1.c_str());
    if (b < 0) std::printf("[lesson] fighter '%s' is not in the mounted data\n", run.p2.c_str());
    palPath_[0].clear();
    palPath_[1].clear();
    stageAsked_ = run.level;
    stageUnresolved_ = false;
    if (!run.level.empty()) {
        const int li = levelIndexFor(run.level);
        if (li >= 0) stageIdx_ = li;
        else {
            stageUnresolved_ = true;
            if (!levels_.empty()) stageIdx_ = randomLevelIndex();
            std::printf("[lesson] stage '%s' matches none of the %zu .lvl files\n", run.level.c_str(), levels_.size());
        }
    } else if (!levels_.empty()) {
        stageIdx_ = randomLevelIndex();
    }
    startMatch();
}

void PlayScreen::leaveLesson() {
    job_.reset();
    lesson_.reset();
    lessonHud_.reset();
    overlay_ = Overlay::None;
    lessonMode_ = false;
    lessonOutcome_ = LessonOutcome::None;
    lessonDoneTicks_ = 0;
    training_ = false;
    lastWinner_ = -1;
    phase_ = Phase::Select;
    wins_[0] = wins_[1] = 0;
    round_ = 1;
}

void PlayScreen::beginLessonScript() {
    lesson_.reset();
    lessonHud_.reset();
    lessonHud_.setTranslator([this](const std::string& key) {
        (void)tr("Back", "Back");   // the strings load on the first lookup
        return loc_.has(key) ? loc_.tr(key) : std::string();
    });
    if (!lessonMode_ || !foit_ || !gd_ || lessonRun_.script.empty()) return;
    auto lesson = std::make_unique<script::Lesson>();
    const Vfs& vfs = gd_->vfs();
    auto read = [&vfs](const std::string& name, std::string& out) {
        if (vfs.readText(name, out)) return true;
        return false;
    };
    if (!lesson->begin(foit_.get(), &lessonHud_, read, lessonRun_.script, lessonRun_.level_ai)) {
        std::printf("[lesson] %s did not load: %s\n", lessonRun_.script.c_str(), lesson->host().lastError().c_str());
        return;
    }
    lesson_ = std::move(lesson);
}

void PlayScreen::lessonTick(uint16_t p1) {
    if (!lessonMode_) return;
    lessonHud_.tick();
    if (lessonHud_.explaining()) {   // any new attack press turns the page
        constexpr uint16_t kAtk = fight::kLP | fight::kMP | fight::kHP | fight::kLK | fight::kMK | fight::kHK;
        if ((p1 & kAtk & ~lessonPadPrev_) != 0) lessonHud_.pressAny();
        else if (std::getenv("ZDROID_LESSON_AUTOPAGE")) lessonHud_.pressAny();   // headless smoke runs: turn the pages by themselves
    }
    lessonPadPrev_ = p1;
    if (lessonDoneTicks_ == 0 && lesson_ && (lessonHud_.missionDone() || lesson_->host().missionComplete())) lessonDoneTicks_ = 1;
    if (lessonDoneTicks_ > 0 && ++lessonDoneTicks_ > 200 && lessonOutcome_ == LessonOutcome::None) lessonOutcome_ = LessonOutcome::Done;
}

void PlayScreen::drawLessonHud(Ui& ui) {
    if (!lessonMode_) return;
    lessonHud_.draw(ui, 52);
    if (lessonDoneTicks_ > 0) {
        std::string t = tr("Complete", "COMPLETE!");
        for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        ui.fill(Rect{0, 150, 640, 50}, Col{0, 0, 0, 170});
        ui.text(320 - Ui::textW(t, 3) / 2, 164, t, Col{110, 220, 120, 255}, 3);
    }
}

}  // namespace play
}  // namespace zengine
