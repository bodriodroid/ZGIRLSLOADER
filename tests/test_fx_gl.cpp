// FxRenderer tests: the PAFX programs inside SDL's real OpenGL ES 2 renderer, headless.
//
// Needs a GLES2 implementation that SDL can reach without a display (Mesa llvmpipe through EGL):
//   SDL_VIDEODRIVER=offscreen EGL_PLATFORM=surfaceless zdroid_fx_gl_tests
// Without one the test prints SKIP and exits 0 - unless ZDROID_GL_REQUIRED is set (the CI job that installs Mesa sets it, so a broken GL
// setup there is a failure, not a silent skip).
//
// What is checked, pixel by pixel against the plain C++ maths of src/fx/fx_math.h and against SDL's own draws:
//   * sprites: window, scale, viewport, clip rectangle, logical size (letterbox), render target (y flip), mirroring, the hue / flash / alpha effects
//   * the same image through the old CPU route (toRgba tables -> SDL texture) and through the GPU route look the same
//   * floor shadows (height interpolated per pixel), the super outline
//   * SDL draws before / between / after passes come out exactly as without them (program, blend, texture binding, attributes restored)
#include <SDL.h>
#include "fx_gl_headers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "fx_gl.h"
#include "imgscale.h"

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

constexpr int kW = 160, kH = 120;   // window
constexpr int kTW = 24, kTH = 16;   // test image
constexpr int kTones = 16, kBands = 6;

struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGBA
    Img() = default;
    Img(int ww, int hh) : w(ww), h(hh), px(static_cast<size_t>(ww) * hh * 4, 0) {}
    uint8_t* at(int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
};

uint32_t g_rng = 12345;
uint32_t rnd() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

struct Planes {
    int w = kTW, h = kTH;
    std::vector<uint8_t> band, grad;
};

Planes makePlanes() {
    Planes p;
    p.band.resize(static_cast<size_t>(p.w) * p.h);
    p.grad.resize(p.band.size());
    for (size_t i = 0; i < p.band.size(); ++i) {
        p.band[i] = static_cast<uint8_t>(rnd() % kBands);   // 0 = outline layer
        p.grad[i] = static_cast<uint8_t>(rnd() & 255);
    }
    return p;
}

Palette makePalette() {
    Palette pal;
    pal.tones = kTones;
    pal.bands = kBands;
    pal.rgb.resize(static_cast<size_t>(kTones) * kBands * 3);
    for (auto& c : pal.rgb) c = static_cast<uint8_t>(rnd() & 255);
    return pal;
}

// ---- GL state snapshot (the test's own entry points)
struct Gl {
    PFNGLGETINTEGERVPROC GetIntegerv = nullptr;
    PFNGLISENABLEDPROC IsEnabled = nullptr;
    PFNGLGETVERTEXATTRIBIVPROC GetVertexAttribiv = nullptr;
    PFNGLGETERRORPROC GetError = nullptr;
    bool load() {
        GetIntegerv = reinterpret_cast<PFNGLGETINTEGERVPROC>(SDL_GL_GetProcAddress("glGetIntegerv"));
        IsEnabled = reinterpret_cast<PFNGLISENABLEDPROC>(SDL_GL_GetProcAddress("glIsEnabled"));
        GetVertexAttribiv = reinterpret_cast<PFNGLGETVERTEXATTRIBIVPROC>(SDL_GL_GetProcAddress("glGetVertexAttribiv"));
        GetError = reinterpret_cast<PFNGLGETERRORPROC>(SDL_GL_GetProcAddress("glGetError"));
        return GetIntegerv && IsEnabled && GetVertexAttribiv && GetError;
    }
};
struct GlState {
    std::array<int, 40> v{};
    bool operator==(const GlState& o) const { return v == o.v; }
};
GlState snapshot(Gl& gl) {
    GlState s;
    int n = 0;
    auto geti = [&](GLenum e, int count = 1) {
        GLint tmp[4] = {};
        gl.GetIntegerv(e, tmp);
        for (int i = 0; i < count; ++i) s.v[n++] = tmp[i];
    };
    geti(GL_CURRENT_PROGRAM);
    geti(GL_ARRAY_BUFFER_BINDING);
    geti(GL_ACTIVE_TEXTURE);
    geti(GL_TEXTURE_BINDING_2D);   // of the active unit
    geti(GL_BLEND_SRC_RGB);
    geti(GL_BLEND_DST_RGB);
    geti(GL_BLEND_SRC_ALPHA);
    geti(GL_BLEND_DST_ALPHA);
    geti(GL_BLEND_EQUATION_RGB);
    geti(GL_BLEND_EQUATION_ALPHA);
    geti(GL_VIEWPORT, 4);
    geti(GL_SCISSOR_BOX, 4);
    geti(GL_FRAMEBUFFER_BINDING);
    s.v[n++] = gl.IsEnabled(GL_BLEND);
    s.v[n++] = gl.IsEnabled(GL_SCISSOR_TEST);
    for (int i = 0; i < 5; ++i) {
        GLint e = 0;
        gl.GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &e);
        s.v[n++] = e;
    }
    return s;
}

