// menu.h - the main menu: title screen -> root menu -> the game's own sub-menus.
//
// The structure is NOT hand-made: pages come from the user's own ui/config/MainMenu/*.ini and
// ui/config/Options/*.ini, and every label/description is looked up in core/localization/<lang>_loc.json
// (menu_data.h).  Entries whose action ZDroid does not implement yet are shown dimmed and say so.
//
//   implemented   Arcade Mode / CPU Quick Match (one match vs CPU), Local Versus, Training Room,
//                 Controls, Settings (language, difficulty, rounds, time limit), Exit
//   not yet       Story, Survival, Tutorials, Challenges, Trials, Extras, Movelist, online play
//
// The top-level list (Single Player / Versus / Training / Extras / Help & Options / Exit) follows the
// TopText of those .ini files and the localisation keys the game ships; the original's own list lives
// in UIMainMenuHandler, which is not decoded.
#pragma once
#include <SDL.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "attract.h"
#include "audio.h"
#include "charselect.h"
#include "colour_editor.h"
#include "confirm_dialog.h"
#include "fav_colours.h"
#include "game_ui.h"
#include "gamedata.h"
#include "gbs_anim.h"
#include "menu_list_anim.h"
#include "menu_data.h"
#include "modsdir.h"
#include "play.h"
#include "boot_splash.h"
#include "splash.h"
#include "ui.h"

namespace zengine {
namespace menu {

struct Result {
    enum class Kind { None, Play, Home, Quit, Story, Gallery, Credits, ReloadMods, Tutorials, Trials, Challenges, Fighters, PadEditor } kind = Kind::None;   // ReloadMods: Extras -> Mods -> Apply: the mods folder changed, rescan the data; Fighters: Extras -> Custom fight (fighter_pick.cpp)
    bool contributors = false;              // Kind::Credits: the backers' list instead of the credits
    play::Mode mode = play::Mode::VsCpu;  // for Kind::Play
    std::string storyChar;                  // Kind::Story: character id from the Character Select ("filia"), "" = the story screen's own list
    std::string p1, p2, stage, p1PalPath, p2PalPath;   // Kind::Play: what the Character Select picked ("" p2 = random)
};

class MenuScreen {
public:
    // Call after every (re)scan of the data folder.
    void setGameData(const GameData* gd, const Inventory* inv);
    // What the mods folder holds (Extras -> Mods) and where it lives.  The report is read, never written; the switches edit mods.ini
    // through modsdir::setItemEnabled and take effect when the data is rescanned (Result::Kind::ReloadMods).
    void setMods(const modsdir::Report* rep, const std::string& dataRoot);
    // Folder where menu_settings.txt lives; loads the saved settings.
    void setPrefsDir(const std::string& dir);
    // "en", "es", ... : overrides the Language setting (command line / tests).  Empty = use the setting.
    void setLanguageOverride(const std::string& code);
    // "STEAM" on desktop, "ANDROID" on the apk: drives AllowedPlatforms / BlockedPlatforms in the .ini files.
    void setPlatform(const std::string& p) { platform_ = p; }
    // Menu music + UI sounds (optional: without it the menu is silent).  Call before setGameData().
    void setAudio(Audio* a) { audio_ = a; }
    // The save's [options] changed behind the menu's back (the in-match Options page): read them again and re-apply the volumes.
    void reloadSettings() {
        loadSettings();
        applyAudio();
    }

