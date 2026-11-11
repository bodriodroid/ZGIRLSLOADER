// fx_gl.h - runs the PAFX effect programs (src/fx/fx_glsl.cpp) inside SDL's own OpenGL ES 2 renderer.
//
// SDL keeps a private cache of the GL state it last set (program, blend, texture bindings, vertex attributes ...) and trusts it
// on the next draw.  FxRenderer therefore works in "passes": a Pass lets SDL finish its queued drawing and apply its viewport /
// clip / render target, reads what SDL set, draws with its own programs, and puts every piece of GL state it touched back before
// SDL draws again.  SDL's drawing before and after a pass is unaffected (tests/test_fx_gl.cpp checks it against SDL's own draws
// and against the CPU maths of fx_math.h).
//
// Only SDL's "opengles2" renderer is supported (the one Android uses); init() fails on any other and the caller keeps its CPU path.
// Everything here must run on the render thread, with the renderer's GL context current.
#pragma once
#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "art.h"
#include "fx_math.h"
#include "fx_pack.h"

namespace zengine {

// A fighter image as index planes on the GPU: RGBA8 with G = grad byte, B = band (palette row), nearest sampling.
struct FxIndexTex {
    unsigned gl = 0;
    int w = 0, h = 0;
    bool valid() const { return gl != 0; }
};
// A palette as a tones x bands texture (RGB, nearest).
struct FxPaletteTex {
    unsigned gl = 0;
    int tones = 0, bands = 0;
    bool valid() const { return gl != 0; }
};

// The silhouette of a floor shadow: four corners in strip order (top-left, top-right, bottom-left, bottom-right), in the renderer's
// units, with the height above the floor of each corner (interpolated per pixel, like the PC game's shader does).
struct FxShadowQuad {
    float x[4] = {}, y[4] = {}, z[4] = {};
    float u[4] = {}, v[4] = {};
};

class FxRenderer {
    struct Gl;        // the GL entry points
    struct Program;   // one compiled effect program and its uniform locations
public:
    FxRenderer();
    ~FxRenderer();
    FxRenderer(const FxRenderer&) = delete;
    FxRenderer& operator=(const FxRenderer&) = delete;

    // Loads the GL entry points, compiles the programs of `pack` (sprite_palette and floor_shadow are required, super_outline is
    // optional).  False + `err` when `r` is not an opengles2 renderer, an entry point is missing or a program does not compile.
    bool init(SDL_Renderer* r, const fx::FxPack& pack, std::string* err = nullptr);
    // Frees every GL object it made (the context must still be alive).  Textures handed out earlier become invalid.
    void shutdown();
    bool ready() const { return ready_; }
    SDL_Renderer* renderer() const { return r_; }
    const fx::FxTuning& tuning() const { return tuning_; }
    bool hasOutline() const { return progOutline_.id != 0; }
    // the bilinear ("Smooth") sprite program; packs made before it existed lack it and sprites stay nearest-sampled
    bool hasSmooth() const { return progSmooth_.id != 0; }

    // Textures.  Both return an invalid handle when the GPU refuses (bigger than GL_MAX_TEXTURE_SIZE, out of memory).
    // packIndex() builds the texels of an index texture from the two planes (it touches no GL, so a worker thread can run it);
    // createIndex() uploads them.
    static std::vector<uint8_t> packIndex(const uint8_t* band, const uint8_t* grad, int w, int h);
    FxIndexTex createIndex(const uint8_t* packed, int w, int h);
    FxPaletteTex createPalette(const Palette& pal);
    void destroy(FxIndexTex& t);
    void destroy(FxPaletteTex& t);
    int maxTextureSize() const { return maxTex_; }

    // One batch of draws.  The constructor flushes SDL and saves the GL state, the destructor restores it; keep it as short as the
    // draws that belong together, and do not call SDL's render functions while it is alive.
    class Pass {
    public:
        explicit Pass(FxRenderer& fx);
        ~Pass();
        Pass(const Pass&) = delete;
        Pass& operator=(const Pass&) = delete;
        bool ok() const { return ok_; }

        // The image drawn into `dst` (renderer units; flipH mirrors it); colours through the palette, the hue shift and the screen
        // effects of `p`; `p.alpha` is the sprite's opacity.  `smooth` filters the coloured texels bilinearly (nearest otherwise).
        void sprite(const FxIndexTex& img, const FxPaletteTex& pal, const SDL_FRect& dst, bool flipH, const fx::SpriteParams& p, bool smooth = false);
        // The silhouette of the image in the two flat colours of the super outline, blended by `mix` (needs hasOutline()).
        void outline(const FxIndexTex& img, const SDL_FRect& dst, bool flipH, float alpha, const float a[3], const float b[3], float mix);
        // A floor shadow: black, alpha fading with the height above the floor.  `fadeDist` > 0 is the distance where it reaches 0
        // (the sign the game uses is handled here), `maxAlpha` the alpha at height 0.
        void shadow(const FxIndexTex& img, const FxShadowQuad& q, float fadeDist, float maxAlpha);

    private:
        struct Saved;
        bool prepare(const Program& pr, const FxIndexTex& img, const FxPaletteTex* pal);
        void setScreen(const Program& pr, const fx::ScreenFx& s);
        void emit(const float xy[4][2], const float z[4], const float uv[4][2]);
        FxRenderer& fx_;
        Saved* saved_ = nullptr;
        bool ok_ = false;
        float vpW_ = 0, vpH_ = 0, sx_ = 1, sy_ = 1;
        bool target_ = false;
    };

private:
    struct Program {
        unsigned id = 0;
        int uMvp = -1, uScreen = -1, uFlashLow = -1, uFlashHigh = -1, uFlashLuma = -1, uFlashCurve = -1, uShadowTint = -1,
            uShadowLuma = -1, uIndex = -1, uPalette = -1, uPaletteSize = -1, uAlpha = -1, uLine = -1, uHue = -1, uFloor = -1,
            uOutlineA = -1, uOutlineB = -1, uOutlineMix = -1, uTexSize = -1;
    };
    bool build(const fx::FxPack& pack, const char* name, Program& out, std::string* err);
    // uploads RGBA texels and leaves SDL's texture bindings as they were
    unsigned makeTexture(int w, int h, const uint8_t* rgba);

    SDL_Renderer* r_ = nullptr;
    SDL_GLContext ctx_ = nullptr;   // the context the objects belong to: a different current one (device reset) makes every pass fail
    Gl* gl_ = nullptr;
    bool ready_ = false;
    int maxTex_ = 0;
    unsigned vbo_ = 0;
    Program progSprite_, progSmooth_, progShadow_, progOutline_;
    fx::FxTuning tuning_;
};

}  // namespace zengine
