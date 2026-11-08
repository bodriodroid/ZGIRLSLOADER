// char_sprite.h - the characters that stand on the pedestals of the Character Select: character_select_<name>.spr.msb +
// character_select_<name>.dds (a tile sprite, see spr_tiles.h) with an "idle" animation, read from the user's sprites folder.
//
// Each frame carries a hot spot (the frame's own offset = where the feet / body centre are inside the picture); the sprites are
// stored at twice the canvas resolution, so they are drawn at kScale (0.5) of their pixel size.
// The textures are composed on first use and the last few characters are kept (the cursor moves between neighbours a lot).
#pragma once
#include <SDL.h>

#include <string>
#include <vector>

#include "game_ui.h"

namespace zengine {
namespace menu {

struct CharSpriteFrame {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;          // pixels of the composed frame
    float hotX = 0, hotY = 0;  // hot spot inside the frame (pixels)
};

struct CharSprite {
    std::string name;
    std::vector<CharSpriteFrame> idle;   // the frames of the "idle" animation, in order
};

class CharSprites {
public:
    static constexpr float kScale = 0.5f;   // sprite pixels -> canvas pixels
    static constexpr int kFrameMs = 83;     // the idle loop runs at 12 frames a second

    // nullptr when the user's data has no sprite for `name` (the screen then simply shows no standing character).
    // `palPath`: a palette texture to colour it with (a real file path: one of the character's <name>_Np.dds or a baked mods/pal
    // palette); empty = the game's own <name>_1p.dds.
    const CharSprite* get(GameUi& gfx, const std::string& name, const std::string& palPath = std::string());
    void clear();   // drops everything (textures are destroyed while the renderer is alive)

    // Draws the idle animation `ms` after it started, hot spot on canvas (x, y).  `flip` mirrors it (player 2 faces left).
    static void draw(GameUi& gfx, const CharSprite& s, int ms, float x, float y, bool flip, float alpha);

private:
    struct Slot {
        std::string key;   // name + palette
        CharSprite spr;
        bool ok = false;
        unsigned long long used = 0;
    };
    std::vector<Slot> slots_;
    unsigned long long clock_ = 0;
};

}  // namespace menu
}  // namespace zengine