    // One frame: input, logic, drawing.
    Result frame(Ui& ui, SDL_Renderer* r, int dtMs);
    // Android back / Escape. Returns false only on the title screen (nothing left to close).
    bool onBack();
    // Jump past the legal notice + title screen (smoke tests, "back to menu").
    void skipTitle() { state_ = State::Menu; }
    // captures / tests: open a page of the menu by file name ("options.ini", "zd_dev.ini") on top of the root
    void openPageForCapture(const std::string& file) { state_ = State::Menu; push(file); }
    // Accept the legal notice programmatically (smoke tests); the photosensitivity warning comes next (acceptNotice(true) skips it too).
    // No logo loader and no copyright / trademark line (the publisher marks are never shown): notice -> warning -> opening montage -> title -> menu.
    void acceptNotice(bool skipWarning = false) {
        if (state_ == State::Boot) {   // (async start-up: the splash is still up; the notice is accepted when it ends)
            bootAutoAccept_ = true;
            bootAutoSkipWarn_ = skipWarning;
        } else if (state_ == State::Disclaimer) {
            if (skipWarning) state_ = State::Menu;
            else showWarning();
        }
    }
    void showTitle() { showTitleImpl(); titleAnimOn_ = false; }
    // The photosensitivity warning (FrontCredits.gbs "PhotosensativeWarning"): a few seconds on black, any held button skips it.
    void showWarning();
    // The game's own loading screen (loading_<lang>.dds): shown after the legal notice, then the title follows.
    void showSplash();
    // The opening montage (AttractMode.gbs): shot `shot` from tick `tick` (smoke runs / --screen attract).  It plays by itself after
    // the loading screen; false = the user's data has no such scene.
    void showAttract(int shot = 0, int tick = 0);
    // The Character Select screen (Charselect.gbs).  Fight actions open it while fight mode is switched off; false = the
    // user's data has no such scene.
    bool showCharSelect(bool story = false, int firstStep = 0);   // firstStep 0 size / 1 fighter / 2 stage (smoke runs)
    // Same, but waits until the UI scenes are loaded (they are decoded on the first drawn frame): smoke runs / --screen.
    void requestCharSelectP2(const std::string& firstPick) {   // smoke runs: the second pass of the selector, with player 1 already on "firstPick"
        pick1_ = firstPick;
        pickingP2_ = twoPicks_ = true;
        requestCharSelect(false, 1);
    }
    void requestCharSelect(bool story = false, int firstStep = 0, int palette = 0, const std::string& pick = std::string()) { pendingCharSelect_ = true; pendingStory_ = story; pendingStep_ = firstStep; pendingPal_ = palette; pendingPick_ = pick; }
    void requestColourEditor(const std::string& who = std::string()) { pendingEdit_ = true; pendingEditWho_ = who; }   // smoke runs: open the colour editor on the character under the cursor as soon as the selector is up
    bool charSelectReady() const { return charSel_.ready(); }
    const CharSelect& charSelect() const { return charSel_; }
    // Start-up: the loader's own splash (its logo + a loader with four stages) is on screen while the game data is indexed on a
    // worker thread; setGameData() ends the loading state.  After it: the legal notice (only until it has been accepted once), the
    // photosensitivity warning, the menu.  setBootStage(): the stage the worker is in (1..3); the main thread does the fourth.
    void setLoading(bool on) {
        loading_ = on;
        if (on && state_ == State::Disclaimer) {
            state_ = State::Boot;
            bootT0_ = timeMs_;
            bootProg_ = 0;
            bootStage_ = 1;
            bootPrep_ = 0;
        }
    }
    void setBootStage(int stage) { bootStage_ = stage; }
    bool loading() const { return loading_; }

    play::Rules rules() const;

    // ---- exposed to tests / smoke
    const char* stateName() const;
    size_t depth() const { return stack_.size(); }
    std::string language() const { return langCode_; }
    size_t locSize() const { return loc_.size(); }
    std::string settingValue(const std::string& keyword) const;
    std::string pageTitle() const;

private:
    // Boot: logo + loader.  Disclaimer: the unofficial-project / own-your-copy notice, shown once (the first launch).
    enum class State { Boot, Disclaimer, Warning, Splash, Attract, Title, Menu, CharSelect };
    struct Page {
        MenuDef def;
        std::vector<int> vis;  // indices into def.options that are shown on this platform
        std::string file;
        int sel = 0;
        float scroll = 0;      // in rows
        bool dense = false;    // small rows (many settings) instead of big title-style rows
        bool root = false;
    };

