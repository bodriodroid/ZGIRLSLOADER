#include "touch_pad.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "fight.h"

namespace zengine {
namespace touch {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

Layout Layout::defaults() {
    using namespace fight;
    Layout l;
    // punches on top, kicks below - the arcade layout of the PC version's default pad mapping
    const int bw = 50, bh = 46, gx = 8, gy = 8, x0 = 404, y0 = 226;
    l.buttons = {
        {x0, y0, bw, bh, kLP, "LP"},
        {x0 + (bw + gx), y0 - 10, bw, bh, kMP, "MP"},
        {x0 + 2 * (bw + gx), y0 - 14, bw, bh, kHP, "HP"},
        {x0, y0 + bh + gy, bw, bh, kLK, "LK"},
        {x0 + (bw + gx), y0 + bh + gy - 10, bw, bh, kMK, "MK"},
        {x0 + 2 * (bw + gx), y0 + bh + gy - 14, bw, bh, kHK, "HK"},
        // macros, on the outside edge where the thumb rolls to them
        {x0 + 3 * (bw + gx), y0 - 14, 46, bh, kMacro1, "3P"},
        {x0 + 3 * (bw + gx), y0 + bh + gy - 14, 46, bh, kMacro2, "3K"},
        {x0 - 54, y0 + 22, 44, 50, static_cast<uint16_t>(kLP | kLK), "P+K"},
        {590, 8, 42, 26, kStart, "ST"},
    };
    return l;
}

Layout Layout::widened(const Layout& base, int extra) {
    Layout l = base;
    if (extra <= 0) return l;
    for (PadButton& b : l.buttons)
        if (b.x + b.w / 2 >= 320) b.x += extra;
    if (l.stickX >= 320) {   // a left-handed layout: the stick sits on the right and goes with that edge
        l.stickX += extra;
        l.zoneLeft += extra;
        l.zoneRight += extra;
    }
    return l;
}

const std::vector<Layout::Preset>& Layout::presets() {
    using namespace fight;
    static const std::vector<Preset> p = {
        {"LP", kLP}, {"MP", kMP}, {"HP", kHP}, {"LK", kLK}, {"MK", kMK}, {"HK", kHK}, {"3P", kMacro1}, {"3K", kMacro2},
        {"P+K", static_cast<uint16_t>(kLP | kLK)},
        {"LP+MP", static_cast<uint16_t>(kLP | kMP)}, {"MP+HP", static_cast<uint16_t>(kMP | kHP)},
        {"LK+MK", static_cast<uint16_t>(kLK | kMK)}, {"MK+HK", static_cast<uint16_t>(kMK | kHK)},
        {"HP+HK", static_cast<uint16_t>(kHP | kHK)}, {"ST", kStart},
    };
    return p;
}

namespace {
bool overlapsAny(const Layout& l, int x, int y, int w, int h, size_t skip) {
    for (size_t i = 0; i < l.buttons.size(); ++i) {
        if (i == skip) continue;
        const PadButton& b = l.buttons[i];
        if (x < b.x + b.w + 4 && x + w + 4 > b.x && y < b.y + b.h + 4 && y + h + 4 > b.y) return true;
    }
    return false;
}
}  // namespace

int Layout::addButton(const Preset& p, int w, int h) {
    if (buttons.size() >= kMaxButtons) return -1;
    const bool start = p.bits == fight::kStart;
    PadButton b{0, 0, start ? 42 : 46, start ? 26 : 46, p.bits, p.label};
    // the first free spot, from the lower right (where the thumb rests) towards the upper left
    int bx = w / 2, by = h / 2;
    bool found = false;
    for (int y = h - b.h - 8; y >= 40 && !found; y -= 10)
        for (int x = w - b.w - 8; x >= 8 && !found; x -= 10)
            if (!overlapsAny(*this, x, y, b.w, b.h, buttons.size())) {
                bx = x;
                by = y;
                found = true;
            }
    b.x = bx;
    b.y = by;
    buttons.push_back(b);
    return static_cast<int>(buttons.size()) - 1;
}

bool Layout::removeButton(size_t i) {
    if (i >= buttons.size() || buttons.size() <= 1) return false;
    buttons.erase(buttons.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
}

void Layout::moveButton(size_t i, int x, int y, int w, int h) {
    if (i >= buttons.size()) return;
    PadButton& b = buttons[i];
    b.x = std::clamp(x, 0, std::max(0, w - b.w));
    b.y = std::clamp(y, 0, std::max(0, h - b.h));
}

void Layout::resizeButton(size_t i, int newW, int w, int h) {
    if (i >= buttons.size()) return;
    PadButton& b = buttons[i];
    const int nw = std::clamp(newW, 24, 120);
    const int nh = std::max(18, static_cast<int>(static_cast<float>(b.h) * static_cast<float>(nw) / static_cast<float>(std::max(1, b.w)) + 0.5f));
    const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    b.w = nw;
    b.h = std::min(nh, 120);
    moveButton(i, cx - b.w / 2, cy - b.h / 2, w, h);
}

void Layout::setStickRadius(int r, int w, int h) {
    stickRadius = std::clamp(r, 30, 90);
    moveStick(stickX, stickY, w, h);
}

void Layout::moveStick(int x, int y, int w, int h) {
    const int r = scaledRadius();
    stickX = std::clamp(x, r, std::max(r, w - r));
    stickY = std::clamp(y, r, std::max(r, h - r));
    fitZone(w);
}

void Layout::fitZone(int w) {
    const int reach = stickRadius + 136;
    if (stickX < w / 2) {   // right-handed: the stick zone starts at the left edge
        int stop = stickX + reach;
        for (const PadButton& b : buttons)
            if (b.x + b.w / 2 > stickX) stop = std::min(stop, b.x - 6);
        zoneLeft = 0;
        zoneRight = std::max(stickX + stickRadius + 16, stop);
    } else {
        int start = stickX - reach;
        for (const PadButton& b : buttons)
            if (b.x + b.w / 2 < stickX) start = std::max(start, b.x + b.w + 6);
        zoneRight = w;
        zoneLeft = std::min(stickX - stickRadius - 16, start);
    }
    zoneLeft = std::max(0, zoneLeft);
}

Layout Layout::mirrored(int w) const {
    Layout l = *this;
    for (PadButton& b : l.buttons) b.x = w - b.x - b.w;
    l.stickX = w - stickX;
    l.zoneLeft = std::max(0, w - zoneRight);
    l.zoneRight = std::max(l.zoneLeft + 40, w - zoneLeft);
    return l;
}

PadButton Layout::scaled(const PadButton& b) const {
    if (scale == 1.0f) return b;
    PadButton r = b;
    const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
    r.w = static_cast<int>(b.w * scale + 0.5f);
    r.h = static_cast<int>(b.h * scale + 0.5f);
    r.x = static_cast<int>(cx - r.w * 0.5f);
    r.y = static_cast<int>(cy - r.h * 0.5f);
    return r;
}

std::string Layout::serialize() const {
    std::ostringstream o;
    o << "zdroid-touch 1\n";
    o << "stick " << stickX << ' ' << stickY << ' ' << stickRadius << ' ' << deadZone << ' ' << diagonalDeg << ' '
      << (floating ? 1 : 0) << ' ' << zoneRight << ' ' << zoneTop << ' ' << zoneLeft << '\n';
    o << "grow " << grow << '\n';
    o << "scale " << scale << '\n';
    o << "opacity " << opacity << '\n';
    for (const PadButton& b : buttons)
        o << "button " << b.label << ' ' << b.x << ' ' << b.y << ' ' << b.w << ' ' << b.h << ' ' << b.bits << '\n';
    return o.str();
}

bool Layout::parse(const std::string& text, Layout& out) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("zdroid-touch 1", 0) != 0) return false;
    Layout l = defaults();
    std::vector<PadButton> btns;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;
        if (key == "stick") {
            int fl = 1;
            if (!(ls >> l.stickX >> l.stickY >> l.stickRadius >> l.deadZone >> l.diagonalDeg >> fl >> l.zoneRight >> l.zoneTop))
                return false;
            l.floating = fl != 0;
            int zl = 0;
            l.zoneLeft = (ls >> zl) ? zl : 0;   // older files have no left edge
        } else if (key == "grow") {
            if (!(ls >> l.grow)) return false;
        } else if (key == "scale") {
            if (!(ls >> l.scale)) return false;
        } else if (key == "opacity") {
            if (!(ls >> l.opacity)) return false;
        } else if (key == "button") {
            PadButton b{};
            unsigned bits = 0;
            if (!(ls >> b.label >> b.x >> b.y >> b.w >> b.h >> bits)) return false;
            b.bits = static_cast<uint16_t>(bits);
            btns.push_back(b);
        }
    }
    l.scale = std::clamp(l.scale, 0.5f, 2.0f);
    l.opacity = std::clamp(l.opacity, 0, 255);
    l.stickRadius = std::clamp(l.stickRadius, 20, 120);
    l.deadZone = std::clamp(l.deadZone, 0, l.stickRadius - 1);
    l.diagonalDeg = std::clamp(l.diagonalDeg, 20, 70);
    // a hand-edited file must not leave the pad off the screen or without a stick zone
    l.stickX = std::clamp(l.stickX, 0, 640);
    l.stickY = std::clamp(l.stickY, 0, 360);
    l.zoneLeft = std::clamp(l.zoneLeft, 0, 640);
    l.zoneRight = std::clamp(l.zoneRight, l.zoneLeft + 40, 1400);
    l.zoneTop = std::clamp(l.zoneTop, 0, 340);
    if (btns.size() > kMaxButtons) btns.resize(kMaxButtons);
    for (PadButton& b : btns) {
        b.w = std::clamp(b.w, 16, 140);
        b.h = std::clamp(b.h, 14, 140);
        b.x = std::clamp(b.x, -20, 660);
        b.y = std::clamp(b.y, -20, 380);
    }
    if (!btns.empty()) l.buttons = btns;
    out = l;
    return true;
}

