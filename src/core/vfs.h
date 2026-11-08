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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zengine {

// Lowercases ASCII, turns '\\' into '/', drops leading "./" and '/', collapses "//".
std::string normalizeKey(std::string_view path);

// Reads a whole file. Returns false on any I/O error.
bool readFile(const std::string& path, std::vector<uint8_t>& out);

class Vfs {
public:
    // Adds a directory tree. Later mounts override earlier ones for identical suffix keys.
    // Directories named extract/build/analysis/analysis_v2/SKGSTEAM/mods are skipped
    // (they are output folders of the extraction tools). Returns files indexed (0 = missing dir).
    size_t mount(const std::string& dir);

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
