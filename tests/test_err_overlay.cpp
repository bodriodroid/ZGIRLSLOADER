// Seven-segment overlay tests: error badge, blinking, cycling, RAM panel, digit sizes.  Software renderer on an off-screen surface
// (no window, no GPU).  Set ZDROID_TEST_SHOTS=<folder> to also write BMP screenshots of every case.
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "err_overlay.h"

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

struct Px { uint8_t r, g, b; };
Px pixel(int x, int y) {
    const uint8_t* row = static_cast<const uint8_t*>(g_surf->pixels) + y * g_surf->pitch;
    const uint32_t v = reinterpret_cast<const uint32_t*>(row)[x];
    return Px{static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
}
void clear() {
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
}
void flush() { SDL_RenderPresent(g_ren); }   // software renderer on a surface draws immediately; present is harmless
template <class F>
int count(Rect b, F pred) {
    int n = 0;
    for (int y = std::max(0, b.y); y < std::min(g_surf->h, b.y + b.h); ++y)
        for (int x = std::max(0, b.x); x < std::min(g_surf->w, b.x + b.w); ++x)
            if (pred(pixel(x, y))) ++n;
    return n;
}
bool litRed(Px p) { return p.r > 200 && p.g < 120 && p.b < 120; }
bool litGreen(Px p) { return p.g > 200 && p.r < 150; }
bool anyInk(Px p) { return p.r > 8 || p.g > 8 || p.b > 8; }

int g_shot = 0;
void shot(const char* tag) {
    const char* dir = std::getenv("ZDROID_TEST_SHOTS");
    if (!dir || !*dir) return;
    char path[512];
    std::snprintf(path, sizeof path, "%s/overlay_%02d_%s.bmp", dir, ++g_shot, tag);
    SDL_SaveBMP(g_surf, path);
}
}  // namespace

