// gfs.h - the game's package container ("Reverge Package File", *.gfs): a table of files followed by their bytes.
//
// Layout (all numbers big endian; written from the format as read in the community's SkullMod tools, own implementation):
//   u32   data offset            where the file bytes start (= the end of the table)
//   u64   20, "Reverge Package File"
//   u64   3,  "1.1"
//   u64   file count             (at 0x2B; the table starts at 0x33)
//   per file:  u64 path length, path bytes, u64 file size, u32 (a flag word, 1 in the files seen; not needed to read)
//   then the bytes of every file one after the other, in table order, from `data offset`.
//
// This reads only: it never writes into an archive.  Extraction refuses paths that would leave the output folder.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace zengine {
namespace gfs {

struct Entry {
    std::string path;       // as stored (forward slashes after safePath())
    uint64_t offset = 0;    // absolute position of the bytes inside the archive
    uint64_t size = 0;
};

struct Index {
    uint32_t dataOffset = 0;
    std::string version;
    uint64_t fileSize = 0;      // size of the archive file the table was checked against
    std::vector<Entry> entries;
};

// Parses the header and table from the first `headLen` bytes of the archive (at least `dataOffset` of them).  `fileSize` = the whole archive's
// size: every entry must fit in it ("archive is incomplete" otherwise, e.g. a download that is not finished).
bool parseIndex(const uint8_t* head, size_t headLen, uint64_t fileSize, Index& out, std::string* err = nullptr);
// Reads only the head of the file.
bool readIndex(const std::string& file, Index& out, std::string* err = nullptr);
// The bytes of one entry.
bool readEntry(const std::string& file, const Entry& e, std::vector<uint8_t>& out);

// A stored path made safe to join under an output folder: backslashes to '/', "." and empty parts dropped.  Empty result = refused (".." part,
// a drive ("C:"), a NUL byte, nothing left).
std::string safePath(std::string_view stored);

struct ExtractStats {
    size_t files = 0;       // written
    size_t skipped = 0;     // already there with the same size (overwrite = false)
    uint64_t bytes = 0;
    std::vector<std::string> problems;   // one line per refused path / failed write
};
// Called after every file with (done, total); return false to stop (the files written so far stay).
using Progress = std::function<bool(size_t done, size_t total)>;
// Writes every file under `outDir` (created).  false + *err when the archive cannot be read; unsafe paths and write failures are listed in
// `st.problems` and the rest is still extracted.
bool extract(const std::string& file, const std::string& outDir, ExtractStats& st, const Progress& progress = Progress(), bool overwrite = true,
             std::string* err = nullptr);

}  // namespace gfs
}  // namespace zengine
