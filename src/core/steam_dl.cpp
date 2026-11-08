#include "steam_dl.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace zengine {

namespace {

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }
bool contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

uint64_t toU64(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 10); }

// Next whitespace-delimited token starting at pos; advances pos past it.
bool nextToken(const std::string& s, size_t& pos, std::string& tok) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
    size_t b = pos;
    while (pos < s.size() && s[pos] != ' ' && s[pos] != '\t') ++pos;
    if (pos == b) return false;
    tok = s.substr(b, pos - b);
    return true;
}

}  // namespace

std::vector<std::string> buildDepotDownloaderArgs(const DownloadRequest& r, const SteamLogin& login) {
    std::vector<std::string> a;
    a.push_back("-app");
    a.push_back(std::to_string(r.appId));
    if (!r.depots.empty()) {
        a.push_back("-depot");  // DepotDownloader reads every following non-dash argument as an id
        for (uint32_t d : r.depots) a.push_back(std::to_string(d));
    }
    if (!r.branch.empty()) {
        a.push_back("-branch");
        a.push_back(r.branch);
    }
    if (!r.os.empty()) {
        a.push_back("-os");
        a.push_back(r.os);
    }
    if (!r.installDir.empty()) {
        a.push_back("-dir");
        a.push_back(r.installDir);
    }
    if (!r.fileListPath.empty()) {
        a.push_back("-filelist");
        a.push_back(r.fileListPath);
    }
    if (r.maxDownloads > 0) {
        a.push_back("-max-downloads");
        a.push_back(std::to_string(r.maxDownloads));
    }
    if (r.validate) a.push_back("-validate");
    if (r.manifestOnly) a.push_back("-manifest-only");
    // The username is not a secret. With no -password argument DepotDownloader asks on stdin.
    a.push_back("-username");
    a.push_back(login.username);
    if (login.rememberPassword) a.push_back("-remember-password");
    return a;
}

std::string makeFileListText(const std::vector<std::string>& exactPaths, const std::vector<std::string>& regexes) {
    std::string out;
    for (const std::string& p : exactPaths) {
        std::string q = p;
        std::replace(q.begin(), q.end(), '\\', '/');
        out += q;
        out += '\n';
    }
    for (const std::string& rx : regexes) out += "regex:" + rx + "\n";
    return out;
}

Event parseOutputLine(const std::string& rawLine) {
    Event e;
    std::string line = rawLine;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    e.text = line;

    // "{0,6:#00.00}% {1}"  e.g. " 05.20% depots/245171/12345/data01/levels.gfs"
    {
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;
        size_t d0 = i;
        while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
        if (i > d0 && i + 3 < line.size() && line[i] == '.' && std::isdigit(static_cast<unsigned char>(line[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(line[i + 2])) && line[i + 3] == '%') {
            e.type = EventType::Progress;
            e.percent = std::atof(line.substr(d0, i + 3 - d0).c_str());
            size_t f = i + 4;
            while (f < line.size() && line[f] == ' ') ++f;
            e.file = line.substr(f);
            return e;
        }
    }
    // "Got 12 licenses for account!"
    if (startsWith(line, "Got ") && contains(line, " licenses for account")) {
        e.type = EventType::Licenses;
        e.number = static_cast<uint32_t>(std::strtoul(line.c_str() + 4, nullptr, 10));
        return e;
    }
    // "Processing depot 245171"
    if (startsWith(line, "Processing depot ")) {
        e.type = EventType::DepotStart;
        e.number = static_cast<uint32_t>(std::strtoul(line.c_str() + 17, nullptr, 10));
        return e;
    }
    // "Total downloaded: 123 bytes (456 bytes uncompressed) from 2 depots"
    if (startsWith(line, "Total downloaded: ")) {
        e.type = EventType::Finished;
        e.bytes = toU64(line.substr(18));
        return e;
    }
    if (startsWith(line, "Error:") || contains(line, "is not available from this account") ||
        startsWith(line, "Download failed") || contains(line, "was not completely downloaded") ||
        startsWith(line, "Unable to ") || startsWith(line, "No valid depot key") ||
        contains(line, "Encountered 40") || startsWith(line, "Failed to ")) {
        e.type = EventType::Error;
        return e;
    }
    return e;
}

PromptKind classifyPrompt(const std::string& pending) {
    // Only an unterminated tail ending in ": " is a prompt waiting for input.
    if (pending.size() < 2 || pending.compare(pending.size() - 2, 2, ": ") != 0) return PromptKind::None;
    if (contains(pending, "Enter account password for \"")) return PromptKind::Password;
    if (contains(pending, "STEAM GUARD! Please enter your 2-factor auth code")) return PromptKind::GuardAppCode;
    if (contains(pending, "STEAM GUARD! Please enter the auth code sent to the email")) return PromptKind::GuardEmailCode;
    return PromptKind::None;
}

bool isMobileConfirmLine(const std::string& line) { return contains(line, "STEAM GUARD! Use the Steam Mobile App to confirm"); }
bool isWrongCodeLine(const std::string& line) { return contains(line, "2-factor auth code you have provided is incorrect"); }

bool parseManifestListing(const std::string& text, ManifestListing& out) {
    out = ManifestListing{};
    size_t pos = 0;
    bool table = false, sawHeader = false;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;

        if (startsWith(line, "Content Manifest for Depot ")) {
            out.depot = static_cast<uint32_t>(std::strtoul(line.c_str() + 27, nullptr, 10));
            sawHeader = true;
            continue;
        }
        auto after = [&](const char* key, uint64_t& dst) {
            if (!startsWith(line, key)) return false;
            size_t c = line.find(':');
            if (c != std::string::npos) dst = toU64(line.substr(c + 1));
            return true;
        };
        if (after("Total number of files", out.totalFiles) || after("Total bytes on disk", out.totalBytes)) continue;
        if (line.find("Size Chunks File SHA") != std::string::npos) { table = true; continue; }
        if (!table) continue;

        // "{size,14} {chunks,6} {sha1 hex} {flags,5:x} {name}"
        size_t p = 0;
        std::string size, chunks, sha, flags;
        if (!nextToken(line, p, size) || !nextToken(line, p, chunks) || !nextToken(line, p, sha) || !nextToken(line, p, flags))
            continue;
        if (!allDigits(size) || !allDigits(chunks) || sha.size() != 40) continue;
        ++p;  // single separating space; the rest is the file name (may contain spaces)
        if (p > line.size()) continue;
        std::string name = line.substr(p);
        const unsigned long f = std::strtoul(flags.c_str(), nullptr, 16);
        if (f & 0x40) continue;  // EDepotFileFlag.Directory
        out.files.push_back({toU64(size), name});
    }
    return sawHeader && table;
}

std::vector<std::string> selectByExtension(const ManifestListing& m, const std::vector<std::string>& exts) {
    std::vector<std::string> r;
    for (const ManifestFile& f : m.files) {
        const std::string n = lower(f.name);
        for (const std::string& e : exts)
            if (n.size() >= e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0) {
                r.push_back(f.name);
                break;
            }
    }
    return r;
}

uint64_t totalSize(const ManifestListing& m, const std::vector<std::string>& names) {
    std::set<std::string> want(names.begin(), names.end());
    uint64_t t = 0;
    for (const ManifestFile& f : m.files)
        if (want.count(f.name)) t += f.size;
    return t;
}

}  // namespace zengine
