// fx_math.h - the effects of the Android render path, written out in plain C++.
//
// Two jobs: (1) the CPU path - the same maths applied to RGBA buffers when no GLES pass is available (desktop test builds, thumbnails),
// and (2) the reference the GLSL ES programs of fx_glsl.cpp are checked against (tools/fx_gles_check.py renders every program with a
// real GLES driver and compares it, pixel by pixel, with these functions).
//
// All colours are linear 0..1 floats, no premultiplied alpha.  Nothing here reads a file: tuning numbers arrive through FxTuning.
#pragma once
#include <cstdint>

#include "art.h"
#include "fx_tuning.h"

namespace zengine {
namespace fx {

struct Rgb {
    float r = 0, g = 0, b = 0;
};
struct Rgba {
    float r = 0, g = 0, b = 0, a = 0;
};

// ---- screen effects: what the shader uniforms hold during a super / counter flash
struct ScreenFx {
    float shadowT = 0;   // 0..1  darken the background behind a super
    float flashT = 0;    // 0..1  remap towards the flash look
    FlashLook look;      // the selected look (tuning.sprite.look[n] or tuning.ui.look[n])
    float lumaCoeff[3] = {0.299f, 0.587f, 0.114f};
    float contrast = 1, brightness = 1, saturation = 0;
    float shadowTint[3] = {0.5f, 0.5f, 0.5f};
    float shadowLuma[3] = {0.299f, 0.587f, 0.114f};
    // Which look, shadow strength and flash strength: `flashNum` is the game's 1-based flash id (0 / 1 = first look).
    static ScreenFx make(const FxTuning& t, bool uiDomain, int flashNum, float shadowT, float flashT);
};

Rgb superShadow(Rgb in, const ScreenFx& fx);
Rgb superFlash(Rgb in, const ScreenFx& fx);
inline Rgb screenFx(Rgb in, const ScreenFx& fx) { return superFlash(superShadow(in, fx), fx); }

// Rotates the colour about the grey axis by the angle whose cosine / sine are given; `amount` 0..1 blends from the original.
Rgb hueShift(Rgb in, float cosA, float sinA, float amount);

// ---- fighter sprites: index planes (band = palette row, grad = line bits 7..5 | tone bits 4..0) + a palette
struct SpriteParams {
    float alpha = 1;
    float line[3] = {0, 0, 0};                       // colour the outline layer is drawn in
    float hueCos = 1, hueSin = 0, hueAmount = 0;
    ScreenFx screen;
};

// The colour of one texel.  With default params this is exactly toRgba(buildRgbaTables(pal)) of art.h, to within one 8-bit step.
Rgba spritePalette(const Palette& pal, uint8_t band, uint8_t grad, const SpriteParams& p);

// Flat-colour "super outline": the sprite's coverage in two fixed colours blended by `mix`.
Rgba superOutline(const Palette& pal, uint8_t band, uint8_t grad, const SpriteParams& p, const float a[3], const float b[3], float mix);

// Floor shadow: alpha of the black silhouette.  Returns a < 0 when the pixel is discarded (nothing to cast, or outside the fade
// distance / on the wrong side of the floor).  `fadeDist` is signed like the game's value (the sign says which side casts).
float floorShadowAlpha(uint8_t band, uint8_t grad, float worldZ, float fadeDist, float maxAlpha);

// ---- UI "death" look (portrait + background)
Rgba deathPortrait(const FxTuning& t, float tex0R, float tex1R, float vertexAlpha);
Rgb deathBackground(const FxTuning& t, float u, float v);

// ---- flat textured stuff (stages, UI cards): texture * vertex colour, then the screen effect.
Rgba flatTint(Rgba tex, Rgba vertex, const ScreenFx& fx);

}  // namespace fx
}  // namespace zengine
