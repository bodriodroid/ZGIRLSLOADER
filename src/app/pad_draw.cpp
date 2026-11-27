#include "pad_draw.h"

#include <algorithm>
#include <cmath>

#include "fight.h"

namespace zengine {
namespace padview {

namespace {

// how far a rounded corner of `radius` cuts into a row `y` rows from the top of a shape `h` rows tall
int inset(int y, int h, int radius) {
    const int r = std::min(radius, std::min(h / 2, 1000));
    if (r <= 0) return 0;
    const double d = (y < r) ? (r - y - 0.5) : (y >= h - r ? (y - (h - r) + 0.5) : -1.0);
    if (d < 0) return 0;
    const double over = static_cast<double>(r) * r - d * d;
    return r - static_cast<int>(std::lround(std::sqrt(std::max(0.0, over))));
}

Col withAlpha(Col c, int a) {
    c.a = static_cast<Uint8>(std::clamp(a, 0, 255));
    return c;
}

Col scaleRgb(Col c, float f) {
    auto s = [&](Uint8 v) { return static_cast<Uint8>(std::clamp(static_cast<int>(v * f + 0.5f), 0, 255)); };
    return Col{s(c.r), s(c.g), s(c.b), c.a};
}

void fillRow(Ui& ui, int x0, int x1, int y, Col c) {   // [x0, x1)
    if (x1 <= x0 || c.a == 0) return;
    ui.fill(Rect{x0, y, x1 - x0, 1}, c);
}

}  // namespace

void roundedRect(Ui& ui, Rect r, int radius, Col body, Col ring, int ringWidth) {
    if (r.w <= 0 || r.h <= 0) return;
    const int iw = std::max(0, ringWidth);
    const int innerR = std::max(0, radius - iw);
    for (int y = 0; y < r.h; ++y) {
        const int o = inset(y, r.h, radius);
        const int x0 = r.x + o, x1 = r.x + r.w - o;
        const int yi = y - iw, hi = r.h - 2 * iw;
        if (iw > 0 && yi >= 0 && yi < hi && hi > 0) {
            const int oi = iw + inset(yi, hi, innerR);
            const int ix0 = r.x + std::max(oi, o), ix1 = r.x + r.w - std::max(oi, o);
            fillRow(ui, x0, ix0, r.y + y, ring);
            fillRow(ui, ix0, ix1, r.y + y, body);
            fillRow(ui, ix1, x1, r.y + y, ring);
        } else {
            fillRow(ui, x0, x1, r.y + y, iw > 0 ? ring : body);
        }
    }
}

Col tint(uint16_t bits) {
    using namespace fight;
    const uint16_t punches = bits & (kLP | kMP | kHP), kicks = bits & (kLK | kMK | kHK);
    if (bits == kStart) return Col{170, 170, 190, 255};
    if (bits & (kMacro1 | kMacro2)) return (bits & kMacro1) ? Col{150, 130, 245, 255} : Col{236, 128, 170, 255};
    if (punches && kicks) return Col{190, 120, 235, 255};          // a punch + kick chord
    const bool multi = (punches & (punches - 1)) != 0 || (kicks & (kicks - 1)) != 0;   // two buttons of one kind
    if (punches) {
        if (multi) return Col{120, 130, 240, 255};
        return (punches & kLP) ? Col{120, 190, 255, 255} : (punches & kMP) ? Col{84, 150, 250, 255} : Col{62, 112, 232, 255};
    }
    if (kicks) {
        if (multi) return Col{240, 120, 110, 255};
        return (kicks & kLK) ? Col{255, 188, 110, 255} : (kicks & kMK) ? Col{248, 148, 84, 255} : Col{232, 108, 64, 255};
    }
    return Col{170, 170, 190, 255};
}

void draw(Ui& ui, const touch::Layout& L, const touch::TouchPad& pad, int selected) {
    const int a = std::clamp(L.opacity, 0, 255);
    auto al = [&](int v) { return std::clamp(v * a / 255, 0, 255); };
    const Col kAcc = kAccent;

    // ---- the stick: a dark base with a ring, eight direction dots (the active one lit) and the knob
    {
        const int bx = pad.baseX(), by = pad.baseY(), rad = L.scaledRadius();
        const int br = rad + 8;
        roundedRect(ui, Rect{bx - br, by - br, br * 2, br * 2}, br, Col{16, 18, 30, static_cast<Uint8>(al(150))},
                    Col{150, 156, 190, static_cast<Uint8>(al(190))}, 2);
        static const int dirs[8][3] = {{6, 1, 0}, {9, 1, -1}, {8, 0, -1}, {7, -1, -1}, {4, -1, 0}, {1, -1, 1}, {2, 0, 1}, {3, 1, 1}};
        for (const auto& d : dirs) {
            const double len = (d[1] != 0 && d[2] != 0) ? 0.7071 : 1.0;
            const int tx = bx + static_cast<int>(d[1] * len * (rad - 4)), ty = by + static_cast<int>(d[2] * len * (rad - 4));
            const bool lit = pad.direction() == d[0] && d[0] != 5;
            const int r = lit ? 5 : 2;
            ui.disc(tx, ty, r, r, lit ? kAcc : Col{170, 176, 205, static_cast<Uint8>(al(200))});
        }
        const bool active = pad.stickActive();
        const int kr = std::max(13, rad * 9 / 20);
        const int kx = pad.knobX(), ky = pad.knobY();
        roundedRect(ui, Rect{kx - kr, ky - kr, kr * 2, kr * 2}, kr, active ? Col{255, 120, 160, 255} : Col{88, 92, 124, static_cast<Uint8>(al(225))},
                    active ? Col{255, 255, 255, 255} : Col{206, 210, 236, static_cast<Uint8>(al(225))}, 3);
        ui.disc(kx - kr / 3, ky - kr / 3, std::max(2, kr / 3), std::max(2, kr / 4), Col{255, 255, 255, static_cast<Uint8>(active ? 120 : al(80))});   // the gloss
    }

    // ---- the buttons
    for (size_t i = 0; i < L.buttons.size(); ++i) {
        const touch::PadButton b = L.scaled(L.buttons[i]);
        Rect r{b.x, b.y, b.w, b.h};
        const bool held = pad.pressed(i);
        const Col t = tint(b.bits);
        const int rad = std::min(b.w, b.h) / 2;   // round for a normal button, a pill for a wide one
        if (std::abs(b.w - b.h) <= std::max(8, std::max(b.w, b.h) / 5)) {   // nearly square: a true circle, centred
            const int d = std::min(b.w, b.h);
            r = Rect{b.x + (b.w - d) / 2, b.y + (b.h - d) / 2, d, d};
        }
        if (held) {
            roundedRect(ui, r, rad, withAlpha(t, 240), Col{255, 255, 255, 255}, 3);
        } else {
            roundedRect(ui, r, rad, withAlpha(scaleRgb(t, 0.28f), al(150)), withAlpha(t, al(215)), 3);
        }
        const Col lab = held ? Col{255, 255, 255, 255} : withAlpha(Col{238, 240, 250, 255}, al(240));
        const int sc = (b.h >= 40 && b.w >= 40 && Ui::glyphCount(b.label) <= 2) ? 2 : 1;
        const int tw = Ui::textW(b.label, sc);
        ui.text(r.x + (r.w - tw) / 2, r.y + (r.h - 8 * sc) / 2, b.label, lab, sc);
    }

    // ---- the editor's selection
    if (selected != -1) {
        Rect box{};
        if (selected == kStick) {
            const int rr = L.scaledRadius() + 12;
            box = Rect{pad.baseX() - rr, pad.baseY() - rr, rr * 2, rr * 2};
        } else if (selected >= 0 && static_cast<size_t>(selected) < L.buttons.size()) {
            const touch::PadButton b = L.scaled(L.buttons[static_cast<size_t>(selected)]);
            box = Rect{b.x - 4, b.y - 4, b.w + 8, b.h + 8};
        }
        if (box.w > 0) {
            ui.outline(box, Col{255, 214, 96, 255});
            ui.outline(Rect{box.x + 1, box.y + 1, box.w - 2, box.h - 2}, Col{255, 214, 96, 140});
        }
    }
}

}  // namespace padview
}  // namespace zengine
