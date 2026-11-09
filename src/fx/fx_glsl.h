// fx_glsl.h - the effect programs, GLSL ES 1.00 (runs on every GLES2+ phone; desktop Mesa accepts it as is).
//
// Written for this engine from the behaviour of the game's effects (docs/RE_NOTES.md, "Render effects"); the sources contain no
// tuning number - the numbers are uniforms fed from FxTuning (fx_tuning.h) so they can come from the player's own Core through
// a .pafx.  fx_math.h has the same maths in C++, and tools/fx_gles_check.py renders every program here and compares.
//
// Common vertex format for every program: attribute vec3 aPos (x, y in pixels, z = height above the floor), vec2 aUV, vec4 aColor;
// uniform mat4 uMvp.  Common screen-effect uniforms (all programs that colour pixels): vec2 uScreen (shadowT, flashT),
// vec3 uFlashLow / uFlashHigh / uFlashLuma, vec3 uFlashCurve (contrast, brightness, saturation), vec3 uShadowTint / uShadowLuma.
#pragma once
#include <string>
#include <vector>

namespace zengine {
namespace fx {

struct FxProgramSource {
    std::string name;      // "sprite_palette" ...
    std::string vertex;
    std::string fragment;
};

// Every program, in a fixed order.
const std::vector<FxProgramSource>& fxProgramSources();
const FxProgramSource* findFxProgram(const std::string& name);

}  // namespace fx
}  // namespace zengine
