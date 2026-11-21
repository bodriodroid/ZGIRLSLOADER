// test_bar_fill.cpp - the sides of a 16:9 picture on a wider screen (src/app/bar_fill.*): when they are needed, that the picture keeps
// its shape in the middle, that the sides are filled (not black) and that the logical size comes back.  SDL's software renderer on a surface.
#include <SDL.h>

#include <cstdio>
#include <vector>

#include "bar_fill.h"
#include "target_switch.h"

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

static void testLayout() {
    BarFill::Layout l = BarFill::layout(1920, 1080, 640, 360);   // exactly 16:9
    CHECK(!l.wanted);
    l = BarFill::layout(1950, 1080, 640, 360);                   // +1.5 %: not worth it
    CHECK(!l.wanted);
    l = BarFill::layout(2400, 1080, 640, 360);                   // 20:9 phone
    CHECK(l.wanted);
    CHECK(l.picH == 1080 && l.picW == 1920 && l.x == 240);
    CHECK(l.visible > 0.79 && l.visible < 0.81);                  // the blurred copy is scaled to the window width: 1080 of its 1350 px show
    l = BarFill::layout(1280, 1080, 640, 360);                   // taller than 16:9: nothing to fill
    CHECK(!l.wanted);
    l = BarFill::layout(0, 0, 640, 360);
    CHECK(!l.wanted);
}

int main() {
    testLayout();
    // 800 x 360 window (20:9), picture 640 x 360: all red, with a green square in the middle
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 800, 360, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* ren = SDL_CreateSoftwareRenderer(surf);
    if (!ren) {
        std::printf("bar_fill: no software renderer, layout checks only (%d checks, %d failed)\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    SDL_RenderSetLogicalSize(ren, 640, 360);
    BarFill bf;
    CHECK(bf.begin(ren, 640, 360));
    CHECK(bf.active());
    SDL_SetRenderDrawColor(ren, 200, 30, 30, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 30, 200, 30, 255);
    const SDL_Rect sq{300, 160, 40, 40};   // logical units
    SDL_RenderFillRect(ren, &sq);
    bf.end(ren);
    CHECK(!bf.active());
    auto px = [&](int x, int y) {
        const uint8_t* row = static_cast<const uint8_t*>(surf->pixels) + y * surf->pitch;
        return reinterpret_cast<const uint32_t*>(row)[x];
    };
    auto rgb = [&](int x, int y, int& r, int& g, int& b) {
        const uint32_t v = px(x, y);
        r = (v >> 16) & 0xff;
        g = (v >> 8) & 0xff;
        b = v & 0xff;
    };
    int r = 0, g = 0, b = 0;
    rgb(400, 180, r, g, b);   // the green square keeps its place and colour (picture centred: x offset 80 px)
    CHECK(g > 150 && r < 90);
    rgb(120, 40, r, g, b);    // the picture's own red, at the left edge of the picture
    CHECK(r > 150 && g < 80);
    rgb(10, 180, r, g, b);    // the left side: filled with the blurred copy (dark red), not black
    CHECK(r > 30);
    CHECK(r < 150);           // ... and darker than the picture
    rgb(790, 20, r, g, b);    // right side too
    CHECK(r > 30);
    // the logical size is back: a 640x360 picture drawn now fills 640 of the 800 pixels, centred (SDL's own letterbox)
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    const SDL_Rect all{0, 0, 640, 360};
    SDL_RenderFillRect(ren, &all);
    rgb(400, 180, r, g, b);
    CHECK(r == 255);
    rgb(5, 180, r, g, b);
    CHECK(r == 0);
    // a 16:9 window needs nothing: begin() declines and draws go straight to the window
    SDL_Surface* s2 = SDL_CreateRGBSurfaceWithFormat(0, 640, 360, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* r2 = SDL_CreateSoftwareRenderer(s2);
    SDL_RenderSetLogicalSize(r2, 640, 360);
    BarFill bf2;
    CHECK(!bf2.begin(r2, 640, 360));
    SDL_DestroyRenderer(r2);
    SDL_FreeSurface(s2);
    // TargetSwitch: a texture inside a texture gives the scale of the first one back (the bug that drew the HUD and the touch buttons at a third of
    // their size once the shadow layer ran inside BarFill's picture); a texture inside the window gives the window its own
    {
        SDL_Surface* s3 = SDL_CreateRGBSurfaceWithFormat(0, 800, 360, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer* r3 = SDL_CreateSoftwareRenderer(s3);
        SDL_Texture* a = SDL_CreateTexture(r3, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 300, 100);
        SDL_Texture* c = SDL_CreateTexture(r3, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 100, 50);
        CHECK(a && c);
        float sx = 0, sy = 0;
        SDL_SetRenderTarget(r3, a);
        SDL_RenderSetScale(r3, 3.0f, 2.0f);
        {
            TargetSwitch ts;
            CHECK(ts.enter(r3, c));
            CHECK(ts.active() && SDL_GetRenderTarget(r3) == c);
            SDL_RenderGetScale(r3, &sx, &sy);
            CHECK(sx == 1.0f && sy == 1.0f);   // SDL starts a texture target at 1
            ts.leave();
            CHECK(!ts.active() && SDL_GetRenderTarget(r3) == a);
            SDL_RenderGetScale(r3, &sx, &sy);
            CHECK(sx == 3.0f && sy == 2.0f);   // ... and we gave the first one its scale back
            ts.leave();                        // a second leave does nothing
            CHECK(SDL_GetRenderTarget(r3) == a);
        }
        {
            TargetSwitch ts;   // the destructor leaves too
            CHECK(ts.enter(r3, c));
        }
        SDL_RenderGetScale(r3, &sx, &sy);
        CHECK(SDL_GetRenderTarget(r3) == a && sx == 3.0f && sy == 2.0f);
        SDL_SetRenderTarget(r3, nullptr);
        SDL_RenderSetScale(r3, 2.5f, 2.5f);
        {
            TargetSwitch ts;
            CHECK(ts.enter(r3, c));
        }
        SDL_RenderGetScale(r3, &sx, &sy);
        CHECK(SDL_GetRenderTarget(r3) == nullptr && sx == 2.5f && sy == 2.5f);
        TargetSwitch none;
        CHECK(!none.enter(nullptr, c));
        SDL_DestroyTexture(a);
        SDL_DestroyTexture(c);
        SDL_DestroyRenderer(r3);
        SDL_FreeSurface(s3);
    }
    bf.release();
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    std::printf("bar_fill tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
