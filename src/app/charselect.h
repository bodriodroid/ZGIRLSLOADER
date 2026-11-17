// charselect.h - the Character Select flow, built from the game's own Charselect.gbs scene and its LAGG animations.
//
// Steps (as on the PC edition):  1. SELECT SIZE (team size 1 / 2 / 3 for both players)  ->  2. the fighter grid with the player's
// data bar and the name plate on the pedestal  ->  3. SELECT STAGE (the stage thumbnails from Textures/charselect/stages).
// Story mode has no size step and no stage step: the fighter pick ends the screen (one fighter, the story picks the stage).
//
// Everything on screen comes from the user's data: the background / filmstrip / pedestals / portrait / data bar are nodes of
// Charselect.gbs (CharSelectBackground_NEW, CharSelectFighters, CharSelectTeamSizeP1/P2, CharSelectPickFighterP1,
// CharSelectStage), the motion is the scene's own LAGG animations (OnActivate, Intro, Outro, TeamSizeNSelect, StageSelectOutro ...)
// played on a 60 Hz clock, the icons are Textures/charselect/charselect_select_icons_<name>0N.dds (01 normal, 02 highlighted,
// 03 greyed out = locked).
//
// What was read out of SkullGirls.exe (the constructor of the select screen, 0x5e6dce) and is reproduced here:
//   * the icon grid is 8 rows x 3 columns; each cell holds a roster index, -1 is the "Random!" cell
//   * cells whose roster slot does not exist (or has no art in the user's data) are simply left empty
//   * locked characters keep their portrait, get the greyed icon and a "LOCKED!" plate over the portrait
// The roster order (index -> character) was matched against the icons of a real screenshot.
//
// Not done yet: the character idle animation standing on the pedestal, assist / colour pages, a real second player.
#pragma once
#include <SDL.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "char_sprite.h"
#include "game_ui.h"
#include "menu_data.h"
#include "ui.h"

namespace zengine {
namespace menu {

class CharSelect {
public:
    static constexpr int kRoster = 18;  // characters the game knows (indices used by the cell table)
    static constexpr int kCells = 24;   // 8 rows x 3 columns

    enum class Step { Size, Fighter, Stage };

    struct Out {
        enum class Kind { None, Moved, Confirm, Locked, Back, Step, Edit } kind = Kind::None;   // Edit: the standing character was held (the colour editor)
        std::string name;   // lower-case character id ("filia"), "random" for the dice cell
        std::string stage;  // Confirm: stage id ("new_meridian"), "random", "" in story mode (the story picks it)
        int teamSize = 1;   // Confirm: 1..3 (1 in story mode)
        int palette = 0;            // Confirm: index into the character's palette list (0 = the game's 1P colours)
        std::string paletteLabel;   // "1p", "7p", "mod:<file name>" (mods/pal)
    };
    // A stage from mods/stages: thumbnail stage_<id>.png / .dds (found through the mounted folder), `name` shown on the bar.
    struct CustomStage {
        std::string id, name;
    };
    // Appended to the stage list after the game's own ones.
    void setCustomStages(std::vector<CustomStage> v) { custom_ = std::move(v); stagesDirty_ = true; }
    // One colour choice of a character: its palette texture (a real file path) and a short label.
    struct PaletteChoice {
        std::string label, path;
    };

    // Looks the views up (switches GameUi to Charselect.gbs and back to `restore`).  false = scene or art missing.
    bool load(GameUi& gfx, const std::string& restore);
    bool ready() const { return ready_; }

    // Characters whose Story entry in ProgressionInit.txt is 'L' (locked in a fresh save): lower-case ids.
    void setLocked(const std::set<std::string>& ids) { locked_ = ids; }
    // Reads "StoryXxx = L" lines of the game's ProgressionInit.txt.
    static std::set<std::string> parseLocked(const std::string& progressionText);

    // `story`: Story Mode (no size step, no stage step, one fighter).  `first` jumps over the earlier steps (smoke runs).
    // Language of the announcer voices ("jp" has its own recordings).
    void setLang(const std::string& lang) { jpVoice_ = lang == "jp"; }
    // The colours each character can wear, by lower-case id: the game's <name>_Np.dds followed by the baked mods/pal palettes.
    // Tab (or a tap on the standing character) steps through them on the fighter step; they colour the standing sprite.
    void setPalettes(std::map<std::string, std::vector<PaletteChoice>> p) { pals_ = std::move(p); palIdx_ = 0; }
    int paletteIndex() const { return palIdx_; }
    void setPaletteIndex(int i);   // smoke runs / tests (clamped to the character under the cursor)
    std::string paletteLabel() const;
    // Who is picking: 0 = player 1 (left pedestal and data bar), 1 = player 2 (the mirrored right side; player 1's pick stays on its pedestal).
    void setPlayer(int p) {
        player_ = p == 1 ? 1 : 0;
        if (p1View_) p1_.v = (player_ == 1 && p2View_) ? p2View_ : p1View_;
    }
    int player() const { return player_; }
    void setFirstPick(const std::string& name, const std::string& palPath) { firstPick_ = name; firstPal_ = palPath; }
    // Fighter step confirms without a stage step (player 2's pass: the stage was chosen in player 1's).
    void setSkipStage(bool on) { skipStage_ = on; }
    // Team size: only 1 for now (the 2 and 3 character modes are not built yet), so the size step is skipped.
    void setTeamSizeLocked(bool on) { sizeLocked_ = on; }
    void enter(int nowMs, bool story = false);
    void enterAt(int nowMs, bool story, Step first);
    Out update(const Input& in, int nowMs);
    // Draws the whole screen (canvas 1280x720 -> 640x360 logical).  `restore` is the scene to switch back to afterwards.
    void draw(GameUi& gfx, const Localization& loc, const std::string& lang, int nowMs, const std::string& restore);

