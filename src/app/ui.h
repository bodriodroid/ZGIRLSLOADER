// ui.h - the tiny immediate-mode UI both the loader screens and the Play screen draw with.
//
// Everything is SDL_Renderer primitives plus a 8x8 bitmap font, at a fixed 640x360 logical
// resolution.  Input is pointer-based (mouse events on the desktop, emulated mouse events
// from touch on Android) *plus* a list of live touch points, because a fighting game needs
// two fingers at once (move + attack) and SDL only synthesises a single mouse pointer.
#pragma once
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {

struct Col { uint8_t r, g, b, a; };
constexpr Col kBg{16, 16, 22, 255}, kPanel{28, 28, 38, 255}, kPanel2{42, 42, 58, 255}, kText{230, 230, 240, 255},
    kDim{138, 138, 160, 255}, kAccent{232, 72, 120, 255}, kGood{96, 206, 128, 255}, kWarn{242, 192, 72, 255},
    kBad{244, 96, 96, 255}, kLine{64, 64, 86, 255};

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct Input {
    int mx = 0, my = 0;      // pointer (logical coordinates)
    int pmx = 0, pmy = 0;    // where the current press started
    bool down = false, pressed = false, released = false;
    int dragDy = 0;          // pointer movement on y during this frame while held
    int wheel = 0;
    std::string text;
    bool backspace = false, enter = false, tab = false, left = false, right = false, back = false;
    bool keyUp = false, keyDown = false;  // arrow keys (menus)
    bool touchPointer = false;       // the pointer is an emulated mouse driven by a finger (no hover)

    // Every finger currently on the glass, in logical coordinates.  Populated from
    // SDL_FINGER* (plus the mouse pointer while it is held) by the main loop.
    std::vector<std::pair<int, int>> touches;
    std::vector<int64_t> touchIds;   // same order as `touches`: stable id per finger (-1 = desktop mouse)
    bool touchIn(const Rect& b) const {
        for (const auto& t : touches)
            if (b.contains(t.first, t.second)) return true;
        return false;
    }
};

class Ui {
public:
    SDL_Renderer* r = nullptr;
    SDL_Texture* font = nullptr;
    Input in;
    int focus = 0;
    Uint32 now = 0;

    // Builds the 16x8 glyph atlas from the public-domain font8x8 (defined in ui.cpp).
    bool init(SDL_Renderer* renderer);

    void beginFrame() {
        fieldOrder_.clear();
        defocus_ = in.pressed;
    }
    void endFrame() {
        if (defocus_) focus = 0;
        if (in.released) pressId_ = 0;
        if (in.tab && focus && !fieldOrder_.empty()) {
            size_t i = 0;
            while (i < fieldOrder_.size() && fieldOrder_[i] != focus) ++i;
            focus = fieldOrder_[(i + 1) % fieldOrder_.size()];
        }
    }

    void color(Col c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }
    void fill(Rect b, Col c) {
        color(c);
        SDL_Rect q{b.x, b.y, b.w, b.h};
        SDL_RenderFillRect(r, &q);
    }
    void outline(Rect b, Col c) {
        color(c);
        SDL_Rect q{b.x, b.y, b.w, b.h};
        SDL_RenderDrawRect(r, &q);
    }
    // Filled disc: used for the fighters' floor shadows.
    void disc(int cx, int cy, int rx, int ry, Col c) {
        color(c);
        for (int y = -ry; y <= ry; ++y) {
            const double f = 1.0 - (double(y) * y) / (double(ry) * ry);
            if (f <= 0) continue;
            const int half = static_cast<int>(std::lround(rx * std::sqrt(f)));
            SDL_Rect q{cx - half, cy + y, half * 2 + 1, 1};
            SDL_RenderFillRect(r, &q);
        }
    }

    // Text is UTF-8.  ASCII plus the Latin letters Spanish/French/Portuguese/Italian/German need
    // (accented lowercase, n-tilde, inverted ? and !) are drawn; accented capitals fold to the plain
    // capital and anything else shows as '?'.
    void text(int x, int y, const std::string& s, Col c, int scale = 1) {
        SDL_SetTextureColorMod(font, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(font, c.a);
        for (size_t i = 0; i < s.size();) {
            const int ch = glyphIndex(nextCodepoint(s, i));
            SDL_Rect src{(ch % 16) * 8, (ch / 16) * 8, 8, 8};
            SDL_Rect dst{x, y, 8 * scale, 8 * scale};
            SDL_RenderCopy(r, font, &src, &dst);
            x += 8 * scale;
        }
    }
    static int textW(const std::string& s, int scale = 1) { return glyphCount(s) * 8 * scale; }

    // UTF-8 helpers (implemented in ui.cpp).
    static uint32_t nextCodepoint(const std::string& s, size_t& i);  // advances i; U+FFFD on bad bytes
    static int glyphIndex(uint32_t cp);                              // atlas slot (0..255)
    static int glyphCount(const std::string& s);                     // number of code points
    static std::string fit(const std::string& s, int widthPx);       // clipped with ".." to widthPx

    // Text clipped (with "..") to the box, vertically centred.
    void textIn(Rect b, const std::string& s, Col c, bool centre = false) {
        const std::string t = fit(s, b.w);
        const int x = centre ? b.x + (b.w - textW(t)) / 2 : b.x;
        text(x, b.y + (b.h - 8) / 2, t, c);
    }

    bool button(int id, Rect b, const std::string& label, bool enabled = true, bool active = false) {
        const bool inside = b.contains(in.mx, in.my);
        bool clicked = false;
        if (enabled) {
            if (in.pressed && b.contains(in.pmx, in.pmy)) pressId_ = id;
            if (in.released && pressId_ == id && inside) clicked = true;
        }
        const bool held = enabled && pressId_ == id && in.down && inside;
        Col bg = !enabled ? Col{34, 34, 44, 255} : held ? kAccent : active ? Col{150, 48, 86, 255} : kPanel2;
        fill(b, bg);
        outline(b, active ? kAccent : kLine);
        textIn(b, label, enabled ? kText : Col{96, 96, 112, 255}, true);
        return clicked;
    }

    // Text field. Returns true when Enter was pressed while it had focus.
    bool field(int id, Rect b, std::string& value, const char* placeholder, bool mask = false) {
        fieldOrder_.push_back(id);
        if (in.pressed && b.contains(in.pmx, in.pmy)) {
            focus = id;
            defocus_ = false;
        }
        bool entered = false;
        if (focus == id) {
            if (!in.text.empty()) value += in.text;
            if (in.backspace && !value.empty()) value.pop_back();
            if (in.enter) entered = true;
        }
        fill(b, Col{12, 12, 18, 255});
        outline(b, focus == id ? kAccent : kLine);
        std::string shown = mask ? std::string(value.size(), '*') : value;
        const size_t cap = static_cast<size_t>(std::max(b.w / 8 - 2, 1));
        if (shown.size() > cap) shown = shown.substr(shown.size() - cap);
        if (shown.empty() && focus != id) {
            text(b.x + 6, b.y + (b.h - 8) / 2, fit(placeholder, b.w - 12), Col{96, 96, 112, 255});
        } else {
            text(b.x + 6, b.y + (b.h - 8) / 2, shown, kText);
            if (focus == id && ((now / 500) % 2 == 0))
                fill(Rect{b.x + 6 + textW(shown), b.y + 4, 2, b.h - 8}, kAccent);
        }
        return entered;
    }

    bool check(int id, Rect b, const std::string& label, bool& v) {
        bool clicked = false;
        if (in.pressed && b.contains(in.pmx, in.pmy)) pressId_ = id;
        if (in.released && pressId_ == id && b.contains(in.mx, in.my)) {
            v = !v;
            clicked = true;
        }
        const Rect box{b.x, b.y + (b.h - 16) / 2, 16, 16};
        fill(box, Col{12, 12, 18, 255});
        outline(box, kLine);
        if (v) fill(Rect{box.x + 4, box.y + 4, 8, 8}, kAccent);
        text(b.x + 24, b.y + (b.h - 8) / 2, fit(label, b.w - 24), kText);
        return clicked;
    }

    void bar(Rect b, double pct, Col c) {
        fill(b, Col{12, 12, 18, 255});
        const int w = static_cast<int>(std::lround((b.w - 2) * std::clamp(pct, 0.0, 100.0) / 100.0));
        if (w > 0) fill(Rect{b.x + 1, b.y + 1, w, b.h - 2}, c);
        outline(b, kLine);
    }

private:
    int pressId_ = 0;
    bool defocus_ = false;
    std::vector<int> fieldOrder_;
};

}  // namespace zengine
