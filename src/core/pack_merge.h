// pack_merge.h - how two resource packs (or a pack and the game) are MIXED instead of one replacing the other.
//
// Until now a pack file replaced the game file of the same name whole.  For text that is wrong more often than right: a pack that only
// translates three lines would wipe the rest of the language file, and two packs that touch the same file could not both be used.  A pack
// can now say, per kind of file, how its file is combined with what is already there (the game's file, or what an earlier pack made of it):
//
//   pack.ini
//     match = path                         files replace only the game file at the path their folder mirrors (see below); default: name
//     ignore = notes/*, *.psd              pack files that are not taken at all
//     name = My pack                       (flat keys, as before: name version author description priority enabled id)
//     requires  = id1, id2                 packs that must be on (otherwise this one is not loaded)
//     conflicts = id3                      packs it cannot share a game with (this one is not loaded while they are on)
//     [merge]
//     loc = keys                           *_loc.json   (the default)
//     ini = replace                        every .ini   (the default)
//     ui/config/MainMenu/challenges.ini = records ChalName
//     *.txt = append
//
//   replace   the pack's file takes the place of the other one                     (what a pack always did)
//   keys      key by key: a key the pack has replaces that key, a new key is added; everything else stays.
//             *.json: flat {"key": "text"} files.  Text: "Key: value" / "Key = value" lines (a key that repeats -- Choice: -- is
//             replaced as a group, in the place of its first line).
//   records   for the game's "Key: value" files that are lists of blank-line separated records (challenges.ini, the option menus):
//             the record whose <Key> line has the same value is replaced, a record that is not there yet is added at the end.
//   append    the pack's lines are added after the other file's.
//
// match = path is the rule for NOT mixing things up: by default an automatic pack's file replaces every game file with the same NAME, so a
// dx1.dds in two stage folders would both become the pack's.  With `match = path` the file's folder counts: stages/b/dx1.dds only touches the
// game's .../stages/b/dx1.dds.  A path that stands for two different game files (a bare "dx1.dds" that exists twice) is skipped with a
// warning instead of guessed.  .foit / .art / .art-pt are taken in this mode (the path names them).
//
// A path pattern is "*suffix" or a path / path tail ("challenges.ini", "MainMenu/challenges.ini"); the words `loc` and `ini` stand for
// "*_loc.json" and "*.ini".  A path pattern beats a word; among path patterns the first in the file wins.  No rule = replace, except
// *_loc.json = keys.  The combined text is written to mods/.cache (never into the game's folder) and answers for the game path.
#pragma once
#include <string>
#include <vector>

namespace zengine {
namespace packmerge {

enum class How { Replace, Keys, Records, Append };

struct Rule {
    std::string pattern;   // lowercase: "loc" / "ini", "*suffix", or a path
    How how = How::Replace;
    std::string idKey;     // Records: the key that names a record (lowercase)
};

struct Config {
    bool matchPath = false;               // match = path: a file only replaces the game file at the path its folder mirrors (never "every file of that name")
    std::vector<std::string> ignore;      // ignore = patterns: pack files that are not taken at all (same patterns as [merge])
    std::vector<Rule> rules;
    std::vector<std::string> requires_;   // lowercase pack ids
    std::vector<std::string> conflicts;
};

// Reads the merge / requires / conflicts part of a pack.ini (anything else in it is not this module's business).  Lines it cannot
// understand are appended to *warnings.
Config parseConfig(const std::string& iniText, std::vector<std::string>* warnings = nullptr);

// true when `ignore` names this pack file (pack-relative path)
bool ignored(const Config& cfg, const std::string& packRel);

// The rule for a game path ("ui/config/MainMenu/challenges.ini"); How::Replace when nothing asks for more.
Rule ruleFor(const Config& cfg, const std::string& path);

const char* howName(How h);

// Combines `over` (the pack's file) into `base` (what is there now) as `r` says.  false = one of them could not be read in that
// form (*err says why); the caller then keeps the plain replacement.
bool merge(const std::string& path, const Rule& r, const std::string& base, const std::string& over, std::string& out, std::string* err = nullptr);

}  // namespace packmerge
}  // namespace zengine
