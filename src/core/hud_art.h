// hud_art.h - the HUD's palettized portraits.
//
// The HUD's frames, plates, digits and symbols are the game's own pictures: InGame.gbs lists them with the texture atlas
// (Textures/TA_1001_1|2.dds) and the rectangle each one occupies, and GameUi cuts them out of it.  The big portraits are not in
// the atlas: the game keeps them as palettized layers (`character_<name>_colors|lines|shade.pcx`) and applies the palette of the
// colour the fighter was picked in, so that is what this module does with the user's own files.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "art.h"

namespace zengine {
namespace hudart {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // w*h*4, straight alpha
};

struct Pcx {
    int w = 0, h = 0;
    std::vector<uint8_t> idx;   // w*h palette indices
};
// 8-bit RLE .pcx (the only kind the portraits use).  The 256-colour palette at the end is not needed: the files store indices.
bool decodePcx(const uint8_t* data, size_t size, Pcx& out, std::string* err = nullptr);
// colors = palette row (x5), shade = tone, lines = outline strength (255 = none); background stays transparent.
bool composePortrait(const Pcx& colors, const Pcx& lines, const Pcx& shade, const Palette& pal, Image& out);

// The name a fighter's plate carries when the plate picture is missing: "bossmarie" -> "BLOODY MARIE", "msfortune" -> "MS. FORTUNE".
std::string displayName(const std::string& key);

}  // namespace hudart
}  // namespace zengine
