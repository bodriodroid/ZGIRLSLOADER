// play_menu.cpp - the overlay menus of a match, built from the game's own scenes:
//
//   Pause        InGame.gbs "PauseOptions": dim, the ornate dialog frame, the "PLAYER 1" plate, the winged title and the
//                menu rows (Resume Game / Help & Options... / Command Lists... / Leaderboards / Achievements... / Exit Game)
//   End of match InGame.gbs "Continue Service" banner ("Encore Performance?", checkers and wings) over
//                InGame.gbs "EndMatchOptions" (Play Again / Change Characters / Change Stage / Return to Main Menu)
//   Options      InGame.gbs "MediumOptions1P": "Help & Options" of the pause menu - Controls (the Help sheet), the three
//                volumes (the same [options] keys as the main menu) and the touch pad's size / opacity
//   Help         Help.gbs "Help" (the controls sheet), opened from the Options page
//
// The scene files hold the frame, plate, title and wings at their final size; the rows are what the game's code adds
// (a copy of the "ChoiceGRP" template per slot), so they are drawn here: selector bar (Textures/Options/selector01/02),
// "Dialogue Text" label.  Labels come from the game's localisation (ResumeGame, Again, ChangeStage ...).  Nothing from
// the game is bundled: every texture and font is read from the user's mounted UI pack.
#include "play.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "audio.h"
#include "dev_options.h"
#include "savini.h"

namespace zengine {
namespace play {

namespace {
std::string upperText(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}
constexpr float kCanvas = 0.5f;   // 1280x720 canvas -> 640x360 logical
constexpr int kRowH = 38, kRowPitch = 39;
}  // namespace

void PlayScreen::setLanguage(const std::string& lang) {
    lang_ = lang.empty() ? std::string("en") : lang;
    locTried_ = false;
}

std::string PlayScreen::tr(const char* key, const char* fallback) {
    if (!locTried_ && gd_) {
        locTried_ = true;
        loc_.clear();
        std::string json;
        if (gd_->vfs().readText(lang_ + "_loc.json", json) || (lang_ != "en" && gd_->vfs().readText("en_loc.json", json))) loc_.parseJson(json);
    }
    return loc_.trOr(key, fallback);
}

void PlayScreen::openOverlay(Overlay o) {
    overlay_ = o;
    menuSel_ = 0;
    overlayT_ = 0;
    csBound_ = false;
    toast_.clear();
}

// One menu row: selector bar when highlighted + label.  `w` = row width in canvas pixels.
void PlayScreen::drawMenuRow(float x, float y, float w, const std::string& label, bool sel, bool enabled) {
    if (sel) {
        int sw = 0, sh = 0;
        SDL_Texture* cap = hud_.texture("Textures/Options/selector01.png", &sw, &sh);
        SDL_Texture* mid = hud_.texture("Textures/Options/selector02.png");
        if (cap && mid) {
            hud_.drawTexture(cap, x, y, 5, kRowH);
            hud_.drawTexture(mid, x + 5, y, w - 10, kRowH);
            hud_.drawTexture(cap, x + w - 5, y, 5, kRowH, 0, 0, 0, true, false);
        } else {
            hud_.fillRect(x, y, w, kRowH, Col{40, 6, 10, 235});
            hud_.fillRect(x, y, w, 2, Col{190, 20, 30, 255});
            hud_.fillRect(x, y + kRowH - 2, w, 2, Col{190, 20, 30, 255});
        }
    }
    if (const GbsFont* f = hud_.fontNamed("Dialogue Text")) {
        const Col c = !enabled ? Col{120, 118, 112, 255} : (sel ? Col{255, 255, 255, 255} : Col{226, 222, 212, 255});
        hud_.drawLine(*f, label, x + 12, y + 3, c, 1.0f);
    }
}

// Runs the menu logic for `n` rows whose top-left canvas corner is (x, y): keyboard / tap input, returns the row that was
// activated (-1 = none yet).  Disabled rows can be highlighted but do nothing (a toast says why).
int PlayScreen::menuInput(Ui& ui, int n, float x, float y, float w, const std::vector<char>& enabled) {
    if (n <= 0) return -1;
    menuSel_ = std::clamp(menuSel_, 0, n - 1);
    if (confirm_.isOpen()) return -1;   // the question on top has the input
    if (ui.in.keyDown) menuSel_ = (menuSel_ + 1) % n;
    if (ui.in.keyUp) menuSel_ = (menuSel_ + n - 1) % n;
    int act = -1;
    if (ui.in.enter) act = menuSel_;
    const auto rowAt = [&](int px, int py) {
        for (int i = 0; i < n; ++i) {
            const Rect r{static_cast<int>((x - 10) * kCanvas), static_cast<int>((y + i * kRowPitch) * kCanvas),
                         static_cast<int>((w + 20) * kCanvas), static_cast<int>(kRowPitch * kCanvas)};
            if (r.contains(px, py)) return i;
        }
        return -1;
    };
    if (ui.in.pressed) {
        const int r = rowAt(ui.in.mx, ui.in.my);
        if (r >= 0) menuSel_ = r;
    }
    if (ui.in.released && overlayT_ > 8) {
        const int r0 = rowAt(ui.in.pmx, ui.in.pmy), r1 = rowAt(ui.in.mx, ui.in.my);
        if (r0 >= 0 && r0 == r1) act = r0;
    }
    if (act >= 0 && !enabled[static_cast<size_t>(act)]) {
        showToast(tr("NotAvailableOffline", "Not available in this version"));
        act = -1;
    }
    return act;
}

// The pause menu (InGame.gbs "PauseOptions").
void PlayScreen::drawPause(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    const GbsSceneView* v = hudOk_ && hud_.scene() ? hud_.scene()->view("PauseOptions") : nullptr;
    const std::vector<std::string> rows = {tr("ResumeGame", "Resume Game"), tr("Options", "Help & Options") + "...",
                                           std::string("Command Lists..."), tr("Leaderboards", "Leaderboards"),
                                           tr("Achievements", "Achievements") + "...", tr("Exit", "Exit Game")};
    const std::vector<char> enabled = {1, 1, 1, 0, 0, 1};   // Command Lists works (the P1 character's list; none = a toast)
    // frame + plate + title + wings: the scene (final size), with our texts
    float fx = 354, fy = 184, rowX = 388, rowY = 258, rowW = 507;
    if (v) {
        hud_.setViewport(0, 0, kCanvas);
        const std::string title = upperText(tr("Pause", "Pause"));
        hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
            if (n.name == "BGFade") st.fallback = Col{0, 0, 0, 160};
            else if (n.name == "P2Menu" || n.name == "ChoiceGRP" || n.name == "AdvanceGRP" || n.name == "ConfirmGRP" ||
                     n.name == "SlotGRP" || n.name == "ArrowGroup" || n.name == "ActionFrame" || n.name == "Copy of ActionFrame")
                st.hide = true;
            else if (n.name == "Header_Text" || n.name == "Player1Label") { st.hasText = true; st.text = "PLAYER 1"; }
            else if (n.name == "P1TitleTxt") st.hide = true;   // drawn below: one line, fitted between the wings
        });
        if (const GbsFont* f = hud_.fontNamed("Big Options w Wings")) {
            const std::string t = "PLAYER 1 " + title;
            const float sc = 0.44f, w = hud_.textWidth(*f, t, sc);
            hud_.drawLine(*f, t, 642 - w / 2, 233 - f->lineHeight * sc / 2, Col{255, 255, 255, 255}, sc);
        }
    } else {
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 170});
        ui.fill(Rect{177, 92, 288, 202}, Col{10, 8, 12, 245});
        ui.outline(Rect{177, 92, 288, 202}, Col{190, 120, 90, 255});
    }
    (void)fx; (void)fy;
    const int act = menuInput(ui, static_cast<int>(rows.size()), rowX, rowY, rowW, enabled);
    if (hudOk_)
        for (size_t i = 0; i < rows.size(); ++i)
            drawMenuRow(rowX, rowY + static_cast<float>(i) * kRowPitch, rowW, rows[i], static_cast<int>(i) == menuSel_, enabled[i] != 0);
    else
        for (size_t i = 0; i < rows.size(); ++i)
            ui.text(190, 108 + static_cast<int>(i) * 20, (static_cast<int>(i) == menuSel_ ? "> " : "  ") + rows[i], enabled[i] ? kText : kDim);
    if (act == 0) overlay_ = Overlay::None;
    else if (act == 1) openOverlay(Overlay::Options);
    else if (act == 2) {
        if (loadMovelist()) openOverlay(Overlay::Movelist);
        else showToast(tr("NotAvailableOffline", "Not available in this version"));
    } else if (act == 5) askConfirm(ConfirmAct::ExitMatch);
}

