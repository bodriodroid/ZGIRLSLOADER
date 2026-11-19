// boot_splash.h - the loader's own start-up logo (boot_logo.h: line art on a transparent 1600x900 canvas, packed by
// tools/gen_boot_logo.py).  It is decoded once into a texture and drawn over the whole 640x360 logical screen.  No game data is
// involved, so it can be on screen from the very first frame, while the data is still being indexed.
#pragma once
#include <SDL.h>

namespace zengine {

class BootLogo {
public:
    ~BootLogo() { reset(); }
    // Decodes the embedded logo into a texture (once).  false = the renderer refused it (the caller draws text instead).
    bool load(SDL_Renderer* r);
    void reset();
    bool ok() const { return tex_ != nullptr; }
    // Draws it over (x, y, w, h) with alpha 0..255 and `tint` (white = as drawn).
    void draw(SDL_Renderer* r, int x, int y, int w, int h, Uint8 alpha, SDL_Color tint = SDL_Color{255, 255, 255, 255}) const;

private:
    SDL_Texture* tex_ = nullptr;
    bool tried_ = false;
};

}  // namespace zengine
