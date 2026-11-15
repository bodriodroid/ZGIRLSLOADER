// shadow_layer.h - a fighter's floor shadow made of several silhouettes, drawn so that they do not add up.
//
// The fight shows a character as a handful of images (body, hair, weapon ...).  The "silhouette" shadow flattens every one of them onto the floor.
// Drawn one by one with a translucent black, the places where two images overlap come out darker than the rest and the shadow looks
// blotchy (and never like the PC game, where the floor shows through one even tone).  Here every silhouette goes opaque onto a transparent
// off-screen layer first - an overlap stays as dark as the rest - and the layer is then laid over the picture once, with the shadow's strength
// and its fade with height applied to the whole.  The layer is drawn at half size and enlarged with linear filtering, which softens the edge the
// way the game's shadows are soft.
//
// Presentation only.  Uses SDL render targets; Draw() returns -1 when they are not available and the caller draws the old way.
#pragma once
#include <SDL.h>

#include <vector>

namespace zengine {

class ShadowLayer {
public:
    // One flattened image: its four corners in picture units (the quad is axis-aligned: x0..x1 wide, yTop at its top edge, yBot at its bottom edge) and
    // the texture coordinates of its left / right edge (swapped for a mirrored image).
    struct Piece {
        SDL_Texture* tex = nullptr;
        float x0 = 0, x1 = 0, yTop = 0, yBot = 0;
        float u0 = 0, u1 = 1;
    };
    // The shadow's opacity at a picture row; the rows are ascending, at least two, and the opacity is linear between them.
    struct Stop {
        float y = 0;
        float alpha = 0;   // 0..1
    };

    ShadowLayer() = default;
    ShadowLayer(const ShadowLayer&) = delete;
    ShadowLayer& operator=(const ShadowLayer&) = delete;
    // No destructor work: the layer texture belongs to the renderer, which may already be gone when this object dies.  The owner calls release().

    // Draws the shadow made of `pieces` with the opacity of `stops`.  Returns the number of pieces drawn, or -1 when the layer cannot be used
    // (no render-target support, an absurd size, bad numbers).  `resolution` is the layer's size relative to the picture (0.5 = half).
    int draw(SDL_Renderer* r, const std::vector<Piece>& pieces, const std::vector<Stop>& stops, float resolution = 0.5f);

    // Frees the layer's texture.  `rendererAlive` false: the renderer is already destroyed (it took the texture with it), just forget it.
    void release(bool rendererAlive);
    void reset() { release(true); }

private:
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
    bool failed_ = false;   // the renderer refused a target texture once: do not try again every frame
};

}  // namespace zengine
