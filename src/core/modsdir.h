// modsdir.h - the <gameDir>/mods/ folder: created automatically on first run, five sub-folders:
//
//   mods/
//     mods.ini            optional:  enabled = 0   (kills every mod below)     disabled = id1, id2
//                                    japanese_voices = auto | 1 | 0            (see sdvoice_japanese)
//     pal/                custom palettes: *.pal files from the community palette editor (palmod.h).  Each one shows up as an
//                         extra colour of its character after the game's own 1P..30P.  Sub-folders are fine.
//     modscustom/         mods that change the game: one folder per mod, files mirror the game's layout (mod.ini optional:
//                         name / version / author / priority / enabled).  Mounted over the game and over the resource packs.
//     stages/             custom stages: one folder per stage with its .lvl + textures (+ stage_<id>.png/.dds thumbnail).
//                         The folder is mounted, so the .lvl is found like any of the game's own and shows up in the stage list.
//     sdvoice_japanese/   Japanese voice files, same layout as the game's sound folders.  Only mounted while the voice
//                         language is Japanese (language jp, or japanese_voices = 1 in mods.ini).
//     resourcepacks/      texture / sound / text replacement packs: one folder per pack (pack.ini optional, same keys as
//                         mod.ini).  Mounted over the game but UNDER modscustom, so a pack never beats a gameplay mod and the
//                         original files are never touched.
//     .cache/             generated files (baked palette textures).  Safe to delete.
//
// The base game folder is never written to except for creating `mods/` itself.  Mount order, later wins:
//   game  <  resourcepacks  <  modscustom  <  stages  <  sdvoice_japanese
//
// Not supported yet: packs / mods as .zip files (unzip them into the folder).
#pragma once
#include <string>
#include <vector>

#include "modcheck.h"
#include "palmod.h"
#include "vfs.h"

namespace zengine {
namespace modsdir {

struct Layout {
    std::string root, pal, custom, stages, jpVoice, packs, cache;
};
Layout layout(const std::string& gameDir);

// Creates mods/ and its five folders (and a short README.txt in each that does not have one).  false = the game folder is
// read-only (nothing else is affected: the loaders just find empty folders).
bool ensure(const std::string& gameDir, std::vector<std::string>* log = nullptr);

struct StageMod {
    std::string id;     // folder name; the thumbnail is stage_<id>.png / .dds
    std::string name;   // stage.ini "name = ..." (else mod.ini / the folder name)
    std::string dir;
};

// One folder of resourcepacks/, modscustom/ or stages/ as the Mods screen lists it (on or off).
struct Item {
    std::string kind;          // "pack" | "mod" | "stage"
    std::string id;
    std::string name;
    std::string version, author, description;
    std::string dir;
    int priority = 100;
    size_t files = 0;
    bool enabled = true;       // false: mod.ini enabled = 0, or listed in mods.ini disabled =
    bool mounted = false;      // really mounted this run (enabled, not blocked by mods.ini enabled = 0, has files)
    bool blocked = false;      // checked and refused: damaged files (error code 4) or native code / links out (error code 5)
    std::string problem;       // the first thing that was wrong
};

struct Report {
    bool layoutOk = false;
    bool allEnabled = true;    // false: mods.ini has enabled = 0
    std::string root;          // <gameDir>/mods
    std::vector<Item> items;   // packs, then mods, then stages, each in folder order
    size_t packs = 0, mods = 0, stages = 0, voiceFiles = 0;
    palmod::Library pal;
    std::vector<StageMod> stageMods;   // the folders of mods/stages that were mounted, in load order
    std::vector<std::string> log;       // one line per mounted pack / mod / stage
    std::vector<std::string> warnings;
    std::vector<modcheck::Conflict> conflicts;   // files two or more mounted folders both hold: who wins (later mount) and who loses
};

// Switches one folder on or off for good: edits mods.ini (disabled = ...) and the folder's own mod.ini / pack.ini, never the game's
// files.  Takes effect at the next mountAll().  false = the files could not be written.
bool setItemEnabled(const std::string& gameDir, const Item& item, bool enabled);

// ensure() + the mounts in the order above + the palette scan.  `japaneseVoices`: the voice language is Japanese (mods.ini can
// force it on / off).
Report mountAll(Vfs& vfs, const std::string& gameDir, bool japaneseVoices);

}  // namespace modsdir
}  // namespace zengine
