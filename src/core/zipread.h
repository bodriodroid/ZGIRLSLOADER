// zipread.h - reads .zip files (stored + deflate), so a mod or resource pack can be shipped as one file.
//
// Own implementation of RFC 1951 (inflate) and the zip container's central directory; no ZIP64, no encryption, no other methods
// (those entries are reported, never guessed).  Extraction refuses paths that would leave the output folder (gfs::safePath), checks every
// entry's size and CRC-32, and puts hard limits on what a file may expand to.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {
namespace zipread {

// Raw deflate stream -> bytes.  `maxOut` = the most it may produce (a zip bomb stops there).
bool inflate(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOut, std::string* err = nullptr);

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0);

struct Entry {
    std::string name;            // as stored
    uint16_t method = 0;         // 0 stored, 8 deflate
    uint16_t flags = 0;
    uint32_t crc = 0;
    uint64_t csize = 0, usize = 0;
    uint64_t localOffset = 0;
    bool isDir() const { return !name.empty() && name.back() == '/'; }
};

bool list(const std::string& zipFile, std::vector<Entry>& out, std::string* err = nullptr);
bool read(const std::string& zipFile, const Entry& e, std::vector<uint8_t>& out, std::string* err = nullptr);

struct ExtractStats {
    size_t files = 0;
    uint64_t bytes = 0;
    std::string strippedFolder;          // the single top folder every entry lived in, taken off ("" = none)
    std::vector<std::string> problems;   // one line per refused / unreadable entry
};
// Writes everything under `outDir`.  `stripWrapper`: when every file sits in one top-level folder ("mymod-master/..."), that folder is
// left out.  "__MACOSX/" entries are skipped.  false + *err = the zip itself cannot be read.
bool extract(const std::string& zipFile, const std::string& outDir, ExtractStats& st, bool stripWrapper = true, std::string* err = nullptr);

}  // namespace zipread
}  // namespace zengine
