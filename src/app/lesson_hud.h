// lesson_hud.h - the lesson's own HUD: the objective list, the "press any button" explanation pages and the hint line.
//
// It is the UiSink the Lua host (core/lua_host.h) talks to: the lesson script says `ui.add(key, progress, max)`, `ui.setexplanation(key)`,
// `ui.showexplanation(true)` ... and this keeps that state and draws it over the fight.  The texts are localisation keys (the game's
// <lang>_loc.json); `setTranslator` gives it the lookup.  No SDL types leak out of draw().
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "lua_host.h"

namespace zengine {

class Ui;

class LessonHud : public script::UiSink {
public:
    // key -> text; an empty string = the key is not translated (the key itself is shown).
    void setTranslator(std::function<std::string(const std::string&)> tr) { tr_ = std::move(tr); }
    void reset();

    // ---- script side (UiSink)
    void loadCatalog(const std::string&) override {}
    void clearAll() override;
    void setMax(int rows) override { maxRows_ = rows < 1 ? 1 : rows; }
    void add(const std::string& key, int progress, int maxProgress) override;
    void update(int index, const std::string& key, int progress, int maxProgress) override;
    void complete(int index, bool done) override;
    void scrollTo(int index) override { first_ = index > 0 ? index - 1 : 0; }
    void setExplanation(const std::string& key) override { explKey_ = key; }
    void showExplanation(bool on) override;
    bool explanationActive() override { return explActive_; }
    void setDetails(const std::string& key) override { detailsKey_ = key; }
    void showDetails(bool on) override { detailsOn_ = on; }
    void screenFade(float amount, int dir, float alpha) override;
    void endMission() override { missionDone_ = true; }
    void setSlowMoPct(float pct) override { slowMo_ = pct; }

    // ---- app side
    // The player pressed a button while a page waits: it goes away (the script sees explanationActive() false).  Ignored for the first
    // few ticks of a page so that the press that started the lesson (or the last page) does not dismiss it at once.
    void pressAny();
    bool explaining() const { return explActive_; }
    bool missionDone() const { return missionDone_; }
    float slowMo() const { return slowMo_; }
    // objectives done / total (for the result line)
    int rows() const { return static_cast<int>(rows_.size()); }
    int doneRows() const;
    // One tick of the HUD's own clock (blinking prompt, page age, fade).
    void tick();
    // Draws over a 640x360 picture.  `below` = first y free of the match HUD (the life bars above it).
    void draw(Ui& ui, int below);

    // text of a key as the screen shows it (translated, placeholders filled); exposed for tests
    std::string text(const std::string& key, int progress = 0, int maxProgress = 0) const;

private:
    struct Row {
        std::string key;
        int progress = 0, max = 0;
        bool done = false;
    };
    std::function<std::string(const std::string&)> tr_;
    std::vector<Row> rows_;
    int maxRows_ = 5, first_ = 0;
    std::string explKey_, detailsKey_;
    bool explActive_ = false, detailsOn_ = false, missionDone_ = false;
    int explAge_ = 0, clock_ = 0;
    float slowMo_ = 0;
    float fade_ = 0;   // 0..1 black over the picture
};

// Words of `s` laid out in lines of at most `cols` characters ('\n' breaks a line, an empty line is kept).
std::vector<std::string> wrapLines(const std::string& s, int cols);

}  // namespace zengine