int main() {
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    g_surf = SDL_CreateRGBSurfaceWithFormat(0, 640, 360, 32, SDL_PIXELFORMAT_ARGB8888);
    g_ren = SDL_CreateSoftwareRenderer(g_surf);
    SDL_RenderSetLogicalSize(g_ren, 640, 360);
    Ui ui;
    CHECK(ui.init(g_ren));
    ErrOverlay ov;
    CHECK(!ov.ready());
    CHECK(ov.init(g_ren));
    CHECK(ov.ready());

    const Rect full{0, 0, 640, 360};

    // nothing raised, debug off: not a single pixel is touched
    {
        err::Registry reg;
        clear();
        ov.draw(ui, 5000, reg, false);
        CHECK(count(full, anyInk) == 0);
    }
    // one code: the badge sits flush in the bottom right corner and only there
    {
        err::Registry reg;
        reg.raise(err::kNoData, "no UI scenes");
        clear();
        ov.draw(ui, 1000, reg, false);              // first sight of the code: lit
        const Rect b = ErrOverlay::badgeRect(640, 360, ErrOverlay::kErrDigitH, 1);
        CHECK(b.x + b.w == 640 && b.y + b.h == 360);
        CHECK(count(Rect{b.x + 8, b.y + 7, b.w - 16, ErrOverlay::kErrDigitH}, litRed) > 40);
        CHECK(count(Rect{0, 0, 640, b.y - 1}, anyInk) == 0);      // nothing above the badge (no debug line)
        CHECK(count(Rect{0, 0, b.x - 1, 360}, anyInk) == 0);      // nothing to its left
        shot("err3_lit");
        const int lit3 = count(b, litRed);
        // another digit lights other segments
        err::Registry reg5;
        reg5.raise(err::kRuntimeAltered, "dll");
        clear();
        ErrOverlay ov2;
        ov2.init(g_ren);
        ov2.draw(ui, 1000, reg5, false);
        const int lit5 = count(b, litRed);
        CHECK(lit3 > 0 && lit5 > 0);
        // "3" and "5" share five of their segments: the pictures differ, but only a little
        int diff = 0;
        {
            std::vector<Px> a;
            for (int y = b.y; y < b.y + b.h; ++y)
                for (int x = b.x; x < b.x + b.w; ++x) a.push_back(pixel(x, y));
            clear();
            ov.draw(ui, 1000, reg, false);
            size_t i = 0;
            for (int y = b.y; y < b.y + b.h; ++y)
                for (int x = b.x; x < b.x + b.w; ++x, ++i) {
                    const Px p = pixel(x, y);
                    if (std::abs(int(p.r) - int(a[i].r)) > 60) ++diff;
                }
        }
        CHECK(diff > 10 && diff < 700);
        // ghost: unlit segments stay faintly visible (dim red, never bright)
        const int ghost = count(Rect{b.x + 8, b.y + 7, b.w - 16, ErrOverlay::kErrDigitH}, [](Px p) { return p.r > 12 && p.r < 120 && p.g < 40; });
        CHECK(ghost > 20);
    }
    // a new code blinks for 2.4 s, then stays lit
    {
        err::Registry reg;
        reg.raise(err::kModDamaged, "mod x");
        ErrOverlay o;
        o.init(g_ren);
        const Rect b = ErrOverlay::badgeRect(640, 360, ErrOverlay::kErrDigitH, 1);
        const Rect digits{b.x + 8, b.y + 7, b.w - 16, ErrOverlay::kErrDigitH};
        auto litAt = [&](uint32_t t) {
            clear();
            o.draw(ui, t, reg, false);
            return count(digits, litRed);
        };
        const int on0 = litAt(10000);       // first frame sets the clock
        const int off1 = litAt(10300);      // 300 ms later: dark phase
        const int on2 = litAt(10600);
        const int steady = litAt(10000 + 2500);
        CHECK(on0 > 40 && on2 > 40 && steady > 40);
        CHECK(off1 == 0);
        // a crash never blinks
        err::Registry crash;
        crash.raise(err::kCrash, "boom");
        ErrOverlay o2;
        o2.init(g_ren);
        clear();
        o2.draw(ui, 20000, crash, false);
        const int c0 = count(digits, litRed);
        clear();
        o2.draw(ui, 20300, crash, false);
        CHECK(c0 > 40 && count(digits, litRed) == c0);
        shot("err1_crash");
    }
    // several codes take turns, with a pip per code
    {
        err::Registry reg;
        reg.raise(err::kNoAudio, "a");
        reg.raise(err::kNoData, "b");
        reg.raise(err::kModDamaged, "c");
        ErrOverlay o;
        o.init(g_ren);
        const Rect b = ErrOverlay::badgeRect(640, 360, ErrOverlay::kErrDigitH, 3);
        CHECK(b.h == ErrOverlay::kErrDigitH + 14 + 6);
        auto snap = [&](uint32_t t) {
            clear();
            o.draw(ui, t, reg, false);
            std::vector<int> v;
            for (int y = b.y; y < b.y + b.h; ++y)
                for (int x = b.x; x < b.x + b.w; ++x) v.push_back(pixel(x, y).r);
            return v;
        };
        o.draw(ui, 0, reg, false);                       // register the generation
        const auto s1 = snap(9000 + 0 * 1800);           // 9000/1800 = 5 -> index 2
        const auto s2 = snap(9000 + 1800);               // next code
        CHECK(s1 != s2);
        CHECK(count(Rect{b.x + 8, b.y + b.h - 7, 3 * 7, 3}, litRed) >= 5 * 3);   // at least the lit pip
        shot("three_codes");
    }
    // RAM panel: tall digits, green, left of the badge when both show
    {
        err::Registry none;
        ErrOverlay o;
        o.init(g_ren);
        clear();
        o.draw(ui, 1000, none, true);
        const Rect panel{640 - 260, 360 - 90, 260, 90};
#if defined(__linux__)
        CHECK(count(panel, litGreen) > 200);
        CHECK(count(panel, litRed) == 0);
#endif
        CHECK(count(Rect{0, 0, 640, 360 - 90}, anyInk) == 0);
        shot("ram");
        DebugReadout dr;
        dr.haveWorld = true;
        dr.worldHash = 0xA3F09C12u;
        dr.spriteBytes = 123ull << 20;
        clear();
        o.draw(ui, 1100, none, true, &dr);
        CHECK(count(Rect{640 - 260, 360 - 90, 260, 90}, litGreen) > 300);
        shot("ram_match");
        // with an error too: the badge keeps the corner, the panel moves left of it
        err::Registry reg;
        reg.raise(err::kNoAudio, "no .wav files: voices and sound effects are not used");
        clear();
        o.draw(ui, 3000, reg, true, &dr);
        const Rect b = ErrOverlay::badgeRect(640, 360, ErrOverlay::kErrDigitH, 1);
        CHECK(count(b, litRed) > 40);
        CHECK(count(Rect{b.x - 4, 360 - 90, 4, 90}, litGreen) == 0);
        CHECK(count(Rect{0, 360 - 140, 640, 40}, anyInk) > 0);      // the detail line above the badge
        shot("ram_and_err");
    }
    // digit sizes: tall readout digits really are taller, and scale with the request
    {
        ErrOverlay o;
        o.init(g_ren);
        CHECK(o.textWidth("0000", ErrOverlay::kRamBigH) > o.textWidth("0000", ErrOverlay::kErrDigitH) * 17 / 10);
        for (int h : {14, 18, 34, 60}) {
            clear();
            o.drawText("8", 100, 100, h, Col{255, 255, 255, 255}, false);
            int y0 = 999, y1 = -1;
            for (int y = 0; y < 360; ++y)
                for (int x = 90; x < 260; ++x)
                    if (pixel(x, y).r > 128) { y0 = std::min(y0, y); y1 = std::max(y1, y); }
            const int hh = y1 - y0 + 1;
            CHECK(hh >= h - 3 && hh <= h + 1);
        }
    }
    SDL_DestroyRenderer(g_ren);
    SDL_FreeSurface(g_surf);
    SDL_Quit();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