// ---- Command Lists (Options.gbs "Movelist") ---------------------------------------------------------------------------------
namespace {
std::string alnumLower(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
}  // namespace

bool PlayScreen::loadMovelist() {
    mlRows_.clear();
    if (!gd_) return false;
    (void)tr("Back", "Back");   // (the strings load on the first lookup)
    std::string txt;
    if (!gd_->vfs().readText("movelists.ini", txt)) return false;
    const MenuDef index = parseMenuIni(txt);
    const std::string want = alnumLower(playerName(0));
    if (want.empty()) return false;
    std::string id;
    for (const MenuOption& o : index.options) {   // the list's names are the character ids: Filia, BlackDahlia, RoboFortune ...
        if (o.keyword != "ACT_MOVELIST") continue;
        const std::string have = alnumLower(o.text);
        if (have == want) { id = o.text; break; }
        if (id.empty() && (have.find(want) != std::string::npos || want.find(have) != std::string::npos) && have != "universal") id = o.text;
    }
    if (id.empty() || !gd_->vfs().readText("movelist_" + id + ".ini", txt)) return false;
    const MenuDef def = parseMenuIni(txt);
    for (const MenuOption& o : def.options) {
        if (!o.isMove) continue;
        MovelistRow r;
        r.name = loc_.tr(o.text);
        if (o.hasDescription()) r.command = loc_.tr(o.description);
        if (r.command == o.description) r.command.clear();   // not in the strings: no raw key on screen
        r.kind = o.moveKind;
        r.indent = o.indent;
        mlRows_.push_back(std::move(r));
    }
    mlTitle_ = loc_.tr(def.topText.empty() ? id : def.topText);
    mlSel_ = 0;
    mlScroll_ = 0;
    mlKey_.clear();
    return !mlRows_.empty();
}

void PlayScreen::drawMovelist(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    const int n = static_cast<int>(mlRows_.size());
    if (n == 0) { openOverlay(Overlay::Pause); menuSel_ = 2; return; }
    if (ui.in.keyDown) mlSel_ = std::min(n - 1, mlSel_ + 1);
    if (ui.in.keyUp) mlSel_ = std::max(0, mlSel_ - 1);
    mlSel_ = std::clamp(mlSel_, 0, n - 1);
    mlScroll_ -= static_cast<float>(ui.in.wheel);
    // touch: a tap on a slot selects it, a drag scrolls, a tap on the strip under the list goes back
    const auto slotAt = [&](int px, int py) {
        const float cy = static_cast<float>(py) / kCanvas, cx = static_cast<float>(px) / kCanvas;
        if (cx < kMovelistSlotX || cx > kMovelistSlotX + kMovelistSlotW) return -1;
        const float rel = (cy - kMovelistSlotY) / kMovelistSlotStep;
        if (rel < 0 || rel >= kMovelistSlots) return -1;
        const int k = static_cast<int>(std::floor(rel + mlScroll_));
        return k >= 0 && k < n ? k : -1;
    };
    static bool dragged = false;
    if (ui.in.pressed) dragged = false;
    if (ui.in.down && n > kMovelistSlots && std::abs(ui.in.my - ui.in.pmy) > 8) dragged = true;
    if (ui.in.down && dragged && n > kMovelistSlots) mlScroll_ -= static_cast<float>(ui.in.dragDy) / (kMovelistSlotStep * kCanvas);
    if (ui.in.released && overlayT_ > 8 && !dragged) {
        const int k0 = slotAt(ui.in.pmx, ui.in.pmy), k1 = slotAt(ui.in.mx, ui.in.my);
        if (k0 >= 0 && k0 == k1) mlSel_ = k0;
        else if (static_cast<float>(ui.in.my) / kCanvas > kMovelistSlotY + kMovelistSlotStep * kMovelistSlots + 6.0f) { openOverlay(Overlay::Pause); menuSel_ = 2; return; }
    }
    if (mlSel_ < mlScroll_) mlScroll_ = static_cast<float>(mlSel_);
    if (mlSel_ > mlScroll_ + kMovelistSlots - 1) mlScroll_ = static_cast<float>(mlSel_ - kMovelistSlots + 1);
    mlScroll_ = std::clamp(mlScroll_, 0.0f, static_cast<float>(std::max(0, n - kMovelistSlots)));
    bool ok = false;
    if (hudOk_) {
        hud_.setViewport(0, 0, kCanvas);
        mlAnim_.tick();
        ok = drawMovelistDialog(hud_, mlAnim_, mlKey_, "ml", mlRows_, mlSel_, std::round(mlScroll_), mlTitle_, overlayT_ * 16);
        if (ok) {   // the way back, on the strip under the list
            if (const GbsFont* f = hud_.fontNamed("Dialogue Text")) {
                const std::string t = tr("Back", "Back");
                const float sc = 0.8f, w = hud_.textWidth(*f, t, sc);
                hud_.drawLine(*f, t, 640.0f - w / 2, 636.0f, Col{226, 222, 212, 255}, sc);
            }
        }
        hud_.loadScene("InGame.gbs");   // back to the scene the HUD / menus read their fonts and textures from
    }
    if (!ok) {   // the scene is not in the data: a plain list
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 200});
        ui.text(20, 14, mlTitle_, kAccent, 2);
        for (int i = 0; i < kMovelistSlots && static_cast<int>(mlScroll_) + i < n; ++i) {
            const int k = static_cast<int>(mlScroll_) + i;
            ui.text(24, 44 + i * 44, (k == mlSel_ ? "> " : "  ") + mlRows_[static_cast<size_t>(k)].name, k == mlSel_ ? kText : kDim);
            ui.text(40, 56 + i * 44, commandToText(mlRows_[static_cast<size_t>(k)].command), kDim);
        }
    }
}

