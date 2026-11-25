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
#include "pack_merge.h"
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
            "  CCLib.gfsAddFile('data01', 'chars/filia.foit', 'files/x.foit')\n"       // a character file named by a rule: accepted
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
        CHECK(via(v, "chars/filia.foit") == "never");
        CHECK(via(v, "escape.txt") == "<missing>");
        bool charLog = false;
        for (const std::string& l : rep.log) charLog = charLog || l.find("character file(s)") != std::string::npos;
        CHECK(charLog);
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


    // ---- changes.lua in the restoration-pack shape: a filelist.txt of "archive|entry" lines read with io.open + f:lines()
    {
        fs::remove_all(game / "mods", ec);
        const fs::path pack = game / "mods" / "resourcepacks" / "Restore";
        put(pack / "filelist.txt", "chars|chars/filia.foit\r\nchars|chars/filia.art\r\n\r\nui|ui/hit.txt\r\n");
        put(pack / "chars.gfs" / "chars" / "filia.foit", "RESTORED foit");
        put(pack / "chars.gfs" / "chars" / "filia.art", "RESTORED art");
        put(pack / "ui.gfs" / "ui" / "hit.txt", "RESTORED hit");
        put(pack / "changes.lua",
            "ModInfo = { ModName = 'Restore', Version = 1 }\n"
            "Mod = { install = function()\n"
            "  local base = CCLib.work_dir .. '\\\\mods\\\\Restore\\\\'\n"
            "  local list = io.open(base .. 'filelist.txt', 'rb')\n"
            "  if not list then return false, 'no filelist' end\n"
            "  local n = 0\n"
            "  for line in list:lines() do\n"
            "    line = (line:gsub('\\r', ''))\n"
            "    if line ~= '' then\n"
            "      local archive, entry = line:match('^([^|]+)|(.+)$')\n"
            "      local path = base .. archive .. '.gfs\\\\' .. (entry:gsub('/', '\\\\'))\n"
            "      local probe = io.open(path, 'rb')\n"
            "      if not probe then return false, 'missing ' .. path end\n"
            "      probe:close()\n"
            "      if not CCLib.gfsAddFile(archive, entry, path) then return false, 'rejected ' .. entry end\n"
            "      n = n + 1\n"
            "    end\n"
            "  end\n"
            "  list:close()\n"
            "  if not CCLib.gfsCommitChanges() then return false, 'commit' end\n"
            "  return n == 3\n"
            "end }\n");
        Vfs v;
        v.mount((game / "data").string());
        const modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        CHECK(rep.packs == 1);
        CHECK(via(v, "chars/filia.foit") == "RESTORED foit" && via(v, "chars/filia.art") == "RESTORED art" && via(v, "ui/hit.txt") == "RESTORED hit");
        CHECK(!hasWarn(rep, "NOT loaded") && !hasWarn(rep, "failed"));
    }

    // ---- match = path: the same file name in two game folders is not mixed up
    {
        fs::remove_all(game / "mods", ec);
        put(game / "data" / "stages" / "a" / "dx1.txt", "GAME a");
        put(game / "data" / "stages" / "b" / "dx1.txt", "GAME b");
        const fs::path packs = game / "mods" / "resourcepacks";
        put(packs / "ByName" / "dx1.txt", "NAME");                                     // default: both folders
        Vfs v0;
        v0.mount((game / "data").string());
        modsdir::mountAll(v0, game.string(), false);
        CHECK(via(v0, "stages/a/dx1.txt") == "NAME" && via(v0, "stages/b/dx1.txt") == "NAME");
        fs::remove_all(packs, ec);
        put(packs / "Exact" / "pack.ini", "name = Exact\nmatch = path\nignore = notes/*, *.psd\n");
        put(packs / "Exact" / "stages" / "b" / "dx1.txt", "PATH b");
        put(packs / "Exact" / "dx1.txt", "BARE");                                      // two game files: skipped, not guessed
        put(packs / "Exact" / "notes" / "todo.txt", "ignored");
        put(packs / "Exact" / "art.psd", "ignored");
        put(packs / "Exact" / "chars" / "filia.foit", "PATH foit");                    // named by its path: taken
        put(packs / "Exact" / "brandnew" / "x.txt", "NEW");                            // nothing below it: simply new
        Vfs v;
        v.mount((game / "data").string());
        const modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        CHECK(via(v, "stages/a/dx1.txt") == "GAME a");
        CHECK(via(v, "stages/b/dx1.txt") == "PATH b");
        CHECK(via(v, "notes/todo.txt") == "<missing>" && via(v, "art.psd") == "<missing>");
        CHECK(via(v, "chars/filia.foit") == "PATH foit");
        CHECK(via(v, "brandnew/x.txt") == "NEW");
        CHECK(hasWarn(rep, "more than one game file"));
        bool pathMode = false;
        for (const auto& it : rep.items) pathMode = pathMode || (it.id == "Exact" && it.mode == "path");
        CHECK(pathMode);
        CHECK(packmerge::parseConfig("match = path\nignore = a/*, *.tmp").matchPath);
        CHECK(packmerge::ignored(packmerge::parseConfig("ignore = a/*, *.tmp"), "A/b/c.txt") && packmerge::ignored(packmerge::parseConfig("ignore = *.tmp"), "x/y.TMP"));
        CHECK(!packmerge::ignored(packmerge::parseConfig("ignore = a/*"), "b/a/c.txt"));
    }

