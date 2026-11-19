// menu_scene.cpp - the title screen and the main menu drawn from the game's own UI scenes (Main.gbs):
//   Splash_2ND     title screen (logo, shield, "press any button")
//   MainMenu_BG    the city / spotlights backdrop
//   MainMenu_New   the filmstrip band with the big header image, the item list, the description
// Layout, textures and fonts all come from the user's data (see gbs_scene.h / game_ui.h).  The movement comes from the
// scenes' own animations (LAGG chunks) played at 60 Hz: the backdrop and the title by GbsAnimPlayer, the main menu by
// MenuListAnim (core/menu_list_anim.h), which only chooses which animation plays - film steps, header art pop-in, label
// fades and tints, the intro and the band slide of the sub-pages are all in the data.  The pages that the original builds
// from code (sub-menu rows) are laid out by us.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "menu.h"
#include "menu_list_anim.h"
#include "movelist_dialog.h"

namespace zengine {
namespace menu {

namespace {

// The scene views of the menu are positioned by pivot (see GameUi::setOtterPivot); the Character Select shares the same
// GameUi and keeps its own reading, so the flag is only on while a menu view is drawn.
struct PivotScope {
    GameUi& ui;
    bool prev;
    PivotScope(GameUi& u, bool on) : ui(u), prev(u.otterPivot()) { ui.setOtterPivot(on); }
    ~PivotScope() { ui.setOtterPivot(prev); }
};

constexpr float kCanvasW = 1280.0f, kCanvasH = 720.0f;
constexpr float kTitleFooterDy = 44.0f;   // copyright / version line: from the PressButton row down to the bottom edge
constexpr Col kWhite{255, 255, 255, 255};
constexpr Col kGrey{150, 150, 160, 255};
constexpr Col kWarm{255, 226, 150, 255};
constexpr Col kWarnCol{255, 205, 110, 255};
constexpr Col kChoiceRed{222, 38, 66, 255};
constexpr float kSelShift = 40.0f;      // the highlighted sub-page row slides this far to the right
constexpr float kSlotX = -334.0f, kSlotY = 240.0f, kSlotStep = 64.0f;   // SubSlot0..6 of the scene
constexpr int kSubRows = 6;
constexpr float kItemHitShiftX = -124.0f;  // the list labels are placed by their pivot (124 px in from the corner): where a row really is, for touches             // rows that fit above the description line

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float easeOutCubic(float v) {
    v = 1.0f - clamp01(v);
    return 1.0f - v * v * v;
}

// text with a dark outline drawn from four offsets: the spotlights at the bottom wash plain text out
void shadowLabel(GameUi& g, const GbsFont& f, const std::string& s, float x, float y, float w, float h, int align, Col c, float scale,
                 float alpha) {
    const Col shade{0, 0, 0, 255};
    for (const float o : {-2.0f, 2.0f}) {
        g.drawLabelText(f, s, x + o, y, w, h, align, true, shade, scale, 0.85f * alpha);
        g.drawLabelText(f, s, x, y + o, w, h, align, true, shade, scale, 0.85f * alpha);
    }
    g.drawLabelText(f, s, x, y, w, h, align, true, c, scale, alpha);
}

// The game's hint rows are strings with button tokens, e.g. "{gu}{gd} Menu Select | {glk}{glp} Accept | {gmk}{gmp} Cancel".
// The PC edition draws the tokens as button icons; the arrow ones are small triangles here, the face buttons are the
// game's own Textures/Buttons/*.dds (A/X accept, B/Y cancel, Start for the news key).
struct GlyphItem {
    bool glyph = false;
    std::string s;   // text, or the token name without braces
};

std::vector<GlyphItem> parseGlyphLine(const std::string& tpl) {
    std::vector<GlyphItem> out;
    std::string cur;
    auto flush = [&]() {
        size_t a = 0, b = cur.size();
        while (a < b && cur[a] == ' ') ++a;
        while (b > a && cur[b - 1] == ' ') --b;
        if (b > a) out.push_back({false, cur.substr(a, b - a)});
        cur.clear();
    };
    for (size_t i = 0; i < tpl.size(); ++i) {
        if (tpl[i] == '{') {
            const size_t e = tpl.find('}', i);
            if (e != std::string::npos) {
                flush();
                out.push_back({true, tpl.substr(i + 1, e - i - 1)});
                i = e;
                continue;
            }
        }
        cur += tpl[i];
    }
    flush();
    return out;
}

const char* glyphTexture(const std::string& tok) {
    if (tok == "glk") return "Textures/Buttons/A01.png";
    if (tok == "glp") return "Textures/Buttons/X01.png";
    if (tok == "gmk") return "Textures/Buttons/B01.png";
    if (tok == "gmp") return "Textures/Buttons/Y01.png";
    if (tok == "srtg") return "Textures/Buttons/start_diagramonly.png";
    return nullptr;
}

bool isArrowToken(const std::string& t) { return t == "gl" || t == "gr" || t == "gu" || t == "gd"; }

void drawArrowGlyph(GameUi& g, const std::string& t, float x, float y, float sz, Col c) {
    // a pointing triangle (rows of fillRect) with a short shaft
    const int n = 5;
    const float step = sz / (2.0f * n);
    for (int k = 0; k < n; ++k) {
        const float len = step * (2.0f * k + 1.0f);
        const float off = (sz - len) * 0.5f;
        if (t == "gu") g.fillRect(x + off, y + k * step * 1.0f, len, step + 0.5f, c);
        else if (t == "gd") g.fillRect(x + off, y + sz * 0.5f + (n - 1 - k) * step, len, step + 0.5f, c);
        else if (t == "gl") g.fillRect(x + k * step, y + off, step + 0.5f, len, c);
        else g.fillRect(x + sz * 0.5f + (n - 1 - k) * step, y + off, step + 0.5f, len, c);
    }
}

float glyphLineWidth(GameUi& g, const GbsFont& f, const std::vector<GlyphItem>& items, float iconH, float scale) {
    float w = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const GlyphItem& it = items[i];
        float iw = 0;
        if (!it.glyph) iw = g.textWidth(f, it.s, scale);
        else if (const char* tex = glyphTexture(it.s)) {
            int tw = 0, th = 0;
            if (g.texture(tex, &tw, &th) && th > 0) iw = iconH * static_cast<float>(tw) / static_cast<float>(th);
        } else if (isArrowToken(it.s)) iw = iconH * 0.85f;
        if (iw <= 0) continue;
        w += iw + (i + 1 < items.size() ? (it.glyph && items[i + 1].glyph ? 2.0f : 8.0f) : 0.0f);
    }
    return w;
}

// Draws the row ending at rightX (canvas px), vertically centred in [y, y+h].  Returns the left edge it started at.
float drawGlyphLine(GameUi& g, const GbsFont& f, const std::string& tpl, float rightX, float y, float h, Col c, float scale) {
    const std::vector<GlyphItem> items = parseGlyphLine(tpl);
    const float iconH = 26.0f * scale;
    float x = rightX - glyphLineWidth(g, f, items, iconH, scale);
    const float left = x;
    for (size_t i = 0; i < items.size(); ++i) {
        const GlyphItem& it = items[i];
        float iw = 0;
        if (!it.glyph) {
            iw = g.textWidth(f, it.s, scale);
            shadowLabel(g, f, it.s, x, y, iw + 12.0f, h, 0, c, scale, 1.0f);
        } else if (const char* tex = glyphTexture(it.s)) {
            int tw = 0, th = 0;
            if (SDL_Texture* t = g.texture(tex, &tw, &th)) {
                iw = iconH * static_cast<float>(tw) / static_cast<float>(th);
                g.drawTexture(t, x, y + (h - iconH) * 0.5f, iw, iconH);
            }
        } else if (isArrowToken(it.s)) {
            iw = iconH * 0.85f;
            const float sz = iconH * 0.8f;
            drawArrowGlyph(g, it.s, x + (iw - sz) * 0.5f + 1.0f, y + (h - sz) * 0.5f + 4.0f, sz, Col{0, 0, 0, 200});
            drawArrowGlyph(g, it.s, x + (iw - sz) * 0.5f, y + (h - sz) * 0.5f + 3.0f, sz, Col{232, 220, 170, 255});
        }
        if (iw > 0) x += iw + (i + 1 < items.size() ? (it.glyph && items[i + 1].glyph ? 2.0f : 8.0f) : 0.0f);
    }
    return left;
}

bool findAbs(const std::vector<GbsNode>& nodes, const std::string& name, float ox, float oy, float& x, float& y, float& w, float& h) {
    for (const GbsNode& n : nodes) {
        const float ax = ox + n.x, ay = oy + n.y;
        if (n.name == name) {
            x = ax;
            y = ay;
            w = n.w;
            h = n.h;
            return true;
        }
        if (findAbs(n.kids, name, ax, ay, x, y, w, h)) return true;
    }
    return false;
}

// "MenuItem7" -> 7, anything else -> -1
int slotIndex(const std::string& name) {
    static const std::string pre = "MenuItem";
    if (name.size() <= pre.size() || name.compare(0, pre.size(), pre) != 0) return -1;
    int v = 0;
    for (size_t i = pre.size(); i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') return -1;
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

// "Top_Shield_Frm07" -> 7
int trailingNumber(const std::string& name) {
    size_t i = name.size();
    while (i > 0 && name[i - 1] >= '0' && name[i - 1] <= '9') --i;
    return i == name.size() ? -1 : std::atoi(name.c_str() + i);
}

Rect logical(float x, float y, float w, float h) {
    return Rect{static_cast<int>(std::lround(x * 0.5f)), static_cast<int>(std::lround(y * 0.5f)),
                static_cast<int>(std::lround(w * 0.5f)), static_cast<int>(std::lround(h * 0.5f))};
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Nodes that belong to sub-menu / store / news / online parts of MainMenu_New: their state comes from code
// and animations in the original, so they stay out of the resting pose we draw.
bool isCodeDriven(const std::string& n) {
    return n == "SubChoiceGRP" || n == "SubAdvanceGRP" || startsWith(n, "SubSlot") || n == "SubMenuDescGrp" ||
           n == "StoreMessageGRP" || n == "Arrows" || n == "NewsFeed_Main_GRP" || n == "StoreMsg_Header" ||
           n == "StoreMsg_Label" || n == "Black_Fader";
}

}  // namespace

bool MenuScreen::loadScenes(SDL_Renderer* r) {
    sceneOk_ = false;
    vBg_ = vMenu_ = vSplash_ = nullptr;
    if (!gd_) return false;
    gfx_.attach(r, &gd_->vfs());
    if (!gfx_.loadScene("Main.gbs")) return false;
    const GbsScene* sc = gfx_.scene();
    vBg_ = sc->view("MainMenu_BG");
    vMenu_ = sc->view("MainMenu_New");
    vSplash_ = sc->view("Splash_2ND");
    for (const char* f : {"SelznickTitleTexOtl", "Main Menu Desc", "Dialogue Text", "Data Frame Text", "Menu Text"})
        if (!sc->fontByName(f)) return false;
    if (!vBg_ || !vMenu_ || !vSplash_) return false;
    for (int i = 0; i < 11; ++i) {
        Slot& s = itemSlot_[i];
        s.ok = findAbs(vMenu_->nodes, "MenuItem" + std::to_string(i), 0, 0, s.x, s.y, s.w, s.h);
        if (!s.ok) return false;
    }
    aBg_.bind(vBg_);
    aBg_.play("Lights");
    aBg_.play("BG");
    aSplash_.bind(vSplash_);
    gfx_.preload(*vBg_);       // decode the textures of the title / backdrop now, not in the middle of an animation
    gfx_.preload(*vSplash_);
    animAcc_ = 0;
    titleAnimOn_ = pressOn_ = false;
    bandSlot_.ok = findAbs(vMenu_->nodes, "MenuItemActive", 0, 0, bandSlot_.x, bandSlot_.y, bandSlot_.w, bandSlot_.h);
    if (!bandSlot_.ok) return false;
    hintSlot_.ok = findAbs(vMenu_->nodes, "Button_Label", 0, 0, hintSlot_.x, hintSlot_.y, hintSlot_.w, hintSlot_.h);
    auto idOf = [&](const char* name) { const GbsNode* nd = vMenu_->find(name); return nd ? static_cast<int>(nd->id) : -1; };
    hGrpId_ = idOf("HorizontalGRP");
    glowId_ = idOf("Glow Topper");
    activeId_ = idOf("MenuItemActive");
    btnLabelId_ = idOf("Button_Label");
    if (const GbsNode* hg = vMenu_->find("HorizontalGRP")) hGrpRestY_ = hg->y;
    aMenu_.bind(vMenu_);
    menuLive_ = false;
    sceneOk_ = true;
    {   // the Character Select scene (optional: without it the fight entries stay dimmed)
        std::string prog;
        charSel_.setLocked(gd_->vfs().readText("ProgressionInit.txt", prog) ? CharSelect::parseLocked(prog) : std::set<std::string>{});
        const bool csOk = charSel_.load(gfx_, "Main.gbs");
        std::printf("[menu] Character Select scene: %s\n", csOk ? "ok" : "not available");
    }
    std::printf("[menu] UI scene mode: %zu fonts, %zu textures, %zu nodes in MainMenu_New\n", sc->fonts.size(), sc->textures.size(),
                vMenu_->nodes.size());
    return true;
}

std::vector<char> MenuScreen::rootUsable() const {
    std::vector<char> u;
    if (stack_.empty()) return u;
    const Page& rp = stack_.front();
    for (int vi : rp.vis) u.push_back(optionUsable(rp.def.options[static_cast<size_t>(vi)]) ? 1 : 0);
    return u;
}

float MenuScreen::bandDy() const {
    const GbsPose* ps = hGrpId_ >= 0 ? aMenu_.player().pose(static_cast<uint32_t>(hGrpId_)) : nullptr;
    return ps ? ps->y - hGrpRestY_ : 0.0f;
}

// SubAdvSelect / SubChoiceSelect: the highlighted row slides 40 px to the right in 5 ticks (8 px each), SubAdvDeselect the way back.
void MenuScreen::stepRowShift(const Page& p) {
    if (rowShift_.size() != p.vis.size()) rowShift_.assign(p.vis.size(), 0.0f);
    for (size_t i = 0; i < rowShift_.size(); ++i) {
        const float want = static_cast<int>(i) == p.sel ? kSelShift : 0.0f;
        const float step = kSelShift / 5.0f;
        rowShift_[i] += std::clamp(want - rowShift_[i], -step, step);
    }
}

// ---------------------------------------------------------------------------------------- geometry

void MenuScreen::sceneLayoutRoot(Page& p) {
    const int n = static_cast<int>(p.vis.size());
    const int sel = menuLive_ ? aMenu_.listSel() : p.sel;   // the labels show the list as it is bound, which lags a move by a few ticks
    rowPx_ = 28;
    tapSplitX_ = -1;
    visibleRows_ = std::max(1, n);
    listBox_ = Rect{0, 0, 0, 0};
    // Items above the active one fill MenuItem0..3 (nearest last); slot 4 is the band of the active item itself and stays
    // empty, the items below fill MenuItem5..10 (checked against a screenshot of the PC edition).
    for (int i = 0; i < 4; ++i) {
        const int idx = sel - 4 + i;
        if (idx >= 0 && idx < n)
            rows_.push_back({idx, logical(itemSlot_[i].x + kItemHitShiftX, itemSlot_[i].y, itemSlot_[i].w, itemSlot_[i].h)});
    }
    for (int j = 0; j < 6; ++j) {
        const int idx = sel + 1 + j;
        if (idx >= 0 && idx < n)
            rows_.push_back({idx, logical(itemSlot_[5 + j].x + kItemHitShiftX, itemSlot_[5 + j].y, itemSlot_[5 + j].w, itemSlot_[5 + j].h)});
    }
    if (n > 0) rows_.push_back({std::clamp(p.sel, 0, n - 1), logical(bandSlot_.x, bandSlot_.y, bandSlot_.w, bandSlot_.h)});
}

// Pages drawn as the big dialog of Options.gbs: the command lists ("Movelist": 72-px slots, a move over its command) and the pages
// of settings (the pause menu's "MediumOptions1P": label + < value >).  The pages of plain entries (Single Player, Extras ...) stay on
// the film strip like the PC edition.
int MenuScreen::dialogKind(const Page& p) const {
    if (p.root || !sceneOk_) return 0;
    if (p.file.rfind("movelist", 0) == 0) return 1;
    // A page of settings, not a list of entries that happens to carry one choice: Single Player (Arcade, Quick Match, Difficulty ...)
    // keeps the film strip like the PC edition, with its Difficulty row on the strip.
    int settings = 0;
    for (const MenuOption& o : p.def.options)
        if (o.type == MenuOptType::Setting) ++settings;
    return settings * 2 > static_cast<int>(p.def.options.size()) ? 2 : 0;
}

bool MenuScreen::isDialogPage(const Page& p) const { return dialogKind(p) != 0; }

void MenuScreen::sceneLayoutDialog(Page& p) {
    const int n = static_cast<int>(p.vis.size());
    const bool opts = dialogKind(p) == 2;
    const float step = opts ? kOptionSlotStep : kMovelistSlotStep;
    const int slots = opts ? kOptionSlots : kMovelistSlots;
    const float sx = opts ? kOptionSlotX : kMovelistSlotX, sy = opts ? kOptionSlotY : kMovelistSlotY, sw = opts ? kOptionSlotW : kMovelistSlotW;
    rowPx_ = static_cast<int>(std::lround(step * 0.5f));
    visibleRows_ = slots;
    // a tapped choice row: left of the value = previous, right = next
    tapSplitX_ = opts ? static_cast<int>(std::lround((sx + kOptionValueC) * 0.5f)) : -1;
    const int top = static_cast<int>(std::lround(sy * 0.5f));
    const int hBox = static_cast<int>(std::lround(step * 0.5f * static_cast<float>(slots)));
    listBox_ = Rect{static_cast<int>(sx * 0.5f), top, static_cast<int>(sw * 0.5f), hBox};
    for (int k = 0; k < n; ++k) {
        const int y = static_cast<int>(std::lround((sy + (static_cast<float>(k) - p.scroll) * step) * 0.5f));
        const int h = static_cast<int>(std::lround(step * 0.5f));
        if (y + h <= top || y >= top + hBox) continue;
        rows_.push_back({k, Rect{listBox_.x, y, listBox_.w, h}});
    }
}

void MenuScreen::sceneLayoutPage(Page& p) {
    if (isDialogPage(p)) {
        sceneLayoutDialog(p);
        return;
    }
    const int n = static_cast<int>(p.vis.size());
    // Rows sit in the scene's own SubSlot positions (canvas 1280x720 -> logical x 0.5).  The hit area covers the bar
    // plus the distance the highlighted row slides to the right.
    rowPx_ = static_cast<int>(kSlotStep * 0.5f);
    visibleRows_ = kSubRows;
    const int top = static_cast<int>(std::lround(kSlotY * 0.5f));
    listBox_ = Rect{0, top - 6, static_cast<int>(std::lround((426.0f + kSelShift) * 0.5f)), rowPx_ * kSubRows + 12};
    for (int k = 0; k < n; ++k) {
        const int y = top + static_cast<int>(std::lround((static_cast<float>(k) - p.scroll) * static_cast<float>(rowPx_)));
        if (y + rowPx_ <= top || y >= top + rowPx_ * kSubRows) continue;
        rows_.push_back({k, Rect{0, y, listBox_.w, rowPx_}});
    }
    // the arrows of a tapped choice row: left of the value = previous, right = next (the row may have slid right)
    float shift = 0;
    if (p.sel >= 0 && static_cast<size_t>(p.sel) < rowShift_.size()) shift = rowShift_[static_cast<size_t>(p.sel)];
    tapSplitX_ = static_cast<int>(std::lround((kSlotX + 604.7f + shift) * 0.5f));
}

std::string MenuScreen::headerTexture(const Page& root) const {
    if (root.vis.empty()) return {};
    const MenuOption& o = root.def.options[static_cast<size_t>(root.vis[static_cast<size_t>(std::clamp(root.sel, 0, static_cast<int>(root.vis.size()) - 1))])];
    const std::string& k = o.keyword;
    std::string stem;
    if (k == "@page:singlePlayer.ini") stem = "singleplayer";
    else if (k == "@page:versusSTEAM.ini") stem = "versus";
    else if (k == "@page:training.ini") stem = "training";
    else if (k == "@page:extras.ini") stem = "extras";
    else if (k == "@page:helpSTEAM.ini") stem = "help";
    else if (k == "@exit") stem = "exitgame";
    else if (k == "@download") stem = "download";
    else if (k == "@achievements") stem = "achievements";
    else if (k == "@leaderboards") stem = "leaderboards";
    if (stem.empty()) return {};
    GameUi& g = const_cast<GameUi&>(gfx_);
    if (langCode_ == "es" || langCode_ == "fr" || langCode_ == "de" || langCode_ == "it" || langCode_ == "jp") {
        const std::string loc = "Textures/MainMenu/" + langCode_ + ".header_" + stem + "01.png";
        if (g.hasTexture(loc)) return loc;
    }
    const std::string plain = "Textures/MainMenu/header_" + stem + "01.png";
    return g.hasTexture(plain) ? plain : std::string();
}

// ---------------------------------------------------------------------------------------- drawing

// The scenes' animations run on their own 60 Hz clock: a slow frame catches up (at most 8 ticks at once), a fast one waits.
int MenuScreen::stepAnims(int dtMs) {
    animAcc_ += std::clamp(dtMs, 0, 250);
    int n = 0;
    constexpr double kTickMs = 1000.0 / 60.0;
    while (animAcc_ >= kTickMs && n < 8) {
        animAcc_ -= kTickMs;
        ++n;
    }
    if (animAcc_ >= kTickMs) animAcc_ = 0;   // far behind: drop the rest instead of fast-forwarding
    return n;
}

void MenuScreen::playCues(GbsAnimPlayer& p) {
    const std::vector<GbsSoundCue> cues = p.takeCues();
    if (!audio_ || !gd_ || !gfx_.scene()) return;
    for (const GbsSoundCue& c : cues) {
        auto it = cueSfx_.find(c.soundId);
        if (it == cueSfx_.end()) {
            const std::string& name = gfx_.scene()->soundName(c.soundId);
            const int id = name.empty() ? -1 : audio_->loadSfx(gd_->vfs(), name);
            it = cueSfx_.emplace(c.soundId, id >= 0 ? id : -2).first;
        }
        if (it->second >= 0) audio_->playSfx(it->second, c.volume);
    }
}

// The photosensitivity warning: FrontCredits.gbs "PhotosensativeWarning" (white text on black, fading in and out over 360 ticks).  The game's
// Spanish file still has the English text, so Spanish uses the app's own translation; other languages use the game's string.
void MenuScreen::drawWarning(Ui& ui) {
    std::string text = langCode_ == "es" ? own("warn.text") : loc_.trOr("PhotosensitiveSplash", own("warn.text"));
    if (langCode_ != "es" && text.empty()) text = own("warn.text");
    const GbsSceneView* v = nullptr;
    if (sceneOk_ && gfx_.loadScene("FrontCredits.gbs") && gfx_.scene()) v = gfx_.scene()->view("PhotosensativeWarning");
    if (!v) {   // the scene is not in the mounted data: the same words on plain black
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 255});
        const int age = timeMs_ - warnT0_;
        const int a = std::clamp(std::min(age, 6200 - age) / 4, 0, 255);
        int y = 40;
        std::string line;
        for (size_t i = 0; i <= text.size(); ++i) {
            const char c = i < text.size() ? text[i] : '\n';
            if (c == '\n') {
                // wrap the paragraph at 70 columns of the small font
                std::string w, cur;
                for (size_t k = 0; k <= line.size(); ++k) {
                    const char d = k < line.size() ? line[k] : ' ';
                    if (d == ' ') {
                        if (!cur.empty() && Ui::textW(cur + " " + w) > 560) {
                            ui.text(40, y, cur, Col{255, 255, 255, static_cast<uint8_t>(a)});
                            y += 11;
                            cur = w;
                        } else {
                            cur = cur.empty() ? w : cur + " " + w;
                        }
                        w.clear();
                    } else {
                        w += d;
                    }
                }
                if (!cur.empty()) ui.text(40, y, cur, Col{255, 255, 255, static_cast<uint8_t>(a)});
                y += 14;
                line.clear();
            } else {
                line += c;
            }
        }
        return;
    }
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.setJapanese(langCode_ == "jp");
    if (!warnBound_) {
        warnBound_ = true;
        aWarn_.bind(v);
        aWarn_.play("PhotosensativeWarning");
        warnLastMs_ = timeMs_;
    }
    const int ticks = stepAnims(std::clamp(timeMs_ - warnLastMs_, 0, 100));
    warnLastMs_ = timeMs_;
    for (int i = 0; i < ticks; ++i) aWarn_.tick();
    gfx_.setAnimPlayer(&aWarn_);
    gfx_.drawView(*v, [&](const GbsNode& n, NodeState& st) {
        if (n.type == GbsNode::Type::Label && n.name == "PhotosensativeWarning") {
            st.hasText = true;
            st.text = text;
        }
    });
    gfx_.setAnimPlayer(nullptr);
    gfx_.loadScene("Main.gbs");   // back to the scene the menu reads its fonts and textures from
}

void MenuScreen::drawScene(Ui& ui, SDL_Renderer* r) {
    (void)r;
    gfx_.loadScene("Main.gbs");
    gfx_.setViewport(0, 0, 0.5f);
    gfx_.setJapanese(langCode_ == "jp");
    sceneDtMs_ = std::clamp(timeMs_ - lastSceneMs_, 0, 100);
    lastSceneMs_ = timeMs_;
    if (state_ != State::Title) titleAnimOn_ = false;
    const int ticks = stepAnims(sceneDtMs_);
    const bool inMenu = state_ != State::Title && state_ != State::CharSelect && !stack_.empty();
    if (!inMenu) menuLive_ = false;
    if (inMenu) {
        const Page& rp = stack_.front();
        const int rsel = std::clamp(rp.sel, 0, std::max(0, static_cast<int>(rp.vis.size()) - 1));
        if (!menuLive_) {   // the menu comes up (from the title or back from the selector): its own intro
            menuLive_ = true;
            aMenu_.activate(rsel, rootUsable());
            if (!stack_.back().root) aMenu_.enterSub(true);
        }
        const bool wantSub = !stack_.back().root;
        if (wantSub && aMenu_.mode() != MenuListAnim::Mode::Sub) aMenu_.enterSub();
        else if (!wantSub && aMenu_.mode() == MenuListAnim::Mode::Sub) aMenu_.leaveSub();
        aMenu_.setTarget(rsel, rootUsable());
    }
    for (int i = 0; i < ticks; ++i) {
        aBg_.tick();
        if (state_ == State::Title) aSplash_.tick();
        if (inMenu) {
            aMenu_.tick();
            if (!stack_.back().root) stepRowShift(stack_.back());
            if (isDialogPage(stack_.back())) aDlg_.tick();
        }
    }
    playCues(aBg_);
    if (inMenu) playCues(aMenu_.player());
    if (state_ == State::CharSelect) {
        charSel_.draw(gfx_, loc_, langCode_, timeMs_, "Main.gbs");
    } else if (state_ == State::Title) {
        sceneTitle();
    } else {
        Page& p = stack_.back();
        const std::string key = std::to_string(stack_.size()) + ":" + p.file;
        if (key != pageKey_) {
            pageKey_ = key;
            pageT0_ = timeMs_;
            rowShift_.assign(p.vis.size(), 0.0f);
        }
        layoutRows(p);
        if (p.root) sceneRoot(p);
        else if (isDialogPage(p)) sceneDialog(p);
        else scenePage(p);
    }
    sceneModals(ui);
}

void MenuScreen::sceneBackground() {
    PivotScope pivotScope(gfx_, true);
    gfx_.setAnimPlayer(&aBg_);
    gfx_.drawView(*vBg_, [&](const GbsNode& n, NodeState& st) {
        const std::string& nm = n.name;
        // the online lobby pieces of the view (globe, film strips) belong to other screens
        if (nm == "Globe_GRP" || nm == "BG_Filmstrips_GRP") st.hide = true;
    });
    gfx_.setAnimPlayer(nullptr);
}

void MenuScreen::sceneTitle() {
    PivotScope pivotScope(gfx_, true);
    sceneBackground();
    if (!titleAnimOn_) {   // the title just came up: the intro of the scene starts from tick 0
        titleAnimOn_ = true;
        pressOn_ = false;
        aSplash_.clear();
        aSplash_.play("OnActivate");
    }
    // the blinking prompt starts once the logo has landed
    if (!pressOn_ && aSplash_.tickOf("OnActivate") >= 36) {
        pressOn_ = true;
        aSplash_.play("PressButtonOn");
    }
    playCues(aSplash_);
    const std::string press = loc_.trOr("Press Any Button to Start", own("enter"));
    const std::string copy = loc_.trOr("Copyright", "");
    gfx_.setAnimPlayer(&aSplash_);
    gfx_.drawView(*vSplash_, [&](const GbsNode& n, NodeState& st) {
        const std::string& nm = n.name;
        if (nm == "ActiveUser") {
            st.hide = true;   // the online profile badge
        } else if (nm == "Encore_Glow") {
            st.additive = true;
        } else if (nm == "PressButton") {
            st.hasText = true;
            st.text = press;
        } else if (nm == "CopyrightNotice") {
            // the rest pose sits on the PressButton line; the original moves it from code: park it on the bottom edge
            st.hasText = !copy.empty();
            st.text = copy;
            st.dy = kTitleFooterDy;
        } else if (nm == "VersionNumber") {
            st.hasText = true;
            st.text = "ZDroid-Engine (unofficial)";
            st.dy = kTitleFooterDy;
            st.dx = kCanvasW - 18.0f - (n.x + n.w);   // right-aligned label: end it at the right edge of the screen
        }
    });
    gfx_.setAnimPlayer(nullptr);
}

void MenuScreen::sceneRoot(Page& p) {
    PivotScope pivotScope(gfx_, true);
    sceneBackground();
    const int n = static_cast<int>(p.vis.size());
    const int sel = std::clamp(p.sel, 0, std::max(0, n - 1));
    const std::string header = headerTexture(p);
    auto optionAt = [&](int idx) -> const MenuOption* {
        if (idx < 0 || idx >= n) return nullptr;
        return &p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(idx)])];
    };

    // description (+ the "not available" notes) of the highlighted entry
    std::string desc;
    bool warn = false;
    for (const auto& line : descriptionLines(p)) {
        if (!desc.empty()) desc += "\n";
        desc += line.first;
        if (line.second == 1) warn = true;
    }
    const bool touch = SDL_GetNumTouchDevices() > 0;
    const std::string hint = own(touch ? "hint.touch" : "hint.keys");
    const GbsAnimPlayer& pl = aMenu_.player();
    auto poseAlpha = [&](int id) {
        const GbsPose* ps = id >= 0 ? pl.pose(static_cast<uint32_t>(id)) : nullptr;
        return ps ? ps->alpha() : 1.0f;
    };

    // Everything moves by the scene's animations (see MenuListAnim): the film, the glow, the header art, the labels.  The
    // item names sit on top of the vertical film strip; the scene lists them before the strip, so they are drawn in a
    // second pass once the whole view is down.
    gfx_.setAnimPlayer(&pl);
    gfx_.drawView(*vMenu_, [&](const GbsNode& n2, NodeState& st) {
        const std::string& nm = n2.name;
        if (nm == "NewsFeed_Main_GRP") {
            // The news box rests below the screen; News_Frame_Anim_On slides it up and fades it in.  The real feed comes from
            // Lab Zero's server; offline the game falls back to NewsMessageDefault.
            return;
        }
        if (nm == "NewsFeed_BG01" || nm == "NewsFeed_BG02_GRP" || nm == "NewsFeed_BG03_GRP" || nm == "NewsFeed_Pip_Bar_TXT") {   // BG01 is a placeholder image: the real art comes with the online feed
            st.hide = true;
            return;
        }
        if (nm == "NewsFeed_Header_TXT") {
            st.hide = true;
            return;
        }
        if (nm == "NewsFeed_Body_TXT") {
            st.hasText = true;
            st.text = loc_.trOr("NewsMessageDefault", "Thank you for purchasing Skullgirls! Watch this space for upcoming announcements!");
            st.textColor = kWhite;
            // the scene's own label is one line without wrap (it expects a short headline from the news server); the offline
            // notice is a sentence, so it wraps into two lines inside the panel
            st.fitOverride = kFitWrap;
            st.textBoxH = 70.0f;
            return;
        }
        if (isCodeDriven(nm)) {
            st.hide = true;
            return;
        }
        if (nm == "MenuItemActive") {
            if (header.empty()) st.hide = true;
            else st.texName = header;
        } else if (nm == "ActiveItemDesc") {
            st.hasText = true;
            st.text = desc;
            st.textColor = warn ? kWarnCol : kWhite;
        } else if (nm == "Main Menu Icon") {
            // icon01 is the Versus emblem; every other entry of the PC edition shows the plain lit cell inside the glow ring
            // (no animation shows it: the game sets it from code, together with the ring above it - Glow Topper)
            const MenuOption* cur = optionAt(sel);
            const float ga = poseAlpha(glowId_);
            st.hide = true;
            if (ga > 0.0f) {
                if (cur && cur->keyword == "@page:versusSTEAM.ini") {
                    gfx_.drawTexture(gfx_.texture("Textures/MainMenu/icon01.png"), n2.x, n2.y, n2.w, n2.h, 0, 0, 0, false, false,
                                     Col{255, 255, 255, 255}, ga);
                } else {
                    const float cx = n2.x + n2.w * 0.5f, cy = n2.y + n2.h * 0.5f;   // inside the ring of overglow01 (about 63 px)
                    gfx_.fillRect(cx - 33.0f, cy - 33.0f, 66.0f, 66.0f, Col{226, 200, 166, static_cast<uint8_t>(150 * ga)});
                    gfx_.fillRect(cx - 29.0f, cy - 29.0f, 58.0f, 58.0f, Col{236, 214, 184, static_cast<uint8_t>(255 * ga)});
                }
            }
        } else if (nm == "Msg_Label") {
            st.hide = true;
        } else if (nm == "Button_Label") {
            st.hide = true;   // drawn last, with a shadow: the bottom spotlight washes plain text out
        } else if (nm == "MenuItemOffTop" || slotIndex(nm) >= 0) {
            st.hide = true;   // the labels of the list: second pass
        }
    });

    // second pass: only the labels of the list (MainMenuGRP > MenuGRP > MenuItemOffTop, MenuItem0..10), bound to the entries
    // the window shows right now; the poses place them, fade them and tint the locked ones
    gfx_.drawNodes(vMenu_->nodes, 0, 0, [&](const GbsNode& n2, NodeState& st) {
        const std::string& nm = n2.name;
        if (nm == "MainMenuGRP" || nm == "MenuGRP") return;
        const int si = nm == "MenuItemOffTop" ? -1 : slotIndex(nm);
        if (si < -1 || (si == -1 && nm != "MenuItemOffTop") || si > 10) {
            st.hide = true;
            return;
        }
        const MenuOption* o = optionAt(MenuListAnim::itemOf(si, aMenu_.listSel()));
        st.hasText = true;
        st.text = o ? L(o->text) : std::string();
        // locked entries get their dark tint from the poses (Menu<N>Locked...); the intro has none for them yet
        st.textColor = aMenu_.introRunning() && o && !optionUsable(*o) ? Col{64, 64, 64, 255} : kWhite;
        st.alignOverride = 2;
    });
    gfx_.setAnimPlayer(nullptr);

    if (hintSlot_.ok && poseAlpha(btnLabelId_) > 0.0f) {
        if (const GbsFont* fh = gfx_.font(15)) {  // 15 = "Dialogue Text", the font of Button_Label (honours Japanese)
            const Col hc{236, 228, 214, static_cast<uint8_t>(255 * poseAlpha(btnLabelId_))};
            // left: "Choose a Menu Option" (loc key "Label Text"), right: the real "Button Text MainMenu" row with its icons
            shadowLabel(gfx_, *fh, loc_.trOr("Label Text", "Choose a Menu Option"), 57.0f, hintSlot_.y, 560.0f, hintSlot_.h, 0, hc, 1.0f, 1.0f);
            if (touch) {
                shadowLabel(gfx_, *fh, hint, hintSlot_.x, hintSlot_.y, hintSlot_.w - 56.0f, hintSlot_.h, 2, hc, 0.8f, 1.0f);
            } else {
                const std::string row = loc_.trOr("Button Text MainMenu", "{gl}{gr}{srtg} News | {gu}{gd} Menu Select | {glk}{glp} Accept | {gmk}{gmp} Cancel");
                drawGlyphLine(gfx_, *fh, row, kCanvasW - 52.0f, hintSlot_.y, hintSlot_.h, hc, 0.9f);
            }
        }
    }


    // an entry with no header art (e.g. "Game Data") shows its name in the band instead
    if (header.empty() && n > 0) {
        if (const GbsFont* f = gfx_.fontNamed("SelznickTitleTexOtl")) {
            if (const MenuOption* o = optionAt(sel))
                gfx_.drawLabelText(*f, L(o->text), bandSlot_.x, bandSlot_.y, bandSlot_.w, bandSlot_.h, 1, true, kWhite, 1.7f, poseAlpha(activeId_));
        }
    }
}

