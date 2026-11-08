// splash.h - the game's own loading screen: "LOADING..." with the little bomb, over a slow slideshow of line drawings.
//
// Both come from the user's loading_<lang>.dds + loading_<lang>.spr.msb (a tile-packed sprite, see spr_tiles.h): the
// first 9 frames are the bomb animation (text + bomb), the next 19 are the line-art slideshow.  The placement is
// measured on the PC build: the drawing at 1.5x (top-left 120,14), the bomb frame in the lower right, the bomb at ~9 fps,
// the drawings cross-fading (the exact timing of the original is not decoded).
#pragma once
#include <SDL.h>

#include <string>
#include <vector>

#include "game_ui.h"
#include "spr_tiles.h"
#include "vfs.h"

namespace zengine {

class LoadingSplash {
public:
    ~LoadingSplash() { reset(); }
    // Reads loading_<lang> (falls back to English).  false = no such files in the mounted folders.
    bool load(SDL_Renderer* r, const Vfs& vfs, const std::string& lang);
    void reset();
    bool ok() const { return ok_; }
    const std::string& loadedName() const { return name_; }
    // Draws the whole 1280x720 canvas (black background included).  `ms` = time since the splash started.
    void draw(GameUi& ui, int ms) const;

    int bombFrames() const { return bombCount_; }
    int slideFrames() const { return slideCount_; }

private:
    SDL_Texture* frameTex(size_t k) const;

    SDL_Renderer* r_ = nullptr;
    bool ok_ = false;
    std::string name_;
    SprTiles spr_;
    int atlasW_ = 0, atlasH_ = 0;
    std::vector<uint8_t> atlas_;
    mutable std::vector<SDL_Texture*> tex_;  // composed frames, created the first time they are drawn
    int bombFirst_ = 0, bombCount_ = 0, slideFirst_ = 0, slideCount_ = 0;
};

}  // namespace zengine
