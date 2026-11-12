// vault.h - the player's own copy of the game's Core, sealed inside the app's private storage.
//
// Why: the Android build used to ask for "all files access" to read the Core from wherever the player had put it.  Instead, the
// player picks the Core folder once (the system folder picker, no storage permission needed), the app copies it into its own
// private folder and seals the copy into ONE file, core.vault, with the per-install key (the same key file as the effects pack,
// pafx.key).  From then on the loader reads the Core out of the vault; nothing outside the app's own folders is touched and the
// player's original files are never changed or deleted.
//
// The container is the PAFX one (src/fx/pafx.h: ChaCha20-Poly1305, one sealed table + sealed entries, every entry bound to its
// slot), so a vault built on one install does not open on another and a modified byte is refused instead of read.  The vault is
// made on the device from the player's own files: it is never part of the repository, the public zips or the APK.
//
// Reading: Vfs::mountVault(vault) indexes the entries like a folder and hands out virtual paths ("vault:<id>/<name>"); readFile()
// (vfs.h) understands them, so the code that reads Core files does not know the difference.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pafx.h"

namespace zengine {
namespace vault {

constexpr const char* kVaultFile = "core.vault";
constexpr const char* kImportDirName = "core_import";   // where the Android shell leaves the copy of the picked folder, until it is sealed
constexpr uint64_t kMaxFileBytes = 64ull << 20;           // one file of a Core is a few MB at most
constexpr uint64_t kMaxTotalBytes = 192ull << 20;         // a Core is ~13 MB; anything near this is the wrong folder (the whole game)
constexpr const char* kImportStatusFile = "core_import_status.txt";   // one line, "ok ..." or "error ...", for the setup screen

// The folder that is the Core inside `dir`: `dir` itself or `dir/core` (or `dir/core/core`, how some extractions nest), recognised by
// shaders/ or the stage / music / toc ini files.  "" when `dir` holds no Core.
std::string findCoreRoot(const std::string& dir);

struct BuildReport {
    size_t files = 0;
    uint64_t bytes = 0;
    std::string root;   // the folder the files were read from
};

// Seals every file of the Core inside `dir` into `<storeDir>/core.vault` (through a temporary file) with the per-install key in
// `<storeDir>/pafx.key` (created when missing).  Entry names are the paths relative to the Core root, with '/' separators.  False +
// *err when there is no Core in `dir`, it is implausibly big, a file cannot be read or the store cannot be written.
bool build(const std::string& dir, const std::string& storeDir, BuildReport* rep = nullptr, std::string* err = nullptr);

class Vault {
public:
    // Opens `<storeDir>/core.vault` with `<storeDir>/pafx.key`.  False + *err when there is none, it does not open with the key
    // (made on another install, or damaged) or it is truncated.
    bool open(const std::string& storeDir, std::string* err = nullptr);
    // Same, from bytes already in memory (tests): the vault keeps its own copy.
    bool openBytes(std::vector<uint8_t> blob, const fx::AeadKey& key, std::string* err = nullptr);

    const std::vector<fx::PafxEntryInfo>& entries() const { return ar_.entries(); }
    bool has(const std::string& name) const { return ar_.has(name); }
    // Decrypts and verifies one entry.
    bool read(const std::string& name, std::vector<uint8_t>& out, std::string* err = nullptr) const { return ar_.read(name, out, err); }
    bool readText(const std::string& name, std::string& out, std::string* err = nullptr) const { return ar_.readText(name, out, err); }

private:
    std::vector<uint8_t> blob_;   // the sealed file, as read from disk
    fx::PafxArchive ar_;          // points into blob_
};

// ---- virtual paths: what Vfs hands out for a vault entry
bool isVirtualPath(std::string_view path);
// Registers an opened vault and returns its id for virtual paths.
int registerVault(std::shared_ptr<const Vault> v);
// "vault:<id>/<name>"
std::string virtualPath(int id, const std::string& name);
// Splits a virtual path into its vault and the entry name ("" for the vault root).  False when it is not one or the vault is not registered.
bool resolveVirtual(std::string_view path, std::shared_ptr<const Vault>& v, std::string& name);
// Reads a virtual path.  False when it is not one, the vault is not registered or the entry does not verify.
bool readVirtual(std::string_view path, std::vector<uint8_t>& out);
// Drops every registered vault (tests).
void clearRegistry();

// ---- the import the Android shell leaves behind
struct ImportResult {
    bool attempted = false;   // there was a staging folder
    bool ok = false;          // and it is sealed now
    size_t files = 0;
    uint64_t bytes = 0;
    std::string message;
};
// The Android shell copies the folder the player picked into `importDir` (inside the app's private storage).  When that folder exists,
// it is sealed into the vault under `storeDir` and then DELETED, success or not (a bad one would only fail again on every start).  The
// outcome is also written to `<storeDir>/core_import_status.txt` for the setup screen.  Nothing happens when there is no staging folder.
ImportResult importPending(const std::string& importDir, const std::string& storeDir);

}  // namespace vault
}  // namespace zengine