// Options.gbs "Movelist": the dialog the PC edition shows the command lists in (movelist_dialog.cpp draws it; the rows are the page's).
void MenuScreen::sceneDialog(Page& p) {
    PivotScope pivotScope(gfx_, true);
    sceneBackground();   // (the film band, the lights: the dialog fades them out like the PC edition's BGFade)
    const int n = static_cast<int>(p.vis.size());
    const int sel = n > 0 ? std::clamp(p.sel, 0, n - 1) : 0;
    const std::string title = pageTitle();
    bool ok = false;
    if (dialogKind(p) == 1) {
        std::vector<MovelistRow> rows;
        rows.reserve(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            const MenuOption& o = p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(k)])];
            MovelistRow r;
            r.name = L(o.text);
            if (o.isMove && o.hasDescription()) r.command = L(o.description);
            r.kind = o.moveKind;
            r.indent = o.indent;
            rows.push_back(std::move(r));
        }
        ok = drawMovelistDialog(gfx_, aDlg_, dlgKey_, pageKey_, rows, sel, p.scroll, title, timeMs_ - pageT0_);
    } else {
        std::vector<OptionRow> rows;
        rows.reserve(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            const MenuOption& o = p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(k)])];
            OptionRow r;
            r.label = L(o.text);
            r.enabled = optionUsable(o);
            if (o.type == MenuOptType::Setting) {
                r.choice = true;
                const std::string cur = settingValue(o.keyword);
                r.value = L(cur);
                if (r.value.empty() && !o.choices.empty()) r.value = L(o.choices.front());
                if (o.choices.size() > 30) {   // the 0..100 volume lists do not wrap (see stepSetting): no arrow past either end
                    int idx = 0;
                    for (size_t i = 0; i < o.choices.size(); ++i)
                        if (o.choices[i] == cur) idx = static_cast<int>(i);
                    r.canPrev = idx > 0;
                    r.canNext = idx < static_cast<int>(o.choices.size()) - 1;
                }
            } else if (o.type == MenuOptType::Submenu && !(r.label.size() >= 3 && r.label.compare(r.label.size() - 3, 3, "...") == 0)) {
                r.label += "...";
            }
            rows.push_back(std::move(r));
        }
        std::string desc, note;
        bool warn = false;
        if (n > 0)
            for (const auto& line : descriptionLines(p)) {
                std::string& dst = line.second == 0 ? desc : note;
                if (!dst.empty()) dst += "  ";
                dst += line.first;
                if (line.second == 1) warn = true;
            }
        if (desc.empty()) desc.swap(note);   // a toast / a note on its own takes the description line
        ok = drawOptionsDialog(gfx_, aDlg_, dlgKey_, pageKey_, rows, sel, p.scroll, title, desc, note, warn, timeMs_ - pageT0_);
    }
    if (ok) {   // the key hint under the dialog (the frame ends at y 683)
        if (const GbsFont* fm = gfx_.fontNamed("Dialogue Text")) {
            const std::string hint = own(SDL_GetNumTouchDevices() > 0 ? "hint.touch" : "hint.keys");
            const float sc = std::min(0.78f, 1220.0f / std::max(1.0f, gfx_.textWidth(*fm, hint)));
            shadowLabel(gfx_, *fm, hint, 40.0f, 686.0f, 1220.0f, 30.0f, 2, Col{205, 195, 182, 255}, sc, 1.0f);
        }
    }
    gfx_.loadScene("Main.gbs");   // back to the scene the menu reads its fonts and textures from
    if (!ok) scenePage(p);       // the scene is not in the data: the plain page
}

