// sg_loader core - ties the pieces together: mount the user's folders, inventory what is there,
// load a character's sprite data. Nothing here ships or generates game content; it only reads
// files the user already extracted from their own copy of the game.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "art.h"
#include "errcode.h"
#include "gbs.h"
#include "level.h"
#include "modsdir.h"
#include "palmod.h"
#include "text_formats.h"
#include "vfs.h"

namespace zengine {

struct CharacterAssets {
    std::string name;       // lowercase stem, e.g. "filia", "bossmarie"
    std::string foitPath;
    std::string artPath;    // empty if the .art was not found
    std::string artPtPath;  // empty if there is no .art-pt
    std::vector<std::pair<std::string, std::string>> palettes;  // {"1p", real path of .dds}, sorted by number
};

struct SceneInfo {
    std::string path;
    Gbs gbs;
    std::string error;  // non-empty if parsing failed
};

struct Inventory {
    size_t files = 0;
    size_t luaFiles = 0, iniFiles = 0, stmFiles = 0, seqFiles = 0, glslFiles = 0, ddsFiles = 0, pcxFiles = 0;
    size_t gfsFiles = 0;  // packed archives that still need unpacking (core/gfs.h reads them)
    std::vector<CharacterAssets> characters;
    std::vector<StageEntry> stages;
    std::vector<MusicTrack> music;
    std::vector<std::string> tocFiles;      // from toc.ini
    std::vector<CompileJob> compileJobs;    // from compile.ini (lists every character the game builds)
    std::vector<SceneInfo> scenes;          // every .gbs, parsed
    std::vector<LevelInfo> levels;          // every .lvl stage definition, parsed
    std::vector<std::string> warnings;
};

class GameData {
public:
    // Mounts a folder; returns the number of files indexed.
    size_t mount(const std::string& dir) { return vfs_.mount(dir); }
    // Mounts the sealed copy of the player's Core (vault.h) like a folder; later mounts still override it.
    size_t mountVault(std::shared_ptr<const vault::Vault> v) { return vfs_.mountVault(std::move(v)); }
    const Vfs& vfs() const { return vfs_; }

    // Mounts <gameDir>/SKGLoader/mods/* on top of everything already mounted, so
    // mod files win over the originals.  Returns the number of mod folders applied.
    size_t mountMods(const std::string& gameDir, std::vector<std::string>* log = nullptr);

    // The <gameDir>/mods/ folder (modsdir.h): creates it when missing, mounts resourcepacks / modscustom / stages (and
    // sdvoice_japanese when `japaneseVoices`) over everything mounted so far and bakes mods/pal/*.pal; scan() then lists those
    // palettes after each character's own.  Call after mount() / mountMods().
    modsdir::Report mountModsDir(const std::string& gameDir, bool japaneseVoices);
    const palmod::Library& customPalettes() const { return palLib_; }
    const std::vector<modsdir::StageMod>& customStages() const { return stageMods_; }

    Inventory scan() const;

    // Loads <name>.foit/.art/.art-pt of a character and validates its records.
    bool loadCharacter(const CharacterAssets& c, ArtFile& out, std::string* err = nullptr) const;
    static bool loadPalette(const std::string& ddsPath, Palette& out, std::string* err = nullptr);

private:
    Vfs vfs_;
    palmod::Library palLib_;
    std::vector<modsdir::StageMod> stageMods_;
};

// Which parts of the game the runtime is NOT using, as error codes: 2 = no voices / music (no .wav, no music tracks, or no audio
// output device), 3 = none of the other files (characters, UI scenes, stages, textures) - the "Lite" experience.  Nothing is raised
// for a part that is present.  `audioDeviceOk` = the mixer opened an output device.
void diagnose(const Inventory& inv, const Vfs& vfs, bool audioDeviceOk, err::Registry& reg);

// Lowercase and drop everything that is not a-z0-9 (sg_extract.norm).
std::string normName(const std::string& s);

}  // namespace zengine
