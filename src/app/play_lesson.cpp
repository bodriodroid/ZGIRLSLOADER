// play_lesson.cpp - Tutorials, Trials and Challenges on the Play screen (see play.h "Lessons").
#include <algorithm>
#include <cctype>
#include <cmath>
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

float PlayScreen::lessonPlayerLife() const { return foit_ ? foit_->lifeFraction(0) : 1.0f; }

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
    if (lessonRun_.hasRules) lesson->setRules(lessonRun_.rules);
    lesson_ = std::move(lesson);
}

void PlayScreen::lessonTick(uint16_t p1) {
    if (!lessonMode_) return;
    lessonHud_.tick();
    if (lessonView_) lessonAnim_.tick();
    if (lessonHud_.explaining()) {   // any new attack press turns the page
        constexpr uint16_t kAtk = fight::kLP | fight::kMP | fight::kHP | fight::kLK | fight::kMK | fight::kHK;
        if ((p1 & kAtk & ~lessonPadPrev_) != 0) lessonHud_.pressAny();
        else if (std::getenv("ZDROID_LESSON_AUTOPAGE")) lessonHud_.pressAny();   // headless smoke runs: turn the pages by themselves
    }
    lessonPadPrev_ = p1;
    if (lessonDoneTicks_ == 0 && lesson_ && (lessonHud_.missionDone() || lesson_->host().missionComplete())) lessonDoneTicks_ = 1;
    if (lessonDoneTicks_ > 0 && ++lessonDoneTicks_ > 200 && lessonOutcome_ == LessonOutcome::None) lessonOutcome_ = LessonOutcome::Done;
}

// The objective list, the hint panel and the explanation pages in the game's own ObjectiveList view: the list slides in from the right
// (Intro), the hint panel from the left (ShowDetails), the explanation fades in over the middle (ShowExplanation); a finished objective gets
// the hand-drawn strike-through.  The animations only matter for where they END, so they are held on their last tick.
void PlayScreen::drawLessonScene(Ui& ui, const GbsSceneView& v) {
    if (lessonView_ != &v) {
        lessonView_ = &v;
        lessonAnim_.bind(&v);
        lessonAnim_.play("OnActivate");
        lessonAnim_.tick();
        lessonAnim_.tick();
    }
    if (lessonHud_.fadeAmount() > 0.001f)
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, static_cast<uint8_t>(std::lround(255.0f * lessonHud_.fadeAmount()))});

    const std::vector<LessonHud::RowText> rows = lessonHud_.visibleRows();
    const bool expl = lessonHud_.explanationShown(), det = lessonHud_.detailsShown();
    const bool list = !rows.empty() && !expl;
    auto hold = [&](const char* anim, bool on) {
        if (!on) { lessonAnim_.stop(anim); return; }
        const GbsAnim* a = v.anim(anim);
        if (a) lessonAnim_.seek(anim, std::max(0, a->length - 1));
    };
    hold("Intro", list);
    hold("ShowDetails", det);
    hold("ShowExplanation", expl);

    hud_.setViewport(0, 0, 0.5f);
    auto hook = [&](const GbsNode& n, NodeState& st) {
        const std::string& nm = n.name;
        if (nm == "StrikeOutGroup" || nm == "InputDisplayGroup" || nm == "PlacingHelperTop" || nm == "PlacingHelperBottom") { st.hide = true; return; }
        if (nm == "ListHolder") { if (!list) st.hide = true; return; }
        if (nm == "DetailsGroup") { if (!det) st.hide = true; return; }
        if (nm == "ExplanationGroup") { if (!expl) st.hide = true; return; }
        if (nm == "DetailsLabel") {
            st.hasText = true;
            st.text = det ? lessonHud_.detailsText() : std::string();
            // the scene keys the label higher than the frame it belongs to (the code of the game lays it out): keep it inside the frame
            if (const GbsNode* bk = v.find("DetailsBKGRP")) {
                st.forceBox = true;
                st.forceX = bk->x + 34; st.forceY = bk->y + 30;
                st.forceW = bk->w - 68 + 64; st.forceH = bk->h - 60;
            }
            return;
        }
        if (nm == "ExplanationLabel") { st.hasText = true; st.text = expl ? lessonHud_.explanationText() : std::string(); return; }
    };
    hud_.setAnimPlayer(&lessonAnim_);
    hud_.drawView(v, hook);
    hud_.setAnimPlayer(nullptr);

    // the rows go where the scene's two placing helpers say: the first at the top helper, the next ones one helper-step lower
    const GbsNode* top = v.find("PlacingHelperTop");
    const GbsNode* bot = v.find("PlacingHelperBottom");
    const GbsScene* sc = hud_.scene();
    if (list && top && sc) {
        float lx = 655, ly = 166;   // the list's resting place after Intro
        if (const GbsAnim* a = v.anim("Intro"))
            for (const GbsChannel& ch : a->channels)
                if (!ch.keys.empty() && v.find("ListHolder") && ch.nodeId == v.find("ListHolder")->id) { lx = ch.keys.back().x; ly = ch.keys.back().y; }
        const float step = bot ? std::max(1.0f, bot->y - top->y) : 38.0f;
        const GbsFont* f = sc->fontById(top->fontId);
        const GbsNode* strike = v.find("strike");
        for (size_t i = 0; i < rows.size(); ++i) {
            const float rx = lx + top->x, ry = ly + top->y + step * static_cast<float>(i);
            if (f) hud_.drawLabelText(*f, rows[i].text, rx, ry, 500.0f, top->h, top->hAlign, true,
                                      rows[i].done ? Col{150, 150, 150, 255} : Col{255, 255, 255, 255}, top->scaleX, 1.0f);
            if (rows[i].done && strike) {
                GbsNode sk = *strike;
                sk.x = rx; sk.y = ry + strike->y; sk.kids.clear();
                std::vector<GbsNode> one{sk};
                hud_.drawNodes(one, 0, 0, [](const GbsNode&, NodeState&) {});
            }
        }
    }
    // "press any button" under an explanation page
    if (expl && lessonHud_.promptVisible()) {
        const GbsNode* label = v.find("ExplanationLabel");
        const GbsFont* f = label && sc ? sc->fontById(label->fontId) : nullptr;
        if (f) {
            const std::string p = lessonHud_.promptText();
            const float w = hud_.textWidth(*f, p, 0.6f);
            hud_.drawLine(*f, p, 640.0f - w / 2, 650.0f, Col{200, 200, 200, 255}, 0.6f);
        }
    }
}

void PlayScreen::drawLessonHud(Ui& ui) {
    if (!lessonMode_) return;
    const GbsSceneView* ov = hudOk_ && hud_.scene() ? hud_.scene()->view("ObjectiveList") : nullptr;
    if (ov) drawLessonScene(ui, *ov);   // the game's own layout (InGame.gbs); without it the plain panels below
    else lessonHud_.draw(ui, 52);
    if (lessonDoneTicks_ > 0) {
        std::string t = tr("Complete", "COMPLETE!");
        for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        ui.fill(Rect{0, 150, 640, 50}, Col{0, 0, 0, 170});
        ui.text(320 - Ui::textW(t, 3) / 2, 164, t, Col{110, 220, 120, 255}, 3);
    }
}

}  // namespace play
}  // namespace zengine