void MenuScreen::scenePage(Page& p) {
    PivotScope pivotScope(gfx_, true);
    sceneBackground();
    const int n = static_cast<int>(p.vis.size());
    const int age = timeMs_ - pageT0_;
    const std::string header = stack_.empty() ? std::string() : headerTexture(stack_.front());
    const float bandDy = this->bandDy();

    // The scene's own pieces: the film band moved up (SubMenuIntro) and the film on the right, posed by the animations.
    // Item names, the icon and the glow belong to the root list; the rows themselves are built from code in the original.
    gfx_.setAnimPlayer(&aMenu_.player());
    gfx_.drawView(*vMenu_, [&](const GbsNode& n2, NodeState& st) {
        const std::string& nm = n2.name;
        if (isCodeDriven(nm) || nm == "Main Menu Icon" || nm == "Glow Topper" || nm == "MenuDescGRP" || nm == "MessageGRP" ||
            nm == "MenuItemOffTop" || slotIndex(nm) >= 0 || nm == "Underglow") {
            st.hide = true;
            return;
        }
        if (nm == "MenuItemActive") {
            if (header.empty()) st.hide = true;
            else st.texName = header;
        }
    });
    gfx_.setAnimPlayer(nullptr);
    if (header.empty()) {
        if (const GbsFont* f = gfx_.fontNamed("SelznickTitleTexOtl"))
            gfx_.drawLabelText(*f, pageTitle(), bandSlot_.x, bandSlot_.y + bandDy, bandSlot_.w, bandSlot_.h, 1, true, kWhite, 1.7f);
    }

    // ---- rows: SubSlot0..6 at x=-334, y=240+64k.  Advance rows = SubOptionHeader bar + black text window + bracket +
    // label (Data Frame Text); choice rows = SubOptionChoice bar + red label + arrows + value (Dialogue Text).
    const GbsFont* fAdv = gfx_.fontNamed("Data Frame Text");
    const GbsFont* fTxt = gfx_.fontNamed("Dialogue Text");
    SDL_Texture* barAdv = gfx_.texture("Textures/MainMenu/SubOptionHeader.png");
    SDL_Texture* barChoice = gfx_.texture("Textures/MainMenu/SubOptionChoice.png");
    SDL_Texture* bracket = gfx_.texture("Textures/charselect/charselect_data_frame_bracket01.png");
    SDL_Texture* arrowOff = gfx_.texture("Textures/MainMenu/arrow01.png");
    SDL_Texture* arrowOn = gfx_.texture("Textures/MainMenu/arrow02.png");

    if (rowShift_.size() != static_cast<size_t>(n)) rowShift_.assign(static_cast<size_t>(n), 0.0f);   // (the slide runs per tick: stepRowShift)

    SDL_Renderer* ren = ren_;
    if (ren) {
        SDL_Rect clip{0, listBox_.y, 640, listBox_.h};
        SDL_RenderSetClipRect(ren, &clip);
    }
    for (int vi = 0; vi < n; ++vi) {
        const float sy = kSlotY + kSlotStep * (static_cast<float>(vi) - p.scroll);
        if (sy + kSlotStep < kSlotY - 12.0f || sy > kSlotY + kSlotStep * kSubRows) continue;
        const MenuOption& o = p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(vi)])];
        const bool sel = vi == p.sel;
        const bool usable = optionUsable(o);
        // SubMenuIntro: the rows slide in from the left one after the other
        const float e = easeOutCubic(static_cast<float>(age - 45 * std::min(vi, 7)) / 260.0f);
        if (e <= 0.0f) continue;
        const float sx = kSlotX + rowShift_[static_cast<size_t>(vi)] - (1.0f - e) * 470.0f;
        const float a = e;
        const uint8_t shade = usable ? 255 : 120;
        const Col tint{shade, shade, shade, 255};
        Col fg = usable ? kWhite : kGrey;
        std::string label = L(o.text);
        if (o.isMove) {   // supers in gold, the follow-ups of a move (Indent) behind a dash
            if (o.moveKind.rfind("Super", 0) == 0) fg = Col{255, 214, 120, 255};
            if (o.indent > 0) label = "- " + label;
        }

        if (o.type == MenuOptType::Setting) {
            gfx_.drawTexture(barChoice, sx - 2.0f, sy + 1.0f, 762.0f, 64.0f, 0, 0, 0, false, false, tint, a);
            if (!fTxt) continue;
            std::string val = L(settingValue(o.keyword));
            if (val.empty() && !o.choices.empty()) val = L(o.choices.front());
            // label: left part of the bar, red
            const float lw = 156.0f;
            const float ls = std::min(1.0f, lw / std::max(1.0f, gfx_.textWidth(*fTxt, label)));
            gfx_.drawLabelText(*fTxt, label, sx + 444.0f - lw * 0.5f - 30.0f, sy + 14.0f, lw + 60.0f, 40.0f, 1, true, usable ? kChoiceRed : kGrey, ls, a);
            // value between the two arrows
            const float vw = 118.0f;
            const float vs = std::min(1.0f, vw / std::max(1.0f, gfx_.textWidth(*fTxt, val)));
            gfx_.drawLabelText(*fTxt, val, sx + 604.7f - vw * 0.5f - 30.0f, sy + 14.0f, vw + 60.0f, 40.0f, 1, true, fg, vs, a);
            SDL_Texture* ar = sel && usable ? arrowOn : arrowOff;
            gfx_.drawTexture(ar, sx + 516.6f, sy + 12.8f, 26.0f, 38.0f, 0, 0, 0, true, false, tint, a);
            gfx_.drawTexture(ar, sx + 666.9f, sy + 12.8f, 26.0f, 38.0f, 0, 0, 0, false, false, tint, a);
        } else {
            if (o.type == MenuOptType::Submenu && !(label.size() >= 3 && label.compare(label.size() - 3, 3, "...") == 0)) label += "...";
            gfx_.drawTexture(barAdv, sx - 2.0f, sy + 1.0f, 762.0f, 64.0f, 0, 0, 0, false, false, tint, a);
            gfx_.fillRect(sx + 392.3f, sy + 11.1f, 279.3f, 40.7f, Col{0, 0, 0, static_cast<uint8_t>(255.0f * a)});
            gfx_.drawTexture(bracket, sx + 288.2f, sy - 6.2f, 130.0f, 78.0f, 0, 0, 0, false, false, tint, a);
            if (fAdv) {
                const float lw = 262.0f;   // the window the text must fit in; the box is wider so that a rounding error never wraps it
                const float ls = std::min(1.0f, lw / std::max(1.0f, gfx_.textWidth(*fAdv, label)));
                gfx_.drawLabelText(*fAdv, label, sx + 541.0f - lw * 0.5f - 40.0f, sy + 11.5f, lw + 80.0f, 43.1f, 1, true, fg, ls, a);
            }
        }
    }
    if (ren) SDL_RenderSetClipRect(ren, nullptr);

    // scroll hints (page has more rows than fit)
    if (arrowOff && n > kSubRows) {
        const float ax = kSlotX + 762.0f + kSelShift + 24.0f;
        if (p.scroll > 0.01f) gfx_.drawTexture(arrowOff, ax, kSlotY + 12.0f, 26.0f, 38.0f, -90, 13, 19);
        if (p.scroll < static_cast<float>(n - kSubRows) - 0.01f)
            gfx_.drawTexture(arrowOff, ax, kSlotY + kSlotStep * (kSubRows - 1) + 12.0f, 26.0f, 38.0f, 90, 13, 19);
    }

    // ---- bottom: soft fade, the description of the highlighted row, the note about what is not available, the hint
    for (int k = 0; k < 8; ++k)
        gfx_.fillRect(0, 612.0f + 13.5f * static_cast<float>(k), kCanvasW, 13.6f, Col{0, 0, 0, static_cast<uint8_t>(20 * (k + 1))});
    if (const GbsFont* fm = gfx_.font(15)) {
        std::string desc, note;
        bool warn = false;
        for (const auto& line : descriptionLines(p)) {
            std::string& dst = line.second == 0 ? desc : note;
            if (!dst.empty()) dst += "  ";
            dst += line.first;
            if (line.second == 1) warn = true;
        }
        if (desc.empty()) desc.swap(note);   // a toast / a note on its own takes the description line
        const float dx = 57.0f, dw = 1166.0f;
        auto fit = [&](const std::string& s, float maxW) { return std::min(1.0f, maxW / std::max(1.0f, gfx_.textWidth(*fm, s))); };
        const MenuOption* selMove = (p.vis.empty() || toastMs_ > 0) ? nullptr : &p.def.options[static_cast<size_t>(p.vis[static_cast<size_t>(std::clamp(p.sel, 0, n - 1))])];
        if (selMove && selMove->isMove && selMove->hasDescription()) {   // a move: its command in button art, up to three lines
            drawCommandBlock(gfx_, *fm, L(selMove->description), dx, 616.0f, dw, 3, kWhite);
            note.clear();
        } else {
            shadowLabel(gfx_, *fm, desc, dx, 631.0f, dw, 36.0f, 0, kWhite, fit(desc, dw), 1.0f);
        }
        if (!note.empty()) shadowLabel(gfx_, *fm, note, dx, 657.0f, dw, 36.0f, 0, warn ? kWarnCol : Col{200, 192, 182, 255}, fit(note, dw), 1.0f);
        const bool touch = SDL_GetNumTouchDevices() > 0;
        const std::string hint = own(touch ? "hint.touch" : "hint.keys");
        shadowLabel(gfx_, *fm, hint, 40.0f, 684.0f, 1220.0f, 30.0f, 2, Col{205, 195, 182, 255}, std::min(0.78f, fit(hint, 1220.0f)), 1.0f);
    }
}

