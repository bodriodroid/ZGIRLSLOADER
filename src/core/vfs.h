// sg_loader core - virtual file system over the user's own extracted game folders.
// Language: C++17. No platform dependencies beyond <filesystem>.
//
// The game's data is Windows-cased and the extracted folders are nested differently
// depending on how they were unpacked (core/core/..., sg_dump/unpacked/sprites/sprites/...).
// The Vfs therefore indexes every file under ALL of its path suffixes, case-insensitively,
// so that "arcade/story/Filia.stm", "Filia.stm" and "ARCADE\STORY\FILIA.STM" all resolve
// no matter where the folder was mounted.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zengine {

namespace vault {
class Vault;
}

// Lowercases ASCII, turns '\\' into '/', drops leading "./" and '/', collapses "//".
std::string normalizeKey(std::string_view path);

// Reads a whole file. Returns false on any I/O error.  Also understands the virtual paths of a mounted Core vault ("vault:<id>/<name>",
// see vault.h), so callers never need to know whether a file is a real one or sealed inside the app's private storage.
bool readFile(const std::string& path, std::vector<uint8_t>& out);

class Vfs {
public:
    // Adds a directory tree. Later mounts override earlier ones for identical suffix keys.
    // Directories named extract/build/analysis/analysis_v2/SKGSTEAM/mods are skipped
    // (they are output folders of the extraction tools). Returns files indexed (0 = missing dir).
    size_t mount(const std::string& dir);
    // Adds the entries of a sealed Core vault, indexed exactly like a mounted folder (same suffix rules, same override order).  The
    // vault is registered (vault::registerVault) and its files get virtual paths.  Returns the number of entries (0 = empty).
    size_t mountVault(std::shared_ptr<const vault::Vault> v);

    // Resource packs (the game's files are never written to; these only change what a lookup resolves to).
    // mountByName: like mount(), and in addition every file of `dir` replaces EVERY already indexed file that has the same name, whatever
    // its folder ("filia_1p.dds" answers to "characters/filia_1p.dds" and to "data/characters/filia_1p.dds").  Files left out and listed in *skipped:
    // those whose name ends in an entry of `skipExt` that starts with a dot (".foit"), or whose own name is an entry that does not
    // ("pack.ini"), case-insensitive.  Returns the files taken.
    size_t mountByName(const std::string& dir, const std::vector<std::string>& skipExt, std::vector<std::string>* skipped = nullptr);
    // mountOverrides: `game` path -> real file.  Every indexed path that is `game` or ends with "/" + `game` now resolves to the real file.
    // A later rule for the same path wins.  Returns the rules applied.
    struct Override {
        std::string game, real;
    };
    size_t mountOverrides(const std::vector<Override>& rules);

    bool resolve(std::string_view path, std::string* realPath = nullptr) const;
    bool exists(std::string_view path) const { return resolve(path, nullptr); }
    bool read(std::string_view path, std::vector<uint8_t>& out) const;
    bool readText(std::string_view path, std::string& out) const;

    // Real paths of every file whose extension equals ext (lowercase, with dot, e.g. ".gbs").
    std::vector<std::string> listByExt(std::string_view ext) const;
    // Real paths of every file whose basename equals name (case-insensitive).
    std::vector<std::string> findByName(std::string_view name) const;
    // Best candidate for a basename: paths containing "characters" first, then shorter paths.
    bool pick(std::string_view name, std::string* realPath) const;

    size_t fileCount() const { return files_.size(); }
    const std::vector<std::string>& files() const { return files_; }
    const std::vector<std::string>& mounts() const { return mounts_; }

private:
    void addFile(const std::string& real, const std::string& rel, int order);
    struct Entry {
        std::string real;
        int mountOrder = 0;
        size_t depth = 0;
    };
    std::unordered_map<std::string, Entry> bySuffix_;
    std::unordered_map<std::string, std::vector<std::string>> byName_;
    std::vector<std::string> files_;
    std::vector<std::string> mounts_;
};

}  // namespace zengine
