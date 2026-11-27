#include "fighter_pick.h"

#include <algorithm>
#include <cctype>

#include "art.h"
#include "charselect.h"
#include "vfs.h"

namespace zengine {
namespace fighters {
namespace {

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

constexpr int kW = 640, kH = 360;

}  // namespace

bool FighterPick::isBaseGameName(const std::string& name) {
    if (name == "bossmarie" || name == "hitspark") return true;
    for (int i = 0; i < menu::CharSelect::kRoster; ++i) {
        const char* rn = menu::CharSelect::rosterName(i);
        if (rn && name == rn) return true;
    }
    return false;
}

size_t FighterPick::customCount() const {
    size_t n = 0;
    for (const Fighter& f : list_) n += f.custom ? 1 : 0;
    return n;
}

const std::string& FighterPick::pickName(int side) const {
    static const std::string none;
    const int i = side_[side & 1].idx;
    return i >= 0 && i < static_cast<int>(list_.size()) ? list_[static_cast<size_t>(i)].name : none;
}

void FighterPick::close() {
    for (Side& s : side_) {
        s.prev.close();
        s.openedIdx = -1;
        s.previewOk = false;
        s.dirty = true;
    }
}

void FighterPick::setGameData(const GameData* gd, const Inventory* inv) {
    for (int i = 0; i < 2; ++i) keepName_[i] = pickName(i);   // keep the picks by name across a rescan
    close();
    gd_ = gd;
    inv_ = inv;
    gfxReady_ = false;
    list_.clear();
    stages_.clear();
    if (!inv) return;
    for (const CharacterAssets& c : inv->characters) {
        if (c.name == "hitspark" || (c.artPath.empty() && c.artPtPath.empty())) continue;   // the spark effect is not a fighter
        Fighter f;
        f.name = c.name;
        f.custom = !isBaseGameName(c.name);
        f.foit = c.foitPath;
        f.art = c.artPath;
        f.artPt = c.artPtPath;
        f.pals = c.palettes;
        if (gd)
            for (const palmod::Entry* e : gd->customPalettes().forCharacter(c.name)) f.pals.push_back({"mod:" + e->label, e->dds});
        list_.push_back(std::move(f));
    }
    // the game's own first (inventory order), the mods' after them
    std::stable_partition(list_.begin(), list_.end(), [](const Fighter& f) { return !f.custom; });
    if (!inv->levels.empty()) {
        stages_.push_back("random");
        for (const LevelInfo& l : inv->levels) stages_.push_back(l.name);
    }
    stageIdx_ = 0;

    const int n = static_cast<int>(list_.size());
    auto find = [&](const std::string& name) {
        for (int i = 0; i < n; ++i)
            if (list_[static_cast<size_t>(i)].name == name) return i;
        return -1;
    };
    int a = keepName_[0].empty() ? -1 : find(keepName_[0]);
    int b = keepName_[1].empty() ? -1 : find(keepName_[1]);
    if (a < 0) {   // first start: the first fighter a mod adds (that is what this screen is for), else the first of the game
        for (int i = 0; i < n && a < 0; ++i)
            if (list_[static_cast<size_t>(i)].custom) a = i;
        if (a < 0) a = 0;
    }
    if (b < 0) {
        b = find("filia");
        if (b < 0 || b == a) {
            b = -1;
            for (int i = 0; i < n && b < 0; ++i)
                if (i != a && !list_[static_cast<size_t>(i)].custom) b = i;
        }
        if (b < 0) b = n > 1 ? (a + 1) % n : 0;
    }
    side_[0].idx = std::max(0, a);
    side_[1].idx = std::max(0, b);
    side_[0].pal = side_[1].pal = 0;
}

void FighterPick::begin(const std::string& lang) {
    lang_ = lang.empty() ? "en" : lang;
    for (Side& s : side_) s.dirty = true;
}

void FighterPick::step(int side, int dir) {
    const int n = static_cast<int>(list_.size());
    if (n <= 0) return;
    Side& s = side_[side];
    s.idx = ((s.idx + dir) % n + n) % n;
    s.pal = 0;
    s.dirty = true;
}

void FighterPick::stepPal(int side, int dir) {
    Side& s = side_[side];
    if (s.idx < 0 || s.idx >= static_cast<int>(list_.size())) return;
    const int n = static_cast<int>(list_[static_cast<size_t>(s.idx)].pals.size());
    if (n <= 0) return;
    s.pal = ((s.pal + dir) % n + n) % n;
    s.dirty = true;
}

std::string FighterPick::palLabel(const Fighter& f, int pal) const {
    if (f.pals.empty()) return tr("DEFAULT", "NORMAL");
    const std::string& l = f.pals[static_cast<size_t>(std::clamp(pal, 0, static_cast<int>(f.pals.size()) - 1))].first;
    if (l.rfind("mod:", 0) == 0) return std::string("MOD ") + upper(l.substr(4));
    if (l.rfind("fav:", 0) == 0) return std::string(tr("FAV ", "FAV ")) + upper(l.substr(4));
    std::string num = l;
    if (!num.empty() && num.back() == 'p') num.pop_back();
    return std::string(tr("COLOR ", "COLOR ")) + num;
}

void FighterPick::refreshPreview(int side) {
    Side& s = side_[side];
    s.dirty = false;
    if (s.idx < 0 || s.idx >= static_cast<int>(list_.size())) {
        s.previewOk = false;
        return;
    }
    const Fighter& f = list_[static_cast<size_t>(s.idx)];
    Palette pal;
    bool have = false;
    if (!f.pals.empty()) {
        std::vector<uint8_t> bytes;
        const std::string& path = f.pals[static_cast<size_t>(std::clamp(s.pal, 0, static_cast<int>(f.pals.size()) - 1))].second;
        have = readFile(path, bytes) && readDds(bytes.data(), bytes.size(), pal) && pal.bands >= 2;
    }
    if (!have) {
        s.prev.close();
        s.openedIdx = -1;
        s.previewOk = false;
        return;
    }
    if (s.openedIdx == s.idx && s.prev.isOpen()) {   // only the colour changed: look the texels up again
        s.prev.paint(pal);
        s.previewOk = true;
        return;
    }
    s.previewOk = s.prev.openArt(gfx_, f.foit, f.art, f.artPt, pal);
    s.openedIdx = s.previewOk ? s.idx : -1;
    if (s.previewOk) s.prev.paint(pal);
}

void FighterPick::drawCard(Ui& ui, int side, Rect card) {
    Side& s = side_[side];
    const Fighter& f = list_[static_cast<size_t>(s.idx)];
    ui.fill(card, kPanel);
    ui.outline(card, kLine);
    ui.text(card.x + 10, card.y + 7, std::string(tr("PLAYER ", "JUGADOR ")) + (side == 0 ? "1" : "2"), kDim);

    // the pose, on a floor line (player 2 faces left in the fight, so its preview is mirrored)
    const Rect box{card.x + 10, card.y + 22, 100, 150};
    ui.fill(box, Col{20, 18, 26, 255});
    ui.outline(box, kLine);
    const int floorY = box.y + box.h - 12;
    ui.fill(Rect{box.x + 6, floorY, box.w - 12, 1}, Col{90, 84, 104, 255});
    if (s.previewOk && s.prev.isOpen()) {
        const SDL_Rect bb = s.prev.bounds();
        if (bb.w > 0 && bb.h > 0) {
            const float sc = std::min((box.w - 10.0f) / bb.w, (box.h - 22.0f) / bb.h);
            const float dw = bb.w * sc, dh = bb.h * sc;
            const SDL_Rect dst{static_cast<int>(box.x + (box.w - dw) / 2), static_cast<int>(floorY - dh), static_cast<int>(dw), static_cast<int>(dh)};
            SDL_RenderCopyEx(ui.r, s.prev.texture(), &bb, &dst, 0.0, nullptr, side == 1 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    } else {
        ui.textIn(Rect{box.x, box.y + box.h / 2 - 8, box.w, 16}, tr("NO PREVIEW", "SIN VISTA"), kDim, true);
    }

    const int bx = card.x + 118;
    const int base = side * 10;
    // fighter row
    const Rect lb{bx, card.y + 22, 34, 44}, nb{bx + 36, card.y + 22, 100, 44}, rb{bx + 138, card.y + 22, 34, 44};
    if (ui.button(base + 1, lb, "<")) step(side, -1);
    if (ui.button(base + 2, rb, ">")) step(side, +1);
    ui.fill(nb, kPanel2);
    ui.outline(nb, kLine);
    const std::string nm = upper(f.name);
    const int sc = Ui::textW(nm, 2) <= nb.w - 6 ? 2 : 1;
    ui.text(nb.x + (nb.w - Ui::textW(Ui::fit(nm, nb.w - 6), sc)) / 2, nb.y + (nb.h - 8 * sc) / 2, Ui::fit(nm, nb.w - 6), kText, sc);
    ui.textIn(Rect{bx, card.y + 72, 172, 12}, f.custom ? tr("FROM A MOD", "DE UN MOD") : tr("BASE GAME", "JUEGO BASE"), f.custom ? kGood : kDim, true);
    // colour row
    ui.text(bx, card.y + 92, tr("COLOR", "COLOR"), kDim);
    const Rect lp{bx, card.y + 104, 34, 34}, np{bx + 36, card.y + 104, 100, 34}, rp{bx + 138, card.y + 104, 34, 34};
    const bool many = f.pals.size() > 1;
    if (ui.button(base + 3, lp, "<", many)) stepPal(side, -1);
    if (ui.button(base + 4, rp, ">", many)) stepPal(side, +1);
    ui.fill(np, kPanel2);
    ui.outline(np, kLine);
    ui.textIn(np, palLabel(f, s.pal), kText, true);
    ui.textIn(Rect{bx, card.y + 146, 172, 12},
              std::to_string(s.idx + 1) + " / " + std::to_string(list_.size()) + "   " + std::to_string(f.pals.empty() ? 0 : s.pal + 1) + " / " + std::to_string(f.pals.size()), kDim,
              true);
}

Out FighterPick::frame(Ui& ui, SDL_Renderer* r, int dtMs) {
    (void)dtMs;
    Out out;
    if (!gfxReady_ && gd_) {
        gfx_.attach(r, &gd_->vfs());
        gfxReady_ = true;
        for (Side& s : side_) s.dirty = true;
    }
    ui.fill(Rect{0, 0, kW, kH}, kBg);
    const std::string title = tr("CUSTOM FIGHT", "COMBATE LIBRE");
    ui.text((kW - Ui::textW(title, 2)) / 2, 10, title, kText, 2);

    if (list_.empty()) {
        ui.textIn(Rect{20, 140, kW - 40, 12}, tr("NO FIGHTERS WERE FOUND IN THE DATA", "NO SE ENCONTRO NINGUN PERSONAJE EN LOS DATOS"), kWarn, true);
        if (ui.button(1, Rect{250, 300, 140, 40}, tr("BACK", "ATRAS"))) out.kind = Out::Kind::Back;
        return out;
    }
    for (int i = 0; i < 2; ++i)
        if (side_[i].dirty && gfxReady_) refreshPreview(i);

    const std::string sub = std::to_string(list_.size()) + tr(" FIGHTERS", " PERSONAJES") + "  -  " + std::to_string(customCount()) + tr(" FROM MODS", " DE MODS");
    ui.textIn(Rect{20, 32, kW - 40, 10}, sub, kDim, true);

    drawCard(ui, 0, Rect{16, 46, 300, 178});
    drawCard(ui, 1, Rect{324, 46, 300, 178});

    // mode
    static const char* kModesEn[3] = {"TRAINING", "VS CPU", "2 PLAYERS"};
    static const char* kModesEs[3] = {"ENTRENAMIENTO", "VS CPU", "2 JUGADORES"};
    for (int i = 0; i < 3; ++i)
        if (ui.button(40 + i, Rect{16 + i * 208, 232, 200, 34}, es() ? kModesEs[i] : kModesEn[i], true, modeIdx_ == i)) modeIdx_ = i;

    // stage
    if (!stages_.empty()) {
        const int n = static_cast<int>(stages_.size());
        if (ui.button(50, Rect{16, 272, 44, 32}, "<")) stageIdx_ = ((stageIdx_ - 1) % n + n) % n;
        if (ui.button(51, Rect{580, 272, 44, 32}, ">")) stageIdx_ = (stageIdx_ + 1) % n;
        ui.fill(Rect{64, 272, 512, 32}, kPanel2);
        ui.outline(Rect{64, 272, 512, 32}, kLine);
        const std::string& st = stages_[static_cast<size_t>(stageIdx_)];
        const std::string label = st == "random" ? std::string(tr("STAGE: RANDOM", "ESCENARIO: ALEATORIO")) : std::string(tr("STAGE: ", "ESCENARIO: ")) + upper(st);
        ui.textIn(Rect{68, 272, 504, 32}, label, kText, true);
    }

    if (ui.button(60, Rect{16, 314, 140, 38}, tr("BACK", "ATRAS"))) out.kind = Out::Kind::Back;
    bool fight = ui.button(61, Rect{324, 314, 300, 38}, tr("FIGHT!", "A PELEAR!"), true, true);
    // keyboard: left / right = player 1, up / down = player 2, Enter = fight
    if (ui.in.left) step(0, -1);
    if (ui.in.right) step(0, +1);
    if (ui.in.keyUp) step(1, -1);
    if (ui.in.keyDown) step(1, +1);
    if (ui.in.enter) fight = true;

    if (fight) {
        out.kind = Out::Kind::Fight;
        out.mode = modeIdx_ == 0 ? play::Mode::Training : modeIdx_ == 2 ? play::Mode::Versus : play::Mode::VsCpu;
        for (int i = 0; i < 2; ++i) {
            const Fighter& f = list_[static_cast<size_t>(side_[i].idx)];
            (i == 0 ? out.p1 : out.p2) = f.name;
            (i == 0 ? out.p1Pal : out.p2Pal) = f.pals.empty() ? std::string() : f.pals[static_cast<size_t>(std::clamp(side_[i].pal, 0, static_cast<int>(f.pals.size()) - 1))].second;
        }
        out.stage = stages_.empty() ? std::string() : stages_[static_cast<size_t>(stageIdx_)];
    }
    return out;
}

}  // namespace fighters
}  // namespace zengine
