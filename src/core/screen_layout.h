// screen_layout.h - "No Limits Screen": the picture's logical size for the shape of the phone's screen.
//
// The picture is drawn in logical units, 360 high and 640 wide (16:9).  A phone that is wider than that (19.5:9, 20:9 ...) normally gets
// black bars at the sides.  With No Limits Screen the logical width grows to the screen's own shape instead, the height stays 360, so
// nothing is stretched: a fight shows more of the stage at both sides, and what is designed for 16:9 (the HUD, menus, dialogs) keeps its
// size and sits in the middle ("safe area").  A screen that is narrower than 16:9 keeps the 640x360 picture with bars above and below.
#pragma once

namespace zengine {
namespace layout {

constexpr int kBaseW = 640, kBaseH = 360;
constexpr int kMaxW = 960;   // 8:3, wider than any phone; a window beyond it gets bars

struct Layout {
    int w = kBaseW, h = kBaseH;   // logical size of the picture
    int safeX = 0;                // left edge of the centred 640-wide area (what is designed for 16:9)
    bool wide() const { return w > kBaseW; }
    int extra() const { return w - kBaseW; }   // logical units added to the 16:9 picture (half on each side)
};

// The layout for a window / screen of physW x physH pixels.  `noLimits` false (or a screen at most as wide as 16:9) gives the 640x360 base.
Layout compute(int physW, int physH, bool noLimits);

}  // namespace layout
}  // namespace zengine