#ifdef ZG_EXAMPLES_DIR
    // ---- the examples/ folder loads as documented (so it cannot rot)
    {
        fs::remove_all(game / "mods", ec);
        fs::remove_all(game / "data" / "core", ec);
        put(game / "data" / "core" / "localization" / "es_loc.json", "{\n \"Play\": \"Jugar\"\n}\n");
        put(game / "data" / "ui" / "config" / "MainMenu" / "challenges.ini", "ChalName: C1\nTime: 99\n");
        fs::create_directories(game / "mods" / "resourcepacks", ec);
        fs::copy(fs::path(ZG_EXAMPLES_DIR) / "resourcepacks", game / "mods" / "resourcepacks", fs::copy_options::recursive, ec);
        CHECK(!ec);
        Vfs v;
        v.mount((game / "data").string());
        const modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        CHECK(rep.packs == 2);
        CHECK(!hasWarn(rep, "NOT loaded") && !hasWarn(rep, "failed") && !hasWarn(rep, "skipped"));
        const std::string loc = via(v, "core/localization/es_loc.json");
        CHECK(loc.find("\"Play\": \"Jugar\"") != std::string::npos && loc.find("EXAMPLE_PACK_HELLO") != std::string::npos);
        const std::string chal = via(v, "ui/config/MainMenu/challenges.ini");
        CHECK(chal.find("Time: 99") != std::string::npos && chal.find("EXAMPLE_CHALLENGE") != std::string::npos);
        CHECK(via(v, "core/example/EXAMPLE_hello.txt").find("EXAMPLE placeholder") != std::string::npos);
        CHECK(via(v, "ui/example/EXAMPLE_hint.txt").find("EXAMPLE placeholder") != std::string::npos);
        CHECK(via(v, "readme.txt") == "<missing>");   // ignore = readme.txt
        fs::remove_all(game / "mods", ec);
    }
