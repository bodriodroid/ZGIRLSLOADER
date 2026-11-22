// lessons_screen.cpp - see lessons_screen.h.
#include "lessons_screen.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>

#include "hud_art.h"
#include "lesson_hud.h"

namespace zengine {
namespace lessons {

namespace {

std::string upperStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string joinList(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

std::string modWord(const std::string& m) {
    if (m == "_REGEN_") return " regen";
    if (m == "_DRAIN_") return " drain";
    if (m == "_HOLD_") return " held";
    return "";
}

}  // namespace

void LessonsScreen::setGameData(const GameData* gd, const std::vector<std::string>& fighters) {
    gd_ = gd;
    fighters_ = fighters;
    loaded_ = false;
    chapters_.clear();
    challenges_.clear();
    loc_.clear();
    pick_ = 0;
}

std::string LessonsScreen::tr(const std::string& key, const std::string& fallback) const {
    if (!key.empty() && loc_.has(key)) {
        const std::string t = loc_.tr(key);
        if (!t.empty()) return t;
    }
    return fallback.empty() ? key : fallback;
}

void LessonsScreen::load() {
    loaded_ = true;
    chapters_.clear();
    challenges_.clear();
    loc_.clear();
    if (!gd_) return;
    const Vfs& vfs = gd_->vfs();
    std::string json;
    if (vfs.readText(lang_ + "_loc.json", json)) loc_.parseJson(json);
    else if (lang_ != "en" && vfs.readText("en_loc.json", json)) loc_.parseJson(json);
    if (!prefsDir_.empty()) prog_.load(prefsDir_);
    if (kind_ == Kind::Tutorials) chapters_ = loadChapters(vfs, "trainingTOC.ini");
    else if (kind_ == Kind::Trials) chapters_ = loadChapters(vfs, "trialsTOC.ini");
    else {
        std::string text;
        if (vfs.readText("challenges.ini", text)) challenges_ = parseChallenges(text);
    }
}

void LessonsScreen::begin(Kind kind, const std::string& lang) {
    kind_ = kind;
    lang_ = lang.empty() ? std::string("en") : lang;
    loaded_ = false;
    state_ = kind == Kind::Challenges ? State::Challenges : State::Chapters;
    chapter_ = -1;
    sel_ = 0;
    scroll_ = 0;
    note_.clear();
    runChapter_ = runEntry_ = runChallenge_ = -1;
    runEnemy_ = 0;
    gfxDirty_ = true;
}

bool LessonsScreen::openChapter(int i) {
    if (!loaded_) load();
    if (i < 0 || i >= static_cast<int>(chapters_.size())) return false;
    chapter_ = i;
    state_ = State::Lessons;
    sel_ = 0;
    scroll_ = 0;
    note_.clear();
    return true;
}

bool LessonsScreen::onBack() {
    if (state_ == State::Lessons) {
        state_ = State::Chapters;
        sel_ = 0;
        scroll_ = 0;
        note_.clear();
        return true;
    }
    return false;
}

std::string LessonsScreen::chapterTitle(const Chapter& ch) const {
    std::string cap = ch.name;
    if (!cap.empty()) cap[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cap[0])));
    for (const std::string& k : {ch.name, cap, ch.stem}) {
        if (!k.empty() && loc_.has(k) && !loc_.tr(k).empty()) return loc_.tr(k);
    }
    return ch.name.empty() ? ch.stem : ch.name;
}

std::string LessonsScreen::lessonTitle(const Chapter& ch, size_t i) const {
    const Entry& e = ch.entries[i];
    return tr(e.title, (kind_ == Kind::Trials ? "Trial " : "Lesson ") + std::to_string(i + 1));
}

bool LessonsScreen::lessonDone(const Chapter& ch, size_t i) const { return prog_.flag(ch.progressKey(ch.entries[i])); }

int LessonsScreen::doneIn(const Chapter& ch) const {
    int n = 0;
    for (size_t i = 0; i < ch.entries.size(); ++i) n += lessonDone(ch, i) ? 1 : 0;
    return n;
}

std::string LessonsScreen::playerFor(const Challenge& c) const {
    if (!c.freePick()) return c.playerTeam.front();
    if (fighters_.empty()) return "Filia";
    return fighters_[static_cast<size_t>(std::clamp(pick_, 0, static_cast<int>(fighters_.size()) - 1))];
}

play::PlayScreen::LessonRun LessonsScreen::challengeRun(const Challenge& c, const std::string& player, int enemy, float carry) {
    play::PlayScreen::LessonRun r;
    r.p1 = player;
    r.p2 = c.enemyTeam.empty() ? std::string("Random") : c.enemyTeam[static_cast<size_t>(std::clamp(enemy, 0, static_cast<int>(c.enemyTeam.size()) - 1))];
    r.level = c.world;
    r.script = c.aiFile;
    r.training = false;
    r.timeLimitSec = c.timeLimit;
    r.level_ai = c.aiLevel;
    r.hasRules = true;
    script::Rules& ru = r.rules;
    ru.locked[0] = c.playerLocked;
    ru.locked[1] = c.enemyLocked;
    ru.lifePct[0] = c.health.playerPct;
    ru.lifePct[1] = c.health.enemyPct;
    ru.lifeMod[0] = script::Rules::modOf(c.health.playerMod);
    ru.lifeMod[1] = script::Rules::modOf(c.health.enemyMod);
    ru.meterBars[0] = c.meter.playerPct;   // the meter column counts bars (0..5)
    ru.meterBars[1] = c.meter.enemyPct;
    ru.meterMod[0] = script::Rules::modOf(c.meter.playerMod);
    ru.meterMod[1] = script::Rules::modOf(c.meter.enemyMod);
    ru.lifeStart[0] = carry;
    return r;
}

// ------------------------------------------------------------------------------------------------ frame

Out LessonsScreen::frame(Ui& ui, SDL_Renderer* r, int dtMs) {
    Out out;
    if (!loaded_) load();
    if (r && (r != ren_ || gfxDirty_)) bindGfx(r);
    stepAnims(dtMs);
    if (music_) {
        const int idx = music_->lib() ? music_->lib()->menu() : -1;
        if (!music_->play(idx)) music_->stop();
    }
    if (tocView_ && state_ != State::Challenges && !chapters_.empty()) {
        drawTocGbs(ui, out);
        return out;
    }
    switch (state_) {
        case State::Chapters: drawChapters(ui, out); break;
        case State::Lessons: drawLessons(ui, out); break;
        case State::Challenges: drawChallenges(ui, out); break;
    }
    return out;
}

static std::string screenTitle(LessonsScreen::Kind k) {
    return k == LessonsScreen::Kind::Tutorials ? "TUTORIALS" : k == LessonsScreen::Kind::Trials ? "TRIALS" : "CHALLENGES";
}

void LessonsScreen::drawChapters(Ui& ui, Out& out) {
    ui.fill(Rect{0, 0, 640, 360}, kBg);
    ui.text(16, 12, upperStr(tr(kind_ == Kind::Tutorials ? "Tutorials" : "Trials", screenTitle(kind_))), kAccent, 2);
    if (ui.button(4601, Rect{512, 8, 120, 26}, "BACK")) out.kind = Out::Kind::Back;
    if (chapters_.empty()) {
        ui.text(16, 60, "Nothing to list in the mounted data.", kWarn);
        ui.text(16, 78, std::string(kind_ == Kind::Tutorials ? "trainingTOC.ini" : "trialsTOC.ini") + " and the .stm files it names are read from your own", kDim);
        ui.text(16, 92, "copy of the game (Home -> data folder).", kDim);
        return;
    }
    const int cols = kind_ == Kind::Tutorials ? 2 : 4, bw = kind_ == Kind::Tutorials ? 304 : 148, bh = 30;
    for (size_t i = 0; i < chapters_.size(); ++i) {
        const Chapter& ch = chapters_[i];
        const int c = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        const Rect b{16 + c * (bw + 8), 46 + row * (bh + 6), bw, bh};
        if (b.y + b.h > 330) break;
        const int done = doneIn(ch), total = static_cast<int>(ch.entries.size());
        std::string label = (kind_ == Kind::Tutorials ? std::to_string(i + 1) + ". " : std::string()) + upperStr(chapterTitle(ch));   // the game's four "Specials" chapters share one name
        const std::string cnt = " " + std::to_string(done) + "/" + std::to_string(total);
        label = Ui::fit(label, bw - 12 - Ui::textW(cnt)) + cnt;
        if (ui.button(4610 + static_cast<int>(i), b, label, true, done == total)) {
            chapter_ = static_cast<int>(i);
            state_ = State::Lessons;
            sel_ = 0;
            for (size_t k = 0; k < ch.entries.size(); ++k)
                if (!lessonDone(ch, k)) {
                    sel_ = static_cast<int>(k);
                    break;
                }
            scroll_ = static_cast<float>(std::max(0, sel_ - 5));
            note_.clear();
        }
    }
    ui.text(16, 344, "highlighted = every lesson done", kDim);
}

bool LessonsScreen::startLesson(size_t chapter, size_t entry, Out& out) {
    if (chapter >= chapters_.size() || entry >= chapters_[chapter].entries.size()) return false;
    const Entry& e = chapters_[chapter].entries[entry];
    std::string text;
    FightIni fi;
    if (!gd_ || !gd_->vfs().readText(e.file, text) || !parseFightIni(text, fi)) {
        note_ = "Cannot read " + e.file + " in the mounted data.";
        return false;
    }
    out.kind = Out::Kind::Start;
    out.run = play::PlayScreen::LessonRun();
    out.run.p1 = fi.fighter[0];
    out.run.p2 = fi.fighter[1];
    out.run.level = fi.world;
    out.run.script = fi.luaFile;
    out.run.training = fi.training;
    runChapter_ = static_cast<int>(chapter);
    runEntry_ = static_cast<int>(entry);
    runChallenge_ = -1;
    note_.clear();
    return true;
}

void LessonsScreen::drawLessons(Ui& ui, Out& out) {
    ui.fill(Rect{0, 0, 640, 360}, kBg);
    if (chapter_ < 0 || chapter_ >= static_cast<int>(chapters_.size())) {
        state_ = State::Chapters;
        return;
    }
    const Chapter& ch = chapters_[static_cast<size_t>(chapter_)];
    ui.text(16, 12, Ui::fit(upperStr(chapterTitle(ch)), 480), kAccent, 2);
    if (ui.button(4602, Rect{512, 8, 120, 26}, "BACK")) {
        onBack();
        return;
    }
    const int n = static_cast<int>(ch.entries.size());
    sel_ = std::clamp(sel_, 0, std::max(0, n - 1));
    ui.text(16, 40, std::to_string(doneIn(ch)) + " / " + std::to_string(n) + " done", kDim);

    // ---- list (drag / wheel to scroll)
    const int rowH = 24, top = 54, visible = 11, listW = 300;
    const float maxScroll = static_cast<float>(std::max(0, n - visible));
    if (ui.in.down && Rect{16, top, listW, visible * rowH}.contains(ui.in.pmx, ui.in.pmy)) scroll_ -= static_cast<float>(ui.in.dragDy) / rowH;
    if (ui.in.wheel) scroll_ -= static_cast<float>(ui.in.wheel);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
    const int first = static_cast<int>(scroll_);
    const bool dragging = ui.in.down && std::abs(ui.in.my - ui.in.pmy) > 8;
    for (int i = first; i < n && i < first + visible; ++i) {
        const Rect b{16, top + (i - first) * rowH, listW, rowH - 3};
        const bool done = lessonDone(ch, static_cast<size_t>(i));
        std::string label = lessonTitle(ch, static_cast<size_t>(i));
        label = Ui::fit(label, listW - 40) + (done ? "  OK" : "");
        if (ui.button(4700 + i, b, label, true, i == sel_) && !dragging) sel_ = i;
    }
    if (n > visible) {
        const int barH = visible * rowH - 3;
        const int knobH = std::max(16, barH * visible / n);
        const int knobY = top + static_cast<int>((barH - knobH) * (maxScroll > 0 ? scroll_ / maxScroll : 0));
        ui.fill(Rect{320, top, 4, barH}, kPanel);
        ui.fill(Rect{320, knobY, 4, knobH}, kLine);
    }
    if (ui.in.keyDown) sel_ = std::min(sel_ + 1, n - 1);
    if (ui.in.keyUp) sel_ = std::max(sel_ - 1, 0);

    // ---- the selected lesson
    ui.fill(Rect{336, 54, 296, 256}, kPanel);
    if (n > 0) {
        const Entry& e = ch.entries[static_cast<size_t>(sel_)];
        int ty = 62;
        for (const std::string& l : wrapLines(lessonTitle(ch, static_cast<size_t>(sel_)), 34)) {
            ui.text(344, ty, l, kText);
            ty += 10;
        }
        ty += 8;
        std::string body = e.text.empty() ? std::string() : lessons::formatText(tr(e.text, std::string()));
        if (body.empty()) body = "(no description in the game's texts)";
        const std::vector<std::string> ls = wrapLines(body, 34);
        for (size_t i = 0; i < ls.size() && i < 20; ++i) ui.text(344, ty + static_cast<int>(i) * 10, ls[i], kDim);
        const bool done = lessonDone(ch, static_cast<size_t>(sel_));
        if (done) ui.text(344, 292, "COMPLETED", kGood);
    }
    if (!note_.empty()) ui.text(16, 322, Ui::fit(note_, 610), kWarn);
    if ((ui.button(4800, Rect{336, 318, 296, 30}, "START", n > 0, true) || ui.in.enter) && n > 0) startLesson(static_cast<size_t>(chapter_), static_cast<size_t>(sel_), out);
}

void LessonsScreen::drawChallenges(Ui& ui, Out& out) {
    if (chalView_ && !challenges_.empty()) {
        drawChallengesGbs(ui, out);
        return;
    }
    ui.fill(Rect{0, 0, 640, 360}, kBg);
    ui.text(16, 12, upperStr(tr("Challenges", "CHALLENGES")), kAccent, 2);
    if (ui.button(4603, Rect{512, 8, 120, 26}, "BACK")) {
        out.kind = Out::Kind::Back;
        return;
    }
    const int n = static_cast<int>(challenges_.size());
    if (n == 0) {
        ui.text(16, 60, "challenges.ini is not in the mounted data.", kWarn);
        ui.text(16, 78, "It is read from your own copy of the game (ui/config/MainMenu).", kDim);
        return;
    }
    sel_ = std::clamp(sel_, 0, n - 1);
    const int rowH = 24, top = 40, visible = 11, listW = 300;
    const float maxScroll = static_cast<float>(std::max(0, n - visible));
    if (ui.in.down && Rect{16, top, listW, visible * rowH}.contains(ui.in.pmx, ui.in.pmy)) scroll_ -= static_cast<float>(ui.in.dragDy) / rowH;
    if (ui.in.wheel) scroll_ -= static_cast<float>(ui.in.wheel);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
    const int first = static_cast<int>(scroll_);
    const bool dragging = ui.in.down && std::abs(ui.in.my - ui.in.pmy) > 8;
    for (int i = first; i < n && i < first + visible; ++i) {
        const Challenge& c = challenges_[static_cast<size_t>(i)];
        const Rect b{16, top + (i - first) * rowH, listW, rowH - 3};
        const bool done = prog_.flag("lesson.challenge." + lowerStr(c.name));
        std::string label = std::to_string(i + 1) + ". " + tr(c.name, c.name);
        label = Ui::fit(label, listW - 40) + (done ? "  OK" : "");
        if (ui.button(4900 + i, b, label, true, i == sel_) && !dragging) sel_ = i;
    }
    if (n > visible) {
        const int barH = visible * rowH - 3;
        const int knobH = std::max(16, barH * visible / n);
        const int knobY = top + static_cast<int>((barH - knobH) * (maxScroll > 0 ? scroll_ / maxScroll : 0));
        ui.fill(Rect{320, top, 4, barH}, kPanel);
        ui.fill(Rect{320, knobY, 4, knobH}, kLine);
    }
    if (ui.in.keyDown) sel_ = std::min(sel_ + 1, n - 1);
    if (ui.in.keyUp) sel_ = std::max(sel_ - 1, 0);

    // ---- the selected challenge
    const Challenge& c = challenges_[static_cast<size_t>(sel_)];
    ui.fill(Rect{336, 40, 296, 270}, kPanel);
    int y = 48;
    auto line = [&](const std::string& s, Col col) {
        for (const std::string& l : wrapLines(s, 34)) {
            ui.text(344, y, l, col);
            y += 10;
        }
        y += 2;
    };
    line(tr(c.name, c.name), kText);
    if (c.freePick()) {
        ui.text(344, y + 2, "Fighter:", kDim);
        if (ui.button(5001, Rect{408, y - 2, 22, 20}, "<") && !fighters_.empty()) pick_ = (pick_ + static_cast<int>(fighters_.size()) - 1) % static_cast<int>(fighters_.size());
        ui.text(438, y + 2, Ui::fit(upperStr(playerFor(c)), 150), kText);
        if (ui.button(5002, Rect{600, y - 2, 22, 20}, ">") && !fighters_.empty()) pick_ = (pick_ + 1) % static_cast<int>(fighters_.size());
        y += 24;
    } else {
        line("Fighter: " + joinList(c.playerTeam), kDim);
    }
    line("Enemies: " + joinList(c.enemyTeam), kDim);
    line("Time: " + std::to_string(c.timeLimit) + "s per fight   Win: " + std::string(c.winCond == "_TIME_" ? "survive" : "KO"), kDim);
    line("Life: you " + std::to_string(c.health.playerPct) + "%" + modWord(c.health.playerMod) + ", enemy " + std::to_string(c.health.enemyPct) + "%" + modWord(c.health.enemyMod), kDim);
    line("Meter: you " + std::to_string(c.meter.playerPct) + ", enemy " + std::to_string(c.meter.enemyPct) + " bars", kDim);
    const std::vector<std::string> lockedMe = c.playerLocked, lockedEn = c.enemyLocked;
    const bool anyLock = (!lockedMe.empty() && lockedMe[0] != "None") || (!lockedEn.empty() && lockedEn[0] != "None");
    if (anyLock) line("Locked: you " + joinList(lockedMe) + " / enemy " + joinList(lockedEn), kWarn);
    if (!c.playerAllowDamage.empty() && c.playerAllowDamage[0] != "All") line("Your damage: " + joinList(c.playerAllowDamage) + " (not enforced yet)", kDim);
    if (c.enemyTeam.size() > 1) line("Team fights are played one enemy after another; your life carries over.", kDim);
    if (!note_.empty()) ui.text(16, 322, Ui::fit(note_, 310), kGood);
    if (ui.button(5100, Rect{336, 318, 296, 30}, "START", true, true) || ui.in.enter) {
        out.kind = Out::Kind::Start;
        out.run = challengeRun(c, playerFor(c), 0, -1.0f);
        runChallenge_ = sel_;
        runEnemy_ = 0;
        runPlayer_ = playerFor(c);
        runChapter_ = runEntry_ = -1;
        note_.clear();
    }
}

// ------------------------------------------------------------------------------------------------ the game's own scenes

namespace {

struct PivotScope {
    GameUi& ui;
    bool prev;
    PivotScope(GameUi& u, bool on) : ui(u), prev(u.otterPivot()) { ui.setOtterPivot(on); }
    ~PivotScope() { ui.setOtterPivot(prev); }
};

}  // namespace

void LessonsScreen::bindGfx(SDL_Renderer* r) {
    gfxDirty_ = false;
    ren_ = r;
    bgView_ = chalView_ = tocView_ = nullptr;
    bgAnim_.bind(nullptr);
    chalAnim_.bind(nullptr);
    tocAnim_.bind(nullptr);
    if (!gd_ || !r) return;
    gfx_.attach(r, &gd_->vfs());   // new renderer: the textures of the old one are gone
    gfxAttached_ = true;
    gfx_.setJapanese(lang_ == "jp" || lang_ == "ja");
    if (!gfx_.loadScene("Main.gbs") || !gfx_.scene()) return;
    const GbsScene* sc = gfx_.scene();
    bgView_ = sc->view("MainMenu_BG");
    chalView_ = sc->view("Challenges");
    tocView_ = sc->view("TrainingMode_TOC_New");
    if (bgView_) {
        bgAnim_.bind(bgView_);
        bgAnim_.play("Lights");
        bgAnim_.play("BG");
    }
    if (chalView_) {
        chalAnim_.bind(chalView_);
        chalAnim_.play("OnActivate");
        chalAnim_.play("Challenges_In");
        chalAnim_.play("Arrow_Left_Right_Animate");
        chalAnim_.play("Status_Animator");
        shownChal_ = sel_;
        pendChal_ = -1;
    }
    if (tocView_) {
        tocAnim_.bind(tocView_);
        tocAnim_.play("OnActivate");
    }
}

void LessonsScreen::stepAnims(int dtMs) {
    animAcc_ += std::clamp(dtMs, 0, 250);
    int n = 0;
    while (animAcc_ >= 1000.0 / 60.0 && n++ < 8) {
        animAcc_ -= 1000.0 / 60.0;
        if (bgView_) bgAnim_.tick();
        if (chalView_ && state_ == State::Challenges) chalAnim_.tick();
        if (tocView_ && state_ != State::Challenges) tocAnim_.tick();
    }
    if (animAcc_ >= 1000.0 / 60.0) animAcc_ = 0;
    bgAnim_.takeCues();
    chalAnim_.takeCues();
    tocAnim_.takeCues();
    if (pendChal_ >= 0 && (!chalView_ || chalAnim_.finished("Challenge_Data_Out"))) {   // the old data is out: show the new one
        sel_ = pendChal_;
        shownChal_ = pendChal_;
        pendChal_ = -1;
        ++andyLine_;
        if (chalView_) {
            chalAnim_.play("Challenge_Data_In");
            chalAnim_.play("Andy_Fade_In_TXT");
        }
    }
}

void LessonsScreen::drawBackdrop() {
    if (!bgView_) return;
    PivotScope ps(gfx_, true);
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.setAnimPlayer(&bgAnim_);
    gfx_.drawView(*bgView_, [](const GbsNode& n, NodeState& st) {
        if (n.name == "Globe_GRP" || n.name == "BG_Filmstrips_GRP") st.hide = true;   // the online lobby's pieces
    });
    gfx_.setAnimPlayer(nullptr);
}

void LessonsScreen::drawChallengesGbs(Ui& ui, Out& out) {
    const int n = static_cast<int>(challenges_.size());
    sel_ = std::clamp(sel_, 0, n - 1);
    if (shownChal_ < 0 || shownChal_ >= n) shownChal_ = sel_;
    const int shown = shownChal_;
    const Challenge& c = challenges_[static_cast<size_t>(shown)];
    const bool done = prog_.flag("lesson.challenge." + lowerStr(c.name));
    const std::string player = playerFor(c);

    auto names = [&](const std::vector<std::string>& v) {
        std::vector<std::string> o;
        for (const std::string& x : v) o.push_back(tr(x, hudart::displayName(x)));
        return o;
    };
    auto teamLine = [&](const char* base, const std::vector<std::string>& team) {
        const size_t k = std::min<size_t>(std::max<size_t>(team.size(), 1), 3);
        std::string t = tr(std::string(base) + ": {1}" + (k > 1 ? " {2}" : "") + (k > 2 ? " {3}" : ""), std::string(base) + ":");
        const std::vector<std::string> nm = names(team);
        for (size_t i = 0; i < k; ++i) {
            const std::string key = "{" + std::to_string(i + 1) + "}";
            const size_t at = t.find(key);
            if (at != std::string::npos) t.replace(at, key.size(), i < nm.size() ? nm[i] : std::string());
        }
        return t;
    };
    auto gauge = [&](const char* base, int v, const std::string& mod) {
        const std::string key = std::string(base) + "_" + std::to_string(v);
        std::string t = tr(key, std::to_string(v) + (std::string(base) == "Health" ? "%" : ""));
        if (mod == "_REGEN_") t += " - " + tr("Health_Regen", "Regenerates");
        else if (mod == "_DRAIN_") t += " - " + tr("Health_Drain", "Drains");
        else if (mod == "_HOLD_" && std::string(base) == "Meter") t = tr(key + "_Hold", t + " (Hold)");
        return t;
    };
    auto locked = [&](const std::vector<std::string>& v) {
        std::string t;
        for (const std::string& k : v) t += (t.empty() ? "" : ", ") + tr("Locked_" + k, k);
        return t.empty() ? tr("Locked_None", "None") : t;
    };
    auto damage = [&](const std::vector<std::string>& v) {
        std::string t;
        for (const std::string& k : v) t += (t.empty() ? "" : "\n") + tr("AllowedDmg_" + k, k);
        return t.empty() ? tr("AllowedDmg_All", "All") : t;
    };
    const std::string winWord = c.winCond == "_TIME_" ? tr("TIME", "Survive") : tr("KO", "Knockout");
    const std::string andy = tr(std::string(done ? "AndyCleared" : "AndyNew") + std::to_string(1 + (andyLine_ + shown) % 7), std::string());

    drawBackdrop();
    {
        PivotScope ps(gfx_, true);
        gfx_.setViewport(0, 0, 0.5f);
        gfx_.setAnimPlayer(&chalAnim_);
        gfx_.drawView(*chalView_, [&](const GbsNode& nd, NodeState& st) {
            const std::string& nm = nd.name;
            auto set = [&](const std::string& t) {
                st.hasText = true;
                st.text = t;
            };
            if (nm == "Header_TXT") set(upperStr(tr("Challenges", "Challenges")));
            else if (nm == "Challenge_Name_TXT") set(tr(c.name, c.name));
            else if (nm == "Challenger_Parameters_TXT") set(tr("TimeLimit", "Time Limit") + ": " + std::to_string(c.timeLimit) + "   " + winWord);
            else if (nm == "Player_Team_TXT") set(teamLine("PlayerTeam", c.freePick() ? std::vector<std::string>{player} : c.playerTeam));
            else if (nm == "Enemy_Team_TXT") set(teamLine("EnemyTeam", c.enemyTeam));
            else if (nm == "Header_Column_01") set(tr("PlayerColumn", "Player"));
            else if (nm == "Header_Column_02") set(tr("EnemyColumn", "Opponent"));
            else if (nm == "Health_Title_01") set(tr("Health", "Health"));
            else if (nm == "Meter_Title_01") set(tr("Meter", "Meter"));
            else if (nm == "Drama_Title_01") set(tr("Drama", "Drama"));
            else if (nm == "Locked_Title_01") set(tr("Locked", "Locked"));
            else if (nm == "Damage_Title_01") set(tr("AllowedDmg", "Damage"));
            else if (nm == "Health_Column_01") set(gauge("Health", c.health.playerPct, c.health.playerMod));
            else if (nm == "Health_Column_02") set(gauge("Health", c.health.enemyPct, c.health.enemyMod));
            else if (nm == "Meter_Column_01") set(gauge("Meter", c.meter.playerPct, c.meter.playerMod));
            else if (nm == "Meter_Column_02") set(gauge("Meter", c.meter.enemyPct, c.meter.enemyMod));
            else if (nm == "Drama_Column_01") set(c.drama.playerPct <= 0 ? tr("Meter_None", "None") : gauge("Meter", c.drama.playerPct, c.drama.playerMod));
            else if (nm == "Drama_Column_02") set(c.drama.enemyPct <= 0 ? tr("Meter_None", "None") : gauge("Meter", c.drama.enemyPct, c.drama.enemyMod));
            else if (nm == "Locked_Column_01") set(locked(c.playerLocked));
            else if (nm == "Locked_Column_02") set(locked(c.enemyLocked));
            else if (nm == "Damage_Column_01") set(damage(c.playerAllowDamage));
            else if (nm == "Damage_Column_02") set(damage(c.enemyAllowDamage));
            else if (nm == "Andy_Comments") set(andy);
            else if (nm == "Challenge_Number_TXT") set(std::to_string(shown + 1));
            else if (nm == "Status_Info") st.texName = done ? "Textures/Challenges/Status_Clear01.png" : "Textures/Challenges/Status_Doit01.png";
            else if (nm == "Side_Arrow_Bar_Left_GRP" && shown == 0) st.hide = true;
            else if (nm == "Side_Arrow_Bar_Right_GRP" && shown == n - 1) st.hide = true;
            else if (nm.rfind("JP_HELPER", 0) == 0) st.hide = true;
        });
        gfx_.setAnimPlayer(nullptr);
    }

    // ---- input: the side bars, the arrow keys, START / BACK
    auto go = [&](int d) {
        const int t = std::clamp(shown + d, 0, n - 1);
        if (pendChal_ >= 0 || t == shown) return;
        pendChal_ = t;
        if (chalView_) {
            chalAnim_.play("Challenge_Data_Out");
            chalAnim_.play("Andy_Fade_Out_TXT");
        }
    };
    if (ui.in.left || ui.in.keyUp) go(-1);
    if (ui.in.right || ui.in.keyDown) go(1);
    if (ui.in.released) {
        const Rect lz{0, 40, 56, 220}, rz{584, 40, 56, 220};
        if (lz.contains(ui.in.pmx, ui.in.pmy) && lz.contains(ui.in.mx, ui.in.my)) go(-1);
        if (rz.contains(ui.in.pmx, ui.in.pmy) && rz.contains(ui.in.mx, ui.in.my)) go(1);
    }
    if (ui.button(5003, Rect{8, 326, 96, 26}, "BACK")) out.kind = Out::Kind::Back;
    if (c.freePick()) {
        ui.text(118, 334, "FIGHTER", kDim);
        if (ui.button(5001, Rect{176, 326, 22, 26}, "<") && !fighters_.empty()) pick_ = (pick_ + static_cast<int>(fighters_.size()) - 1) % static_cast<int>(fighters_.size());
        ui.text(204, 334, Ui::fit(upperStr(player), 110), kText);
        if (ui.button(5002, Rect{322, 326, 22, 26}, ">") && !fighters_.empty()) pick_ = (pick_ + 1) % static_cast<int>(fighters_.size());
    }
    if (!note_.empty()) ui.text(354, 334, Ui::fit(note_, 150), kGood);
    if ((ui.button(5100, Rect{520, 326, 112, 26}, "START", pendChal_ < 0, true) || ui.in.enter) && pendChal_ < 0) {
        sel_ = shown;
        out.kind = Out::Kind::Start;
        out.run = challengeRun(c, player, 0, -1.0f);
        runChallenge_ = shown;
        runEnemy_ = 0;
        runPlayer_ = player;
        runChapter_ = runEntry_ = -1;
        note_.clear();
    }
}

namespace {
// where the chalkboard's column icons and lesson numbers sit on the 1280x720 canvas (the view's resting poses; the scene's own code fades them in)
constexpr float kColX[5] = {502.6f, 593.6f, 674.7f, 756.8f, 836.9f};
constexpr float kColY = 147.1f, kIcon = 86.0f;
constexpr float kRowY[4] = {231.1f, 313.1f, 397.1f, 484.1f};
}  // namespace

void LessonsScreen::drawTocGbs(Ui& ui, Out& out) {
    const int nCh = static_cast<int>(chapters_.size());
    chapter_ = std::clamp(chapter_, 0, nCh - 1);
    const Chapter& ch = chapters_[static_cast<size_t>(chapter_)];
    const int nL = static_cast<int>(ch.entries.size());
    sel_ = std::clamp(sel_, 0, std::max(0, nL - 1));
    if (sel_ < rowOff_) rowOff_ = sel_;
    if (sel_ >= rowOff_ + 4) rowOff_ = sel_ - 3;
    rowOff_ = std::clamp(rowOff_, 0, std::max(0, nL - 4));
    const bool trials = kind_ == Kind::Trials;

    drawBackdrop();
    {
        PivotScope ps(gfx_, true);
        gfx_.setViewport(0, 0, 0.5f);
        gfx_.setAnimPlayer(&tocAnim_);
        gfx_.drawView(*tocView_, [&](const GbsNode& nd, NodeState& st) {
            const std::string& nm = nd.name;
            auto set = [&](const std::string& t) {
                st.hasText = true;
                st.text = t;
            };
            if (nm.rfind("ColIcon0", 0) == 0 || nm.rfind("TutNum0", 0) == 0 || nm == "Selector" || nm == "TutHighlight01") st.ignorePoseAlpha = true;
            if (nm == "ChapterTitle") set(chapterTitle(ch));
            else if (nm == "TutTitle") set(nL ? lessonTitle(ch, static_cast<size_t>(sel_)) : std::string());
            else if (nm == "InfoDisplay") {
                std::string body = nL ? lessons::formatText(tr(ch.entries[static_cast<size_t>(sel_)].text, std::string())) : std::string();
                set(body);
            } else if (nm == "Header") st.texName = trials ? "Textures/Tutorials/trials_header01.png" : "Textures/Tutorials/tutorials_header01.png";
            else if (nm.rfind("ColIcon0", 0) == 0 && nm.size() == 9) {
                const int slot = nm[8] - '1';
                const int ci = chapter_ + slot;
                if (ci >= nCh) st.hide = true;
                else if (!chapters_[static_cast<size_t>(ci)].icon.empty()) st.texName = chapters_[static_cast<size_t>(ci)].icon;
            } else if (nm.rfind("TutNum0", 0) == 0 && nm.size() == 9) {
                const int row = nm[8] - '1';
                const int li = rowOff_ + row;
                if (li >= nL) st.hide = true;
                else {
                    const Entry& e = ch.entries[static_cast<size_t>(li)];
                    const std::string& img = lessonDone(ch, static_cast<size_t>(li)) && !e.doneIcon.empty() ? e.doneIcon : e.icon;
                    if (!img.empty()) st.texName = img;
                }
            } else if (nm == "TutHighlight01") {
                st.dy = kRowY[std::clamp(sel_ - rowOff_, 0, 3)] - kRowY[0];
                if (nL == 0) st.hide = true;
            } else if (nm == "ArrowGrp") {
                if (rowOff_ + 4 >= nL) st.hide = true;          // nothing more below
            } else if (nm == "SideArrow") {
                if (chapter_ + 5 >= nCh) st.hide = true;        // no more chapters to the right
            }
        });
        gfx_.setAnimPlayer(nullptr);
    }
    // ---- input.  Taps on the chapter icons / lesson numbers, <  > for the chapters, the arrow keys, wheel / drag for the rows
    auto toChapter = [&](int c) {
        c = std::clamp(c, 0, nCh - 1);
        if (c == chapter_) return;
        chapter_ = c;
        sel_ = rowOff_ = 0;
        const Chapter& nc = chapters_[static_cast<size_t>(chapter_)];
        for (size_t k = 0; k < nc.entries.size(); ++k)
            if (!lessonDone(nc, k)) {   // the first lesson that is not done yet
                sel_ = static_cast<int>(k);
                break;
            }
        note_.clear();
    };
    bool tapped = false;
    if (ui.in.released) {
        const int mx = ui.in.mx, my = ui.in.my;
        const bool still = std::abs(mx - ui.in.pmx) < 8 && std::abs(my - ui.in.pmy) < 8;
        if (still) {
            for (int k = 1; k < 5 && !tapped; ++k) {
                const Rect rc{static_cast<int>(kColX[k] / 2), static_cast<int>(kColY / 2), 43, 43};
                if (chapter_ + k < nCh && rc.contains(mx, my)) {
                    toChapter(chapter_ + k);
                    tapped = true;
                }
            }
            for (int k = 0; k < 4 && !tapped; ++k) {
                const Rect rc{static_cast<int>(kColX[0] / 2), static_cast<int>(kRowY[k] / 2), 43, 43};
                if (rowOff_ + k < nL && rc.contains(mx, my)) {
                    if (sel_ == rowOff_ + k) startLesson(static_cast<size_t>(chapter_), static_cast<size_t>(sel_), out);   // a second tap starts it
                    else sel_ = rowOff_ + k;
                    tapped = true;
                }
            }
        }
    }
    if (ui.in.wheel) rowOff_ = std::clamp(rowOff_ - ui.in.wheel, 0, std::max(0, nL - 4));
    if (chapter_ > 0 && ui.button(5203, Rect{232, 78, 18, 34}, "<")) toChapter(chapter_ - 1);
    if (chapter_ + 1 < nCh && ui.button(5204, Rect{440, 78, 18, 34}, ">")) toChapter(chapter_ + 1);
    if (ui.button(5201, Rect{8, 326, 96, 26}, "BACK")) out.kind = Out::Kind::Back;
    if (!note_.empty()) ui.text(118, 334, Ui::fit(note_, 380), kGood);
    if (out.kind == Out::Kind::None &&
        (ui.button(5202, Rect{520, 326, 112, 26}, "START", nL > 0, true) || (ui.in.enter && nL > 0)))
        startLesson(static_cast<size_t>(chapter_), static_cast<size_t>(sel_), out);
    if (ui.in.keyDown) sel_ = std::min(sel_ + 1, nL - 1);
    if (ui.in.keyUp) sel_ = std::max(sel_ - 1, 0);
    if (ui.in.right) toChapter(chapter_ + 1);
    if (ui.in.left) toChapter(chapter_ - 1);
}

// ------------------------------------------------------------------------------------------------ result

Out LessonsScreen::lessonResult(bool done, float playerLife) {
    Out out;
    if (!loaded_) load();
    if (runChallenge_ >= 0 && runChallenge_ < static_cast<int>(challenges_.size())) {
        const Challenge& c = challenges_[static_cast<size_t>(runChallenge_)];
        if (done) {
            if (runEnemy_ + 1 < static_cast<int>(c.enemyTeam.size())) {
                ++runEnemy_;
                out.kind = Out::Kind::Start;
                out.run = challengeRun(c, runPlayer_, runEnemy_, std::clamp(playerLife, 0.05f, 1.0f));
                return out;
            }
            prog_.setFlag("lesson.challenge." + lowerStr(c.name));
            note_ = "Challenge complete!";
        } else {
            note_ = "Challenge not completed.";
        }
        runChallenge_ = -1;
        return out;
    }
    if (runChapter_ >= 0 && runChapter_ < static_cast<int>(chapters_.size())) {
        const Chapter& ch = chapters_[static_cast<size_t>(runChapter_)];
        if (runEntry_ >= 0 && runEntry_ < static_cast<int>(ch.entries.size())) {
            if (done) {
                prog_.setFlag(ch.progressKey(ch.entries[static_cast<size_t>(runEntry_)]));
                note_ = "Completed!";
                if (runEntry_ + 1 < static_cast<int>(ch.entries.size())) sel_ = runEntry_ + 1;   // the next one is waiting
            } else {
                note_.clear();
            }
        }
        runChapter_ = runEntry_ = -1;
    }
    return out;
}

}  // namespace lessons
}  // namespace zengine