// "ARCADE MODE  3/9" (or "FINAL BOSS") small, centred under the round clock.
void PlayScreen::drawArcadeTag(Ui& ui, SDL_Renderer* r) {
    (void)ui;
    hudInit(r);
    const GbsFont* f = hudOk_ ? hud_.fontNamed("Dialogue Text") : nullptr;
    if (!f || !ladder_.active()) return;
    std::string t;
    if (ladder_.atBoss()) {
        t = lang_ == "es" ? "JEFE FINAL" : "FINAL BOSS";
    } else {
        t = upperText(tr("Arcade Mode", "ARCADE MODE")) + "  " + std::to_string(ladder_.index() + 1) + " / " + std::to_string(ladder_.total());
    }
    hud_.setViewport(0, 0, kCanvas);
    const float sc = 0.5f, w = hud_.textWidth(*f, t, sc), x = 640.0f - w / 2, y = 122.0f;
    hud_.drawLine(*f, t, x + 1.5f, y + 1.5f, Col{0, 0, 0, 255}, sc, 0.8f);
    hud_.drawLine(*f, t, x, y, ladder_.atBoss() ? Col{255, 120, 100, 255} : Col{236, 214, 168, 255}, sc);
}

// ---- "WARNING - Are you sure?" -------------------------------------------------------------------------------------------
void PlayScreen::askConfirm(ConfirmAct act) {
    confirmAct_ = act;
    ConfirmDialog::Text t;
    t.plate = upperText(tr("Player 1", "PLAYER 1"));
    t.title = tr("Warningtitle", "WARNING");
    t.message = tr("Confirmation", "Are you sure? Any unsaved progress will be lost.");
    t.yes = tr("Yes", "Yes");
    t.no = tr("No", "No");
    confirm_.open(t, true);
}

void PlayScreen::doConfirmed(ConfirmAct act) {
    switch (act) {
        case ConfirmAct::ExitMatch:
            overlay_ = Overlay::None;
            if (storyMode_) storyOutcome_ = StoryOutcome::Quit;
            else leaveMatch();
            break;
        case ConfirmAct::ReturnToMenu: overlay_ = Overlay::None; leaveMatch(); break;
        case ConfirmAct::GiveUp: openOverlay(Overlay::GameOver); break;
    }
}

// Drawn after the menu that asked; input is read here so a tap on "Yes" never also hits a row underneath.
void PlayScreen::drawConfirm(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    if (hudOk_ && confirm_.draw(hud_, "InGame.gbs")) {
        const ConfirmDialog::Answer a = confirm_.input(ui.in);
        if (a == ConfirmDialog::Answer::Yes) { confirm_.close(); doConfirmed(confirmAct_); }
        else if (a == ConfirmDialog::Answer::No) confirm_.close();
        return;
    }
    // no DialogBox in this data: a plain panel (Enter / Y = yes, Back / N = no)
    ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 170});
    ui.fill(Rect{150, 120, 340, 110}, Col{10, 8, 12, 245});
    ui.outline(Rect{150, 120, 340, 110}, Col{190, 120, 90, 255});
    const std::string msg = tr("Confirmation", "Are you sure? Any unsaved progress will be lost.");
    ui.text(160, 136, Ui::fit(msg, 320), kText);
    const Rect yes{190, 180, 110, 28}, no{340, 180, 110, 28};
    const int sel = confirm_.selection();
    ui.fill(yes, sel == 0 ? kAccent : kPanel2);
    ui.fill(no, sel == 1 ? kAccent : kPanel2);
    ui.textIn(yes, tr("Yes", "Yes"), kText, true);
    ui.textIn(no, tr("No", "No"), kText, true);
    ConfirmDialog::Answer a = confirm_.input(ui.in);   // keys; the panel has its own tap areas
    const auto tapped = [&](const Rect& b) { return ui.in.released && b.contains(ui.in.mx, ui.in.my) && b.contains(ui.in.pmx, ui.in.pmy); };
    if (tapped(yes)) a = ConfirmDialog::Answer::Yes;
    else if (tapped(no)) a = ConfirmDialog::Answer::No;
    if (a == ConfirmDialog::Answer::Yes) { confirm_.close(); doConfirmed(confirmAct_); }
    else if (a == ConfirmDialog::Answer::No) confirm_.close();
}

// ---- Options page --------------------------------------------------------------------------------------------------------
namespace {
struct OptRow {
    const char* locKey;    // the game's localisation key ("" = own text)
    const char* en;        // fallback / own text
    const char* sav;       // key in [options] of the save ("" = not a value)
    int lo, hi, step, def;
    const char* suffix;
};
// Controls opens the Help sheet; the three volumes use the keys of the main menu's Settings page, so both agree.
const OptRow kOptRows[] = {
    {"Controls", "Controls", "", 0, 0, 0, 0, ""},
    {"MasterVolume", "Master Volume %", "OPT_INT_MASTER_VOL", 0, 100, 5, 100, ""},
    {"MusicVolume", "Music Volume %", "OPT_INT_MUSIC", 0, 100, 5, 100, ""},
    {"EffectsVolume", "Effects Volume %", "OPT_INT_EFFECTS", 0, 100, 5, 100, ""},
    {"", "Touch Controls Size", "ZD_TOUCH_SCALE", 60, 150, 10, 100, "%"},
    {"", "Touch Controls Opacity", "ZD_TOUCH_OPACITY", 20, 100, 10, 60, "%"},
    {"", "Developer Options", "", 0, 0, 0, 0, ""},
    {"", "Back", "", 0, 0, 0, 0, ""},
};
constexpr int kOptCount = static_cast<int>(sizeof(kOptRows) / sizeof(kOptRows[0]));
int optGet(const OptRow& o) {
    const std::string v = SavIni::shared().get("options", o.sav, "");
    if (v.empty()) return o.def;
    return std::clamp(std::atoi(v.c_str()), o.lo, o.hi);
}
}  // namespace

void PlayScreen::applyOptions() {
    auto val = [&](const char* k, int lo, int hi, int def) {
        const std::string v = SavIni::shared().get("options", k, "");
        return v.empty() ? def : std::clamp(std::atoi(v.c_str()), lo, hi);
    };
    if (audio_)
        audio_->setVolumes(static_cast<float>(val("OPT_INT_MASTER_VOL", 0, 100, 100)) / 100.0f,
                           static_cast<float>(val("OPT_INT_MUSIC", 0, 100, 100)) / 100.0f,
                           static_cast<float>(val("OPT_INT_EFFECTS", 0, 100, 100)) / 100.0f);
    touch::Layout lay = touchPad_.layout();
    const float sc = static_cast<float>(val("ZD_TOUCH_SCALE", 60, 150, 100)) / 100.0f;
    const int op = val("ZD_TOUCH_OPACITY", 20, 100, 60) * 255 / 100;
    if (std::fabs(lay.scale - sc) > 0.001f || lay.opacity != op) {
        lay.scale = sc;
        lay.opacity = op;
        touchPad_.setLayout(lay);
    }
}

