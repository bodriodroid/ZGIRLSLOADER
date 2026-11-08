// modcheck.h - looks at a mod / resource pack / stage folder BEFORE it is mounted (C++17, no platform dependencies).
//
//   damaged  (error code 4)  a file that is empty, cut short or does not parse as what its extension says: .dds .wav .png .foit
//                            .gbs .pal .ini.  The folder is not mounted, so one bad download cannot half-load.
//   altered  (error code 5)  something that would change the runtime's own code instead of its data: native code (.so .dll .exe ...)
//                            or scripts for a shell, or a link that leaves the mod folder.  The folder is not mounted.
//
// Only headers and structure are checked (no pixel decoding, no audio decoding), so a big pack is scanned in a moment.
// Whole-file replacement is the rule of the Vfs (the later mount wins, nothing is merged); conflicts() explains which mod wins which
// file, which is what the Mods screen and `zdroid_mods conflicts` show.
//
// This is the project's own implementation; it follows the behaviour described in the Skullgirls Mod Manager's user guide (load
// order, later mod wins, whole-file replacement, check before applying) and shares no code with it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "errcode.h"

namespace zengine {
namespace modcheck {

enum class Severity { Note, Damaged, Altered };

struct Issue {
    Severity sev = Severity::Note;
    std::string file;   // relative to the mod folder
    std::string what;
};

struct Result {
    std::vector<Issue> issues;
    size_t files = 0;
    uint64_t bytes = 0;
    bool scanCut = false;       // more files than the scan limit: the rest was not looked at

    bool damaged() const;
    bool altered() const;
    bool blocked() const { return damaged() || altered(); }
    // Worst problem first ("file: what"), empty when the folder is fine.
    std::string firstProblem() const;
};

// Scans `dir` recursively.  A folder that does not exist gives an empty, clean result.
Result check(const std::string& dir, size_t maxFiles = 50000);

// Checks one file's bytes against its extension (lowercase, with dot).  Returns true when it is fine; otherwise *what says why.
// Exposed for the tests and for tools that already have the bytes.
bool checkBytes(const std::string& ext, const uint8_t* data, size_t size, std::string* what);

// Files with an extension that is native code or a shell script.
bool isCodeExtension(const std::string& ext);

// Raises code 4 / 5 in `reg` for a blocked folder (`label` = what the player sees, e.g. "mod my-hd-pack"); notes are ignored.
void report(const std::string& label, const Result& r, err::Registry& reg);

struct Layer {
    std::string id;
    std::string dir;
};
struct Conflict {
    std::string path;                    // the most specific path among the files involved (relative to its mod)
    std::string winner;                  // id of the layer whose file the game uses
    std::vector<std::string> overridden; // ids of the layers that lose, in load order
};
// `layers` in load order (the last one wins).  Two files collide when one relative path is the other's suffix on a folder
// boundary ("data/characters/x.dds" and "x.dds"), the same rule the Vfs resolves by.
std::vector<Conflict> conflicts(const std::vector<Layer>& layers, size_t maxConflicts = 5000);

}  // namespace modcheck
}  // namespace zengine