    // ---- configuration
    const GameData* gd_ = nullptr;
    const Inventory* inv_ = nullptr;
    std::string platform_ = "STEAM";
    std::string prefsFile_;
    std::string langOverride_, langCode_ = "en";
    bool jpFallback_ = false;
    Localization loc_;
    std::map<std::string, MenuDef> defs_;  // by .ini name (cached)
    std::map<std::string, std::string> settings_;
    std::map<std::string, std::string> defaults_;
    // ---- Extras -> Mods
    const modsdir::Report* mods_ = nullptr;
    std::string modsRoot_;
    std::map<std::string, bool> modOn_;   // "kind:id" -> the switch as the player left it (the report is only rebuilt on a rescan)
    bool modsDirty_ = false;
    MenuDef modsPage(const std::string& file) const;
    bool modEnabled(const std::string& key) const;
    void toggleMod(const MenuOption& o);

    // ---- state
    State state_ = State::Disclaimer;
    int noticeSel_ = 0;  // 0 continue, 1 exit
    int splashT0_ = 0;   // timeMs_ when the loading screen started
    LoadingSplash splash_;
    bool splashTried_ = false;
    bool loading_ = false;
    BootLogo bootLogo_;
    int bootT0_ = 0;          // timeMs_ when the splash started
    float bootProg_ = 0;      // the bar, 0..1 (eases towards what the stages say)
    int bootStage_ = 0;       // the worker's stage
    bool bootAutoAccept_ = false, bootAutoSkipWarn_ = false;   // acceptNotice() while the splash was up
    int bootPrep_ = 0;        // 0 the data just arrived, 1 "preparing" was shown, 2 the interface is decoded
    void drawBoot(Ui& ui);
    void leaveBoot();
    bool noticeAccepted() const;
    void rememberNotice() const;
    std::vector<Page> stack_;
    bool confirmExit_ = false, controls_ = false;
    int modalSel_ = 1;  // confirm dialog: 0 yes, 1 no (the plain fallback panel; the game's own window keeps its own)
    ConfirmDialog dlg_;     // the game's "WARNING / Are you sure?" window (InGame.gbs DialogBox) for the exit question
    int dlgOk_ = -1;        // -1 not asked yet, 0 the data has no such window (plain panel), 1 usable
    bool useDialog();       // true when the exit question is the game's window (opens it on first use)
    int timeMs_ = 0;
    std::string toast_;
    int toastMs_ = 0;
    int lastMx_ = -1, lastMy_ = -1;
    int pressRow_ = -2;
    bool dragged_ = false;

    // ---- art (decoded from the user's data, lazily)
    SDL_Renderer* ren_ = nullptr;
    bool assetsDirty_ = true;
    play::Backdrop bg_;
    play::Skin showcase_;

    // ---- the game's own UI scenes (Main.gbs + textures + fonts from the user's data).  When they are not
    // there the menu falls back to ZDroid's own 8x8-font drawing above.
    GameUi gfx_;
    bool sceneOk_ = false;
    const GbsSceneView* vBg_ = nullptr;      // MainMenu_BG
    const GbsSceneView* vMenu_ = nullptr;    // MainMenu_New
    const GbsSceneView* vSplash_ = nullptr;  // Splash_2ND
    struct Slot { float x = 0, y = 0, w = 0, h = 0; bool ok = false; };
    Slot itemSlot_[11];                      // MenuItem0..MenuItem10 (canvas pixels, absolute)
    Slot bandSlot_;                          // the active-item header banner
    Slot hintSlot_;                          // Button_Label: the key / touch hint line at the bottom
    int hGrpId_ = -1, glowId_ = -1, activeId_ = -1, btnLabelId_ = -1;   // node ids in MainMenu_New whose pose the drawing reads
    float hGrpRestY_ = 255.0f;               // HorizontalGRP at rest (the film band): SubMenuIntro moves it up from here
    int lastSceneMs_ = 0, sceneDtMs_ = 16;   // frame time of the scene drawing
    int pageT0_ = 0;                         // timeMs_ when the page on top of the stack appeared (rows slide in)
    std::string pageKey_;                    // identity of that page (depth + file) to notice a change
    std::vector<float> rowShift_;            // per row: how far the selected row has slid right (0..kSelShift)
    int tapSplitX_ = -1;                     // logical x that splits "previous" / "next" on a tapped choice row (-1: row based)
    struct RowGeom { int vis; Rect r; };
    std::vector<RowGeom> rows_;              // hit areas of the page on screen (rebuilt every frame)
    Rect listBox_{0, 0, 0, 0};               // scrollable area (empty = the page does not scroll)
    int rowPx_ = 28;                         // row height in logical pixels (scrolling unit)
    int visibleRows_ = 1;