    // Sounds the animations crossed since the last call: {"UISound/....wav", volume 0..1}.
    std::vector<std::pair<std::string, float>> takeSounds();

    // introspection (tests / smoke)
    int cursor() const { return cur_; }
    std::string cursorName() const;
    bool cursorLocked() const;
    int enabledCount() const;
    Step step() const { return step_; }
    bool story() const { return story_; }
    int teamSize() const { return teamSize_; }
    int stageCount() const { return static_cast<int>(stages_.size()); }
    std::string stageId() const;
    static const char* rosterName(int idx);
    static int cellRoster(int cell);

private:
    struct Layer {
        const GbsSceneView* v = nullptr;
        GbsAnimPlayer p;
    };
    struct StageInfo {
        std::string id;    // "new_meridian"; the first entry is "random"
        std::string tex;   // Textures/charselect/stages/stage_new_meridian.png
        std::string key;   // loc key of the name ("New_Meridian")
        std::string song;  // track name from music.ini ("" = none known)
    };

    bool ready_ = false;
    bool art_[kRoster] = {};
    std::set<std::string> locked_;
    int cur_ = 9, prev_ = 9;
    int moveMs_ = 0, enterMs_ = 0;
    std::string spriteIcon_[kCells];

    Step step_ = Step::Fighter;
    bool story_ = false;
    int teamSize_ = 3;
    int stageIdx_ = 0, prevStageIdx_ = 0, stageDir_ = 0;
    int stageMoveMs_ = -10000;
    std::string slotTex_[2];   // the two stage pictures of the background (Stage_01 / Stage_02) that cross-fade
    int slotUp_ = 0;           // which one is fully visible
    std::vector<StageInfo> stages_;
    std::vector<CustomStage> custom_;
    bool stagesDirty_ = false;

    // geometry read from the scene (canvas pixels, absolute)
    float gridX_ = 533.1f, gridY_ = 101.0f, stepX_ = 71.0f, stepY_ = 71.0f, cellW_ = 68.0f, cellH_ = 70.0f;
    float selX_ = 515.5f, selY_ = 86.3f, selW_ = 96.0f, selRestX_ = 644.0f, selRestY_ = -155.5f;
    float cancelX_ = 40.0f, cancelY_ = 668.0f;

    const GbsScene* scene_ = nullptr;
    Layer bg_, fight_, p1_, size1_, size2_, stage_;
    Layer fade_;   // the scene's "Fade" view: a black sheet, FadeIn (5 ticks) when the selector opens
    const GbsSceneView* p1View_ = nullptr;
    const GbsSceneView* p2View_ = nullptr;
    int player_ = 0;
    std::string firstPick_, firstPal_;
    bool skipStage_ = false, sizeLocked_ = true;
    std::string pfx() const { return player_ == 1 ? "P2_" : "P1_"; }
    CharSprites sprites_;      // the characters standing on the pedestals
    std::map<std::string, std::vector<PaletteChoice>> pals_;
    int palIdx_ = 0;           // colour of the character under the cursor (reset when the cursor moves)
    int holdMs_ = -1;          // when the press on the standing character began (-1 = none); held kHoldMs without moving = the colour editor
    bool holdFired_ = false;   // the hold already opened the editor (the release that follows is not a tap)
    static constexpr int kHoldMs = 450;
    Rect spriteHit_{0, 0, 0, 0};   // where the standing character was drawn last frame (logical pixels): a tap there changes colour
    bool stepPalette(int dir);
    std::string palPath() const;
    bool jpVoice_ = false;
    double acc_ = 0;
    int lastMs_ = -1;
    std::vector<std::pair<std::string, float>> sounds_;

    bool cellOk(int cell) const;
    bool lockedAt(int cell) const;
    std::string nameAt(int cell) const;
    bool stepCursor(int dCol, int dRow);
    Rect cellRect(int cell) const;  // logical pixels
    void tickClock(int nowMs);
    void startLayers(bool first);
    void setSize(int n, bool announce);
    void toSize();
    void toFighter();
    void toStage();
    void showStage(bool switching);
    void moveStage(int dir, int nowMs);
    void buildStages(GameUi& gfx);
    static Rect chipRect(int player, int n);  // logical pixels
};

}  // namespace menu
}  // namespace zengine
