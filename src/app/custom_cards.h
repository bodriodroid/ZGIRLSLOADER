// custom_cards.h - the pictures the Character Select shows for a fighter that only exists in a mod.
//
// The game's select screen reads, for every character, a handful of textures by name:
//   charselect_select_icons_<name>01 / 02 / 03   the cell of the grid (normal / highlighted / locked),  102 x 105
//   charselect_portraits_<name>01                 the big picture behind the data bar,                      939 x 612
//   charselect_portrait_name_<name>01             the name plate over it,                                   514 x 144
//   charselect_pedestal_name_<name>01             the name plate on the pedestal,                           450 x 118
//   data_charcoin_<name>_glow01                   the coin of the data bar,                                  90 x 90
// A character a mod adds has none of those, so they are drawn here from what the mod does ship: its "Stand" pose (.foit / .art / .art-pt,
// coloured with its first palette) and its name in the game's own display font.  The result is put into the GameUi texture cache under the
// names above, so the Character Select draws it exactly like a texture of the game.
//
// Nothing is saved to disk: the pictures are made again the next time the screen is built.
#pragma once
#include <SDL.h>

#include <string>

#include "char_sprite.h"
#include "game_ui.h"

namespace zengine {
namespace menu {

// A fighter the select screen lists beside the game's own ones (the files the inventory found for it).
struct CustomFighter {
    std::string name;   // lower-case id ("anton")
    std::string foit, art, artPt;
};

namespace customcards {

// "Textures/charselect/<what><name><tail>.png" (the scene's naming; the cache is case-insensitive).
std::string texName(const char* what, const std::string& name, const char* tail = "01");

// Draws every picture of `name` from `pose` (an open CharSpriteEdit holding its Stand pose) and puts them into `gfx`.  The Charselect scene must be
// the current one (its fonts write the name).  false = nothing could be drawn (no renderer, no pose).
bool make(GameUi& gfx, const std::string& name, const CharSpriteEdit& pose);
// Removes them again.
void drop(GameUi& gfx, const std::string& name);

// The pose standing on a pedestal: its feet (the pose's hot spot) on canvas (x, y), `height` canvas pixels tall, mirrored when `flip`.
void drawStanding(GameUi& gfx, const CharSpriteEdit& pose, float x, float y, float height, bool flip, float alpha);
// Where drawStanding puts it (canvas pixels): for the tap target.
SDL_FRect standingRect(const CharSpriteEdit& pose, float x, float y, float height, bool flip);

}  // namespace customcards
}  // namespace menu
}  // namespace zengine