// "Help & Options" of the pause menu (InGame.gbs "MediumOptions1P"): same dialog as the pause menu, rows with < value >.
void PlayScreen::drawOptions(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    const GbsSceneView* v = hudOk_ && hud_.scene() ? hud_.scene()->view("MediumOptions1P") : nullptr;
    float rowX = 388, rowY = 176, rowW = 507;   // this dialog is taller than the pause one: its plate / wings / first row sit 82 px higher
    if (v) {
        hud_.setViewport(0, 0, kCanvas);
        hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
            if (n.name == "BGFade") st.fallback = Col{0, 0, 0, 160};
            else if (n.name == "ChoiceGRP" || n.name == "AdvanceGRP" || n.name == "ConfirmGRP" || n.name == "SlotGRP" ||
                     n.name == "ArrowGroup" || n.name == "P2Menu" || n.name == "ActionFrame" || n.name == "Copy of ActionFrame")
                st.hide = true;
            else if (n.name == "Header_Text" || n.name == "Player1Label") { st.hasText = true; st.text = "PLAYER 1"; }
            else if (n.name == "P1TitleTxt") st.hide = true;
        });
        if (const GbsFont* f = hud_.fontNamed("Big Options w Wings")) {
            const std::string t = "PLAYER 1 OPTIONS";   // shorter than "Help & Options": it has to fit between the wings
            const float sc = 0.44f, w = hud_.textWidth(*f, t, sc);
            hud_.drawLine(*f, t, 642 - w / 2, 151 - f->lineHeight * sc / 2, Col{255, 255, 255, 255}, sc);
        }
    } else {
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 170});
        ui.fill(Rect{177, 92, 288, 202}, Col{10, 8, 12, 245});
        ui.outline(Rect{177, 92, 288, 202}, Col{190, 120, 90, 255});
    }
    menuSel_ = std::clamp(menuSel_, 0, kOptCount - 1);
    // value column (canvas px, relative to the row): left arrow, centre of the number, right arrow
    constexpr float kLArrow = 292, kValueC = 392, kRArrow = 468;
    int act = -1, step = 0;
    if (ui.in.keyDown) menuSel_ = (menuSel_ + 1) % kOptCount;
    if (ui.in.keyUp) menuSel_ = (menuSel_ + kOptCount - 1) % kOptCount;
    if (ui.in.left) step = -1;
    if (ui.in.right) step = +1;
    if (ui.in.enter) act = menuSel_;
    const auto rowAt = [&](int px, int py) {
        for (int i = 0; i < kOptCount; ++i) {
            const Rect rr{static_cast<int>((rowX - 10) * kCanvas), static_cast<int>((rowY + i * kRowPitch) * kCanvas),
                          static_cast<int>((rowW + 20) * kCanvas), static_cast<int>(kRowPitch * kCanvas)};
            if (rr.contains(px, py)) return i;
        }
        return -1;
    };
    if (ui.in.pressed)
        if (const int rI = rowAt(ui.in.mx, ui.in.my); rI >= 0) menuSel_ = rI;
    if (ui.in.released && overlayT_ > 8) {
        const int r0 = rowAt(ui.in.pmx, ui.in.pmy), r1 = rowAt(ui.in.mx, ui.in.my);
        if (r0 >= 0 && r0 == r1) {
            menuSel_ = r0;
            if (kOptRows[r0].sav[0]) {   // a value row: the left half of the value column lowers, the right half raises
                const float cx = static_cast<float>(ui.in.mx) / kCanvas - rowX;
                if (cx >= kLArrow - 30 && cx < kValueC) step = -1;
                else if (cx >= kValueC && cx <= kRArrow + 40) step = +1;
            } else {
                act = r0;
            }
        }
    }
    const OptRow& cur = kOptRows[menuSel_];
    if (step != 0 && cur.sav[0]) {
        const int nv = std::clamp(optGet(cur) + step * cur.step, cur.lo, cur.hi);
        SavIni::shared().setInt("options", cur.sav, nv);
        applyOptions();
    }
    if (act == 0) openOverlay(Overlay::Help);
    else if (act == kOptCount - 2) {   // Developer Options
        openOverlay(Overlay::Dev);
        devSel_ = devScroll_ = 0;
        return;
    }
    else if (act == kOptCount - 1) {
        openOverlay(Overlay::Pause);
        menuSel_ = 1;
        return;
    }
    // rows
    for (int i = 0; i < kOptCount; ++i) {
        const OptRow& o = kOptRows[i];
        const float y = rowY + static_cast<float>(i) * kRowPitch;
        std::string label = o.locKey[0] ? tr(o.locKey, o.en) : (std::string(o.en) == "Back" ? tr("Back", "Back") : std::string(o.en));
        if (i == kOptCount - 2) label = devopt::text("dev", lang_ == "es");   // ZDroid's own row
        if (!hudOk_) {
            ui.text(190, 108 + i * 18, (i == menuSel_ ? "> " : "  ") + label + (o.sav[0] ? "  " + std::to_string(optGet(o)) + o.suffix : std::string()), kText);
            continue;
        }
        drawMenuRow(rowX, y, rowW, label, i == menuSel_, true);
        if (o.sav[0]) {
            const bool on = i == menuSel_;
            const int val = optGet(o);
            SDL_Texture* aOff = hud_.texture(on ? "Textures/MainMenu/arrow02.png" : "Textures/MainMenu/arrow01.png");
            if (aOff) {
                // arrow01/02 point to the right: the left one is mirrored
                if (val > o.lo) hud_.drawTexture(aOff, rowX + kLArrow, y + 4, 21, 29, 0, 0, 0, true, false);
                if (val < o.hi) hud_.drawTexture(aOff, rowX + kRArrow, y + 4, 21, 29);
            }
            if (const GbsFont* f = hud_.fontNamed("Dialogue Text")) {
                const std::string t = std::to_string(val) + o.suffix;
                const float w = hud_.textWidth(*f, t, 1.0f);
                hud_.drawLine(*f, t, rowX + kValueC - w / 2, y + 3, on ? Col{255, 255, 255, 255} : Col{226, 222, 212, 255}, 1.0f);
            }
        }
    }
}

