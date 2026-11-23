// modloader.h - SKGLoader mod support for ZDroid-Engine.
//
// Layout inside the user's game folder:
//
//   <gameDir>/SKGLoader/
//     mods.ini                 global switch (enabled=0 kills every mod)
//     mods/
//       <mod-id>/
//         mod.ini              id / name / version / author / priority / enabled
//         ...everything else is a game-relative override...
//
// Example: a file at
//   <gameDir>/SKGLoader/mods/hd-filia/data/characters/filia_1p.dds
// wins over the original
//   <gameDir>/data/characters/filia_1p.dds
//
// The Vfs indexes every file by *path suffix*, case-insensitively, so the mirror
// folder a mod happens to use does not matter: "data/characters/x.dds",
// "characters/x.dds" and "x.dds" all override the same game file.  Mount order
// decides the winner, and later mounts win, so the game is mounted first and the
// mods are mounted afterwards in priority order (higher priority = applied later
// = overrides lower priority).
//
// No JSON dependency on purpose: mod.ini is a flat key = value file so it can be
// edited on a phone with any text editor.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vfs.h"

namespace zengine {
namespace mods {

struct ModInfo {
    std::string id;
    std::string name;
    std::string version = "0.1";
    std::string author;
    std::string description;
    int priority = 100;      // higher wins
    bool enabled = true;
    std::string dir;         // absolute path of the mod folder
    size_t fileCount = 0;
    std::vector<std::string> overrides;  // game-relative paths this mod replaces
    bool blocked = false;    // vet() refused it: damaged files (error code 4) or code that would alter the runtime (error code 5)
    std::string problem;     // the first thing vet() found
};

struct ModSet {
    std::string root;        // <gameDir>/SKGLoader
    bool globalEnabled = true;
    std::vector<ModInfo> mods;
    std::vector<std::string> warnings;
};

// "<gameDir>/SKGLoader"
std::string rootDir(const std::string& gameDir);

// Reads mods.ini + every mods/*/mod.ini.  Never fails: an absent SKGLoader folder
// simply yields an empty set.
ModSet scan(const std::string& gameDir);

// Same scan for any folder whose sub-folders are mods / resource packs (each may carry a mod.ini or pack.ini;
// `globalIni` is the optional "enabled = 0" / "disabled = id1, id2" switch file).  Used by <gameDir>/mods/ (modsdir.h).
ModSet scanFolder(const std::string& modsFolder, const std::string& globalIni);

// Looks the folder over (modcheck.h) before it is mounted.  true = fine.  false = `m.blocked` and `m.problem` are set, the folder must
// not be mounted, the problem is appended to `log` and error code 4 / 5 is raised in err::global().  `kind` is "mod", "pack" or
// "stage" and only goes into the messages.
bool vet(ModInfo& m, const char* kind, std::vector<std::string>* log = nullptr, const std::vector<std::string>& ignoreExt = {});

// Enabled mods, sorted by (priority asc, id asc).  Later entries override earlier.
std::vector<ModInfo> loadOrder(const ModSet& set);

// Mount the game dir and then every enabled mod.  Returns the number of mod folders
// actually mounted.  When log is non-null each mount is appended as "mod <id>: N files".
size_t apply(Vfs& vfs, const std::string& gameDir, const ModSet& set,
             std::vector<std::string>* log = nullptr);

// One-shot convenience used by the viewer / Android activity.
size_t apply(Vfs& vfs, const std::string& gameDir, std::vector<std::string>* log = nullptr);

// Which real file does the game path resolve to, and which mod (if any) supplied it?
struct Resolution {
    std::string realPath;
    std::string modId;  // empty => the base game
    bool found = false;
};
Resolution resolve(const std::string& gameDir, const std::string& gamePath);

// Flip mod.ini's enabled flag.  Creates the mod folder's ini if it is missing.
// `name` only seeds the display name when the ini does not have one yet.
bool setEnabled(const std::string& gameDir, const std::string& id, bool enabled,
                const std::string& name = std::string());

// Create a skeleton mod with a mod.ini and a README the user can edit on the phone.
bool createSkeleton(const std::string& gameDir, const std::string& id,
                    const std::string& name = std::string());

}  // namespace mods
}  // namespace zengine
