// sg_loader core - Steam download support via the open-source DepotDownloader (SteamRE/DepotDownloader).
//
// This file is the portable, testable half: building the command line, writing the file list, and
// understanding DepotDownloader's output. All formats below were read from DepotDownloader's source
// (Program.cs, ConsoleAuthenticator.cs, ContentDownloader.cs, Util.cs), not guessed.
//
// What this does and does not do:
//  * It logs in with the USER'S OWN Steam account. Steam itself decides whether the account owns the app
//    (DepotDownloader prints "Depot N is not available from this account." otherwise). Nothing here
//    bypasses licensing, shares accounts, or uses anonymous/leaked manifests.
//  * The password and Steam Guard codes are only ever written to the child's stdin. They are never put
//    on the command line (visible in `ps`) and are redacted from every log line.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zengine {

constexpr uint32_t kSkullgirlsAppId = 245170;  // Skullgirls 2nd Encore (store.steampowered.com/app/245170)

struct SteamLogin {
    std::string username;
    bool rememberPassword = true;  // -remember-password: later runs reuse the stored login token
};

struct DownloadRequest {
    uint32_t appId = kSkullgirlsAppId;
    std::string installDir;           // -dir
    std::string os = "windows";       // -os (the data the loader reads is the Windows build's)
    std::string branch;               // -branch, empty = public
    std::vector<uint32_t> depots;     // -depot, empty = every depot the account can access
    std::string fileListPath;         // -filelist, empty = whole depot
    int maxDownloads = 8;             // -max-downloads
    bool validate = false;            // -validate
    bool manifestOnly = false;        // -manifest-only: write manifest_<depot>_<id>.txt, download nothing
};

// Arguments for DepotDownloader (without argv[0]). Never contains a password or code.
std::vector<std::string> buildDepotDownloaderArgs(const DownloadRequest& req, const SteamLogin& login);

// One path per line (exact match; '/' separators), or "regex:<pattern>" lines (case-insensitive).
std::string makeFileListText(const std::vector<std::string>& exactPaths, const std::vector<std::string>& regexes = {});

// ---- output understanding -----------------------------------------------------------------------
enum class EventType { Log, Progress, Licenses, DepotStart, Finished, Error };

struct Event {
    EventType type = EventType::Log;
    std::string text;      // the raw (redacted) line
    double percent = -1;   // Progress: 0..100 of the whole download
    std::string file;      // Progress: file being written
    uint32_t number = 0;   // Licenses: count | DepotStart: depot id
    uint64_t bytes = 0;    // Finished: total downloaded bytes
};

// Classifies one complete output line.
Event parseOutputLine(const std::string& line);

enum class PromptKind { None, Password, GuardAppCode, GuardEmailCode };
// Prompts are printed WITHOUT a trailing newline, so this must be called on the unterminated tail.
PromptKind classifyPrompt(const std::string& pendingText);
// Informational Steam Guard lines that need no answer ("Use the Steam Mobile App to confirm...", "...incorrect").
bool isMobileConfirmLine(const std::string& line);
bool isWrongCodeLine(const std::string& line);

// ---- manifest listing (-manifest-only output) ----------------------------------------------------
struct ManifestFile {
    uint64_t size = 0;
    std::string name;  // as written by DepotDownloader (platform separators)
};
struct ManifestListing {
    uint32_t depot = 0;
    uint64_t totalFiles = 0;
    uint64_t totalBytes = 0;
    std::vector<ManifestFile> files;  // directories excluded
};
bool parseManifestListing(const std::string& text, ManifestListing& out);

// Convenience filter: files whose lowercase name ends with any of the extensions (e.g. {".gfs"}).
std::vector<std::string> selectByExtension(const ManifestListing& m, const std::vector<std::string>& extsLower);
uint64_t totalSize(const ManifestListing& m, const std::vector<std::string>& names);

}  // namespace zengine
