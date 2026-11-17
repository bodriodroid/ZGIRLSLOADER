// colour_edit.h - the player's own colours for a fighter: a palette seen as a few key colours per body region.
//
// A fighter's palette texture (<name>_Np.dds) is one row of 16 tones per region (row 0 = the outline layer).  Every region's ramp is a
// handful of KEY colours (lightest ... darkest) with the tones interpolated between them; that is also what the community's .pal files
// store (palmod.h).  This file turns a palette into those keys (a small least-squares fit, exact for a palette made that way), lets the
// keys be changed (one key, a whole region keeping its shading, the whole character), and bakes the result back into a palette texture.
//
// What is saved is the player's own favourite: a small text file (no game content in it) that lives in the app's settings folder, not in the
// mods folders, so it never goes through the mod loader and never touches the game's files.  The lit flags of the palette (which colours
// take the stage's light, art.h) are not part of the favourite: a baked palette borrows them from the character's own palette.
// Presentation only; nothing here touches the fight.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "art.h"

namespace zengine {
namespace coledit {

using Rgb = std::array<uint8_t, 3>;

struct Hsv {
    float h = 0;   // degrees 0..360
    float s = 0;   // 0..1
    float v = 0;   // 0..1
};
Hsv toHsv(const Rgb& c);
Rgb fromHsv(const Hsv& c);

// One palette row (a body region): key colours, lightest first / darkest last (the order of a .pal).
struct Region {
    std::vector<Rgb> key;
};

struct Scheme {
    std::string character;           // lower-case id ("filia")
    std::string name;                // what the player called it
    int tones = 16;                  // columns of the palette texture
    std::vector<Region> regions;     // palette rows 1..N
    Rgb outline{0, 0, 0};            // row 0
    std::vector<uint8_t> litAlpha;   // (regions + 1) * tones: the lit flags of the palette this came from (empty = all lit); not saved

    bool valid() const { return !regions.empty() && tones > 0; }
};

// The tones of one region from its keys (same maths as the baked .pal rows): key[0] lightest ... key[n-1] darkest, tone 0 = darkest.
// `out` gets `tones` colours.
void rampOf(const Region& r, int tones, std::vector<Rgb>& out);

// Keys from the palette's own rows.  `counts` = key colours per region when known (a palmod region table whose size matches the palette); a
// region the table's count does not reproduce (another build of the game) and every region without a table get the smallest number of keys
// (1, 2, 3 ... 16) that reproduces the row.  false when the palette is unusable.
bool fromPalette(const Palette& base, const std::vector<int>* counts, Scheme& out);

// The palette texture of a scheme.  The lit flags come from `s.litAlpha` (set by fromPalette).
Palette bake(const Scheme& s);
// The 32-bit ARGB .dds file of a palette (the layout of the game's own <name>_Np.dds).
std::vector<uint8_t> toDds(const Palette& p);

// ---- changes ------------------------------------------------------------------------------------------------------------------------
// The colour a region is "known by": its middle key (what the player sees as its colour).
Rgb mainKey(const Region& r);
// Moves every key by the same hue shift (degrees), saturation factor and value factor: the shading of the region is kept.
void adjust(Region& r, float dHue, float satMul, float valMul);
// Gives the region this colour as its main one, keeping the shading (the other keys follow the same hue / saturation / value change).
void setMain(Region& r, const Rgb& c);
// The same hue shift / factors for every region (and the outline stays).
void adjustAll(Scheme& s, float dHue, float satMul, float valMul);
// A random but tidy recolour: a few hue families, shading kept.  Same seed, same result.
void randomise(Scheme& s, uint32_t seed);

// ---- file ---------------------------------------------------------------------------------------------------------------------------
// "ZGCOL 1" text: character, name, outline, one `r <keys> <hex>...` line per region.  parse() is tolerant of blank lines / comments; it
// does not know the lit flags (the caller sets litAlpha from the character's own palette).
std::string serialize(const Scheme& s);
bool parse(const std::string& text, Scheme& out, std::string* err = nullptr);
// File-name safe version of a name ("Mi color 1" -> "mi_color_1"); never empty.
std::string slug(const std::string& name);

}  // namespace coledit
}  // namespace zengine
