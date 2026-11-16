// fighter_light.h - the stage's lights on the fighters.
//
// A stage's .lvl lists lights (`Light: Amb|VitaAmb|Dir|Pt ...`, see level.h).  They do not touch the stage's own pictures; they are for the
// fighters, whose sprites carry a surface direction per pixel (art.h, ArtPlanes::normal) and a "takes light" flag per palette colour.  A pixel
// that takes light comes out as
//
//     base * (ambient + diffuse) + rim
//
// - ambient: one colour for the whole stage;
// - diffuse: how much a light faces the sprite as a whole (fighters are flat cut-outs facing the camera, so the pixel's own direction is not used);
// - rim: the light grazing the edges - the pixel's direction against the light's sideways direction, so the side of a body that faces the light
//   catches it and the side that faces away does not.
//
// Directional lights (`Dir`) do not depend on where the fighter stands: ambient, diffuse and rim of all of them are one table per sprite
// orientation, which is baked into the sprite's pixels once (applyLighting).  Point lights (`Pt`, a position and a radius) depend on where the
// fighter is: pointDiffuse() gives the colour they add at a position.
//
// Colours are the .lvl's 0..255 integers divided by 255 (they may exceed 1: a stage can light a fighter brighter than its palette).
// Presentation only; nothing here touches the fight.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "art.h"
#include "level.h"

namespace zengine {

struct FighterLight {
    struct Dir {
        float colour[3] = {0, 0, 0};
        float dir[3] = {0, 0, 0};   // unit vector, the direction the light travels (z < 0: from the camera's side)
    };
    struct Pt {
        float colour[3] = {0, 0, 0};
        float pos[3] = {0, 0, 0};   // stage units
        float invRadius = 0;        // 1 / radius; 0 = no reach
    };
    float ambient[3] = {1, 1, 1};
    std::vector<Dir> dirs;   // at most kMaxDir
    std::vector<Pt> pts;     // every point light of the level (pointDiffuse picks the strongest kMaxPt at a position)
    static constexpr size_t kMaxDir = 3;
    static constexpr size_t kMaxPt = 4;

    // Which ambient line a stage gives its fighters: `Amb` is the PC game's (a fighter's colours measured against the original on asg_labs only
    // fit it); `VitaAmb` is the handheld build's brighter one and is kept for comparison.
    enum class Ambient { Amb, VitaAmb };
    // The lights of a level.  A level without the line asked for takes the other one, and one without either 1 (no change).
    static FighterLight fromLevel(const LevelInfo& lv, Ambient which = Ambient::Amb);

    // True when this light does anything to a fighter (not "white ambient and nothing else").
    bool active() const;

    // ambient + the diffuse of the directional lights: what a lit pixel is multiplied by.
    void baseMultiplier(float out[3]) const;
    // The rim of the directional lights for a pixel with the surface direction (nx, ny) (see normalOf).  `mirrored`: the sprite is drawn flipped,
    // so its sideways direction is flipped with it.
    void rim(float nx, float ny, bool mirrored, float out[3]) const;
    // The same for all 256 texel values of the normal plane (index = the texel byte).
    std::array<std::array<float, 3>, 256> rimTable(bool mirrored) const;

    // What the point lights add (diffuse), as a multiplier of the base colour, for a fighter whose centre is at (x, y, z) in stage units.
    void pointDiffuse(float x, float y, float z, float out[3]) const;
    // True when any point light can reach anything at all.
    bool hasPoints() const { return !pts.empty(); }
};

// Bakes the lighting into already coloured pixels: `rgba` is toRgba() of `pl` (w*h*4, straight alpha), `lit` buildLitTable() of the palette.
// Needs pl.normal (nothing is changed without it).  Returns the number of pixels that took light.
size_t applyLighting(std::vector<uint8_t>& rgba, const ArtPlanes& pl, const std::vector<uint8_t>& lit, const FighterLight& light, bool mirrored);

}  // namespace zengine
