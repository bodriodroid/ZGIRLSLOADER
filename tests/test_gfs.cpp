// test_gfs.cpp - src/core/gfs.*: header + table parsing, extraction, the refusals (unsafe paths, a cut-off archive, not a .gfs).
// Everything runs on archives built here; no game data is read.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "gfs.h"

using namespace zengine;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                     \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(c)) {                                                                  \
            ++g_fail;                                                                \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c);                       \
        }                                                                            \
    } while (0)

static void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}
static void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int s = 56; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}
static void putStr(std::vector<uint8_t>& v, const std::string& s) { v.insert(v.end(), s.begin(), s.end()); }

// The archive of `files` as the game's tools write it.
static std::vector<uint8_t> build(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<uint8_t> table;
    put64(table, 20);
    putStr(table, "Reverge Package File");
    put64(table, 3);
    putStr(table, "1.1");
    put64(table, files.size());
    for (const auto& f : files) {
        put64(table, f.first.size());
        putStr(table, f.first);
        put64(table, f.second.size());
        put32(table, 1);
    }
    std::vector<uint8_t> out;
    put32(out, static_cast<uint32_t>(table.size() + 4));
    out.insert(out.end(), table.begin(), table.end());
    for (const auto& f : files) putStr(out, f.second);
    return out;
}
static void save(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main() {
    const fs::path dir = fs::temp_directory_path() / ("zg_gfs_test_" + std::to_string(std::rand()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    const std::vector<std::pair<std::string, std::string>> files = {
        {"characters/filia/filia.foit", "FOIT-bytes"}, {"ui\\Win\\Main.gbs", std::string("GBS\0\1\2", 6)}, {"empty.txt", ""}, {"a/b/c.ini", "x=1\n"}};
    const std::vector<uint8_t> bytes = build(files);
    save(dir / "t.gfs", bytes);

    // ---- index
    gfs::Index idx;
    std::string err;
    CHECK(gfs::parseIndex(bytes.data(), bytes.size(), bytes.size(), idx, &err));
    CHECK(idx.entries.size() == 4 && idx.version == "1.1" && idx.fileSize == bytes.size());
    CHECK(idx.entries.size() == 4 && idx.entries[0].path == "characters/filia/filia.foit" && idx.entries[0].size == 10 &&
          idx.entries[0].offset == idx.dataOffset);
    CHECK(idx.entries.size() == 4 && idx.entries[1].offset == idx.dataOffset + 10 && idx.entries[2].size == 0 && idx.entries[3].offset == idx.dataOffset + 16);
    gfs::Index viaFile;
    CHECK(gfs::readIndex((dir / "t.gfs").string(), viaFile, &err) && viaFile.entries.size() == 4);
    std::vector<uint8_t> one;
    CHECK(viaFile.entries.size() == 4 && gfs::readEntry((dir / "t.gfs").string(), viaFile.entries[3], one) && std::string(one.begin(), one.end()) == "x=1\n");

    // ---- extraction (a backslash path is split like a slash)
    gfs::ExtractStats st;
    int ticks = 0;
    CHECK(gfs::extract((dir / "t.gfs").string(), (dir / "out").string(), st, [&](size_t d, size_t t) { ++ticks; return d <= t; }, true, &err));
    CHECK(st.files == 4 && st.problems.empty() && st.bytes == 20 && ticks == 4);
    CHECK(slurp(dir / "out" / "characters" / "filia" / "filia.foit") == "FOIT-bytes");
    CHECK(slurp(dir / "out" / "ui" / "Win" / "Main.gbs") == std::string("GBS\0\1\2", 6));
    CHECK(fs::exists(dir / "out" / "empty.txt") && fs::file_size(dir / "out" / "empty.txt") == 0);
    // a second run with overwrite off leaves the finished files alone
    CHECK(gfs::extract((dir / "t.gfs").string(), (dir / "out").string(), st, gfs::Progress(), false, &err) && st.files == 0 && st.skipped == 4);
    // the callback can stop it
    fs::remove_all(dir / "out2");
    CHECK(gfs::extract((dir / "t.gfs").string(), (dir / "out2").string(), st, [](size_t, size_t) { return false; }, true, &err) && st.files == 1);

    // ---- paths that would leave the folder are refused, the rest is extracted
    {
        const std::vector<uint8_t> evil = build({{"../evil.txt", "no"}, {"ok/fine.txt", "yes"}, {"C:/x.txt", "no"}, {"a/../../b.txt", "no"}, {"//abs.txt", "ok"}});
        save(dir / "evil.gfs", evil);
        fs::create_directories(dir / "sandbox");
        CHECK(gfs::extract((dir / "evil.gfs").string(), (dir / "sandbox" / "out").string(), st, gfs::Progress(), true, &err));
        CHECK(st.problems.size() == 3 && st.files == 2);
        CHECK(!fs::exists(dir / "sandbox" / "evil.txt") && !fs::exists(dir / "evil.txt") && !fs::exists(dir / "b.txt"));
        CHECK(slurp(dir / "sandbox" / "out" / "ok" / "fine.txt") == "yes" && slurp(dir / "sandbox" / "out" / "abs.txt") == "ok");
    }
    CHECK(gfs::safePath("a\\b//c/./d") == "a/b/c/d");
    CHECK(gfs::safePath("..").empty() && gfs::safePath("a/..").empty() && gfs::safePath("").empty() && gfs::safePath("/").empty());
    CHECK(gfs::safePath(std::string("a\0b", 3)).empty() && gfs::safePath("d:evil").empty());

    // ---- things that are not archives, and a cut-off one
    {
        gfs::Index x;
        CHECK(!gfs::parseIndex(bytes.data(), 10, bytes.size(), x, &err) && !err.empty());
        std::vector<uint8_t> bad = bytes;
        bad[12] = 'X';   // the magic
        CHECK(!gfs::parseIndex(bad.data(), bad.size(), bad.size(), x, &err));
        std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 3);   // the last file is not all there (a download that is not finished)
        CHECK(!gfs::parseIndex(cut.data(), cut.size(), cut.size(), x, &err) && err.find("incomplete") != std::string::npos);
        std::vector<uint8_t> lie = bytes;   // a file count far beyond the table
        lie[0x2B + 7] = 0xFF;
        lie[0x2B + 6] = 0xFF;
        CHECK(!gfs::parseIndex(lie.data(), lie.size(), lie.size(), x, &err));
        std::vector<uint8_t> off = bytes;   // a data offset beyond the file
        off[0] = 0x7F;
        CHECK(!gfs::parseIndex(off.data(), off.size(), off.size(), x, &err));
        const std::vector<uint8_t> tiny = build({{"a", "b"}});
        CHECK(gfs::parseIndex(tiny.data(), tiny.size(), tiny.size(), x, &err) && x.entries.size() == 1);
        CHECK(!gfs::readIndex((dir / "missing.gfs").string(), x, &err) && !err.empty());
        save(dir / "junk.gfs", {1, 2, 3});
        CHECK(!gfs::readIndex((dir / "junk.gfs").string(), x, &err));
        CHECK(!gfs::extract((dir / "junk.gfs").string(), (dir / "j").string(), st, gfs::Progress(), true, &err));
    }

    fs::remove_all(dir);
    std::printf("gfs tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
