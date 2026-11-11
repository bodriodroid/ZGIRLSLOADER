#include "fx_math.h"

#include <algorithm>
#include <cmath>

namespace zengine {
namespace fx {
namespace {
inline float sat(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline Rgb mix(Rgb a, Rgb b, float t) { return {mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t)}; }
inline float dot3(const float* c, Rgb v) { return c[0] * v.r + c[1] * v.g + c[2] * v.b; }
}  // namespace

ScreenFx ScreenFx::make(const FxTuning& t, bool uiDomain, int flashNum, float shadowT, float flashT) {
    const FlashDomain& d = uiDomain ? t.ui : t.sprite;
    const int n = std::max(0, std::min(3, flashNum - 1));
    ScreenFx f;
    f.shadowT = shadowT;
    f.flashT = flashT;
    f.look = d.look[n];
    for (int i = 0; i < 3; ++i) {
        f.lumaCoeff[i] = d.lumaCoeff[i];
        f.shadowTint[i] = t.shadowTint[i];
        f.shadowLuma[i] = t.shadowLuma[i];
    }
    f.contrast = d.contrast;
    f.brightness = d.brightness;
    f.saturation = d.saturation;
    return f;
}

Rgb superShadow(Rgb in, const ScreenFx& fx) {
    const float lum = dot3(fx.shadowLuma, in);
    const Rgb dark{fx.shadowTint[0] * lum, fx.shadowTint[1] * lum, fx.shadowTint[2] * lum};
    return mix(in, dark, fx.shadowT);
}

Rgb superFlash(Rgb in, const ScreenFx& fx) {
    const float lum = dot3(fx.lumaCoeff, in);
    // contrast pivots at mid grey (the 1.2 lift is part of the look), then brightness scales the remapped colour
    const float lift = fx.brightness * (1.2f + mix(0.5f, lum - 0.5f, fx.contrast));
    const Rgb lo{fx.look.low[0], fx.look.low[1], fx.look.low[2]}, hi{fx.look.high[0], fx.look.high[1], fx.look.high[2]};
    Rgb remap = mix(lo, hi, lum);
    remap = {remap.r * lift, remap.g * lift, remap.b * lift};
    const Rgb grey{lum, lum, lum};
    const Rgb styled = mix(remap, grey, -fx.saturation);
    return mix(in, styled, fx.flashT);
}

Rgb hueShift(Rgb in, float c, float s, float amount) {
    const float k = 0.57735f;  // 1/sqrt(3): the grey axis
    const float kd = k * (in.r + in.g + in.b) * k;
    // Rodrigues: v*cos + (k x v)*sin + k*(k.v)*(1-cos)
    const Rgb cross{k * in.b - k * in.g, k * in.r - k * in.b, k * in.g - k * in.r};
    const Rgb rot{in.r * c + cross.r * s + kd * (1 - c), in.g * c + cross.g * s + kd * (1 - c), in.b * c + cross.b * s + kd * (1 - c)};
    return mix(in, rot, amount);
}

namespace {
// 0..1 colour of (band, tone position) as the shader samples it: two palette texels blended by the tone's fraction.
Rgb paletteColour(const Palette& pal, int band, uint8_t grad) {
    const float tone = static_cast<float>(grad & 31) * 0.5f;
    int t0 = static_cast<int>(tone);
    const float fr = tone - static_cast<float>(t0);
    const int t1 = std::min(t0 + 1, pal.tones - 1);
    t0 = std::min(t0, pal.tones - 1);
    const int row = std::min(band, pal.bands - 1);
    const uint8_t* a = pal.at(row, t0);
    const uint8_t* b = pal.at(row, t1);
    return {mix(a[0] / 255.0f, b[0] / 255.0f, fr), mix(a[1] / 255.0f, b[1] / 255.0f, fr), mix(a[2] / 255.0f, b[2] / 255.0f, fr)};
}
}  // namespace

Rgba spritePalette(const Palette& pal, uint8_t band, uint8_t grad, const SpriteParams& p) {
    const float lf = static_cast<float>(grad >> 5) / 7.0f;
    Rgb col = band == 0 ? Rgb{p.line[0], p.line[1], p.line[2]} : paletteColour(pal, band, grad);
    if (band != 0) {
        col = hueShift(col, p.hueCos, p.hueSin, p.hueAmount);
        col = mix(col, Rgb{p.line[0], p.line[1], p.line[2]}, lf);
    }
    col = screenFx(col, p.screen);
    const float a = (band == 0 ? lf : 1.0f) * p.alpha;
    return {col.r, col.g, col.b, a};
}

Rgba spritePaletteSmooth(const Palette& pal, const uint8_t* band, const uint8_t* grad, int w, int h, float u, float v, const SpriteParams& p) {
    const float px = u * static_cast<float>(w) - 0.5f, py = v * static_cast<float>(h) - 0.5f;
    const float fx0 = std::floor(px), fy0 = std::floor(py);
    const float fx = px - fx0, fy = py - fy0;
    const int ix = static_cast<int>(fx0), iy = static_cast<int>(fy0);
    const float wts[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    float a = 0, cw = 0, r = 0, g = 0, b = 0;
    for (int k = 0; k < 4; ++k) {
        const int x = std::clamp(ix + (k & 1), 0, w - 1), y = std::clamp(iy + (k >> 1), 0, h - 1);
        const size_t i = static_cast<size_t>(y) * w + x;
        // texel colour and coverage with the plain (nearest) maths, before the screen effects
        SpriteParams q = p;
        q.alpha = 1;
        q.screen = ScreenFx{};
        const Rgba t = spritePalette(pal, band[i], grad[i], q);
        a += wts[k] * t.a;
        if (t.a > 0.0001f) {
            cw += wts[k];
            r += wts[k] * t.r;
            g += wts[k] * t.g;
            b += wts[k] * t.b;
        }
    }
    Rgb col = cw > 0 ? Rgb{r / cw, g / cw, b / cw} : Rgb{p.line[0], p.line[1], p.line[2]};
    col = screenFx(col, p.screen);
    return {col.r, col.g, col.b, a * p.alpha};
}

Rgba superOutline(const Palette& pal, uint8_t band, uint8_t grad, const SpriteParams& p, const float a[3], const float b[3], float m) {
    Rgba base = spritePalette(pal, band, grad, p);
    base.r = mix(a[0], b[0], m);
    base.g = mix(a[1], b[1], m);
    base.b = mix(a[2], b[2], m);
    return base;
}

float floorShadowAlpha(uint8_t band, uint8_t grad, float z, float fadeDist, float maxAlpha) {
    const float fade = std::fabs(fadeDist);
    if (std::fabs(z) > fade || z * fadeDist > 0) return -1;
    if (band == 0 && (grad >> 5) == 0) return -1;
    return maxAlpha * (1.0f - sat(std::fabs(z) / fade));
}

Rgba deathPortrait(const FxTuning& t, float r0, float r1, float va) {
    Rgba o;
    o.a = va * std::max(r0, r1);
    const float k = 1.0f - r0;
    o.r = mix(t.deathFill[0], t.deathStroke[0], k);
    o.g = mix(t.deathFill[1], t.deathStroke[1], k);
    o.b = mix(t.deathFill[2], t.deathStroke[2], k);
    return o;
}

Rgb deathBackground(const FxTuning& t, float u, float v) {
    const float dx = (u - 0.5f) * 2.0f * t.deathBgAspect, dy = (v - 0.5f) * 2.0f;
    const float d = std::sqrt(dx * dx + dy * dy);
    for (int i = 0; i < 4; ++i) {
        if (d < t.deathBgStop[i + 1] || i == 3) {
            const float span = t.deathBgStop[i + 1] - t.deathBgStop[i];
            const float k = span > 0 ? sat((d - t.deathBgStop[i]) / span) : 1.0f;
            return mix(Rgb{t.deathBg[i][0], t.deathBg[i][1], t.deathBg[i][2]}, Rgb{t.deathBg[i + 1][0], t.deathBg[i + 1][1], t.deathBg[i + 1][2]}, k);
        }
    }
    return {};
}

Rgba flatTint(Rgba tex, Rgba v, const ScreenFx& fx) {
    Rgba o{v.r * tex.r, v.g * tex.g, v.b * tex.b, v.a * tex.a};
    const Rgb c = screenFx({o.r, o.g, o.b}, fx);
    return {c.r, c.g, c.b, o.a};
}

}  // namespace fx
}  // namespace zengine
