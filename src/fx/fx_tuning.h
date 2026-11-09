// fx_tuning.h - the numbers the effect programs read (flash colour tables, contrast / brightness, the screen-effect tints ...).
//
// The programs themselves (fx_glsl.cpp) are written for this engine and contain no tuning number.  The numbers come from the
// player's own Core: at pack time (tools/zdroid_pafx.cpp) extractTuning() reads them out of the Core shader sources the player
// has on disk, and they travel inside the sealed .pafx (as fx/tuning.ini).  Without a pack the engine uses neutral()
// - plain greyscale ramps and mild tints of our own - so a build without the player's Core still runs, it just does not look
// like the game.
#pragma once
#include <string>
#include <vector>

namespace zengine {
namespace fx {

// One "super flash" look: the screen is remapped between a dark and a light colour by luminance.
struct FlashLook {
    float low[3] = {0, 0, 0};
    float high[3] = {1, 1, 1};
};

// A family of flash looks plus the shaping curve (the same maths is used for sprites, stages and UI; the families differ in numbers).
struct FlashDomain {
    FlashLook look[4];
    float lumaCoeff[3] = {0.299f, 0.587f, 0.114f};  // luminance weights of the flash remap
    float contrast = 1.0f;
    float brightness = 1.0f;
    float saturation = 0.0f;
};

struct FxTuning {
    FlashDomain sprite;                         // fighters and stages
    FlashDomain ui;                             // Otter UI cards
    float shadowTint[3] = {0.5f, 0.5f, 0.5f};   // colour multiplier of the "background in a super" darkening
    float shadowLuma[3] = {0.299f, 0.587f, 0.114f};
    float deathStroke[3] = {0.9f, 0.9f, 0.6f};  // portrait "death" look: outline colour and fill colour
    float deathFill[3] = {0.25f, 0.05f, 0.05f};
    float deathStrokeShift = -0.01f;            // horizontal offset (UV) of the second sample that widens the stroke
    float deathBg[5][3] = {{0.9f, 0.9f, 0.9f}, {0.8f, 0.8f, 0.5f}, {0.6f, 0.3f, 0.15f}, {0.2f, 0.07f, 0.05f}, {0.2f, 0.07f, 0.05f}};
    float deathBgStop[5] = {0.0f, 0.5f, 1.0f, 1.5f, 2.5f};   // distance from the centre where each colour is reached
    float deathBgAspect = 16.0f / 9.0f;
    bool fromCore = false;  // true when the numbers were read from a Core
};

// Neutral numbers (our own); fromCore = false.
FxTuning neutralTuning();

// Reads what it can out of the Core shader sources (the .fx files; any may be empty).  Returns how many groups were found
// (0..N); `notes` lists what was missing.  Missing groups keep the neutral values.
int extractTuning(const std::string& tiledSpriteFp, const std::string& otterVp, const std::string& otterFp, FxTuning& out,
                  std::vector<std::string>* notes = nullptr);

// fx/tuning.ini form: one "key=a,b,c" line per group.
std::string tuningToIni(const FxTuning& t);
bool tuningFromIni(const std::string& ini, FxTuning& out);

}  // namespace fx
}  // namespace zengine