// Developer Options inside a match: the same dialog as Help & Options, a scrolling window of the rows of devopt::def().  Left / right
// (or a tap on the left / right half of the value) change the choice; the new value is written to the save at once, so the menu's
// page shows it too.  What the renderer can change right now (resolution, sprite quality, smoothing, shadows, FPS, boxes) applies
// immediately; the rest (stage backdrop, upscaling, preloading, intro, entrances) is read when the next match is loaded.
void PlayScreen::drawDev(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    const bool es = lang_ == "es";
    const GbsSceneView* v = hudOk_ && hud_.scene() ? hud_.scene()->view("MediumOptions1P") : nullptr;
    float rowX = 388, rowY = 176, rowW = 507;
    if (v) {
        hud_.setViewport(0, 0, kCanvas);
        hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
            if (n.name == "BGFade") st.fallback = Col{0, 0, 0, 160};
            else if (n.name == "ChoiceGRP" || n.name == "AdvanceGRP" || n.name == "ConfirmGRP" || n.name == "SlotGRP" ||
                     n.name == "ArrowGroup" || n.name == "P2Menu" || n.name == "ActionFrame" || n.name == "Copy of ActionFrame")
                st.hide = true;
            else if (n.name == "Header_Text" || n.name == "Player1Label") { st.hasText = true; st.text = "DEVELOPER"; }
            else if (n.name == "P1TitleTxt") st.hide = true;
        });
        if (const GbsFont* f = hud_.fontNamed("Big Options w Wings")) {
            const std::string t = es ? "DESARROLLADOR" : "DEVELOPER OPTIONS";
            const float sc = 0.40f, w = hud_.textWidth(*f, t, sc);
            hud_.drawLine(*f, t, 642 - w / 2, 151 - f->lineHeight * sc / 2, Col{255, 255, 255, 255}, sc);
        }
    } else {
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 170});
        ui.fill(Rect{177, 92, 288, 202}, Col{10, 8, 12, 245});
        ui.outline(Rect{177, 92, 288, 202}, Col{190, 120, 90, 255});
    }
    constexpr int kVisible = 7;
    const int n = devopt::kCount + 1;   // the options + Back
    devSel_ = std::clamp(devSel_, 0, n - 1);
    constexpr float kLArrow = 292, kValueC = 392, kRArrow = 468;
    int act = -1, step = 0;
    if (ui.in.keyDown) devSel_ = (devSel_ + 1) % n;
    if (ui.in.keyUp) devSel_ = (devSel_ + n - 1) % n;
    if (ui.in.left) step = -1;
    if (ui.in.right) step = +1;
    if (ui.in.enter) act = devSel_;
    if (devSel_ < devScroll_) devScroll_ = devSel_;
    if (devSel_ >= devScroll_ + kVisible) devScroll_ = devSel_ - kVisible + 1;
    devScroll_ = std::clamp(devScroll_, 0, std::max(0, n - kVisible));
    const auto rowAt = [&](int px, int py) {
        for (int i = 0; i < kVisible && devScroll_ + i < n; ++i) {
            const Rect rr{static_cast<int>((rowX - 10) * kCanvas), static_cast<int>((rowY + i * kRowPitch) * kCanvas),
                          static_cast<int>((rowW + 20) * kCanvas), static_cast<int>(kRowPitch * kCanvas)};
            if (rr.contains(px, py)) return devScroll_ + i;
        }
        return -1;
    };
    if (ui.in.pressed)
        if (const int rI = rowAt(ui.in.mx, ui.in.my); rI >= 0) devSel_ = rI;
    if (ui.in.released && overlayT_ > 8) {
        const int r0 = rowAt(ui.in.pmx, ui.in.pmy), r1 = rowAt(ui.in.mx, ui.in.my);
        if (r0 >= 0 && r0 == r1) {
            devSel_ = r0;
            if (r0 < devopt::kCount) {   // a value row: the left half of the value column lowers, the right half raises
                const float cx = static_cast<float>(ui.in.mx) / kCanvas - rowX;
                if (cx >= kLArrow - 30 && cx < kValueC) step = -1;
                else if (cx >= kValueC && cx <= kRArrow + 40) step = +1;
                else step = +1;
            } else {
                act = r0;
            }
        }
    }
    // the mouse wheel / a drag is not wired in this overlay: the arrows and Up / Down reach every row
    if (devSel_ < devopt::kCount && step != 0) {
        const devopt::Def& d = devopt::def(static_cast<devopt::Id>(devSel_));
        const std::string cur = SavIni::shared().get("options", d.key, "");
        const int idx = devopt::indexOf(static_cast<devopt::Id>(devSel_), cur);
        const int ni = std::clamp(idx + step, 0, d.nChoices - 1);
        if (ni != idx) {
            SavIni::shared().set("options", d.key, d.choices[ni]);
            pf_.clear();   // no worker may still be decoding with the old level / filter
            refreshDev();
            applyDevToSkins(ren_);
            if (devSel_ == devopt::kBoxes) showBoxes_ = dev_.boxes;
            if (devSel_ == devopt::kFps) showFps_ = dev_.fps;
            if (bg_.tex) SDL_SetTextureScaleMode(bg_.tex, dev_.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        }
    }
    if (act == devopt::kCount) {   // Back
        openOverlay(Overlay::Options);
        menuSel_ = 6;
        return;
    }
    for (int i = 0; i < kVisible && devScroll_ + i < n; ++i) {
        const int ri = devScroll_ + i;
        const float y = rowY + static_cast<float>(i) * kRowPitch;
        const bool on = ri == devSel_;
        const bool isBack = ri == devopt::kCount;
        const devopt::Def* d = isBack ? nullptr : &devopt::def(static_cast<devopt::Id>(ri));
        const std::string label = isBack ? tr("Back", "Back") : std::string(devopt::text(std::string("dev.") + d->name, es));
        if (!hudOk_) {
            ui.text(190, 108 + i * 18, (on ? "> " : "  ") + label, kText);
            continue;
        }
        drawMenuRow(rowX, y, rowW, label, on, true);
        if (isBack) continue;
        const int idx = devopt::indexOf(static_cast<devopt::Id>(ri), SavIni::shared().get("options", d->key, ""));
        SDL_Texture* arrow = hud_.texture(on ? "Textures/MainMenu/arrow02.png" : "Textures/MainMenu/arrow01.png");
        if (arrow) {
            if (idx > 0) hud_.drawTexture(arrow, rowX + kLArrow, y + 4, 21, 29, 0, 0, 0, true, false);
            if (idx < d->nChoices - 1) hud_.drawTexture(arrow, rowX + kRArrow, y + 4, 21, 29);
        }
        if (const GbsFont* f = hud_.fontNamed("Dialogue Text")) {
            const char* ct = devopt::text(std::string("c.") + d->choices[idx], es);
            const std::string t = ct ? ct : d->choices[idx];
            const float w = hud_.textWidth(*f, t, 1.0f);
            hud_.drawLine(*f, t, rowX + kValueC - w / 2, y + 3, on ? Col{255, 255, 255, 255} : Col{226, 222, 212, 255}, 1.0f);
        }
    }
    // what the highlighted row does (two short lines under the dialog)
    if (devSel_ < devopt::kCount) {
        const devopt::Def& d = devopt::def(static_cast<devopt::Id>(devSel_));
        const std::string desc = devopt::text(std::string("dev.") + d.name + ".d", es);
        // word-wrap to the width of the box (bitmap font, 8 px per letter): up to three lines
        std::vector<std::string> lines;
        {
            std::string w, line;
            auto flush = [&]() {
                if (w.empty()) return;
                if (!line.empty() && Ui::textW(line + " " + w) > 506) {
                    lines.push_back(line);
                    line = w;
                } else {
                    line = line.empty() ? w : line + " " + w;
                }
                w.clear();
            };
            for (char c : desc) {
                if (c == ' ') flush();
                else w += c;
            }
            flush();
            if (!line.empty()) lines.push_back(line);
        }
        const int n = static_cast<int>(std::min<size_t>(lines.size(), 3));
        ui.fill(Rect{60, 316 - (n - 1) * 5, 520, 8 + n * 11}, Col{0, 0, 0, 170});
        for (int i = 0; i < n; ++i) ui.text(66, 320 - (n - 1) * 5 + i * 11, lines[static_cast<size_t>(i)], Col{255, 236, 150, 255});
    }
}

