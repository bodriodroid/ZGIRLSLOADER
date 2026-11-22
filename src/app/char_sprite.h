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

#include "art.h"
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

// One standing character that can be recoloured live (the colour editor): the first frame of its idle loop, kept as the raw atlas texels
// (tone | region | line, see char_sprite.cpp) so a new palette only means looking every texel up again - no decoding.
class CharSpriteEdit {
public:
    // (No SDL calls in the destructor: at shutdown the renderer is already gone - the owner calls close() while it is alive.)
    // Composes the frame; `pal` colours it.  false when the user's data has no such sprite.
    bool open(GameUi& gfx, const std::string& name, const Palette& pal);
    // The same, from the FIGHT art: the "Stand" pose (the plain idle, no attack) of <name>.foit / .art / .art-pt at full resolution, all of its image
    // layers put together, coloured by `pal` exactly like the fight does (tone + line darkening).  Much sharper than the Character Select sprite
    // (which is stored at half size).  false when a file is missing or the character has no standing pose; the caller then uses open().
    bool openArt(GameUi& gfx, const std::string& foitPath, const std::string& artPath, const std::string& artPtPath, const Palette& pal);
    void close();   // destroys the texture (call while the renderer is alive)
    bool isOpen() const { return tex_ != nullptr; }
    // Looks the texels up again in `pal`.  `region` (>= 1) is brightened by `pulse` (0..1) to show which part is being edited; 0 = none.
    void paint(const Palette& pal, int region = 0, float pulse = 0);
    // The region (palette row >= 1) at the pixel (x, y) of the frame, looking a few pixels around when the exact one is the outline or empty; 0 = none.
    int regionAt(int x, int y) const;
    SDL_Texture* texture() const { return tex_; }
    int width() const { return w_; }
    int height() const { return h_; }
    float hotX() const { return hotX_; }
    float hotY() const { return hotY_; }
    // The box (inside the frame) that holds every visible texel: the frames carry a lot of empty room, which the editor crops away to draw the character big.
    SDL_Rect bounds() const { return bounds_; }

private:
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
    float hotX_ = 0, hotY_ = 0;
    SDL_Rect bounds_{0, 0, 0, 0};
    std::vector<uint8_t> band_, grad_;   // openArt(): the frame's palette row and grad value per pixel (w*h each); raw_ stays empty then
    std::vector<uint8_t> raw_;   // w*h*4: the atlas texels as stored
    std::vector<uint8_t> out_;   // w*h*4: what was uploaded last
};

}  // namespace menu
}  // namespace zengine