struct Ctx {
    SDL_Window* win = nullptr;
    SDL_Renderer* r = nullptr;
    Gl gl;
};

Img readback(SDL_Renderer* r, int w, int h) {
    Img img(w, h);
    SDL_Rect rc{0, 0, w, h};
    if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_RGBA32, img.px.data(), w * 4) != 0) std::fprintf(stderr, "read: %s\n", SDL_GetError());
    return img;
}

void fillBg(Img& img, uint8_t r, uint8_t g, uint8_t b) {
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            uint8_t* p = img.at(x, y);
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
}

struct Clip {
    int x0 = 0, y0 = 0, x1 = 1 << 20, y1 = 1 << 20;
};

// pixel (x, y) of `img` is inside the destination rectangle [X0,X1) x [Y0,Y1) when its centre is
bool texel(double X0, double Y0, double X1, double Y1, int x, int y, bool flip, int tw, int th, int& tx, int& ty) {
    const double cx = x + 0.5, cy = y + 0.5;
    if (cx < X0 || cx >= X1 || cy < Y0 || cy >= Y1) return false;
    tx = std::min(tw - 1, static_cast<int>(std::floor((cx - X0) / (X1 - X0) * tw)));
    ty = std::min(th - 1, static_cast<int>(std::floor((cy - Y0) / (Y1 - Y0) * th)));
    if (flip) tx = tw - 1 - tx;
    return true;
}

void blendOver(uint8_t* dst, float r, float g, float b, float a) {
    const float c[3] = {r, g, b};
    for (int i = 0; i < 3; ++i) dst[i] = static_cast<uint8_t>(std::lround(std::clamp(c[i] * a + (dst[i] / 255.0f) * (1.0f - a), 0.0f, 1.0f) * 255.0f));
}

// CPU reference of one sprite (spritePalette of fx_math.h) over `img`
void refSprite(Img& img, const Planes& pl, const Palette& pal, double X0, double Y0, double X1, double Y1, bool flip, const fx::SpriteParams& p, const Clip& clip = {}) {
    for (int y = std::max(0, clip.y0); y < std::min(img.h, clip.y1); ++y)
        for (int x = std::max(0, clip.x0); x < std::min(img.w, clip.x1); ++x) {
            int tx, ty;
            if (!texel(X0, Y0, X1, Y1, x, y, flip, pl.w, pl.h, tx, ty)) continue;
            const size_t i = static_cast<size_t>(ty) * pl.w + tx;
            const fx::Rgba c = fx::spritePalette(pal, pl.band[i], pl.grad[i], p);
            blendOver(img.at(x, y), c.r, c.g, c.b, c.a);
        }
}

// CPU reference of the bilinear sprite (spritePaletteSmooth of fx_math.h)
void refSpriteSmooth(Img& img, const Planes& pl, const Palette& pal, double X0, double Y0, double X1, double Y1, bool flip, const fx::SpriteParams& p) {
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const double cx = x + 0.5, cy = y + 0.5;
            if (cx < X0 || cx >= X1 || cy < Y0 || cy >= Y1) continue;
            double u = (cx - X0) / (X1 - X0);
            const double v = (cy - Y0) / (Y1 - Y0);
            if (flip) u = 1.0 - u;
            const fx::Rgba c = fx::spritePaletteSmooth(pal, pl.band.data(), pl.grad.data(), pl.w, pl.h, static_cast<float>(u), static_cast<float>(v), p);
            blendOver(img.at(x, y), c.r, c.g, c.b, c.a);
        }
}

struct Diff {
    int max = 0;
    int bad = 0;
    int bx = -1, by = -1;
};
Diff compare(const Img& got, const Img& ref, int tol) {
    Diff d;
    for (int y = 0; y < ref.h; ++y)
        for (int x = 0; x < ref.w; ++x)
            for (int c = 0; c < 3; ++c) {
                const int df = std::abs(static_cast<int>(got.at(x, y)[c]) - static_cast<int>(ref.at(x, y)[c]));
                d.max = std::max(d.max, df);
                if (df > tol) {
                    if (!d.bad) {
                        d.bx = x;
                        d.by = y;
                    }
                    ++d.bad;
                    break;
                }
            }
    return d;
}

