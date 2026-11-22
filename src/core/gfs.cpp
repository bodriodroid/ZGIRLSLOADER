#include "gfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace zengine {
namespace gfs {

namespace fs = std::filesystem;

namespace {
constexpr size_t kHeaderSize = 0x33;
constexpr uint32_t kMaxTable = 512u * 1024u * 1024u;   // a table bigger than this is not an archive
const char kMagic[] = "Reverge Package File";

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }
}  // namespace

bool parseIndex(const uint8_t* head, size_t headLen, uint64_t fileSize, Index& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    out = Index();
    if (!head || headLen < kHeaderSize) return fail("too short to be a .gfs");
    const size_t magicLen = sizeof(kMagic) - 1;
    if (be64(head + 4) != magicLen || std::memcmp(head + 12, kMagic, magicLen) != 0) return fail("not a Reverge Package File");
    const uint64_t verLen = be64(head + 32);
    if (verLen != 3) return fail("unsupported .gfs header");
    out.version.assign(reinterpret_cast<const char*>(head + 40), 3);
    out.dataOffset = be32(head);
    out.fileSize = fileSize;
    const uint64_t count = be64(head + 0x2B);
    if (out.dataOffset < kHeaderSize || out.dataOffset > fileSize || out.dataOffset > kMaxTable) return fail("the data offset is outside the file");
    if (headLen < out.dataOffset) return fail("the table is not fully read");
    if (count > (out.dataOffset - kHeaderSize) / 20) return fail("the file count does not fit the table");   // an entry takes at least 20 bytes
    size_t pos = kHeaderSize;
    uint64_t at = out.dataOffset;
    out.entries.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        if (pos + 8 > out.dataOffset) return fail("the table ends early");
        const uint64_t pl = be64(head + pos);
        pos += 8;
        if (pl > 4096 || pos + pl + 12 > out.dataOffset) return fail("a path in the table is damaged");
        Entry e;
        e.path.assign(reinterpret_cast<const char*>(head + pos), static_cast<size_t>(pl));
        pos += static_cast<size_t>(pl);
        e.size = be64(head + pos);
        pos += 8 + 4;   // the size, then the flag word
        if (e.size > fileSize || at > fileSize - e.size) return fail("archive is incomplete (a file lies beyond its end)");
        e.offset = at;
        at += e.size;
        out.entries.push_back(std::move(e));
    }
    return true;
}

bool readIndex(const std::string& file, Index& out, std::string* err) {
    std::error_code ec;
    const uint64_t size = fs::file_size(file, ec);
    if (ec) {
        if (err) *err = "cannot open " + file;
        return false;
    }
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + file;
        return false;
    }
    std::vector<uint8_t> head(kHeaderSize);
    f.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    if (static_cast<size_t>(f.gcount()) < kHeaderSize) {
        if (err) *err = "too short to be a .gfs";
        return false;
    }
    const uint32_t table = be32(head.data());
    if (table >= kHeaderSize && table <= size && table <= kMaxTable) {
        head.resize(table);
        f.read(reinterpret_cast<char*>(head.data() + kHeaderSize), static_cast<std::streamsize>(table - kHeaderSize));
        if (static_cast<size_t>(f.gcount()) < table - kHeaderSize) head.resize(kHeaderSize + static_cast<size_t>(f.gcount()));
    }
    return parseIndex(head.data(), head.size(), size, out, err);
}

bool readEntry(const std::string& file, const Entry& e, std::vector<uint8_t>& out) {
    std::ifstream f(file, std::ios::binary);
    if (!f || e.size > (uint64_t(1) << 31)) return false;
    f.seekg(static_cast<std::streamoff>(e.offset));
    out.resize(static_cast<size_t>(e.size));
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<uint64_t>(f.gcount()) == e.size;
}

std::string safePath(std::string_view stored) {
    std::string out;
    size_t i = 0;
    while (i <= stored.size()) {
        size_t j = stored.find_first_of("/\\", i);
        if (j == std::string_view::npos) j = stored.size();
        const std::string_view part = stored.substr(i, j - i);
        i = j + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") return std::string();
        if (part.find('\0') != std::string_view::npos || part.find(':') != std::string_view::npos) return std::string();
        if (!out.empty()) out += '/';
        out.append(part);
    }
    return out;
}

bool extract(const std::string& file, const std::string& outDir, ExtractStats& st, const Progress& progress, bool overwrite, std::string* err) {
    st = ExtractStats();
    Index idx;
    if (!readIndex(file, idx, err)) return false;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open " + file;
        return false;
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        if (err) *err = "cannot create " + outDir;
        return false;
    }
    std::vector<char> buf(1 << 20);
    for (size_t i = 0; i < idx.entries.size(); ++i) {
        const Entry& e = idx.entries[i];
        const std::string rel = safePath(e.path);
        if (rel.empty()) {
            st.problems.push_back("refused path: " + e.path);
        } else {
            const fs::path dest = fs::path(outDir) / fs::path(rel);
            std::error_code e1;
            if (!overwrite && fs::is_regular_file(dest, e1) && fs::file_size(dest, e1) == e.size) {
                ++st.skipped;
            } else {
                fs::create_directories(dest.parent_path(), e1);
                std::ofstream o(dest, std::ios::binary | std::ios::trunc);
                bool ok = static_cast<bool>(o);
                in.clear();
                in.seekg(static_cast<std::streamoff>(e.offset));
                uint64_t left = e.size;
                while (ok && left > 0) {
                    const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
                    in.read(buf.data(), static_cast<std::streamsize>(n));
                    if (static_cast<size_t>(in.gcount()) != n) ok = false;
                    else o.write(buf.data(), static_cast<std::streamsize>(n)), ok = static_cast<bool>(o);
                    left -= n;
                }
                o.close();
                if (ok) {
                    ++st.files;
                    st.bytes += e.size;
                } else {
                    st.problems.push_back("could not write: " + rel);
                    fs::remove(dest, e1);
                }
            }
        }
        if (progress && !progress(i + 1, idx.entries.size())) break;
    }
    return true;
}

}  // namespace gfs
}  // namespace zengine