#endif

    // ---- mixing: pack_merge on its own
    {
        using namespace packmerge;
        std::string out, err;
        const Rule keys{"loc", How::Keys, ""};
        CHECK(merge("es_loc.json", keys, "{\n \"a\": \"uno\",\n \"b\": null,\n \"c\": \"tres\"\n}\n",
                    "{\"b\": \"dos\", \"c\": null, \"d\": \"cu\\u00e1tro \\\"x\\\"\\n\"}", out, &err));
        CHECK(out == "{\n \"a\": \"uno\",\n \"b\": \"dos\",\n \"c\": \"tres\",\n \"d\": \"cu\xC3\xA1tro \\\"x\\\"\\n\"\n}\n");   // null never blanks a key
        CHECK(!merge("es_loc.json", keys, "{\"a\": \"x\"}", "{\"a\": ", out, &err) && err.find("pack's file") != std::string::npos);
        CHECK(!merge("es_loc.json", keys, "{\"a\": [1]}", "{}", out, &err));

        // "Key: value" files: a repeated key is replaced as a group, in the place of its first line
        const std::string base = "# comment\nName: A\nChoice: One\nChoice: Two\nTime: 5\n";
        CHECK(merge("x.ini", keys, base, "Choice: Only\nExtra: 1\ntime = 9\n", out));
        CHECK(out == "# comment\nName: A\nChoice: Only\ntime = 9\nExtra: 1\n");
        CHECK(merge("x.ini", keys, "a=1\r\nb=2\r\n", "b=3\r\n", out) && out == "a=1\r\nb=3\r\n");   // the line ending of what is below stays

        // records
        const std::string chal = "#### one\nChalName: C1\nTime: 99\n\n\n#### two\nChalName: C2\nTime: 80\n";
        const Rule rec{"challenges.ini", How::Records, "chalname"};
        CHECK(merge("challenges.ini", rec, chal, "ChalName: C2\nTime: 10\n\nChalName: C3\nTime: 7\n", out));
        CHECK(out == "#### one\nChalName: C1\nTime: 99\n\n\nChalName: C2\nTime: 10\n\nChalName: C3\nTime: 7\n");
        CHECK(merge("challenges.ini", rec, chal, "chalname: c1 # again\nTime: 1\n", out) && out.find("Time: 1\n\n\n#### two") != std::string::npos);
        CHECK(!merge("a_loc.json", rec, "{}", "{}", out));
        CHECK(merge("n.txt", Rule{"*.txt", How::Append, ""}, "a\nb", "c\n", out) && out == "a\nb\nc\n");

        // pack.ini: sections, shortcuts, specificity, bad lines
        std::vector<std::string> warn;
        const Config c = parseConfig(
            "name = X\nrequires = Aotc, b\nconflicts = Z\n[merge]\nini = keys\nui/config/MainMenu/challenges.ini = records ChalName\n"
            "*.txt = append\nloc = replace\nbogus = nope\nx.ini = records\n",
            &warn);
        CHECK(c.requires_.size() == 2 && c.requires_[0] == "aotc" && c.conflicts.size() == 1 && c.conflicts[0] == "z");
        CHECK(warn.size() == 2);
        CHECK(ruleFor(c, "UI\\config\\MainMenu\\Challenges.ini").how == How::Records);   // a path beats the word `ini`
        CHECK(ruleFor(c, "ui/config/MainMenu/challenges.ini").idKey == "chalname");
        CHECK(ruleFor(c, "ui/other.ini").how == How::Keys);
        CHECK(ruleFor(c, "notes.txt").how == How::Append);
        CHECK(ruleFor(c, "core/localization/es_loc.json").how == How::Replace);          // asked for explicitly
        CHECK(ruleFor(Config(), "core/localization/es_loc.json").how == How::Keys);      // the default
        CHECK(ruleFor(Config(), "ui/a.ini").how == How::Replace);
    }

    // ---- mixing: two packs on the same game files, requires / conflicts
    {
        fs::remove_all(game / "mods", ec);
        put(game / "data" / "core" / "localization" / "es_loc.json", "{\n \"Play\": \"Jugar\",\n \"Quit\": \"Salir\"\n}\n");
        put(game / "data" / "ui" / "config" / "MainMenu" / "challenges.ini", "ChalName: C1\nTime: 99\n\nChalName: C2\nTime: 80\n");
        put(game / "data" / "ui" / "config" / "Other" / "challenges.ini", "ChalName: O1\n");
        const fs::path packs = game / "mods" / "resourcepacks";
        // A (priority 10): by name, only retranslates one line and changes one challenge
        put(packs / "A" / "pack.ini", "name = A\npriority = 10\n[merge]\nchallenges.ini = records ChalName\n");
        put(packs / "A" / "localization" / "es_loc.json", "{\"Play\": \"A jugar\"}");
        put(packs / "A" / "challenges.ini", "ChalName: C2\nTime: 55\n");
        // B (priority 20): rules mode on top of A; adds its own line and challenge, needs A
        put(packs / "B" / "pack.ini", "name = B\npriority = 20\nrequires = A\n[merge]\nui/config/MainMenu/challenges.ini = records ChalName\n");
        put(packs / "B" / "loc.json", "{\"Extra\": \"Algo\"}");
        put(packs / "B" / "chal.ini", "ChalName: C3\nTime: 7\n");
        put(packs / "B" / "changes.lua",
            "return {\n  ['core/localization/es_loc.json'] = 'loc.json',\n  ['ui/config/MainMenu/challenges.ini'] = 'chal.ini',\n}\n");
        // C: needs a pack that is not there, D: cannot share a game with A
        put(packs / "E" / "pack.ini", "name = E\npriority = 15\n");   // by name too, no rules: loc files are mixed by default
        put(packs / "E" / "localization" / "es_loc.json", "{\"FromE\": \"E\"}");
        put(packs / "C" / "pack.ini", "name = C\nrequires = missing\n");
        put(packs / "C" / "any.txt", "C");
        put(packs / "D" / "pack.ini", "name = D\nconflicts = A\n");
        put(packs / "D" / "any2.txt", "D");
        Vfs v;
        v.mount((game / "data").string());
        const modsdir::Report rep = modsdir::mountAll(v, game.string(), false);
        const std::string loc = via(v, "core/localization/es_loc.json");
        CHECK(loc.find("\"Play\": \"A jugar\"") != std::string::npos);   // A's line
        CHECK(loc.find("\"Quit\": \"Salir\"") != std::string::npos);     // the game's other lines are still there
        CHECK(loc.find("\"Extra\": \"Algo\"") != std::string::npos);     // B on top of A's result, not on the game's file
        const std::string chal = via(v, "ui/config/MainMenu/challenges.ini");
        CHECK(chal.find("Time: 99") != std::string::npos && chal.find("Time: 55") != std::string::npos && chal.find("Time: 80") == std::string::npos);
        CHECK(chal.find("ChalName: C3") != std::string::npos);
        const std::string other = via(v, "ui/config/Other/challenges.ini");
        CHECK(other.find("ChalName: O1") != std::string::npos && other.find("Time: 55") != std::string::npos);   // by name (A): mixed into every challenges.ini
        CHECK(other.find("ChalName: C3") == std::string::npos);                                                   // by path (B): only the one it names
        CHECK(via(v, "es_loc.json") == loc);
        CHECK(get(game / "data" / "core" / "localization" / "es_loc.json").find("A jugar") == std::string::npos);   // the game's file is never written
        CHECK(via(v, "any.txt") == "<missing>" && via(v, "any2.txt") == "<missing>");
        CHECK(hasWarn(rep, "needs pack missing") && hasWarn(rep, "cannot be used together with a"));
        CHECK(loc.find("\"FromE\": \"E\"") != std::string::npos);
        CHECK(rep.packs == 3);
        size_t mergedA = 0, mergedB = 0;
        for (const auto& it : rep.items) {
            if (it.id == "A") mergedA = it.merged;
            if (it.id == "B") mergedB = it.merged;
        }
        CHECK(mergedA >= 2 && mergedB == 2);
        bool mixedConflict = false;
        for (const auto& c : rep.conflicts)
            if (c.path.find("es_loc.json") != std::string::npos && c.mixed && c.winner == "pack:E") mixedConflict = true;
        CHECK(mixedConflict);

        // a pack file that is not valid for its rule is not mixed: it replaces whole, with a warning
        fs::remove_all(packs, ec);
        put(packs / "Bad" / "es_loc.json", "this is not json");
        Vfs v2;
        v2.mount((game / "data").string());
        const modsdir::Report rep2 = modsdir::mountAll(v2, game.string(), false);
        CHECK(via(v2, "core/localization/es_loc.json") == "this is not json");
        CHECK(hasWarn(rep2, "not mixed"));
    }

    fs::remove_all(root, ec);
    std::printf("pack tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