void report(const char* name, const Diff& d, int tol, int allowBad = 0) {
    const bool ok = d.bad <= allowBad;
    std::printf("  %-44s max %3d  pixels over %d: %4d  %s\n", name, d.max, tol, d.bad, ok ? "ok" : "FAIL");
    if (!ok) std::printf("      first bad pixel (%d, %d)\n", d.bx, d.by);
    CHECK(ok);
}

void clearTo(SDL_Renderer* r, uint8_t R, uint8_t G, uint8_t B) {
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_RenderClear(r);
}

SDL_Texture* makeSdlTexture(SDL_Renderer* r, const Img& img) {
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, img.px.data(), img.w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    return t;
}

}  // namespace

int main() {
    // a pack from neutral numbers (no game data needed)
    fx::FxPack pack;
    {
        fx::AeadKey key{};
        for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7 + 3);
        std::array<uint8_t, 16> id{};
        for (size_t i = 0; i < id.size(); ++i) id[i] = static_cast<uint8_t>(i + 1);
        std::vector<uint8_t> bytes;
        std::string err;
        fx::CoreShaderInputs none;
        if (!fx::buildFxPack(none, key, id, bytes, nullptr, &err) || !pack.load(bytes.data(), bytes.size(), key, &err)) {
            std::fprintf(stderr, "cannot build a pack: %s\n", err.c_str());
            return 1;
        }
    }

    const int skipCode = std::getenv("ZDROID_GL_REQUIRED") ? 1 : 0;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("%s: SDL video: %s\n", skipCode ? "FAIL" : "SKIP", SDL_GetError());
        return skipCode;
    }
    Ctx cx;
    cx.win = SDL_CreateWindow("fx", 0, 0, kW, kH, SDL_WINDOW_HIDDEN);
    if (cx.win) cx.r = SDL_CreateRenderer(cx.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    SDL_RendererInfo info{};
    if (!cx.r || SDL_GetRendererInfo(cx.r, &info) != 0 || std::strcmp(info.name, "opengles2") != 0 || !cx.gl.load()) {
        std::printf("%s: no opengles2 renderer (%s)\n", skipCode ? "FAIL" : "SKIP", SDL_GetError());
        if (cx.r) SDL_DestroyRenderer(cx.r);
        if (cx.win) SDL_DestroyWindow(cx.win);
        SDL_Quit();
        return skipCode;
    }
    SDL_Renderer* r = cx.r;
    std::printf("renderer: %s, window %dx%d\n", info.name, kW, kH);

    FxRenderer fxr;
    std::string err;
    CHECK(!fxr.ready());
    CHECK(fxr.init(r, pack, &err));
    if (!fxr.ready()) {
        std::fprintf(stderr, "init: %s\n", err.c_str());
        return 1;
    }
    CHECK(fxr.hasOutline());
    CHECK(fxr.maxTextureSize() >= 64);

    const Planes pl = makePlanes();
    const Palette pal = makePalette();
    const std::vector<uint8_t> packed = FxRenderer::packIndex(pl.band.data(), pl.grad.data(), pl.w, pl.h);
    CHECK(packed.size() == static_cast<size_t>(pl.w) * pl.h * 4);
    FxIndexTex tex = fxr.createIndex(packed.data(), pl.w, pl.h);
    FxPaletteTex ptex = fxr.createPalette(pal);
    CHECK(tex.valid() && tex.w == pl.w && tex.h == pl.h);
    CHECK(ptex.valid() && ptex.tones == kTones && ptex.bands == kBands);
    FxIndexTex huge = fxr.createIndex(packed.data(), fxr.maxTextureSize() + 1, 1);
    CHECK(!huge.valid());   // bigger than the GPU takes: refused, nothing leaks
    CHECK(!fxr.createIndex(nullptr, 4, 4).valid());
    const fx::SpriteParams plain;   // alpha 1, black outline, no hue, no screen effect

    auto spriteDraw = [&](const SDL_FRect& d, bool flip, const fx::SpriteParams& p) {
        FxRenderer::Pass pass(fxr);
        CHECK(pass.ok());
        pass.sprite(tex, ptex, d, flip, p);
    };

    // ---------------------------------------------------------------- window
    std::printf("sprites in the window\n");
    {
        clearTo(r, 40, 90, 160);
        spriteDraw({10, 7, 48, 32}, false, plain);
        Img ref(kW, kH);
        fillBg(ref, 40, 90, 160);
        refSprite(ref, pl, pal, 10, 7, 58, 39, false, plain);
        report("plain", compare(readback(r, kW, kH), ref, 2), 2);

        clearTo(r, 40, 90, 160);
        spriteDraw({70, 50, 48, 32}, true, plain);
        Img ref2(kW, kH);
        fillBg(ref2, 40, 90, 160);
        refSprite(ref2, pl, pal, 70, 50, 118, 82, true, plain);
        report("mirrored", compare(readback(r, kW, kH), ref2, 2), 2);
    }
    {
        // the effects: hue shift, the super darkening and flash, a translucent sprite, a coloured outline layer
        fx::SpriteParams p;
        p.alpha = 0.8f;
        p.line[0] = 0.1f;
        p.line[1] = 0.05f;
        p.line[2] = 0.0f;
        p.hueCos = std::cos(0.8f);
        p.hueSin = std::sin(0.8f);
        p.hueAmount = 0.7f;
        p.screen = fx::ScreenFx::make(fxr.tuning(), false, 2, 0.5f, 0.6f);
        clearTo(r, 200, 120, 30);
        spriteDraw({20, 20, 72, 48}, false, p);   // 3 px per texel
        Img ref(kW, kH);
        fillBg(ref, 200, 120, 30);
        refSprite(ref, pl, pal, 20, 20, 92, 68, false, p);
        report("hue + flash + shadow + alpha + line colour", compare(readback(r, kW, kH), ref, 3), 3);
    }

    // ---------------------------------------------------------------- SDL's transforms
    std::printf("SDL viewport, scale, clip, logical size\n");
    {
        SDL_RenderSetScale(r, 2.0f, 2.0f);
        clearTo(r, 10, 20, 30);
        spriteDraw({5, 4, 24, 16}, false, plain);   // 48x32 pixels from (10, 8)
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        Img ref(kW, kH);
        fillBg(ref, 10, 20, 30);
        refSprite(ref, pl, pal, 10, 8, 58, 40, false, plain);
        report("scale 2", compare(readback(r, kW, kH), ref, 2), 2);
    }
    {
        const SDL_Rect vp{20, 10, 100, 80};
        SDL_RenderSetViewport(r, &vp);
        SDL_RenderSetViewport(r, nullptr);
        clearTo(r, 10, 20, 30);
        SDL_RenderSetViewport(r, &vp);
        spriteDraw({6, 5, 48, 32}, false, plain);
        SDL_RenderSetViewport(r, nullptr);
        Img ref(kW, kH);
        fillBg(ref, 10, 20, 30);
        refSprite(ref, pl, pal, 26, 15, 74, 47, false, plain, Clip{20, 10, 120, 90});
        report("viewport offset", compare(readback(r, kW, kH), ref, 2), 2);
    }
    {
        clearTo(r, 10, 20, 30);
        const SDL_Rect clip{30, 20, 28, 20};
        SDL_RenderSetClipRect(r, &clip);
        spriteDraw({10, 7, 48, 32}, false, plain);
        SDL_RenderSetClipRect(r, nullptr);
        Img ref(kW, kH);
        fillBg(ref, 10, 20, 30);
        refSprite(ref, pl, pal, 10, 7, 58, 39, false, plain, Clip{30, 20, 58, 40});
        report("clip rectangle", compare(readback(r, kW, kH), ref, 2), 2);
        // the same, with the clip set but the sprite outside it: nothing is drawn
        clearTo(r, 10, 20, 30);
        const SDL_Rect far{120, 90, 20, 20};
        SDL_RenderSetClipRect(r, &far);
        spriteDraw({10, 7, 48, 32}, false, plain);
        SDL_RenderSetClipRect(r, nullptr);
        Img ref2(kW, kH);
        fillBg(ref2, 10, 20, 30);
        report("clip rectangle elsewhere", compare(readback(r, kW, kH), ref2, 0), 0);
    }
    {
        // 64x36 logical units on the 160x120 window: scale 2.5, 15 px letterbox at the top
        SDL_RenderSetLogicalSize(r, 64, 36);
        clearTo(r, 10, 20, 30);
        spriteDraw({4, 2, 48, 32}, false, plain);   // 120x80 px from (10, 20): 5 px per texel, no texel edge on a pixel centre
        SDL_RenderSetLogicalSize(r, 0, 0);
        Img ref(kW, kH);
        fillBg(ref, 10, 20, 30);
        refSprite(ref, pl, pal, 10, 20, 130, 100, false, plain);
        report("logical size (letterbox, scale 2.5)", compare(readback(r, kW, kH), ref, 2), 2, 0);
    }

    // ---------------------------------------------------------------- render target (the Play screen draws into one)
    std::printf("render target\n");
    {
        constexpr int TW = 96, TH = 64;
        SDL_Texture* rt = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, TW, TH);
        CHECK(rt != nullptr);
        if (rt) {
            SDL_SetRenderTarget(r, rt);
            clearTo(r, 50, 60, 70);
            spriteDraw({8, 6, 48, 32}, false, plain);
            Img got = readback(r, TW, TH);
            Img ref(TW, TH);
            fillBg(ref, 50, 60, 70);
            refSprite(ref, pl, pal, 8, 6, 56, 38, false, plain);
            report("render target 96x64", compare(got, ref, 2), 2);

            // scaled target, like the Play screen's 640x360 units on a smaller texture
            clearTo(r, 50, 60, 70);
            SDL_RenderSetScale(r, 1.5f, 1.5f);
            spriteDraw({4, 4, 32, 21.333333f}, true, plain);   // not exact in pixels: only the covered area has to agree
            SDL_RenderSetScale(r, 1.0f, 1.0f);
            got = readback(r, TW, TH);
            Img ref2(TW, TH);
            fillBg(ref2, 50, 60, 70);
            refSprite(ref2, pl, pal, 6, 6, 54, 38, true, plain);
            report("render target, scale 1.5", compare(got, ref2, 2), 2, 40);
            SDL_SetRenderTarget(r, nullptr);
            SDL_DestroyTexture(rt);
        }
    }

    // ---------------------------------------------------------------- same picture through the old CPU route
    std::printf("GPU route against the CPU route (toRgba -> SDL texture)\n");
    {
        ArtPlanes ap;
        ap.width = pl.w;
        ap.height = pl.h;
        ap.band = pl.band;
        ap.grad = pl.grad;
        const std::vector<uint8_t> tables = buildRgbaTables(pal);
        Img cpu(pl.w, pl.h);
        cpu.px = toRgba(ap, tables);
        SDL_Texture* t = makeSdlTexture(r, cpu);
        CHECK(t != nullptr);
        if (t) {
            clearTo(r, 40, 90, 160);
            const SDL_FRect d{12, 9, 72, 48};
            SDL_RenderCopyExF(r, t, nullptr, &d, 0, nullptr, SDL_FLIP_NONE);
            const Img viaCpu = readback(r, kW, kH);
            clearTo(r, 40, 90, 160);
            spriteDraw(d, false, plain);
            report("same sprite, SDL texture vs GPU", compare(readback(r, kW, kH), viaCpu, 3), 3);
            clearTo(r, 40, 90, 160);
            const SDL_FRect d2{12, 9, 72, 48};
            SDL_RenderCopyExF(r, t, nullptr, &d2, 0, nullptr, SDL_FLIP_HORIZONTAL);
            const Img viaCpu2 = readback(r, kW, kH);
            clearTo(r, 40, 90, 160);
            spriteDraw(d2, true, plain);
            report("mirrored, SDL texture vs GPU", compare(readback(r, kW, kH), viaCpu2, 3), 3);
            SDL_DestroyTexture(t);
        }
    }

    // ---------------------------------------------------------------- smooth (bilinear) sprites
    std::printf("smooth sprites\n");
    CHECK(fxr.hasSmooth());
    {
        auto smoothDraw = [&](const SDL_FRect& d, bool flip, const fx::SpriteParams& p) {
            FxRenderer::Pass pass(fxr);
            pass.sprite(tex, ptex, d, flip, p, true);
        };
        clearTo(r, 40, 90, 160);
        smoothDraw({10, 8, 36, 24}, false, plain);   // 1.5 px per texel
        Img ref(kW, kH);
        fillBg(ref, 40, 90, 160);
        refSpriteSmooth(ref, pl, pal, 10, 8, 46, 32, false, plain);
        report("1.5x, against the C++ bilinear maths", compare(readback(r, kW, kH), ref, 3), 3);

        clearTo(r, 40, 90, 160);
        smoothDraw({60, 30, 55.5f, 37.25f}, true, plain);   // 2.3125 px per texel, mirrored, on fractional edges
        Img ref2(kW, kH);
        fillBg(ref2, 40, 90, 160);
        refSpriteSmooth(ref2, pl, pal, 60, 30, 115.5, 67.25, true, plain);
        report("2.3x mirrored, fractional edges", compare(readback(r, kW, kH), ref2, 3), 3, 4);

        fx::SpriteParams p;
        p.alpha = 0.7f;
        p.hueCos = std::cos(1.1f);
        p.hueSin = std::sin(1.1f);
        p.hueAmount = 1.0f;
        p.screen = fx::ScreenFx::make(fxr.tuning(), false, 1, 0.3f, 0.5f);
        clearTo(r, 90, 40, 60);
        smoothDraw({20, 20, 60, 40}, false, p);
        Img ref3(kW, kH);
        fillBg(ref3, 90, 40, 60);
        refSpriteSmooth(ref3, pl, pal, 20, 20, 80, 60, false, p);
        report("hue + flash + alpha", compare(readback(r, kW, kH), ref3, 3), 3);

        // at whole-texel scale the bilinear program and the nearest one agree wherever neighbouring texels are alike, and the smooth one
        // never samples outside the picture: nothing is drawn beyond the rectangle
        clearTo(r, 0, 0, 0);
        smoothDraw({40, 30, 48, 32}, false, plain);
        const Img edge = readback(r, kW, kH);
        bool leak = false;
        for (int y = 0; y < kH && !leak; ++y)
            for (int x = 0; x < kW; ++x)
                if ((x < 40 || x >= 88 || y < 30 || y >= 62) && (edge.at(x, y)[0] | edge.at(x, y)[1] | edge.at(x, y)[2])) {
                    leak = true;
                    break;
                }
        CHECK(!leak);

        // against the old CPU route: toRgba -> bleedTransparent -> SDL texture with the linear filter.  The two filter in a different
        // order (the CPU route blends 8-bit colours that were already rounded), so this compares looks, not bits: a smooth picture must
        // agree closely, the noise picture used above (every texel unrelated to the next) only on average.
        ArtPlanes ap;
        ap.width = pl.w;
        ap.height = pl.h;
        ap.band = pl.band;
        ap.grad = pl.grad;
        Img cpu(pl.w, pl.h);
        cpu.px = toRgba(ap, buildRgbaTables(pal));
        bleedTransparent(cpu.px.data(), cpu.w, cpu.h, 2);
        SDL_Texture* t = makeSdlTexture(r, cpu);
        CHECK(t != nullptr);
        if (t) {
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
            const SDL_FRect d{20, 20, 60, 40};
            clearTo(r, 40, 90, 160);
            SDL_RenderCopyExF(r, t, nullptr, &d, 0, nullptr, SDL_FLIP_NONE);
            const Img viaCpu = readback(r, kW, kH);
            clearTo(r, 40, 90, 160);
            smoothDraw(d, false, plain);
            const Img viaGpu = readback(r, kW, kH);
            double sum = 0;
            long n = 0;
            for (int y = 20; y < 60; ++y)
                for (int x = 20; x < 80; ++x)
                    for (int c = 0; c < 3; ++c) {
                        sum += std::abs(static_cast<int>(viaCpu.at(x, y)[c]) - static_cast<int>(viaGpu.at(x, y)[c]));
                        ++n;
                    }
            const double mean = sum / static_cast<double>(n);
            std::printf("  %-44s mean difference %.2f / 255 (noise picture)\n", "SDL linear texture vs GPU bilinear", mean);
            CHECK(mean < 3.0);
            SDL_DestroyTexture(t);
        }
    }

    // ---------------------------------------------------------------- SDL's own draws around passes
    std::printf("SDL draws around a pass\n");
    {
        Img small(8, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                uint8_t* p = small.at(x, y);
                p[0] = static_cast<uint8_t>(x * 30);
                p[1] = static_cast<uint8_t>(y * 30);
                p[2] = static_cast<uint8_t>(255 - x * 20);
                p[3] = 255;
            }
        SDL_Texture* t1 = makeSdlTexture(r, small);
        CHECK(t1 != nullptr);
        if (t1) {
            clearTo(r, 40, 90, 160);
            // 1. a textured draw (SDL caches texture, program, texcoord attribute)
            const SDL_Rect a{4, 4, 16, 16};
            SDL_RenderCopy(r, t1, nullptr, &a);
            // 2. a pass in the middle
            spriteDraw({30, 20, 48, 32}, false, plain);
            // 3. the same texture again: SDL believes it is still bound
            const SDL_Rect b{100, 4, 16, 16};
            SDL_RenderCopy(r, t1, nullptr, &b);
            // 4. an opaque fill with blending off: SDL believes blending is off
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 200, 10, 10, 128);
            const SDL_Rect c{100, 60, 20, 20};
            SDL_RenderFillRect(r, &c);
            // 5. another pass, then a plain solid draw (texcoord attribute off) and a blended one
            spriteDraw({60, 70, 24, 16}, true, plain);
            SDL_SetRenderDrawColor(r, 10, 200, 10, 128);
            const SDL_Rect d{130, 60, 20, 20};
            SDL_RenderFillRect(r, &d);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 128);
            const SDL_Rect e{130, 90, 20, 20};
            SDL_RenderFillRect(r, &e);
            // 6. a texture draw with a colour modulation after everything
            SDL_SetTextureColorMod(t1, 128, 255, 128);
            const SDL_Rect f{4, 90, 16, 16};
            SDL_RenderCopy(r, t1, nullptr, &f);
            SDL_SetTextureColorMod(t1, 255, 255, 255);

            Img ref(kW, kH);
            fillBg(ref, 40, 90, 160);
            auto copyRef = [&](int dx, int dy, int scale, bool mod) {
                for (int y = 0; y < 8 * scale; ++y)
                    for (int x = 0; x < 8 * scale; ++x) {
                        uint8_t* p = ref.at(dx + x, dy + y);
                        const uint8_t* s = small.at(x / scale, y / scale);
                        p[0] = mod ? static_cast<uint8_t>(s[0] * 128 / 255) : s[0];
                        p[1] = s[1];
                        p[2] = mod ? static_cast<uint8_t>(s[2] * 128 / 255) : s[2];
                    }
            };
            copyRef(4, 4, 2, false);
            refSprite(ref, pl, pal, 30, 20, 78, 52, false, plain);
            copyRef(100, 4, 2, false);
            for (int y = 60; y < 80; ++y)
                for (int x = 100; x < 120; ++x) {
                    uint8_t* p = ref.at(x, y);
                    p[0] = 200;
                    p[1] = 10;
                    p[2] = 10;
                }
            refSprite(ref, pl, pal, 60, 70, 84, 86, true, plain);
            for (int y = 60; y < 80; ++y)
                for (int x = 130; x < 150; ++x) {
                    uint8_t* p = ref.at(x, y);
                    p[0] = 10;
                    p[1] = 200;
                    p[2] = 10;
                }
            for (int y = 90; y < 110; ++y)
                for (int x = 130; x < 150; ++x) blendOver(ref.at(x, y), 1, 1, 1, 128 / 255.0f);
            copyRef(4, 90, 2, true);
            report("SDL draws before / between / after passes", compare(readback(r, kW, kH), ref, 3), 3);
            SDL_DestroyTexture(t1);
        }
    }
    std::printf("GL state restored\n");
    {
        // SDL's own settled state first (an invisible draw makes it apply everything it has pending), then a pass with every kind of draw
        auto settle = [&]() {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            SDL_RenderDrawPoint(r, 0, 0);
            SDL_RenderFlush(r);
        };
        Img small(4, 4);
        fillBg(small, 9, 9, 9);
        SDL_Texture* t1 = makeSdlTexture(r, small);
        const SDL_Rect a{1, 1, 4, 4};
        SDL_RenderCopy(r, t1, nullptr, &a);   // leave SDL with a texture bound, texcoords on
        settle();
        const GlState before = snapshot(cx.gl);
        {
            FxRenderer::Pass pass(fxr);
            CHECK(pass.ok());
            pass.sprite(tex, ptex, {0, 0, 48, 32}, false, plain);
            const float a3[3] = {1, 0, 0}, b3[3] = {0, 0, 1};
            pass.outline(tex, {0, 0, 48, 32}, true, 1.0f, a3, b3, 0.5f);
            FxShadowQuad q;
            const float xs[4] = {0, 48, 0, 48}, ys[4] = {40, 40, 10, 10}, zs[4] = {0, 0, 30, 30}, us[4] = {0, 1, 0, 1}, vs[4] = {0, 0, 1, 1};
            for (int i = 0; i < 4; ++i) {
                q.x[i] = xs[i];
                q.y[i] = ys[i];
                q.z[i] = zs[i];
                q.u[i] = us[i];
                q.v[i] = vs[i];
            }
            pass.shadow(tex, q, 60.0f, 0.5f);
        }
        const GlState after = snapshot(cx.gl);
        CHECK(before == after);
        if (!(before == after))
            for (size_t i = 0; i < before.v.size(); ++i)
                if (before.v[i] != after.v[i]) std::fprintf(stderr, "  state slot %zu: %d -> %d\n", i, before.v[i], after.v[i]);
        CHECK(cx.gl.GetError() == GL_NO_ERROR);
        // creating and destroying textures leaves the bindings alone too
        const GlState b2 = snapshot(cx.gl);
        FxIndexTex t2 = fxr.createIndex(packed.data(), pl.w, pl.h);
        FxPaletteTex p2 = fxr.createPalette(pal);
        CHECK(t2.valid() && p2.valid());
        fxr.destroy(t2);
        fxr.destroy(p2);
        CHECK(!t2.valid() && !p2.valid());
        CHECK(b2 == snapshot(cx.gl));
        SDL_DestroyTexture(t1);
    }

    // ---------------------------------------------------------------- shadows and outline
    std::printf("floor shadow and super outline\n");
    {
        FxShadowQuad q;
        // 48x32 px quad at (20, 30); the height above the floor is 10 at the top edge and 70 at the bottom edge
        const float x0 = 20, x1 = 68, y0 = 30, y1 = 62, zTop = 10, zBot = 70;
        const float xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1}, zs[4] = {zTop, zTop, zBot, zBot}, us[4] = {0, 1, 0, 1}, vs[4] = {0, 0, 1, 1};
        for (int i = 0; i < 4; ++i) {
            q.x[i] = xs[i];
            q.y[i] = ys[i];
            q.z[i] = zs[i];
            q.u[i] = us[i];
            q.v[i] = vs[i];
        }
        const float dist = 60.0f, maxA = 0.6f;
        clearTo(r, 120, 200, 120);
        {
            FxRenderer::Pass pass(fxr);
            pass.shadow(tex, q, dist, maxA);
        }
        Img ref(kW, kH);
        fillBg(ref, 120, 200, 120);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                int tx, ty;
                if (!texel(x0, y0, x1, y1, x, y, false, pl.w, pl.h, tx, ty)) continue;
                const double z = zTop + (zBot - zTop) * ((y + 0.5 - y0) / (y1 - y0));
                const size_t i = static_cast<size_t>(ty) * pl.w + tx;
                const float a = fx::floorShadowAlpha(pl.band[i], pl.grad[i], static_cast<float>(z), -dist, maxA);
                if (a < 0) continue;
                blendOver(ref.at(x, y), 0, 0, 0, a);
            }
        // pixels whose height is within a hair of the fade distance (z = 60 at y = 56.5) may fall on either side of the discard rule
        report("shadow, height fades 10 -> 70 (fade distance 60)", compare(readback(r, kW, kH), ref, 3), 3, 12);
    }
    {
        const float A[3] = {1.0f, 0.2f, 0.0f}, B[3] = {0.0f, 0.4f, 1.0f};
        clearTo(r, 30, 30, 30);
        {
            FxRenderer::Pass pass(fxr);
            pass.outline(tex, {16, 12, 48, 32}, false, 0.75f, A, B, 0.3f);
        }
        Img ref(kW, kH);
        fillBg(ref, 30, 30, 30);
        fx::SpriteParams p;
        p.alpha = 0.75f;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                int tx, ty;
                if (!texel(16, 12, 64, 44, x, y, false, pl.w, pl.h, tx, ty)) continue;
                const size_t i = static_cast<size_t>(ty) * pl.w + tx;
                const fx::Rgba c = fx::superOutline(pal, pl.band[i], pl.grad[i], p, A, B, 0.3f);
                blendOver(ref.at(x, y), c.r, c.g, c.b, c.a);
            }
        report("super outline", compare(readback(r, kW, kH), ref, 3), 3);
    }

    // ---------------------------------------------------------------- lifetime
    std::printf("lifetime\n");
    {
        fxr.destroy(tex);
        fxr.destroy(ptex);
        CHECK(!tex.valid() && !ptex.valid());
        FxRenderer::Pass pass(fxr);
        pass.sprite(tex, ptex, {0, 0, 10, 10}, false, plain);   // invalid textures: a no-op, not a crash
        fxr.shutdown();
        CHECK(!fxr.ready());
        FxRenderer::Pass dead(fxr);
        CHECK(!dead.ok());
        // a second renderer on a renderer that is not "opengles2" is refused
        CHECK(!fxr.init(nullptr, pack, &err));
    }

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(cx.win);
    SDL_Quit();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
