// pack_changes.h - "changes.lua": the rules of a resource pack that say WHICH game file each of its files replaces.
//
// A resource pack (mods/resourcepacks/<pack>/) works in one of two ways:
//   * with a changes.lua: only what the rules say changes ("rules" mode).  The pack's other files do nothing by themselves.
//   * without one: every file of the pack replaces the game files that have the same NAME, whatever their folder ("name" mode).
//     .foit / .art / .art-pt are never taken from a pack (that is a mod: mods/modscustom).
// In both modes the game's own files are never written to: the rules only change what a lookup resolves to (Vfs::mountOverrides).
//
// changes.lua is a Lua 5.1 script run once in a sandbox (no io/os/require; only what is below).  Two styles, both accepted:
//   1. the community loader's style (SkullLoader "CCLib"): a ModInfo table and a Mod table whose install() (or a global install() / init())
//      calls   CCLib.gfsAddFile(archive, "path/in/the/game.ext", "file or folder of the pack")   etc.  The archive name is accepted and ignored.
//   2. a plain table, returned or in a global named Changes / changes / Rules:
//        { ["characters/filia_1p.dds"] = "mine/filia.dds",                 -- game path = pack file
//          { game = "UISound/hit.wav", source = "sfx/hit.wav" },           -- (also target/file/path = with/from/replace/src)
//          { "ui/Win/Main.gbs", "ui/Main.gbs" } }
// Paths of the pack are relative to its folder.  The loader's "<work_dir>\\mods\\<name>\\x" form is understood and means "<pack>/x".
//
// CCLib (all that exists; everything else is nil):
//   gfsAddFile / gfsAddFiles(archive, gamePath, packPathOrFolder)   a file, or every file of a folder under gamePath
//   gfsExtractFile / gfsExtractFiles(archive, gamePath, outPath)    copy a game file (or what an earlier rule made of it) to a scratch path
//   gfsCommitChanges()                                              no-op (rules are applied together at the end; true)
//   addLocalization(key, text)                                      adds the key to every <lang>_loc.json (a copy; the originals stay)
//   addNewPermission(name)                                          accepted, nothing to do here (there is no permission file)
//   hasMod(name) / getModVersion(name) / getWorkingDirectory() / constants work_dir, data01_dir, data02_dir, CC_version
//   gbsMerge, read/writeAddress*, getGameBaseAddress               refused (false / nil) with a warning: they would change the runtime
// io.open and os.remove exist but only reach the pack's scratch folder; print() goes to the log; a script that runs too long is stopped.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "vfs.h"

namespace zengine {
namespace packchanges {

struct Rule {
    std::string game;   // the game path the rule changes
    std::string real;   // the real file that answers for it
};

struct Context {
    std::string packDir;    // the pack's folder
    std::string tempDir;    // scratch folder (emptied and created by run())
    std::string outDir;     // where generated files go (edited *_loc.json)
    const Vfs* base = nullptr;   // what the rules change: the game and the layers below
    std::string gameDir;    // CCLib.work_dir
    std::function<double(const std::string&)> modVersion;   // hasMod / getModVersion: the version, 0 = not installed
};

struct Result {
    bool ok = false;
    std::string error;
    std::vector<Rule> rules;
    std::vector<std::string> log, warnings;
    std::string name, version, author, description;   // ModInfo, when the script has one
};

// .foit / .art / .art-pt: mods, not resource packs.
bool isModFile(const std::string& path);
// Runs `changesFile`.  Rules are only returned when the whole script ran without an error (all or nothing).
Result run(const std::string& changesFile, const Context& ctx);

}  // namespace packchanges
}  // namespace zengine