// The exit question is the game's own window when the data has it: opens it the first time it is asked.
bool MenuScreen::useDialog() {
    if (!sceneOk_) return false;
    if (dlgOk_ < 0) dlgOk_ = ConfirmDialog::available(gfx_, "Main.gbs") ? 1 : 0;
    if (dlgOk_ != 1) return false;
    if (!dlg_.isOpen()) {
        ConfirmDialog::Text t;
        t.plate = L("Player 1");
        t.title = L("Warningtitle");
        t.message = L("Confirmation");
        t.yes = L("Yes");
        t.no = L("No");
        dlg_.open(t, true);
    }
    return true;
}

void MenuScreen::sceneModals(Ui& ui) {
    if (colourEd_.isOpen()) {   // the player's own colours: panels over the dimmed selector; the result (saved / deleted) refreshes the colour list
        finishColourEdit(colourEd_.frame(ui, timeMs_));
        return;
    }
    static bool forced = false;   // smoke captures: ZDROID_MENU_MODAL=exit opens the exit question without a click
    if (!forced && std::getenv("ZDROID_MENU_MODAL")) {
        forced = true;
        confirmExit_ = true;
        modalSel_ = 1;
    }
    if (!confirmExit_ && dlg_.isOpen()) dlg_.close();   // dismissed by the back key
    if (!confirmExit_ && !controls_) return;
    if (confirmExit_ && useDialog() && dlg_.draw(gfx_, "Main.gbs")) return;
    const GbsFont* fTitle = gfx_.fontNamed("SelznickTitleTexOtl");
    const GbsFont* fTxt = gfx_.fontNamed("Dialogue Text");
    if (!fTitle || !fTxt) return;
    gfx_.fillRect(0, 0, kCanvasW, kCanvasH, Col{0, 0, 0, 175});
    auto panel = [&](float x, float y, float w, float h) {
        gfx_.fillRect(x - 4, y - 4, w + 8, h + 8, Col{196, 150, 110, 255});
        gfx_.fillRect(x, y, w, h, Col{24, 16, 18, 250});
    };
    SDL_Texture* bar = gfx_.texture("Textures/MainMenu/SubOptionChoice.png");
    if (confirmExit_) {
        panel(300, 220, 680, 300);
        gfx_.drawLabelText(*fTitle, L("Warningtitle"), 300, 236, 680, 40, 1, true, kWhite, 1.0f);
        gfx_.drawLabelText(*fTxt, L("Confirmation"), 330, 300, 620, 100, 1, false, kWhite, 1.0f);
        for (int i = 0; i < 2; ++i) {
            const float bx = i == 0 ? 392 : 668;
            const bool on = modalSel_ == i;
            const uint8_t shade = on ? 255 : 140;
            gfx_.drawTexture(bar, bx, 428, 220, 56, 0, 0, 0, false, false, Col{shade, shade, shade, 255});
            gfx_.drawLabelText(*fTxt, L(i == 0 ? "Yes" : "No"), bx, 428, 190, 56, 1, true, on ? kWarm : kGrey, 1.0f);
        }
        return;
    }
    panel(120, 140, 1040, 400);
    gfx_.drawLabelText(*fTitle, own("controls"), 150, 156, 980, 40, 0, true, kWhite, 1.0f);
    float y = 216;
    for (const char* k : {"c1", "c2", "c3", "c4"}) {
        for (const std::string& l : gfx_.wrap(*fTxt, own(k), 960)) {
            gfx_.drawLine(*fTxt, l, 150, y, kWhite);
            y += static_cast<float>(fTxt->lineHeight);
        }
        y += 12;
    }
    gfx_.drawLabelText(*fTxt, own("close"), 940, 470, 180, 40, 2, true, kWarm, 1.0f);
}

}  // namespace menu
}  // namespace zengine
