// fighter_pick.h - Extras -> Custom fight: a plain screen where any fighter of the mounted data (the game's own and the ones a mod adds, such as a
// character made with zdroid_mugen) is picked with < > buttons, with its colour, the mode and the stage, and the match starts from there.
//
// The game's own Character Select is a fixed grid copied from the original scene (18 cells), so a character that only exists in a mod has no
// cell in it; this screen is the way to choose one.  It needs no scene file: the list is the inventory's characters, the preview is the
// character's own "Stand" pose from its .foit / .art / .art-pt coloured with the chosen palette.
#pragma once
#include <SDL.h>

#include <string>
#include <utility>
#include <vector>

#include "char_sprite.h"
#include "game_ui.h"
#include "gamedata.h"
#include "play.h"
#include "ui.h"

namespace zengine {
namespace fighters {

struct Out {
    enum class Kind { None, Back, Fight } kind = Kind::None;
    play::Mode mode = play::Mode::VsCpu;
    std::string p1, p2, stage, p1Pal, p2Pal;   // what PlayScreen::enterMatch takes ("" palette = the character's own first colour)
};

class FighterPick {
public:
    FighterPick() = default;
    ~FighterPick() { close(); }
    FighterPick(const FighterPick&) = delete;
    FighterPick& operator=(const FighterPick&) = delete;

    // Call after every (re)scan of the data folder.  The two picks are kept by name across calls.
    void setGameData(const GameData* gd, const Inventory* inv);
    void begin(const std::string& lang);
    Out frame(Ui& ui, SDL_Renderer* r, int dtMs);
    void close();   // destroys the preview textures (the renderer must still be alive)

    // What is listed / picked: for tests and the smoke run.
    size_t count() const { return list_.size(); }
    size_t customCount() const;
    const std::string& pickName(int side) const;
    // true for a fighter that the base game has (everything else comes from a mod).
    static bool isBaseGameName(const std::string& name);

private:
    struct Fighter {
        std::string name;
        bool custom = false;
        std::string foit, art, artPt;
        std::vector<std::pair<std::string, std::string>> pals;   // {label, real path of the palette texture}
    };
    struct Side {
        int idx = 0, pal = 0;
        menu::CharSpriteEdit prev;
        int openedIdx = -1;      // the fighter whose pose `prev` holds
        bool dirty = true;
        bool previewOk = false;
    };

    bool es() const { return lang_ == "es"; }
    const char* tr(const char* en, const char* es_) const { return es() ? es_ : en; }
    void step(int side, int dir);
    void stepPal(int side, int dir);
    void refreshPreview(int side);
    std::string palLabel(const Fighter& f, int pal) const;
    void drawCard(Ui& ui, int side, Rect card);

    const GameData* gd_ = nullptr;
    const Inventory* inv_ = nullptr;
    std::vector<Fighter> list_;
    std::vector<std::string> stages_;   // "random" + every .lvl by name (empty = no stage data: the match builds its own)
    Side side_[2];
    int stageIdx_ = 0;
    int modeIdx_ = 1;   // 0 training, 1 vs CPU, 2 two players
    std::string lang_ = "en";
    GameUi gfx_;
    bool gfxReady_ = false;
    std::string keepName_[2];
};

}  // namespace fighters
}  // namespace zengine
