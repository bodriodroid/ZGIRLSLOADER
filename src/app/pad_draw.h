// pad_draw.h - how the on-screen fighting pad looks (the match and the layout editor draw it the same way).
//
// Round arcade buttons with a coloured ring (punches cool, kicks warm, chords violet, Start grey), a pressed button lights up solid with a white
// ring, the stick is a ring with eight direction dots and a knob that follows the thumb.  Every shape is filled once per scan line, so the pad's
// opacity never shows seams where two shapes meet.
#pragma once
#include "touch_pad.h"
#include "ui.h"

namespace zengine {
namespace padview {

constexpr int kStick = -2;   // `selected` for the stick (a button is its index in Layout::buttons, -1 = nothing)

// The ring colour a button gets from what it presses.
Col tint(uint16_t bits);

// Draws the stick and every button of `L` (the layout with the user's scale / opacity already applied) in the state `pad` reports.
// `selected` outlines one element (the editor).
void draw(Ui& ui, const touch::Layout& L, const touch::TouchPad& pad, int selected = -1);

// The pieces, for the editor's own widgets.
void roundedRect(Ui& ui, Rect r, int radius, Col body, Col ring = Col{0, 0, 0, 0}, int ringWidth = 0);

}  // namespace padview
}  // namespace zengine
