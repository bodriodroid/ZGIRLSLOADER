#include "fx_glsl.h"

namespace zengine {
namespace fx {
namespace {

const char* const kVertex = R"GLSL(
attribute vec3 aPos;
attribute vec2 aUV;
attribute vec4 aColor;
uniform mat4 uMvp;
varying vec2 vUV;
varying vec4 vColor;
varying float vWorldZ;
void main() {
    vUV = aUV;
    vColor = aColor;
    vWorldZ = aPos.z;
    gl_Position = uMvp * vec4(aPos.xy, 0.0, 1.0);
}
)GLSL";

const char* const kPrelude = R"GLSL(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 vUV;
varying vec4 vColor;
varying float vWorldZ;
)GLSL";

// screen effects: the darkening behind a super and the flash remap
const char* const kScreen = R"GLSL(
uniform vec2 uScreen;
uniform vec3 uFlashLow;
uniform vec3 uFlashHigh;
uniform vec3 uFlashLuma;
uniform vec3 uFlashCurve;
uniform vec3 uShadowTint;
uniform vec3 uShadowLuma;
vec3 fxShadow(vec3 c) {
    float l = dot(c, uShadowLuma);
    return mix(c, uShadowTint * l, uScreen.x);
}
vec3 fxFlash(vec3 c) {
    float l = dot(c, uFlashLuma);
    float lift = uFlashCurve.y * (1.2 + mix(0.5, l - 0.5, uFlashCurve.x));
    vec3 remap = mix(uFlashLow, uFlashHigh, l) * lift;
    vec3 styled = mix(remap, vec3(l), -uFlashCurve.z);
    return mix(c, styled, uScreen.y);
}
vec3 fxScreen(vec3 c) { return fxFlash(fxShadow(c)); }
)GLSL";

// index planes + palette
const char* const kSprite = R"GLSL(
uniform sampler2D uIndex;      // G = grad (line bits 7..5, tone bits 4..0), B = band (palette row, 0 = outline layer); nearest
uniform sampler2D uPalette;    // tones x bands, RGB; nearest
uniform vec2 uPaletteSize;     // tones, bands
uniform float uAlpha;
uniform vec3 uLine;            // colour of the outline layer
uniform vec3 uHue;             // cos, sin, amount
vec3 fxHue(vec3 c, vec3 h) {
    vec3 k = vec3(0.57735);
    vec3 r = c * h.x + cross(k, c) * h.y + k * dot(k, c) * (1.0 - h.x);
    return mix(c, r, h.z);
}
// x = band, y = grad byte
vec2 fxIndex() {
    vec4 idx = texture2D(uIndex, vUV);
    return vec2(floor(idx.b * 255.0 + 0.5), floor(idx.g * 255.0 + 0.5));
}
vec3 fxPaletteColour(float band, float g) {
    float tone = mod(g, 32.0) * 0.5;
    float t0 = floor(tone);
    float fr = tone - t0;
    float t1 = min(t0 + 1.0, uPaletteSize.x - 1.0);
    t0 = min(t0, uPaletteSize.x - 1.0);
    float row = min(band, uPaletteSize.y - 1.0);
    vec3 a = texture2D(uPalette, vec2((t0 + 0.5) / uPaletteSize.x, (row + 0.5) / uPaletteSize.y)).rgb;
    vec3 b = texture2D(uPalette, vec2((t1 + 0.5) / uPaletteSize.x, (row + 0.5) / uPaletteSize.y)).rgb;
    return mix(a, b, fr);
}
)GLSL";

const char* const kSpriteMain = R"GLSL(
void main() {
    vec2 bg = fxIndex();
    float lf = floor(bg.y / 32.0) / 7.0;
    vec3 col;
    float a;
    if (bg.x < 0.5) {
        col = uLine;
        a = lf;
    } else {
        col = fxHue(fxPaletteColour(bg.x, bg.y), uHue);
        col = mix(col, uLine, lf);
        a = 1.0;
    }
    gl_FragColor = vec4(fxScreen(col), a * uAlpha);
}
)GLSL";

// Same picture with the coloured texels filtered bilinearly (the CPU route's "Smooth" look): each of the four texels around the
// sample point is turned into (colour, alpha) first, then they are blended; texels with no coverage lend no colour (the CPU route
// copies the neighbours' colour into them for the same reason).
const char* const kSpriteSmoothMain = R"GLSL(
uniform vec2 uTexSize;         // texels of uIndex
vec4 fxTexel(vec2 ij) {
    vec2 uv = (clamp(ij, vec2(0.0), uTexSize - 1.0) + 0.5) / uTexSize;
    vec4 idx = texture2D(uIndex, uv);
    float band = floor(idx.b * 255.0 + 0.5);
    float g = floor(idx.g * 255.0 + 0.5);
    float lf = floor(g / 32.0) / 7.0;
    if (band < 0.5) return vec4(uLine, lf);
    return vec4(mix(fxHue(fxPaletteColour(band, g), uHue), uLine, lf), 1.0);
}
void main() {
    vec2 p = vUV * uTexSize - 0.5;
    vec2 i0 = floor(p);
    vec2 f = p - i0;
    vec4 t00 = fxTexel(i0);
    vec4 t10 = fxTexel(i0 + vec2(1.0, 0.0));
    vec4 t01 = fxTexel(i0 + vec2(0.0, 1.0));
    vec4 t11 = fxTexel(i0 + vec2(1.0, 1.0));
    float w00 = (1.0 - f.x) * (1.0 - f.y), w10 = f.x * (1.0 - f.y), w01 = (1.0 - f.x) * f.y, w11 = f.x * f.y;
    float a = w00 * t00.a + w10 * t10.a + w01 * t01.a + w11 * t11.a;
    float c00 = w00 * step(0.0001, t00.a), c10 = w10 * step(0.0001, t10.a), c01 = w01 * step(0.0001, t01.a), c11 = w11 * step(0.0001, t11.a);
    float cw = c00 + c10 + c01 + c11;
    vec3 col = cw > 0.0 ? (t00.rgb * c00 + t10.rgb * c10 + t01.rgb * c01 + t11.rgb * c11) / cw : uLine;
    gl_FragColor = vec4(fxScreen(col), a * uAlpha);
}
)GLSL";