int stickDirection(int dx, int dy, int deadZone, int diagonalDeg) {
    if (dx * dx + dy * dy <= deadZone * deadZone) return 5;
    // angle with 0 = right, counter-clockwise, y up
    double a = std::atan2(static_cast<double>(-dy), static_cast<double>(dx)) * 180.0 / kPi;
    if (a < 0) a += 360.0;
    const double half = (90.0 - diagonalDeg) * 0.5;  // half width of a cardinal sector
    // cardinal centres 0 (6), 90 (8), 180 (4), 270 (2); everything else is a diagonal
    auto near = [&](double c) {
        double d = std::fabs(a - c);
        if (d > 180.0) d = 360.0 - d;
        return d <= half;
    };
    if (near(0)) return 6;
    if (near(90)) return 8;
    if (near(180)) return 4;
    if (near(270)) return 2;
    if (a < 90) return 9;
    if (a < 180) return 7;
    if (a < 270) return 1;
    return 3;
}

uint16_t dirBits(int d) {
    using namespace fight;
    uint16_t b = 0;
    if (d == 7 || d == 4 || d == 1) b |= kLeft;
    if (d == 9 || d == 6 || d == 3) b |= kRight;
    if (d >= 7) b |= kUp;
    if (d <= 3) b |= kDown;
    return b;
}

