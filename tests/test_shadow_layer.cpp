// test_shadow_layer.cpp - the floor shadow layer (src/app/shadow_layer.*): silhouettes that overlap must not come out darker, the opacity follows the
// stops (the fade with height), the render target is put back, and the ways it can refuse.  SDL's software renderer on an off-screen surface.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "shadow_layer.h"

using namespace zengine;

static int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(c)) {                                                           \
            ++g_fail;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

namespace {
SDL_Surface* g_surf = nullptr;
SDL_Renderer* g_ren = nullptr;

int grey(int x, int y) {   // red channel of a white-to-black picture
    const uint8_t* row = static_cast<const uint8_t*>(g_surf->pixels) + y * g_surf->pitch;
    return static_cast<int>((reinterpret_cast<const uint32_t*>(row)[x] >> 16) & 0xff);
}
void white() {
    SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 255);
    SDL_RenderClear(g_ren);
}
SDL_Texture* opaqueTexture() {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 4, 4, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(s, nullptr, SDL_MapRGBA(s->format, 200, 120, 40, 255));   // any opaque colour: the shadow is black whatever the image looks like
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_ren, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}
ShadowLayer::Piece piece(SDL_Texture* t, float x0, float x1, float yTop, float yBot) {
    ShadowLayer::Piece p;
    p.tex = t;
    p.x0 = x0;
    p.x1 = x1;
    p.yTop = yTop;
    p.yBot = yBot;
    return p;
}
}  // namespace

