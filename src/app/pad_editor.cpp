#include "pad_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "app_paths.h"
#include "pad_draw.h"
#include "savini.h"

namespace zengine {
namespace padedit {

namespace {
constexpr int kNone = -3;
constexpr const char* kScaleKey = "ZD_TOUCH_SCALE";
constexpr const char* kOpacityKey = "ZD_TOUCH_OPACITY";
constexpr Rect kPanel{184, 30, 272, 196};
}  // namespace

bool loadSaved(touch::Layout& out) {
    std::ifstream f(apppaths::prefFile("touch_layout.txt"));
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    touch::Layout l;
    if (!touch::Layout::parse(ss.str(), l)) return false;
    out = l;
    return true;
}

bool save(const touch::Layout& l) {
    std::ofstream f(apppaths::prefFile("touch_layout.txt"), std::ios::trunc);
    if (!f) return false;
    f << l.serialize();
    return static_cast<bool>(f);
}

void clearSaved() { std::remove(apppaths::prefFile("touch_layout.txt").c_str()); }

int PadEditor::optScale() const {
    const std::string v = SavIni::shared().get("options", kScaleKey, "");
    return v.empty() ? 100 : std::clamp(std::atoi(v.c_str()), 60, 150);
}

int PadEditor::optOpacity() const {
    const std::string v = SavIni::shared().get("options", kOpacityKey, "");
    return v.empty() ? 60 : std::clamp(std::atoi(v.c_str()), 20, 100);
}

void PadEditor::begin(const std::string& lang) {
    lang_ = lang.empty() ? "en" : lang;
    base_ = touch::Layout::defaults();
    loadSaved(base_);
    base_.scale = 1.0f;   // the size is the option, not part of the file the editor writes
    sel_ = -1;
    drag_ = kNone;
    adding_ = false;
    startScale_ = optScale();
    startOpacity_ = optOpacity();
    startText_ = base_.serialize();
    toast_.clear();
    toastMs_ = 0;
}

void PadEditor::pick(int mx, int my) {
    sel_ = -1;
    drag_ = kNone;
    for (int i = static_cast<int>(base_.buttons.size()) - 1; i >= 0; --i) {   // the one drawn last is on top
        const touch::PadButton b = view_.scaled(base_.buttons[static_cast<size_t>(i)]);
        if (b.contains(mx, my, 4)) {
            sel_ = drag_ = i;
            grabX_ = mx - (b.x + b.w / 2);
            grabY_ = my - (b.y + b.h / 2);
            return;
        }
    }
    const int rr = view_.scaledRadius() + 12;
    const int dx = mx - base_.stickX, dy = my - base_.stickY;
    if (dx * dx + dy * dy <= rr * rr) {
        sel_ = drag_ = padview::kStick;
        grabX_ = dx;
        grabY_ = dy;
    }
}

void PadEditor::dragTo(int mx, int my) {
    auto snap = [](int v) { return (v + 1) / 2 * 2; };
    if (drag_ == padview::kStick) {
        base_.moveStick(snap(mx - grabX_), snap(my - grabY_));
    } else if (drag_ >= 0 && static_cast<size_t>(drag_) < base_.buttons.size()) {
        touch::PadButton& b = base_.buttons[static_cast<size_t>(drag_)];
        base_.moveButton(static_cast<size_t>(drag_), snap(mx - grabX_) - b.w / 2, snap(my - grabY_) - b.h / 2);
    }
}

void PadEditor::drawBackdrop(Ui& ui) const {
    for (int y = 0; y < 360; y += 4) {
        const float f = static_cast<float>(y) / 360.0f;
        ui.fill(Rect{0, y, 640, 4}, Col{static_cast<Uint8>(26 - 10 * f), static_cast<Uint8>(28 - 10 * f), static_cast<Uint8>(48 - 22 * f), 255});
    }
    ui.fill(Rect{0, 296, 640, 64}, Col{36, 30, 44, 255});   // the floor
    ui.fill(Rect{0, 296, 640, 1}, Col{70, 62, 84, 255});
    for (int x = 40; x < 640; x += 40)                       // a grid to place things by
        for (int y = 40; y < 360; y += 40) ui.fill(Rect{x, y, 1, 1}, Col{90, 92, 120, 255});
    // a match HUD in outline, so the player sees what the pad must leave free
    ui.fill(Rect{14, 10, 250, 12}, Col{54, 56, 76, 255});
    ui.fill(Rect{376, 10, 250, 12}, Col{54, 56, 76, 255});
    ui.fill(Rect{296, 8, 48, 18}, Col{54, 56, 76, 255});
    ui.fill(Rect{228, 236, 36, 60}, Col{50, 52, 74, 255});   // two stand-ins for the fighters
    ui.fill(Rect{376, 236, 36, 60}, Col{50, 52, 74, 255});
    // where a finger takes the stick
    ui.fill(Rect{base_.zoneLeft, base_.zoneTop, std::max(0, std::min(640, base_.zoneRight) - base_.zoneLeft), 360 - base_.zoneTop}, Col{232, 72, 120, 20});
    const std::string z = tr("STICK ZONE", "ZONA DEL STICK");
    ui.text(base_.zoneLeft + 6, base_.zoneTop + 4, z, Col{232, 72, 120, 150});
}

void PadEditor::drawPresets(Ui& ui) {
    ui.fill(kPanel, Col{14, 14, 22, 244});
    ui.outline(kPanel, Col{190, 120, 150, 255});
    const std::string t = tr("ADD A BUTTON", "AÑADIR UN BOTON");
    ui.text(kPanel.x + (kPanel.w - Ui::textW(t)) / 2, kPanel.y + 6, t, kText);
    const auto& pre = touch::Layout::presets();
    constexpr int cw = 48, ch = 22, gap = 4, cols = 5;
    const int x0 = kPanel.x + (kPanel.w - (cols * cw + (cols - 1) * gap)) / 2, y0 = kPanel.y + 24;
    for (size_t i = 0; i < pre.size(); ++i) {
        const Rect r{x0 + static_cast<int>(i % cols) * (cw + gap), y0 + static_cast<int>(i / cols) * (ch + gap), cw, ch};
        if (ui.button(300 + static_cast<int>(i), r, pre[i].label)) {
            const int idx = base_.addButton(pre[i]);
            if (idx < 0) {
                say(tr("NO ROOM: DELETE A BUTTON FIRST", "NO CABE: QUITA UN BOTON"));
            } else {
                sel_ = idx;
                base_.fitZone();
                say(tr("ADDED: DRAG IT WHERE YOU WANT IT", "AÑADIDO: ARRASTRALO DONDE QUIERAS"));
            }
            adding_ = false;
        }
        // a small swatch of the colour the button will have
        ui.fill(Rect{r.x + 3, r.y + r.h - 4, r.w - 6, 2}, padview::tint(pre[i].bits));
    }
    if (ui.button(330, Rect{kPanel.x + 8, kPanel.y + kPanel.h - 28, kPanel.w - 16, 20}, tr("CANCEL", "CANCELAR"))) adding_ = false;
}

void PadEditor::drawPanel(Ui& ui, Out& out) {
    if (adding_) {
        drawPresets(ui);
        return;
    }
    ui.fill(kPanel, Col{14, 14, 22, 232});
    ui.outline(kPanel, Col{120, 124, 160, 255});
    int y = kPanel.y + 6;
    const std::string title = tr("TOUCH CONTROLS", "CONTROLES TACTILES");
    ui.text(kPanel.x + (kPanel.w - Ui::textW(title)) / 2, y, title, kText);
    y += 16;

    // the selected element
    std::string what;
    if (sel_ == padview::kStick) what = tr("STICK", "STICK");
    else if (sel_ >= 0 && static_cast<size_t>(sel_) < base_.buttons.size()) what = base_.buttons[static_cast<size_t>(sel_)].label;
    const std::string hint = what.empty() ? std::string(tr("DRAG ONE TO MOVE IT", "ARRASTRA UNO PARA MOVERLO")) : std::string(tr("SELECTED: ", "ELEGIDO: ")) + what;
    ui.text(kPanel.x + (kPanel.w - Ui::textW(hint)) / 2, y, hint, what.empty() ? kDim : kWarn);
    y += 14;

    const int lx = kPanel.x + 8, bw = 22, rowH = 20;
    auto stepper = [&](int idBase, int row, const std::string& label, const std::string& value, bool enabled, int& step) {
        const int ry = y + row * (rowH + 4);
        ui.text(lx, ry + 6, label, enabled ? kDim : Col{70, 70, 86, 255});
        const int vx = kPanel.x + kPanel.w - 8 - bw * 2 - 54;
        if (ui.button(idBase, Rect{vx, ry, bw, rowH}, "-", enabled)) step = -1;
        const Rect vr{vx + bw, ry, 54, rowH};
        ui.textIn(vr, value, enabled ? kText : Col{70, 70, 86, 255}, true);
        if (ui.button(idBase + 1, Rect{vx + bw + 54, ry, bw, rowH}, "+", enabled)) step = +1;
    };

    // size of the selected element (+ delete / floating)
    {
        int step = 0;
        const bool isBtn = sel_ >= 0 && static_cast<size_t>(sel_) < base_.buttons.size();
        const bool isStick = sel_ == padview::kStick;
        std::string val = "-";
        if (isBtn) val = std::to_string(base_.buttons[static_cast<size_t>(sel_)].w);
        if (isStick) val = std::to_string(base_.stickRadius);
        stepper(10, 0, tr("SIZE", "TAMAÑO"), val, isBtn || isStick, step);
        if (step != 0) {
            if (isBtn) base_.resizeButton(static_cast<size_t>(sel_), base_.buttons[static_cast<size_t>(sel_)].w + step * 4);
            else base_.setStickRadius(base_.stickRadius + step * 4);
            base_.fitZone();
        }
        const int ry = y + (rowH + 4);
        if (isStick) {
            if (ui.button(14, Rect{lx, ry, kPanel.w - 16, rowH}, std::string(tr("FLOATING STICK: ", "STICK FLOTANTE: ")) + (base_.floating ? tr("ON", "SI") : "NO"))) base_.floating = !base_.floating;
        } else if (ui.button(14, Rect{lx, ry, kPanel.w - 16, rowH}, tr("DELETE THIS BUTTON", "QUITAR ESTE BOTON"), isBtn)) {
            if (base_.removeButton(static_cast<size_t>(sel_))) {
                sel_ = -1;
                base_.fitZone();
            } else {
                say(tr("THE LAST BUTTON STAYS", "EL ULTIMO BOTON SE QUEDA"));
            }
        }
    }
    y += 2 * (rowH + 4) + 4;

    // the whole pad
    {
        int s1 = 0, s2 = 0;
        stepper(20, 0, tr("PAD SIZE", "TAMAÑO PAD"), std::to_string(optScale()) + "%", true, s1);
        stepper(22, 1, tr("OPACITY", "OPACIDAD"), std::to_string(optOpacity()) + "%", true, s2);
        if (s1 != 0) SavIni::shared().setInt("options", kScaleKey, std::clamp(optScale() + s1 * 10, 60, 150));
        if (s2 != 0) SavIni::shared().setInt("options", kOpacityKey, std::clamp(optOpacity() + s2 * 10, 20, 100));
    }
    y += 2 * (rowH + 4) + 2;

    const int third = (kPanel.w - 16 - 8) / 3;
    if (ui.button(30, Rect{lx, y, third, rowH}, tr("ADD", "AÑADIR"))) adding_ = true;
    if (ui.button(31, Rect{lx + third + 4, y, third, rowH}, tr("SWAP HAND", "INVERTIR"))) {
        base_ = base_.mirrored();
        say(base_.leftHanded() ? tr("STICK ON THE RIGHT", "STICK A LA DERECHA") : tr("STICK ON THE LEFT", "STICK A LA IZQUIERDA"));
    }
    if (ui.button(32, Rect{lx + 2 * (third + 4), y, third, rowH}, tr("RESET", "RESTAURAR"))) {
        const bool left = false;
        (void)left;
        base_ = touch::Layout::defaults();
        sel_ = -1;
        say(tr("BACK TO THE DEFAULT LAYOUT", "DISEÑO ORIGINAL"));
    }
    y += rowH + 4;
    const int half = (kPanel.w - 16 - 4) / 2;
    if (ui.button(40, Rect{lx, y, half, rowH}, tr("CANCEL", "CANCELAR"))) {
        SavIni::shared().setInt("options", kScaleKey, startScale_);
        SavIni::shared().setInt("options", kOpacityKey, startOpacity_);
        out.done = true;
    }
    if (ui.button(41, Rect{lx + half + 4, y, half, rowH}, tr("SAVE", "GUARDAR"), true, true)) {
        base_.scale = 1.0f;
        if (save(base_)) {
            out.done = out.saved = true;
        } else {
            say(tr("COULD NOT SAVE", "NO SE PUDO GUARDAR"));
        }
    }
}

Out PadEditor::frame(Ui& ui, SDL_Renderer*, int dtMs) {
    Out out;
    toastMs_ = std::max(0, toastMs_ - dtMs);
    view_ = base_;
    view_.scale = static_cast<float>(optScale()) / 100.0f;
    view_.opacity = std::max(90, optOpacity() * 255 / 100);   // never fainter than this here: the editor must show what it edits

    // ---- the pointer: pick on press (outside the panel), move while held
    if (ui.in.pressed && !kPanel.contains(ui.in.mx, ui.in.my)) pick(ui.in.mx, ui.in.my);
    if (drag_ != kNone && ui.in.down) dragTo(ui.in.mx, ui.in.my);
    if (ui.in.released && drag_ != kNone) {
        base_.fitZone();
        drag_ = kNone;
    }
    if (sel_ != -1) {   // arrow keys nudge the selection (desktop)
        const int dx = (ui.in.right ? 2 : 0) - (ui.in.left ? 2 : 0), dy = (ui.in.keyDown ? 2 : 0) - (ui.in.keyUp ? 2 : 0);
        if (dx || dy) {
            if (sel_ == padview::kStick) base_.moveStick(base_.stickX + dx, base_.stickY + dy);
            else if (static_cast<size_t>(sel_) < base_.buttons.size()) base_.moveButton(static_cast<size_t>(sel_), base_.buttons[static_cast<size_t>(sel_)].x + dx, base_.buttons[static_cast<size_t>(sel_)].y + dy);
            base_.fitZone();
        }
    }
    if (ui.in.back) {
        SavIni::shared().setInt("options", kScaleKey, startScale_);
        SavIni::shared().setInt("options", kOpacityKey, startOpacity_);
        out.done = true;
    }

    view_ = base_;   // (the pointer may have moved things)
    view_.scale = static_cast<float>(optScale()) / 100.0f;
    view_.opacity = std::max(90, optOpacity() * 255 / 100);
    drawBackdrop(ui);
    touch::TouchPad idle(view_);
    idle.reset();   // the stick at its resting place
    padview::draw(ui, view_, idle, sel_);
    drawPanel(ui, out);

    if (toastMs_ > 0 && !toast_.empty()) {
        const int w = Ui::textW(toast_) + 16;
        const Rect r{(640 - w) / 2, 324, w, 20};
        ui.fill(r, Col{10, 10, 16, 230});
        ui.outline(r, kWarn);
        ui.textIn(r, toast_, kText, true);
    }
    return out;
}

}  // namespace padedit
}  // namespace zengine