// The Help sheet (Help.gbs "Help"): any tap / key / Back returns to the pause menu.
void PlayScreen::drawHelp(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    bool drawn = false;
    if (hudOk_ && hud_.loadScene("Help.gbs")) {
        if (const GbsSceneView* v = hud_.scene() ? hud_.scene()->view("Help") : nullptr) {
            hud_.setViewport(0, 0, kCanvas);
            static const struct { const char* node; const char* key; const char* en; } kText[] = {
                {"Title", "Controls", "Controls"},
                {"MoveText", "Move", "Move"},        {"JumpText", "Jump", "Jump"},
                {"CrouchText", "Crouch", "Crouch"},  {"ptext", "Punches", "Punches"},
                {"KicksText", "Kicks", "Kicks"},     {"Tag 2", "Tag1", "Tag 1"},
                {"MenuPauseText", "Pause", "Pause"},
                {"Ensemble1", "Ensemble1", "Ensemble 1"}, {"e2txt", "Ensemble2", "Ensemble 2"},
                {"Tag 2B", "Tag2", "Tag 2"},         {"lt", "BlockHelp", "(While Facing Right) Block"},
                {"t2-3", "Throw", "Throw"},
                {"PlusSign", "", "+"},               {"PlusSignB", "", "+"}};
            hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
                if (n.name == "Text" || n.name == "DefaultControls" || n.name == "bt" || n.name == "t2-4") { st.hide = true; return; }
                for (const auto& t : kText)
                    if (n.name == t.node) {
                        st.hasText = true;
                        st.text = t.key[0] ? tr(t.key, t.en) : std::string(t.en);
                        return;
                    }
            });
            drawn = true;
        }
        hud_.loadScene("InGame.gbs");   // back to the scene the HUD / menus read their fonts and textures from
    }
    if (!drawn) {
        ui.fill(Rect{0, 0, 640, 360}, Col{8, 8, 14, 235});
        ui.text(20, 20, "HELP", kAccent, 2);
        ui.text(20, 60, "Move: left stick / arrows    Attacks: A B X Y R1 R2", kText);
    }
    if (overlayT_ > 8 && (ui.in.released || ui.in.enter)) openOverlay(Overlay::Options);
}

// End of a match: "Encore Performance?" + the four choices (story fights: Continue / Retry / Give Up).
void PlayScreen::drawEndMenu(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    std::vector<std::string> rows;
    std::vector<int> ids;   // 0 again, 1 characters, 2 stage, 3 main menu, 10 continue, 11 retry, 12 give up
    if (storyMode_ || arcadeMode_) {
        if (lastWinner_ == 0) { rows = {tr("continue_btn", "Continue")}; ids = {10}; }
        else { rows = {tr("Retry", lang_ == "es" ? "Reintentar" : "Retry"), tr("GiveUp", "Give Up")}; ids = {11, 12}; }
    } else {
        rows = {tr("Again", "Play Again"), tr("ChangeCharacter", "Change Characters"), tr("ChangeStage", "Change Stage"),
                tr("BackToMain", "Return to Main Menu")};
        ids = {0, 1, 2, 3};
    }
    const std::vector<char> enabled(rows.size(), 1);
    float rowX = 340, rowY = 422, rowW = 252;
    // The end-of-match banner is InGame.gbs' "Continue Service" view: the dimmer, the checkered plate with its wings and glyphs, and the
    // header picture (ENCORE PERFORMANCE? / WHAT NOW, BIG SHOT? / THE STORY CONTINUES) that slides in with its OnActivate animation.
    if (hudOk_) {
        hud_.loadScene("InGame.gbs");
        if (const GbsSceneView* cs = hud_.scene() ? hud_.scene()->view("Continue Service") : nullptr) {
            if (!csBound_ || overlayT_ <= 0) {
                csAnim_.bind(cs);
                csAnim_.play("OnActivate");
                csBound_ = true;
            }
            csAnim_.tick();
            hud_.setViewport(0, 0, kCanvas);
            hud_.setAnimPlayer(&csAnim_);
            const char* header = (storyMode_ || arcadeMode_) ? (lastWinner_ == 0 ? "posthud_cservheader_small03" : "posthud_cservheader_small02")
                                                             : "posthud_cservheader_small01b";
            hud_.drawView(*cs, [&](const GbsNode& n, NodeState& st) {
                if (n.name == "Continue Service Header") {   // its keys put the pivot, not the corner, at the position
                    st.texName = std::string("Textures/Posthud/") + header + ".png";
                    st.pivotAnchored = true;
                }
            });
            hud_.setAnimPlayer(nullptr);
        }
    }
    if (confirm_.isOpen()) return;   // "Encore Performance?" stays behind the question, the rows do not
    const GbsSceneView* v = hudOk_ && hud_.scene() ? hud_.scene()->view("EndMatchOptions") : nullptr;
    if (v) {
        hud_.setViewport(0, 0, kCanvas);
        hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
            if (n.name == "P2Menu" || n.name == "ChoiceGRP" || n.name == "AdvanceGRP" || n.name == "ConfirmGRP" ||
                n.name == "SlotGRP" || n.name == "ArrowGroup" || n.name == "ActionFrame" || n.name == "Copy of ActionFrame" ||
                n.name == "ConfirmLabelP1" || n.name == "ConfirmLabelP2")
                st.hide = true;
            else if (n.name == "Player1Label") { st.hasText = true; st.text = "PLAYER 1"; }
        });
    } else {
        ui.fill(Rect{0, 0, 640, 360}, Col{8, 8, 14, 190});
        ui.text(320 - Ui::textW(endText_, 3) / 2, 130, endText_, kAccent, 3);
    }
    const int act = menuInput(ui, static_cast<int>(rows.size()), rowX, rowY, rowW, enabled);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (v) drawMenuRow(rowX, rowY + static_cast<float>(i) * kRowPitch, rowW, rows[i], static_cast<int>(i) == menuSel_, true);
        else ui.text(250, 160 + static_cast<int>(i) * 20, (static_cast<int>(i) == menuSel_ ? "> " : "  ") + rows[i], kText);
    }
    if (act < 0) return;
    switch (ids[static_cast<size_t>(act)]) {
        case 0: overlay_ = Overlay::None; startMatch(); break;
        case 1: overlay_ = Overlay::None; leaveMatch(); break;
        case 2:   // next stage, same fighters
            overlay_ = Overlay::None;
            if (!levels_.empty()) stageIdx_ = (stageIdx_ + 1) % static_cast<int>(levels_.size());
            startMatch();
            break;
        case 3: askConfirm(ConfirmAct::ReturnToMenu); break;
        case 10:
            if (arcadeMode_) { arcadeNext(); break; }
            overlay_ = Overlay::None;
            storyOutcome_ = StoryOutcome::Won;
            break;
        case 11:
            overlay_ = Overlay::None;
            if (arcadeMode_) ladder_.retry();   // a continue: the same fight again
            startMatch();
            break;
        case 12: askConfirm(ConfirmAct::GiveUp); break;
        default: break;
    }
}