    // ---- audio (see audio.h): the theme (menu_intro.wav, then menu_loop.wav on repeat) and the game's UISound/*.wav cues
    Audio* audio_ = nullptr;
    int musicTrack_ = -1, selectTrack_ = -1;   // Audio track ids: menu theme / Character Select theme
    int sfxMove_ = -1, sfxEnter_ = -1, sfxBack_ = -1, sfxNope_ = -1, sfxStart_ = -1;
    void loadAudioAssets();
    void applyAudio();
    void sfx(int id) { if (audio_ && id >= 0) audio_->playSfx(id); }

    // ---- helpers
    std::string L(const std::string& key) const;          // localise (handles "@own:" keys)
    std::string own(const std::string& key) const;        // ZDroid's own strings, en/es
    const MenuDef& def(const std::string& file);
    Page makePage(const std::string& file);
    Page makeRoot();
    void push(const std::string& file);
    void rebuildStack();
    void loadLocalization();
    void loadSettings();
    void saveSettings() const;
    void ensureAssets(SDL_Renderer* r);
    void say(const std::string& msg);

    Result activate(Page& p, int visIdx, int tapX, const Rect& row);
    bool optionUsable(const MenuOption& o) const;
    // Description lines for the highlighted entry: {text, kind} kind 0 = normal, 1 = warning, 2 = dim.
    std::vector<std::pair<std::string, int>> descriptionLines(const Page& p) const;
    void layoutRows(Page& p);
    bool uiPackFonts() const;
    Result runAction(const MenuOption& o);
    void stepSetting(const MenuOption& o, int dir);
    bool settingImplemented(const std::string& keyword) const;
    static bool isFightAction(const std::string& keyword);  // Arcade / quick match / versus / training (see zd_features.h)
    bool actionImplemented(const std::string& keyword) const;
    int rowHeight(const Page& p) const { return p.dense ? 17 : 28; }

    Result handleInput(Ui& ui);
    void draw(Ui& ui, SDL_Renderer* r);
    void drawBackdrop(Ui& ui, SDL_Renderer* r);
    void drawDisclaimer(Ui& ui);
    void drawTitle(Ui& ui);
    void drawList(Ui& ui, Page& p);
    void drawDescription(Ui& ui, Page& p);
    void drawModals(Ui& ui);
    void drawShowcase(Ui& ui, SDL_Renderer* r);
    void showTitleImpl();
    void drawWarning(Ui& ui);
    void endWarning();

