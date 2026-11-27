// touch_pad.h - the on-screen fighting-game controller (pure logic: no SDL, no drawing, testable).
//
// What a phone needs to play a 2D fighter:
//   * a STICK, not four arrow keys: one thumb rolls 2 -> 3 -> 6 for a quarter circle.  The stick finger is tracked by
//     its id from touch-down to release, so it never "jumps" to a button; it reads 8 directions from the angle with
//     wider diagonals (down-forward is what motions are made of), has a dead zone, and its base follows a thumb that
//     drifts too far (floating stick).
//   * six attack buttons in the arcade layout (punches on top, kicks below) with generous hit areas.  Every other
//     finger presses whatever it is over: sliding across buttons works, and a thumb resting on the line between two
//     buttons presses both (LP+LK ...).
//   * macro buttons: 3P (LP+MP+HP) and 3K (LK+MK+HK) - the runtime already expands kMacro1 / kMacro2 - and P+K.
//   * Start.
// Coordinates are the game's logical 640x360 space.  The layout can be scaled per user (big hands, tablets) and saved
// as a small text file (serialize / parse).  The editing helpers (move / resize / add / remove a button, move the stick,
// mirror for left-handed play) are here too, so the layout editor screen only draws and the rules can be tested.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {
namespace touch {

struct Finger {
    int64_t id;  // stable while the finger stays down (SDL touchId/fingerId; the desktop mouse is -1)
    int x, y;    // logical coordinates
};

struct PadButton {
    int x, y, w, h;      // logical rect
    uint16_t bits;       // fight::Button bits it presses
    std::string label;
    bool contains(int px, int py, int grow) const {
        return px >= x - grow && px < x + w + grow && py >= y - grow && py < y + h + grow;
    }
};

struct Layout {
    // stick
    int stickX = 92, stickY = 272;  // resting centre of the stick base
    int stickRadius = 52;           // knob travel
    int deadZone = 14;              // px of travel that still reads as neutral
    int diagonalDeg = 60;           // angular width of each diagonal sector (cardinals get 90 - diagonal); 45 = even
    bool floating = true;           // the base appears where the thumb lands (inside the stick zone)
    int zoneRight = 280;            // fingers that land between zoneLeft and zoneRight (and below zoneTop) take the stick
    int zoneTop = 120;
    int zoneLeft = 0;               // 0 for the usual left-hand stick; a mirrored (left-handed) layout has it on the right
    // buttons
    std::vector<PadButton> buttons;
    int grow = 6;                   // extra px of hit area around each button
    float scale = 1.0f;             // user size factor (applied around each element's centre)
    int opacity = 150;              // 0..255, drawing only

    static Layout defaults();       // arcade layout for a 16:9 phone in landscape
    // The same layout for a picture `extra` logical units wider than 640 (No Limits Screen): what sits in the right half moves to the real
    // right edge, the stick and everything on the left stay where they are.
    static Layout widened(const Layout& base, int extra);
    // One element per line, "key value..." - easy to edit by hand and to save from a layout editor.
    std::string serialize() const;
    static bool parse(const std::string& text, Layout& out);  // false on garbage; `out` keeps defaults for missing keys
    PadButton scaled(const PadButton& b) const;
    int scaledRadius() const { return static_cast<int>(static_cast<float>(stickRadius) * scale + 0.5f); }

    // ---- editing (all in the logical 640 x 360 space of the base layout; w / h are the size of the screen the elements must stay in)
    struct Preset {
        const char* label;
        uint16_t bits;
    };
    static constexpr size_t kMaxButtons = 16;
    static const std::vector<Preset>& presets();            // the buttons the editor offers to add (LP .. HK, 3P, 3K, P+K, two-button chords, Start)
    int addButton(const Preset& p, int w = 640, int h = 360);   // a free spot is searched; the new index, -1 = full (kMaxButtons)
    bool removeButton(size_t i);                            // false for the last one (a pad without buttons is not a pad)
    void moveButton(size_t i, int x, int y, int w = 640, int h = 360);   // top-left; kept inside the screen
    void resizeButton(size_t i, int newW, int w = 640, int h = 360);    // keeps the proportions and the centre
    void moveStick(int x, int y, int w = 640, int h = 360);   // the resting centre; the stick zone follows (fitZone)
    void setStickRadius(int r, int w = 640, int h = 360);
    void fitZone(int w = 640);        // the stick zone from the stick side of the screen up to the nearest button cluster
    Layout mirrored(int w = 640) const;   // the same pad for the other hand: every x is reflected
    bool leftHanded(int w = 640) const { return stickX >= w / 2; }
};

class TouchPad {
public:
    explicit TouchPad(Layout l = Layout::defaults()) : lay_(std::move(l)) {}
    void setLayout(const Layout& l) { lay_ = l; reset(); }
    const Layout& layout() const { return lay_; }
    void reset();

    // Feed every finger currently on the glass, once per frame.  Returns fight::Button bits (directions in SCREEN
    // terms: kLeft / kRight, the runtime makes them facing-relative).
    uint16_t update(const std::vector<Finger>& fingers);

    // ---- for drawing
    bool stickActive() const { return stickId_ != kNone; }
    int baseX() const { return baseX_; }
    int baseY() const { return baseY_; }
    int knobX() const { return knobX_; }
    int knobY() const { return knobY_; }
    int direction() const { return dir_; }        // numpad 1..9 (5 = neutral)
    bool pressed(size_t button) const { return button < 32 && (pressedMask_ >> button) & 1u; }

    static constexpr int64_t kNone = INT64_MIN;

private:
    Layout lay_;
    int64_t stickId_ = kNone;
    int baseX_ = 0, baseY_ = 0, knobX_ = 0, knobY_ = 0;
    int dir_ = 5;
    uint32_t pressedMask_ = 0;
};

// Direction (numpad 1..9) of a stick vector (dx right, dy DOWN, screen convention).  Exposed for tests.
int stickDirection(int dx, int dy, int deadZone, int diagonalDeg);
// Button bits for a numpad direction.
uint16_t dirBits(int numpad);

}  // namespace touch
}  // namespace zengine
