// palgen.h - generates themed character palettes (*.pal) from a character's own palette texture <name>_1p.dds.
//
// The theme is the colours of the Claude chat: black ink, a normal grey (a little lighter than the ink) for the body and the clothes,
// white for the very light parts, and the Claude orange for the eyes.  The generator looks at every REGION of the palette (hair,
// skin, shirt ...) and decides what it is from the 16 tones of its row:
//   * black   - the region was already (nearly) black: it stays black (and so does every tie, see pal_theme_hints.inc),
//   * white   - it was (nearly) white: it becomes white,
//   * skin    - a skin tone: a lighter grey,
//   * cloth   - everything else (hair, clothes, props): the normal grey,
//   * eye     - the iris regions listed in pal_theme_hints.inc: orange.
// Whatever the role, the luminance of every tone goes through the ramp of the role, so the shading of the original art is kept (light
// stays light, dark stays dark, regions keep their contrast against each other).  Then it samples the n key colours the region owns
// in the .pal (palmod.h) and writes the file.
//
// The input is the player's own palette texture, so what comes out is derived from game data: it is made on the player's machine
// and is not part of the repository.  Only this generator (and the small table of which regions are eyes and ties) is.
#pragma once
#include <string>
#include <vector>

#include "art.h"
#include "palmod.h"

namespace zengine {
namespace palgen {

enum class Theme {
    Claude,      // normal greys on white / ink, orange eyes
    ClaudeDark,  // the same colours one step darker (greys towards ink, whites towards light grey)
};
// The file name (without extension) a theme is saved under: "Claude", "ClaudeDark".
const char* themeLabel(Theme t);

enum class Role { Black, Cloth, Skin, White, Eye };
constexpr int kRoleCount = 5;
const char* roleName(Role r);

// What a region is, judged from its 16 tones only (16 * 3 bytes RGB, tone 0 = darkest): never Eye (that comes from the hints table).
Role classify(const uint8_t* rgb16);

// Palette rows (1 = the first region; row 0 is the outline layer) of the regions that are eyes / ties for a character, by normalised name
// ("filia").  Both empty when the table knows nothing about the character.
struct Hints {
    std::vector<int> eyes, ties;
};
Hints hintsFor(const std::string& normalisedName);

// True when every ramp gets lighter from stop to stop and each role's output range is ordered (the maths needs it).  Used by the tests.
bool rampsValid();

struct Report {
    int regions = 0;
    int perRole[kRoleCount] = {};
    int ties = 0;   // regions turned black because the hints table says they are ties
    // What the .pal gives back when the game bakes it (palmod::toPalette) against the recoloured 16 tones the generator aimed at:
    // the n keys cannot follow a ramp with more bends exactly, so this is how far it is off, in channel steps (0..255).
    double maxErr = 0, meanErr = 0;
};

// Makes the themed .pal of `character` ("MsFortune": the name exactly as the .pal header and the region table spell it) from its
// default palette `src` (16 tones x (1 + regions) bands, row 0 = outline layer, as readDds gives it).  False + *err when the
// character is unknown or the texture does not have the rows the region table says (a game build the table is not made for).
bool generate(const std::string& character, const Palette& src, Theme theme, palmod::PalFile& out, Report* rep = nullptr,
              std::string* err = nullptr);

}  // namespace palgen
}  // namespace zengine