    // ---- scene mode (menu_scene.cpp)
    bool loadScenes(SDL_Renderer* r);
    void drawScene(Ui& ui, SDL_Renderer* r);
    void sceneBackground();
    void sceneTitle();
    void sceneRoot(Page& p);
    void scenePage(Page& p);
    // The command lists (movelists.ini, movelist_<Name>.ini) are not rows of the film-strip menu: the PC edition shows them in the
    // big dialog of Options.gbs "Movelist" (frame, winged title, six 72-px slots: move name over its command in button art).
    int dialogKind(const Page& p) const;   // 0 = film strip, 1 = command list, 2 = settings
    bool isDialogPage(const Page& p) const;
    void sceneDialog(Page& p);
    void sceneLayoutDialog(Page& p);
    void sceneModals(Ui& ui);
    void sceneLayoutRoot(Page& p);
    void sceneLayoutPage(Page& p);
    std::string headerTexture(const Page& root) const;
    CharSelect charSel_;
    bool pendingCharSelect_ = false, pendingStory_ = false;
    bool csStory_ = false;                       // the open Character Select belongs to Story Mode
    std::string prefsDir_;
    play::Mode fightMode_ = play::Mode::VsCpu;   // the fight action that opened the Character Select
    bool pickingP2_ = false;                     // the second pass of the selector is for P2
    bool twoPicks_ = false;                      // Quick Match / Local Versus / Training: P2 picks too (Arcade: the opponent is random)
    std::string pick1_, pickStage_, pickPal1_;   // P1's choice while P2 is still picking
    std::map<std::string, std::vector<CharSelect::PaletteChoice>> palChoices_;   // what the selector offers per character
    // ---- the player's own colours (hold the character in the selector): favourites live in <prefs>/favcolours, not in the mod folders
    ColourEditor colourEd_;
    coledit::Favourites favs_;
    std::map<std::string, std::vector<coledit::Fav>> favsByChar_;   // the favourites of each roster character, in the order they follow the other colours
    std::map<std::string, std::vector<CharSelect::PaletteChoice>> buildPaletteChoices();
    void openColourEditor(const std::string& name, int palIdx);
    void finishColourEdit(const ColourEditor::Result& r);
    int pendingStep_ = 0, pendingPal_ = 0;
    std::string pendingPick_;   // --cs-pick (smoke runs): the character the cursor starts on
    bool pendingEdit_ = false;
    std::string pendingEditWho_;

    // ---- the scenes' own animations (LAGG), played at 60 Hz whatever the frame rate is
    GbsAnimPlayer aBg_;       // MainMenu_BG: spotlights, the drifting city, the fade at the loop point
    GbsAnimPlayer aWarn_;
    GbsAnimPlayer aDlg_;        // Options.gbs "Movelist": the dialog's own intro
    std::string dlgKey_;        //   the page it was started for     // FrontCredits.gbs PhotosensativeWarning
    int warnT0_ = 0, warnHold_ = 0, warnLastMs_ = 0;
    bool warnBound_ = false;
    GbsAnimPlayer aSplash_;   // Splash_2ND: logo bounce, spinning shield, the PRESS ANY BUTTON blink
    MenuListAnim aMenu_;      // MainMenu_New: intro, list moves, band slide of the sub-pages (core/menu_list_anim.h)
    bool menuLive_ = false;   // aMenu_ was activated for the menu that is on screen (false on the title / in the selector)
    std::vector<char> rootUsable() const;           // per visible entry of the root list: can it be used (false = drawn dark)
    float bandDy() const;                           // how far the film band has moved from its rest position (from its pose)
    void stepRowShift(const Page& p);               // one 60 Hz tick of the sliding highlighted sub-page row (SubAdvSelect: 5 ticks)
    double animAcc_ = 0;      // milliseconds not yet turned into ticks
    bool titleAnimOn_ = false, pressOn_ = false;
    int stepAnims(int dtMs);                        // how many 60 Hz ticks are due now
    void playCues(GbsAnimPlayer& p);                // sound cues the player crossed
    std::unordered_map<uint32_t, int> cueSfx_;
    // ---- the opening montage (attract.h): loaded the first time it is needed; its music is optional (sg_attract_c*.wav)
    AttractScreen attract_;
    bool attractTried_ = false, attractOk_ = false, attractPending_ = false;
    int attractT0_ = 0;                             // timeMs_ when it started (input is ignored for a moment)
    int attractJumpShot_ = -1, attractJumpTick_ = 0;
    int attractTrack_ = -1;
    int frameDt_ = 16;
    std::unordered_map<std::string, int> sfxByName_;
    bool ensureAttract();
    void beginAttract();                            // after the photosensitivity warning: the opening montage if the scene is there, else the menu
    void stepAttract();      // scene sound id -> Audio sfx id (-2 = not loadable)
};

}  // namespace menu
}  // namespace zengine
