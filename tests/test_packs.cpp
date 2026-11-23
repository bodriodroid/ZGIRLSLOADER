// test_packs.cpp - resource packs: the two modes (changes.lua rules / automatic by name), the changes.lua sandbox, and that the game's
// own files are never touched.  Everything runs on folders built here; no game data is read.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "modsdir.h"
#include "pack_changes.h"
#include "vfs.h"

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

static void put(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
static std::string get(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static std::string via(const Vfs& v, const std::string& game) {
    std::string t;
    return v.readText(game, t) ? t : std::string("<missing>");
}
static bool hasWarn(const modsdir::Report& r, const std::string& needle) {
    for (const auto& w : r.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}
// a tiny DDS-shaped file so modcheck accepts it ("DDS " + 124-byte header + data)
static std::string dds(char tag) {
    std::string s = "DDS ";
    s += std::string(4, '\0');
    s[4] = 124;
    s.resize(128 + 16, tag);
    s[12] = 4;   // height
    s[16] = 4;   // width
    return s;
}

int main() {
    const fs::path root = fs::temp_directory_path() / "zdroid_pack_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path game = root / "game";
    put(game / "data" / "characters" / "filia_1p.txt", "ORIGINAL filia");
    put(game / "data" / "ui" / "hit.txt", "ORIGINAL hit");
    put(game / "data" / "other" / "hit.txt", "ORIGINAL hit (other folder)");
    put(game / "data" / "chars" / "filia.foit", "ORIGINAL foit");

    // ---- mode 1: no changes.lua = by name (any folder), mods excluded
    {
        const fs::path pack = root / "p1";
        put(pack / "deep" / "dir" / "hit.txt", "PACK hit");
        put(pack / "filia.foit", "PACK foit");
        put(pack / "filia.art", "PACK art");
        put(pack / "filia.art-pt", "PACK artpt");
        put(pack / "readme.txt", "readme");
        put(pack / "pack.ini", "name=P1\n");
        Vfs v;
        v.mount((game / "data").string());
        std::vector<std::string> skipped;
        const size_t n = v.mountByName(pack.string(), {".foit", ".art", ".art-pt", "pack.ini", "readme.txt"}, &skipped);
        CHECK(n == 1);
        CHECK(skipped.size() == 5);
        CHECK(via(v, "ui/hit.txt") == "PACK hit");
        CHECK(via(v, "other/hit.txt") == "PACK hit");           // replaced by name in every folder
        CHECK(via(v, "hit.txt") == "PACK hit");
        CHECK(via(v, "characters/filia_1p.txt") == "ORIGINAL filia");
        CHECK(via(v, "chars/filia.foit") == "ORIGINAL foit");   // a pack never replaces a .foit
        CHECK(get(game / "data" / "ui" / "hit.txt") == "ORIGINAL hit");   // the game's file on disk is untouched
    }

    // ---- overrides (what rules mode mounts)
    {
        const fs::path pack = root / "p2";
        put(pack / "x" / "mine.txt", "MINE");
        Vfs v;
        v.mount((game / "data").string());
        CHECK(v.mountOverrides({{"ui/hit.txt", (pack / "x" / "mine.txt").string()}}) == 1);
        CHECK(via(v, "ui/hit.txt") == "MINE");
        CHECK(via(v, "other/hit.txt") == "ORIGINAL hit (other folder)");   // only the path the rule names
        CHECK(v.mountOverrides({}) == 0);
    }

    // ---- changes.lua through mountAll: CCLib style
    {
        fs::remove_all(game / "mods", ec);
        const fs::path pack = game / "mods" / "resourcepacks" / "AOTC";
        put(pack / "files" / "hit_new.txt", "RULE hit");
        put(pack / "files" / "folder" / "a.txt", "A");
        put(pack / "files" / "folder" / "b.txt", "B");
        put(pack / "files" / "x.foit", "never");
        put(pack / "decoy" / "filia_1p.txt", "DECOY (no rule names it)");
        put(pack / "changes.lua",
            "ModInfo = { ModName = 'AOTC', Version = 2.000000, Author = 'someone', Desc = 'test pack' }\n"
            "Mod = {}\n"
            "function Mod.install()\n"
            "  CCLib.gfsAddFile('data01', 'ui/hit.txt', CCLib.work_dir .. '\\\\mods\\\\resourcepacks\\\\AOTC\\\\files\\\\hit_new.txt')\n"
            "  CCLib.gfsAddFiles('data01', 'newdir', 'files/folder')\n"
            "  CCLib.gfsAddFile('data01', 'chars/filia.foit', 'files/x.foit')\n"       // refused: a mod file
            "  CCLib.gfsAddFile('data01', '../escape.txt', 'files/hit_new.txt')\n"      // refused: leaves the game
            "  if CCLib.hasMod('NotInstalled') then error('should not be there') end\n"
            "  CCLib.gbsMerge('x', 'y')\n"                                              // refused + warning
            "  return true\n"
            "end\n");
        Vfs v;
        v.mount((game / "data").string());
        const modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        CHECK(rep.packs == 1);
        CHECK(via(v, "ui/hit.txt") == "RULE hit");
        CHECK(via(v, "newdir/a.txt") == "A" && via(v, "newdir/b.txt") == "B");
        CHECK(via(v, "characters/filia_1p.txt") == "ORIGINAL filia");   // not named by a rule: not changed, even if the pack has a file by that name
        CHECK(via(v, "chars/filia.foit") == "ORIGINAL foit");
        CHECK(via(v, "escape.txt") == "<missing>");
        CHECK(hasWarn(rep, ".foit"));
        CHECK(hasWarn(rep, "refused game path"));
        CHECK(hasWarn(rep, "gbsMerge") || hasWarn(rep, "refused"));
        bool found = false;
        for (const auto& it : rep.items)
            if (it.kind == "pack" && it.id == "AOTC") {
                found = true;
                CHECK(it.mode == "rules");
                CHECK(it.mounted && !it.blocked);
                CHECK(it.name == "AOTC" && it.version == "2");
            }
        CHECK(found);
        CHECK(get(game / "data" / "ui" / "hit.txt") == "ORIGINAL hit");   // the game's file is never written
    }

    // ---- changes.lua: table forms, extract + edit, localization, sandbox
    {
        fs::remove_all(game / "mods", ec);
        put(game / "data" / "loc" / "en_loc.json", "{\n  \"a\": \"one\"\n}\n");
        const fs::path pack = game / "mods" / "resourcepacks" / "Tables";
        put(pack / "sfx" / "hit.wav.txt", "TABLE hit");
        put(pack / "plain.txt", "PLAIN");
        put(pack / "changes.lua",
            "return {\n"
            "  ['ui/hit.txt'] = 'sfx/hit.wav.txt',\n"
            "  { game = 'characters/filia_1p.txt', source = 'plain.txt' },\n"
            "  { 'chars/other.txt', 'plain.txt' },\n"
            "}\n");
        Vfs v;
        v.mount((game / "data").string());
        modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        CHECK(via(v, "ui/hit.txt") == "TABLE hit");
        CHECK(via(v, "characters/filia_1p.txt") == "PLAIN");
        CHECK(via(v, "chars/other.txt") == "PLAIN");

        fs::remove_all(game / "mods", ec);
        const fs::path p2 = game / "mods" / "resourcepacks" / "Edit";
        put(p2 / "changes.lua",
            "function install()\n"
            "  CCLib.gfsExtractFile('data01', 'ui/hit.txt', 'scratch/hit.txt')\n"
            "  local f = io.open('scratch/hit.txt', 'a'); f:write(' + EDITED'); f:close()\n"
            "  CCLib.gfsAddFile('data01', 'ui/hit.txt', 'scratch/hit.txt')\n"
            "  CCLib.addLocalization('NEW_KEY', 'Hello \"world\"')\n"
            "  print('hi')\n"
            "end\n");
        Vfs v2;
        v2.mount((game / "data").string());
        rep = modsdir::mountAll(v2, game.string(), false);
        CHECK(via(v2, "ui/hit.txt") == "ORIGINAL hit + EDITED");
        CHECK(get(game / "data" / "ui" / "hit.txt") == "ORIGINAL hit");
        const std::string loc = via(v2, "loc/en_loc.json");
        CHECK(loc.find("\"a\": \"one\"") != std::string::npos);
        CHECK(loc.find("\"NEW_KEY\": \"Hello \\\"world\\\"\"") != std::string::npos);
        CHECK(get(game / "data" / "loc" / "en_loc.json").find("NEW_KEY") == std::string::npos);   // the original file is not edited
    }

    // ---- sandbox + errors: nothing leaves the pack's scratch folder; a script that fails applies nothing
    {
        fs::remove_all(game / "mods", ec);
        const fs::path pack = game / "mods" / "resourcepacks" / "Bad";
        put(pack / "a.txt", "A");
        put(pack / "changes.lua",
            "CCLib.gfsAddFile('d', 'ui/hit.txt', 'a.txt')\n"
            "error('boom')\n");
        Vfs v;
        v.mount((game / "data").string());
        const modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        CHECK(rep.packs == 0);
        CHECK(via(v, "ui/hit.txt") == "ORIGINAL hit");   // all or nothing
        CHECK(hasWarn(rep, "boom"));

        packchanges::Context ctx;
        ctx.packDir = pack.string();
        ctx.tempDir = (root / "t" / "tmp").string();
        ctx.outDir = (root / "t" / "out").string();
        ctx.gameDir = game.string();
        Vfs b;
        b.mount((game / "data").string());
        ctx.base = &b;
        put(pack / "changes.lua", "local f = io.open('/etc/hostname', 'r'); if f then error('read outside') end\n"
                                  "if os.execute then error('os.execute exists') end\n"
                                  "if io.popen then error('popen exists') end\n"
                                  "if require then error('require exists') end\n"
                                  "if dofile then error('dofile exists') end\n"
                                  "local ok = io.open('../up.txt', 'w'); if ok then error('wrote outside') end\n"
                                  "local w = io.open('scratch.txt', 'w'); w:write('ok'); w:close()\n");
        packchanges::Result r = packchanges::run((pack / "changes.lua").string(), ctx);
        CHECK(r.ok);
        CHECK(fs::exists(root / "t" / "tmp" / "scratch.txt"));
        CHECK(!fs::exists(root / "up.txt"));

        put(pack / "changes.lua", "while true do end\n");
        r = packchanges::run((pack / "changes.lua").string(), ctx);
        CHECK(!r.ok && r.error.find("too long") != std::string::npos);
        put(pack / "changes.lua", "this is not lua (\n");
        r = packchanges::run((pack / "changes.lua").string(), ctx);
        CHECK(!r.ok && !r.error.empty());
        r = packchanges::run((pack / "missing.lua").string(), ctx);
        CHECK(!r.ok);
    }

    // ---- mode 2 through mountAll: automatic by name, with the warning about ignored mod files
    {
        fs::remove_all(game / "mods", ec);
        const fs::path pack = game / "mods" / "resourcepacks" / "Auto";
        put(pack / "any" / "where" / "hit.txt", "AUTO hit");
        put(pack / "chars" / "filia.foit", "AUTO foit");
        put(pack / "pack.ini", "name = Auto\nversion = 1\n");
        Vfs v;
        v.mount((game / "data").string());
        const modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        CHECK(rep.packs == 1);
        CHECK(via(v, "ui/hit.txt") == "AUTO hit");
        CHECK(via(v, "other/hit.txt") == "AUTO hit");
        CHECK(via(v, "chars/filia.foit") == "ORIGINAL foit");
        CHECK(hasWarn(rep, "left out"));
        bool found = false;
        for (const auto& it : rep.items)
            if (it.id == "Auto") {
                found = true;
                CHECK(it.mode == "name" && it.mounted);
            }
        CHECK(found);
        CHECK(get(game / "data" / "ui" / "hit.txt") == "ORIGINAL hit");
    }

    fs::remove_all(root, ec);
    std::printf("pack tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