int main() {
    if (SDL_Init(0) != 0) {
        std::printf("SKIP: SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    g_surf = SDL_CreateRGBSurfaceWithFormat(0, 100, 70, 32, SDL_PIXELFORMAT_ARGB8888);
    g_ren = SDL_CreateSoftwareRenderer(g_surf);
    if (!g_ren) {
        std::printf("SKIP: no software renderer: %s\n", SDL_GetError());
        return 0;
    }
    SDL_Texture* tex = opaqueTexture();
    const std::vector<ShadowLayer::Piece> two = {piece(tex, 10, 40, 10, 30), piece(tex, 30, 60, 10, 30)};   // overlapping in x 30..40
    const std::vector<ShadowLayer::Stop> flat75 = {{0, 0.75f}, {70, 0.75f}};

    {   // overlaps are as dark as the rest, and the picture shows through at the strength asked for: white * (1 - 0.75) = 64
        white();
        ShadowLayer L;
        CHECK(L.draw(g_ren, two, flat75, 1.0f) == 2);
        const int a = grey(20, 20), ov = grey(35, 20), b = grey(50, 20);
        CHECK(std::abs(a - 64) <= 3 && std::abs(ov - 64) <= 3 && std::abs(b - 64) <= 3);
        CHECK(std::abs(ov - a) <= 1);   // the overlap is not darker
        CHECK(grey(5, 5) == 255 && grey(70, 20) == 255 && grey(20, 45) == 255);   // nothing outside the silhouettes
        // the colour of the images never matters: the shadow is black (a coloured shadow would not be neutral)
        CHECK(std::abs(a - ov) <= 1);
        // the piece's texture was not left with a colour / alpha modulation for the next sprite that uses it
        Uint8 r8, g8, b8, a8;
        SDL_GetTextureColorMod(tex, &r8, &g8, &b8);
        SDL_GetTextureAlphaMod(tex, &a8);
        CHECK(r8 == 255 && g8 == 255 && b8 == 255 && a8 == 255);
        // and drawing again with the same layer gives the same picture (it is cleared every time)
        white();
        CHECK(L.draw(g_ren, {piece(tex, 10, 40, 10, 30)}, flat75, 1.0f) == 1);
        CHECK(grey(50, 20) == 255 && std::abs(grey(20, 20) - 64) <= 3);   // the second piece of the first call is gone
    }
    {   // the fade: the opacity runs from 1 at row 10 to 0 at row 30, so the shadow is darker at the top of the quad than at its bottom
        white();
        ShadowLayer L;
        const std::vector<ShadowLayer::Stop> fade = {{10, 1.0f}, {30, 0.0f}};
        CHECK(L.draw(g_ren, {piece(tex, 10, 60, 10, 30)}, fade, 1.0f) == 1);
        const int top = grey(30, 11), mid = grey(30, 20), bot = grey(30, 28);
        CHECK(top < 40 && bot > 200 && top < mid && mid < bot);
        CHECK(std::abs(mid - 128) <= 12);
    }
    {   // stops that do not reach the layer's rows: constant beyond the ends
        white();
        ShadowLayer L;
        CHECK(L.draw(g_ren, {piece(tex, 10, 60, 10, 30)}, {{15, 0.5f}, {20, 0.5f}}, 1.0f) == 1);
        CHECK(std::abs(grey(30, 12) - 128) <= 3 && std::abs(grey(30, 28) - 128) <= 3);
    }
    {   // half-size layer (the GPU filters the enlargement, which softens the edge; the software renderer used here cannot): the overlap is still
        // not darker and the tone is right; at full size the edge is hard
        white();
        ShadowLayer L;
        CHECK(L.draw(g_ren, two, flat75, 0.5f) == 2);
        CHECK(std::abs(grey(35, 20) - grey(20, 20)) <= 4);
        CHECK(std::abs(grey(20, 20) - 64) <= 8);
        CHECK(grey(5, 5) == 255 && grey(70, 20) == 255);
        white();
        ShadowLayer H;
        CHECK(H.draw(g_ren, two, flat75, 1.0f) == 2);
        bool hard = true;
        for (int x = 6; x < 15; ++x) {
            const int g = grey(x, 20);
            hard = hard && (g <= 75 || g >= 245);
        }
        CHECK(hard);
    }
    {   // the render target the caller had is put back (a nested target is the "render resolution" mode)
        SDL_Texture* outer = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 100, 70);
        CHECK(outer != nullptr);
        SDL_SetRenderTarget(g_ren, outer);
        SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 255);
        SDL_RenderClear(g_ren);
        ShadowLayer L;
        CHECK(L.draw(g_ren, two, flat75, 1.0f) == 2);
        CHECK(SDL_GetRenderTarget(g_ren) == outer);
        SDL_SetRenderTarget(g_ren, nullptr);
        white();
        SDL_RenderCopy(g_ren, outer, nullptr, nullptr);
        CHECK(std::abs(grey(20, 20) - 64) <= 3 && grey(5, 5) == 255);   // it drew into the outer target, not onto the window
        SDL_DestroyTexture(outer);
        CHECK(SDL_GetRenderTarget(g_ren) == nullptr);
    }
    {   // refusals: nothing to draw, bad numbers, an absurd size, a missing texture
        ShadowLayer L;
        white();
        CHECK(L.draw(g_ren, {}, flat75) == -1);
        CHECK(L.draw(g_ren, two, {{0, 1.0f}}) == -1);
        CHECK(L.draw(nullptr, two, flat75) == -1);
        CHECK(L.draw(g_ren, {piece(tex, 0, std::numeric_limits<float>::quiet_NaN(), 0, 10)}, flat75) == -1);
        CHECK(L.draw(g_ren, {piece(tex, 0, 100000, 0, 10)}, flat75) == -1);
        CHECK(L.draw(g_ren, {piece(nullptr, 0, 10, 0, 10)}, flat75) == -1);
        CHECK(L.draw(g_ren, two, {{0, std::numeric_limits<float>::infinity()}, {10, 1.0f}}) == -1);
        CHECK(grey(20, 20) == 255);   // none of that drew anything
        L.reset();
        CHECK(L.draw(g_ren, two, flat75, 1.0f) == 2);   // usable again after a reset
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(g_ren);
    SDL_FreeSurface(g_surf);
    SDL_Quit();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