const char* const kOutlineMain = R"GLSL(
uniform vec3 uOutlineA;
uniform vec3 uOutlineB;
uniform float uOutlineMix;
void main() {
    vec2 bg = fxIndex();
    float lf = floor(bg.y / 32.0) / 7.0;
    float a = bg.x < 0.5 ? lf : 1.0;
    gl_FragColor = vec4(mix(uOutlineA, uOutlineB, uOutlineMix), a * uAlpha);
}
)GLSL";

const char* const kFloorMain = R"GLSL(
uniform vec2 uFloor;           // signed fade distance (the sign says which side casts), strongest alpha
void main() {
    float fade = abs(uFloor.x);
    float z = vWorldZ;
    if (abs(z) > fade || z * uFloor.x > 0.0) discard;
    vec2 bg = fxIndex();
    if (bg.x < 0.5 && floor(bg.y / 32.0) < 0.5) discard;
    gl_FragColor = vec4(0.0, 0.0, 0.0, uFloor.y * (1.0 - clamp(abs(z) / fade, 0.0, 1.0)));
}
)GLSL";

const char* const kFlatMain = R"GLSL(
uniform sampler2D uTex;
void main() {
    vec4 c = texture2D(uTex, vUV) * vColor;
    gl_FragColor = vec4(fxScreen(c.rgb), c.a);
}
)GLSL";

const char* const kDeathPortrait = R"GLSL(
uniform sampler2D uTex;        // coverage in R
uniform vec3 uStroke;
uniform vec3 uFill;
uniform float uShift;          // horizontal UV offset of the second sample (widens the stroke)
void main() {
    float r0 = texture2D(uTex, vUV).r;
    float r1 = texture2D(uTex, vUV + vec2(uShift, 0.0)).r;
    gl_FragColor = vec4(mix(uFill, uStroke, 1.0 - r0), vColor.a * max(r0, r1));
}
)GLSL";

const char* const kDeathBackground = R"GLSL(
uniform vec3 uBg[5];
uniform float uBgStop[5];
uniform float uAspect;
float seg(float d, float a, float b) {
    float span = b - a;
    return span > 0.0 ? clamp((d - a) / span, 0.0, 1.0) : 1.0;
}
void main() {
    vec2 q = (vUV - 0.5) * 2.0;
    q.x *= uAspect;
    float d = length(q);
    vec3 c;
    if (d < uBgStop[1]) c = mix(uBg[0], uBg[1], seg(d, uBgStop[0], uBgStop[1]));
    else if (d < uBgStop[2]) c = mix(uBg[1], uBg[2], seg(d, uBgStop[1], uBgStop[2]));
    else if (d < uBgStop[3]) c = mix(uBg[2], uBg[3], seg(d, uBgStop[2], uBgStop[3]));
    else c = mix(uBg[3], uBg[4], seg(d, uBgStop[3], uBgStop[4]));
    gl_FragColor = vec4(c, vColor.a);
}
)GLSL";

std::vector<FxProgramSource> build() {
    const std::string pre = kPrelude;
    const std::string scr = kScreen, spr = kSprite;
    std::vector<FxProgramSource> v;
    v.push_back({"sprite_palette", kVertex, pre + scr + spr + kSpriteMain});
    v.push_back({"super_outline", kVertex, pre + scr + spr + kOutlineMain});
    v.push_back({"floor_shadow", kVertex, pre + scr + spr + kFloorMain});
    v.push_back({"flat_tint", kVertex, pre + scr + kFlatMain});
    v.push_back({"death_portrait", kVertex, pre + kDeathPortrait});
    v.push_back({"death_background", kVertex, pre + kDeathBackground});
    v.push_back({"sprite_palette_smooth", kVertex, pre + scr + spr + kSpriteSmoothMain});   // last: older packs lack it
    return v;
}
}  // namespace

const std::vector<FxProgramSource>& fxProgramSources() {
    static const std::vector<FxProgramSource> v = build();
    return v;
}

const FxProgramSource* findFxProgram(const std::string& name) {
    for (const FxProgramSource& p : fxProgramSources())
        if (p.name == name) return &p;
    return nullptr;
}

}  // namespace fx
}  // namespace zengine
