// The command lists ("Command Lists" of Help & Options and of the pause menu): the dialog of the PC edition's Options.gbs "Movelist"
// view, shared by the main menu (menu_scene.cpp) and the pause menu (play_menu.cpp).
//
// Each slot of the dialog is 72 canvas px high: the move's name over its command, the command written with the game's own button
// art ("{qcf} + {hp}": the face-button textures, the menu chevron turned to the eight directions).  The PC font carries those as
// private-use glyphs which our fonts do not have.
#pragma once

#include <string>
#include <vector>

#include "game_ui.h"
#include "gbs_anim.h"

namespace zengine {

struct MovelistRow {
    std::string name;      // already localised
    std::string command;   // already localised, with {tokens}; empty = a plain list entry (the list of characters)
    std::string kind;      // Special / Super1.. / Normal ("" = a plain entry)
    int indent = 0;        // canvas px the row is pushed in (a follow-up of the move above)
};

constexpr int kMovelistSlots = 6;
constexpr float kMovelistSlotX = 90.0f, kMovelistSlotY = 158.0f, kMovelistSlotStep = 72.0f, kMovelistSlotW = 1100.0f;   // canvas px

// Draws `command` inside maxW canvas px and at most maxLines lines (the scale shrinks until it fits); (x, y) is the top-left.
// Returns the lines used.
int drawCommandBlock(GameUi& g, const GbsFont& f, const std::string& command, float x, float y, float maxW, int maxLines, Col c);

// Draws the dialog (the scene Options.gbs is loaded by this call and left loaded; the caller restores its own).  `animKey` /
// `key` restart the dialog's intro when the page changes.  Returns false when the scene is not in the data.
bool drawMovelistDialog(GameUi& g, GbsAnimPlayer& anim, std::string& animKey, const std::string& key, const std::vector<MovelistRow>& rows,
                        int sel, float scroll, const std::string& title, int ageMs);

}  // namespace zengine