// GameOver.gbs "ThanksForPlaying": the game's own game-over card (the "GameOver" animation, then the fade).
void PlayScreen::drawGameOver(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    bool ok = false;
    if (hudOk_ && hud_.loadScene("GameOver.gbs")) {
        if (const GbsSceneView* v = hud_.scene() ? hud_.scene()->view("ThanksForPlaying") : nullptr) {
            if (!goBound_) {
                goAnim_.bind(v);
                goAnim_.play("OnActivate");
                goAnim_.play("GameOver");
                goBound_ = true;
            }
            if (overlayT_ == 100) goAnim_.play("Fade_Out");
            goAnim_.tick();
            hud_.setViewport(0, 0, kCanvas);
            hud_.setAnimPlayer(&goAnim_);
            const std::string txt = tr("Game Over", "GAME OVER");
            hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
                if (n.name == "Gameover_Text") { st.hasText = true; st.text = upperText(txt); }
                else if (n.name == "text" || n.name == "Number Group") st.hide = true;
                else if (n.name == "blackbg" || n.name == "FadeOut") st.fallback = Col{0, 0, 0, 255};
            });
            hud_.setAnimPlayer(nullptr);
            ok = true;
        }
        hud_.loadScene("InGame.gbs");
    }
    if (!ok) {
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 255});
        const std::string t = "GAME OVER";
        ui.text(320 - Ui::textW(t, 3) / 2, 160, t, kAccent, 3);
    }
    if (overlayT_ > 150) {
        overlay_ = Overlay::None;
        goBound_ = false;
        if (storyMode_) storyOutcome_ = StoryOutcome::Lost;
        else leaveMatch();
    }
}

// LoadingScreen.gbs "Loading Transition": black with the little heart bug in the corner (the "Loading Bug" loop).
bool PlayScreen::drawLoadingScene(SDL_Renderer* r, int ms, bool overStage) {
    (void)ms;
    if (!hudOk_ && !gd_) return false;
    hudInit(r);
    if (!hudOk_ || !hud_.loadScene("LoadingScreen.gbs")) { if (hudOk_) hud_.loadScene("InGame.gbs"); return false; }
    const GbsSceneView* v = hud_.scene() ? hud_.scene()->view("Loading Transition") : nullptr;
    bool ok = false;
    if (v) {
        if (!overStage) hud_.fillRect(0, 0, 1280, 720, Col{0, 0, 0, 255});   // with the stage showing, the animation plays over it
        if (!loadBound_) {
            loadAnim_.bind(v);
            loadAnim_.play("OnActivate");
            loadAnim_.play("Loading Bug");
            loadBound_ = true;
        }
        for (loadTickMs_ += 16; loadTickMs_ >= 17; loadTickMs_ -= 17) loadAnim_.tick();
        hud_.setViewport(0, 0, kCanvas);
        hud_.setAnimPlayer(&loadAnim_);
        hud_.drawView(*v, [overStage](const GbsNode& n, NodeState& st) {
            if (n.name == "Black Screen") { if (overStage) st.hide = true; else st.fallback = Col{0, 0, 0, 255}; }
            else if (n.name == "Fancy Squares Left" || n.name == "Fancy Squares Right") st.hide = true;
        });
        hud_.setAnimPlayer(nullptr);
        ok = true;
    }
    hud_.loadScene("InGame.gbs");
    return ok;
}

void PlayScreen::showToast(const std::string& msg) {
    toast_ = msg;
    toastT_ = 118;      // "Banner1" lasts 115 ticks
    flyBound_ = false;  // restart the banner
}

// FlyingText.gbs "FlyingText" / "Banner1": the black bar with the gold rules unfolds from the middle, the message scales in, then
// everything folds away.  The scene parks the bar mid-screen; here it sits under the menu rows.
void PlayScreen::drawToast(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    bool ok = false;
    if (hudOk_ && hud_.loadScene("FlyingText.gbs")) {
        if (const GbsSceneView* v = hud_.scene() ? hud_.scene()->view("FlyingText") : nullptr) {
            if (!flyBound_) {
                flyAnim_.bind(v);
                flyAnim_.play("OnActivate");
                flyAnim_.play("Banner1");
                flyBound_ = true;
            }
            flyAnim_.tick();
            hud_.setViewport(0, 0, kCanvas);
            hud_.setAnimPlayer(&flyAnim_);
            const std::string txt = upperText(toast_);
            hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
                if (n.name == "MessageTXT") { st.hasText = true; st.text = txt; st.pivotAnchored = true; }
                else if (n.name == "BannerGRP") st.dy = 250;
                else if (n.name == "MessageBG" || n.name == "MessageBGTop" || n.name == "MessageBGBottom") st.pivotAnchored = true;
            });
            hud_.setAnimPlayer(nullptr);
            ok = true;
        }
        hud_.loadScene("InGame.gbs");
    }
    if (!ok) {   // no FlyingText.gbs in the data: a plain strip
        const int w = Ui::textW(toast_);
        ui.fill(Rect{320 - w / 2 - 8, 322, w + 16, 20}, Col{0, 0, 0, 200});
        ui.text(320 - w / 2, 328, toast_, kWarn);
    }
}

// PrePostHud.gbs: "1v1 Match Intro" (the "Take N" coin between two film strips), then "Action Animation" - or
// "Showtime Animation" when both fighters are one round from winning.  Their keys place sprites by pivot (top-left =
// position - pivot), so every node here is drawn pivot-anchored.
void PlayScreen::drawRoundIntro(Ui& ui, SDL_Renderer* r) {
    (void)ui;
    hudInit(r);
    if (!hudOk_ || !hud_.loadScene("PrePostHud.gbs") || !hud_.scene()) {
        if (hudOk_) hud_.loadScene("InGame.gbs");
        return;
    }
    const bool decider = roundsToWin_ > 1 && wins_[0] == roundsToWin_ - 1 && wins_[1] == roundsToWin_ - 1;
    const char* name = introStage_ == 1 ? "1v1 Match Intro" : (decider ? "Showtime Animation" : "Action Animation");
    if (const GbsSceneView* v = hud_.scene()->view(name)) {
        if (introBound_ != introStage_) {
            introAnim_.bind(v);
            introAnim_.play("OnActivate");
            introAnim_.play("Intro Animation");
            introBound_ = introStage_;
        }
        const std::string coin = "Textures/Prehud/coin_number_0" + std::to_string(std::clamp(round_, 1, 7)) + ".png";
        hud_.setViewport(0, 0, kCanvas);
        hud_.setAnimPlayer(&introAnim_);
        hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
            st.pivotAnchored = true;
            if (n.name == "Number Forward" || n.name == "Number Reverse") st.texName = coin;
            else if (n.name == "Center Ref") st.hide = true;
        });
        hud_.setAnimPlayer(nullptr);
    }
    hud_.loadScene("InGame.gbs");
}

