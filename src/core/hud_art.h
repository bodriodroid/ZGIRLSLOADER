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
// Which palette row each value of the colours layer stands for: rows[v], v = 0..255 (0 = the outline layer).  A value is not a row number: it
// is the position of the region's first key colour in the character's flat list of key colours (the list the palette ramps are made from),
// so a value belongs to the last row whose first key sits at or before it.  The size of each row's key list (`tableCounts`: the regions'
// key counts of the game build, palmod.h, may be nullptr) is checked against the palette itself and against the values the layer really
// uses; where a table is missing or wrong (a different build, or a layer made for another one) the counts that make the used values land
// exactly on row starts win.  `pal` is the palette of any colour of the character: only its rows' shapes are looked at.
std::vector<uint8_t> portraitRows(const Pcx& colors, const Palette& pal, const std::vector<int>* tableCounts = nullptr);
// colors = region (see portraitRows), shade = tone, lines = outline strength (255 = none); background stays transparent.  The tone and
// the outline follow the game's own palettized shader: the tone is looked up between two palette tones (the top one is never reached),
// the outline mixes the outline colour in by sixteenths and is also what the transparent outline layer (value 0) is drawn with.
// `shape` = the character's own palette when `pal` is a different colour of it (a mod's, one the player edited): the rows are told from the
// stock palette's ramps, which is what the key counts were made for.  Ignored when it does not have the same number of rows as `pal`.
bool composePortrait(const Pcx& colors, const Pcx& lines, const Pcx& shade, const Palette& pal, Image& out, const std::vector<int>* tableCounts = nullptr,
                     const Palette* shape = nullptr);

// The name a fighter's plate carries when the plate picture is missing: "bossmarie" -> "BLOODY MARIE", "msfortune" -> "MS. FORTUNE".
std::string displayName(const std::string& key);

}  // namespace hudart
}  // namespace zengine