void TouchPad::reset() {
    stickId_ = kNone;
    baseX_ = knobX_ = lay_.stickX;
    baseY_ = knobY_ = lay_.stickY;
    dir_ = 5;
    pressedMask_ = 0;
}

uint16_t TouchPad::update(const std::vector<Finger>& fingers) {
    // 1. the stick finger: keep it while it is down; otherwise adopt the first new finger in the stick zone
    const Finger* stick = nullptr;
    for (const Finger& f : fingers)
        if (f.id == stickId_) stick = &f;
    if (!stick) {
        stickId_ = kNone;
        for (const Finger& f : fingers) {
            if (f.x >= lay_.zoneLeft && f.x < lay_.zoneRight && f.y >= lay_.zoneTop) {
                // a finger that sits on a button belongs to the button, not to the stick
                bool onButton = false;
                for (const PadButton& b0 : lay_.buttons)
                    if (lay_.scaled(b0).contains(f.x, f.y, 0)) onButton = true;
                if (onButton) continue;
                stick = &f;
                stickId_ = f.id;
                if (lay_.floating) {
                    baseX_ = f.x;
                    baseY_ = f.y;
                } else {
                    baseX_ = lay_.stickX;
                    baseY_ = lay_.stickY;
                }
                break;
            }
        }
    }
    const int rad = lay_.scaledRadius();
    if (stick) {
        int dx = stick->x - baseX_, dy = stick->y - baseY_;
        const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        // floating stick: a thumb that drifts past 1.5 x the radius drags the base along
        if (lay_.floating && dist > rad * 1.5) {
            const double k = (dist - rad * 1.5) / dist;
            baseX_ += static_cast<int>(std::lround(dx * k));
            baseY_ += static_cast<int>(std::lround(dy * k));
            dx = stick->x - baseX_;
            dy = stick->y - baseY_;
        }
        dir_ = stickDirection(dx, dy, static_cast<int>(lay_.deadZone * lay_.scale + 0.5f), lay_.diagonalDeg);
        const double d2 = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        if (d2 > rad) {
            knobX_ = baseX_ + static_cast<int>(std::lround(dx * rad / d2));
            knobY_ = baseY_ + static_cast<int>(std::lround(dy * rad / d2));
        } else {
            knobX_ = stick->x;
            knobY_ = stick->y;
        }
    } else {
        dir_ = 5;
        baseX_ = knobX_ = lay_.stickX;
        baseY_ = knobY_ = lay_.stickY;
    }

    // 2. buttons: every other finger presses whatever it is over (several buttons when it sits between them)
    uint16_t bits = dirBits(dir_);
    pressedMask_ = 0;
    for (const Finger& f : fingers) {
        if (f.id == stickId_) continue;
        for (size_t i = 0; i < lay_.buttons.size() && i < 32; ++i) {
            const PadButton b = lay_.scaled(lay_.buttons[i]);
            if (b.contains(f.x, f.y, lay_.grow)) {
                bits = static_cast<uint16_t>(bits | b.bits);
                pressedMask_ |= 1u << i;
            }
        }
    }
    return bits;
}

}  // namespace touch
}  // namespace zengine
