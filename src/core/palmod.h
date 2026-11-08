// palmod.h - custom character palettes (*.pal) dropped into <gameDir>/mods/pal/.
//
// A .pal is the file the community palette editors save (Skullgirls Palette Editor "2nd Encore"):
//
//   char[16]  character name, NUL padded ("Filia", "MsFortune", "BigBand" ...)
//   uint32    numColors  (= stored colours + 1: colour 0 is the implicit "nothing" slot and is not stored)
//   uint8     hue shift increment      uint8  hue shift interval      (animated palettes; kept, not played yet)
//   uint32[numColors-1]  colours, 0xAARRGGBB little endian, grouped by REGION (hair, skin, shirt ...) light -> dark
//   uint32    outline colour (0 = the sprite's own black outline)
//   uint32    super shadow colour 1, uint32 super shadow colour 2
//
// How the game turns that into the 16-tone palette texture <name>_1p.dds (checked texel by texel against Filia's own
// filia_1p.dds: 98.7 % of the channels are identical, the rest differ by 1):
//   * each region owns n of the stored colours (n = 5 for most, 8 for big cloth areas, 1-2 for tiny details); the table of
//     n per region and character is fixed by the game build (the palette editor ships it as 3.7.14.json),
//   * the region's texture row has 16 tones, tone 0 = darkest ... tone 15 = lightest; the n colours sit at tones 15*i/(n-1),
//     the LAST stored colour of the region at tone 0 and the FIRST at tone 15, linear in between (truncated, not rounded),
//   * row 0 of the texture is the outline layer (all zero in the stock files); a non-zero outline colour is written there.
// So a .pal can be baked into a normal palette .dds and everything that already reads <name>_Np.dds (the fights, the
// Character Select sprites, the viewer) uses it unchanged.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "art.h"

namespace zengine {
namespace palmod {

struct PalFile {
    std::string character;           // exactly as stored ("MsFortune")
    uint32_t numColors = 0;          // stored value: colours + 1
    uint8_t hueShiftInc = 0, hueShiftInt = 0;
    std::vector<uint32_t> colors;    // numColors - 1 entries, 0xAARRGGBB
    uint32_t line = 0, shadow1 = 0, shadow2 = 0;
};

// Parses the bytes of a .pal.  false + *err on a short / inconsistent file.
bool parse(const uint8_t* data, size_t size, PalFile& out, std::string* err = nullptr);
// The exact inverse of parse() (what the palette editor writes).
std::vector<uint8_t> serialize(const PalFile& pal);

// Region table of the game build the palettes were made for: key colours per region, by normalised character name
// ("filia", "msfortune" ...).  nullptr when the character is not in the table.
const std::vector<int>* regionCounts(const std::string& normalisedName);

// Lowercase, letters and digits only ("Ms. Fortune" -> "msfortune").
std::string normaliseName(const std::string& s);

// 16 x (1 + regions) palette.  false when the character is unknown or the number of colours does not fit its region table
// (a palette made for another game build).
bool toPalette(const PalFile& pal, Palette& out, std::string* err = nullptr);
// The same as a 32-bit ARGB .dds in the layout of the game's own <name>_1p.dds (alpha of the colours kept).
bool toDds(const PalFile& pal, std::vector<uint8_t>& out, std::string* err = nullptr);

// The characters the palette folder is organised by: one sub-folder per name ("Filia", "MsFortune" ...), created inside mods/pal.
const std::vector<std::string>& rosterFolders();
// Moves every *.pal that lies directly in `palDir` into the sub-folder of the character named in its header (created when
// missing).  Files of an unknown character, unreadable files, and files whose target name already exists stay where they are.
// Returns how many were moved; one line per move / skip goes to *log.
int organise(const std::string& palDir, std::vector<std::string>* log = nullptr);

// One palette found in the mods/pal folder.
struct Entry {
    std::string character;   // normalised: "filia"
    std::string label;       // file name without extension: "FiliaAccuratePreSamson"
    std::string file;        // the .pal
    std::string dds;         // baked palette texture in the cache folder (what the engine loads)
    uint32_t line = 0, shadow1 = 0, shadow2 = 0;
};
struct Library {
    std::vector<Entry> entries;            // sorted by (character, label)
    std::vector<std::string> warnings;     // one line per file that was skipped and why
    std::vector<const Entry*> forCharacter(const std::string& normalisedName) const;
};
// Reads every *.pal under `palDir` (sub-folders too) and bakes each one into `cacheDir` as <character>__<label>.dds.
// Never fails: a missing folder gives an empty library, a bad file a warning.
Library scan(const std::string& palDir, const std::string& cacheDir);

}  // namespace palmod
}  // namespace zengine