// The round-result event of the game's HUD handler, reconstructed: a result code (win by K.O. for either side, win on time for either
// side, draw, double K.O.), the winner's remaining life in percent and a "perfect" flag (the winner never lost life).  The code picks the
// two lines of text of the "Round Win" view - the top one names the result, the one under the plate grades how the winner finished (by
// remaining life: >= 75 / 50 / 35 / 25 / 10 / below, or the perfect line) - and the animation ("Perfect" variant for a flawless win).
void PlayScreen::beginRoundWin() {
    rwOn_ = true;
    rwBound_ = false;
    rwDelay_ = std::clamp(endTimer_, 0, 90);
    const int reason = foit_->roundReason(), w = foit_->roundWinner();
    int code;
    if (reason == 3) code = 6;
    else if (reason == 2) code = w == 0 ? 1 : (w == 1 ? 2 : 3);
    else code = w == 0 ? 4 : (w == 1 ? 5 : 3);
    const float life = w >= 0 ? foit_->lifeFraction(w) : 0.0f;
    int pct = static_cast<int>(life * 100.0f);
    bool perfect = life >= 1.0f;
    if (const char* f = std::getenv("ZDROID_PLAY_RWFORCE")) {   // smoke hook: "code:life percent:perfect" (code 1..6 as above)
        int c = code, p = pct, pf = perfect ? 1 : 0;
        if (std::sscanf(f, "%d:%d:%d", &c, &p, &pf) >= 1) code = c, pct = p, perfect = pf != 0;
    }
    const bool ko = code == 4 || code == 5;
    rwPerfect_ = ko && perfect;
    switch (code) {
        case 4: case 5:
            rwHeader_ = tr(code == 4 ? "P1Win" : "P2Win", code == 4 ? "PLAYER 1 WINS" : "PLAYER 2 WINS");
            if (rwPerfect_) rwFooter_ = tr("WinnerLife100", "PERFECT!");
            else if (pct >= 75) rwFooter_ = tr("WinnerLife75", "GLORIOUS!");
            else if (pct >= 50) rwFooter_ = tr("WinnerLife50", "DECISIVE!");
            else if (pct >= 35) rwFooter_ = tr("WinnerLife35", "ACCEPTABLE!");
            else if (pct >= 25) rwFooter_ = tr("WinnerLife25", "SERVICEABLE!");
            else if (pct >= 10) rwFooter_ = tr("WinnerLife10", "FORGETTABLE!");
            else rwFooter_ = tr("WinnerLife1", "BARELY!");
            break;
        case 1: case 2:
            rwHeader_ = tr(code == 1 ? "P1WinByTO" : "P2WinByTO", code == 1 ? "DECISION: PLAYER 1" : "DECISION: PLAYER 2");
            rwFooter_ = tr("TimeOut", "TIME OVER");
            break;
        case 3:
            rwHeader_ = tr("DrawTop", "TOO CLOSE TO CALL");
            rwFooter_ = tr("Draw", "DRAW");
            break;
        default:
            rwHeader_ = tr("DoubleKOTop", "DOUBLE K.O.");
            rwFooter_ = tr("DoubleKO", "DOUBLE K.O.");
            break;
    }
}

bool PlayScreen::drawRoundWin(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    if (!hudOk_ || !hud_.loadScene("PrePostHud.gbs") || !hud_.scene()) {
        if (hudOk_) hud_.loadScene("InGame.gbs");
        return false;
    }
    const GbsSceneView* v = hud_.scene()->view("Round Win");
    if (!v) {
        hud_.loadScene("InGame.gbs");
        return false;
    }
    if (!rwBound_) {
        rwAnim_.bind(v);
        rwAnim_.play("OnActivate");
        rwAnim_.play(rwPerfect_ ? "Fight Win Animation Perfect" : "Fight Win Animation");
        rwBound_ = true;
    }
    ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 70});
    const std::string head = rwPerfect_ ? "Textures/Posthud/posthud_pefect_large01.png" : "Textures/Posthud/posthud_thatsawrap_large01.png";
    hud_.setViewport(0, 0, kCanvas);
    hud_.setAnimPlayer(&rwAnim_);
    hud_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
        st.pivotAnchored = true;
        if (n.name == "Win Header") {
            st.hasText = true;
            st.text = upperText(rwHeader_);
        } else if (n.name == "Win Footer") {
            st.hasText = true;
            st.text = upperText(rwFooter_);
        } else if (n.name == "Posthud_Header") {
            st.texName = head;
        } else if (n.name == "Perfect_Holder" || n.name == "ThatsAWrap_Holder" || n.name == "TXT_HELPER_JP_RITSY_HUD") {
            st.hide = true;   // texture holders / translator helper, parked off screen in the scene
        }
    });
    hud_.setAnimPlayer(nullptr);
    hud_.loadScene("InGame.gbs");
    return true;
}

// K.O. / TIME UP between rounds: the winged title font of the game's menus, over a dimmed match.
void PlayScreen::drawRoundEnd(Ui& ui, SDL_Renderer* r) {
    hudInit(r);
    if (foitMode_ && rwOn_ && rwDelay_ <= 0 && drawRoundWin(ui, r)) return;
    ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 110});
    const GbsFont* f = hudOk_ ? hud_.fontNamed("Big Options w Wings") : nullptr;
    if (f) {
        const float pop = std::min(1.0f, static_cast<float>(110 - std::max(0, endTimer_)) / 10.0f);   // a quick scale-in
        const float sc = 1.1f + 0.5f * pop;
        const std::string t = upperText(endText_);
        const float w = hud_.textWidth(*f, t, sc);
        hud_.setViewport(0, 0, kCanvas);
        hud_.drawLine(*f, t, 640 - w / 2, 300 - f->lineHeight * sc / 2, Col{255, 255, 255, 255}, sc, pop);
        const std::string sub = "ROUND " + std::to_string(round_);
        const float s2 = 0.55f, w2 = hud_.textWidth(*f, sub, s2);
        hud_.drawLine(*f, sub, 640 - w2 / 2, 400, Col{199, 153, 113, 255}, s2, pop);
        return;
    }
    const int w = Ui::textW(endText_, 3);
    ui.text(320 - w / 2, 150, endText_, kAccent, 3);
    const std::string sub = "round " + std::to_string(round_);
    ui.text(320 - Ui::textW(sub) / 2, 190, sub, kDim);
}

void PlayScreen::drawOverlay(Ui& ui, SDL_Renderer* r) {
    ++overlayT_;
    switch (overlay_) {
        case Overlay::GameOver: drawGameOver(ui, r); break;
        case Overlay::Pause: if (!confirm_.isOpen()) drawPause(ui, r); break;   // the window replaces the menu while it asks
        case Overlay::Options: drawOptions(ui, r); break;
        case Overlay::Dev: drawDev(ui, r); break;
        case Overlay::Movelist: drawMovelist(ui, r); break;
        case Overlay::Help: drawHelp(ui, r); break;
        case Overlay::End: drawEndMenu(ui, r); break;
        case Overlay::None: break;
    }
    if (confirm_.isOpen()) drawConfirm(ui, r);
    if (toastT_ > 0) {
        --toastT_;
        drawToast(ui, r);
    }
}

}  // namespace play
}  // namespace zengine
