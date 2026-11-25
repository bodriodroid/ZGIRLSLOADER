#include "modsdir.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

#include "modloader.h"
#include "pack_changes.h"
#include "pack_merge.h"
#include "zipread.h"

namespace fs = std::filesystem;

namespace zengine {
namespace modsdir {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string readText(const fs::path& p) {
    std::string t;
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return t;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) t.append(buf, n);
    std::fclose(f);
    return t;
}

bool writeIfMissing(const fs::path& p, const std::string& text) {
    std::error_code ec;
    if (fs::exists(p, ec)) return true;
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

// "key = value" lines of mods.ini
std::map<std::string, std::string> readIni(const fs::path& p) {
    std::map<std::string, std::string> m;
    const std::string t = readText(p);
    size_t pos = 0;
    while (pos < t.size()) {
        size_t nl = t.find('\n', pos);
        if (nl == std::string::npos) nl = t.size();
        std::string line = t.substr(pos, nl - pos);
        pos = nl + 1;
        const auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos || line[b] == '#' || line[b] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            const auto x = s.find_first_not_of(" \t\r");
            if (x == std::string::npos) return std::string();
            return s.substr(x, s.find_last_not_of(" \t\r") - x + 1);
        };
        m[lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    return m;
}

const char* kRootReadme =
    "ZDroid-Engine mods folder / carpeta de mods\n"
    "===========================================\n\n"
    "  pal/               custom palettes (*.pal)            paletas personalizadas\n"
    "  modscustom/        mods that change the game          mods que alteran el juego\n"
    "  stages/            custom stages                      escenarios\n"
    "  sdvoice_japanese/  Japanese voices                    voces japonesas\n"
    "  resourcepacks/     replace art / sound, game untouched  reemplazan contenido sin tocar el core ni los .dds originales\n\n"
    "Order, later wins: game < resourcepacks < modscustom < stages < sdvoice_japanese.\n"
    "mods.ini (optional):\n"
    "  enabled = 0              switch every mod off\n"
    "  disabled = id1, id2      switch single mods / packs off\n"
    "  japanese_voices = auto   auto (follows the language) | 1 | 0\n";

const char* kPalReadme =
    "One folder per character lives here (Filia, MsFortune, Annie ...): put each *.pal in the folder of its character.\n"
    "A *.pal left loose in this folder is moved to its character's folder automatically (organize_pal = 0 in ../mods.ini turns that off).\n"
    "These are the *.pal files saved by the Skullgirls palette editor; other sub-folders are fine too.\n"
    "Each palette appears after the game's own colours of its character (name in the 16-byte header: Filia, MsFortune ...).\n"
    "A palette made for another game version (different number of colours) is skipped with a warning in the log.\n\n"
    "Pon aqui los archivos *.pal del editor de paletas. Cada uno aparece despues de los colores normales de su personaje.\n";

const char* kCustomReadme =
    "One folder per mod. Inside, mirror the game's own layout, e.g.  mymod/characters/filia.foit  or  mymod/UISound/x.wav.\n"
    "Optional mymod/mod.ini:  name = ...  version = ...  author = ...  priority = 100 (higher wins)  enabled = 1\n\n"
    "Una carpeta por mod, con la misma estructura de archivos que el juego.\n";

const char* kStagesReadme =
    "One folder per stage: <id>/<id>.lvl + its textures (+ stage_<id>.png or .dds as the select-screen thumbnail).\n"
    "The stage appears in the stage list next to the game's own.\n\n"
    "Una carpeta por escenario: <id>/<id>.lvl + texturas (+ miniatura stage_<id>.png/.dds).\n";

const char* kVoiceReadme =
    "Japanese voice files, same layout as the game's sound folders (e.g. UISound/JP_chars/...).\n"
    "Only used while the voice language is Japanese (language jp, or japanese_voices = 1 in ../mods.ini).\n\n"
    "Voces japonesas, misma estructura que las carpetas de sonido del juego. Solo se usan con voces en japones.\n";

const char* kPacksReadme =
    "One folder (or .zip) per resource pack.  pack.ini is optional (name / description / priority / enabled).\n"
    "Packs are mounted over the game but under modscustom; the original files are never modified - remove the\n"
    "folder and everything is back.\n\n"
    "Two modes:\n"
    "  * with a changes.lua  -> only what its rules say changes (each rule names a game file and the pack file that replaces it).\n"
    "  * without changes.lua -> every file of the pack replaces the game's files with the same NAME, in any folder.\n"
    ".foit / .art / .art-pt are not part of a resource pack (those are mods: put them in ../modscustom).\n"
    "Mixing: a *_loc.json of a pack is MIXED into the language file below it (key by key) instead of replacing it, so a pack can carry\n"
    "three translated lines and several packs can share one file.  pack.ini can ask for the same for other files ([merge]) and say\n"
    "requires = / conflicts = other packs.  Details: docs/MODS.md.\n\n"
    "Una carpeta (o .zip) por pack. Con changes.lua: solo cambia lo que dicen sus reglas. Sin changes.lua: cada archivo reemplaza\n"
    "por NOMBRE al del juego. Los .foit / .art / .art-pt no van en un pack (son mods). Nunca se tocan los archivos originales:\n"
    "borra la carpeta y vuelve todo.\n";

bool writeText(const fs::path& p, const std::string& text) {
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

// Sets "key = value" in an ini text, keeping every other line (comments included); the first line that has the key is replaced,
// the repeated ones are dropped, a missing key is appended.  `value` empty and `dropIfEmpty`: the key is removed.
std::string iniWith(const std::string& text, const std::string& key, const std::string& value, bool dropIfEmpty = false) {
    std::string out;
    bool done = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        const bool last = nl == std::string::npos;
        if (last) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const std::string t = trimmed(line);
        const auto eq = t.find('=');
        const bool isKey = !t.empty() && t[0] != '#' && t[0] != ';' && eq != std::string::npos && lower(trimmed(t.substr(0, eq))) == key;
        if (!isKey) {
            out += line;
            if (!last) out += '\n';
            continue;
        }
        if (done) continue;
        done = true;
        if (!(dropIfEmpty && value.empty())) out += key + " = " + value + "\n";
    }
    if (!done && !(dropIfEmpty && value.empty())) {
        if (!out.empty() && out.back() != '\n') out += '\n';
        out += key + " = " + value + "\n";
    }
    return out;
}

// A "<name>.zip" next to the mod folders is unpacked into "<name>/" (same folder, wrapper folder taken off) and then behaves like any
// other folder: mod.ini, switches, checks.  "<name>/.zipsrc" remembers which zip (size:mtime) it came from, so an unchanged zip is not
// unpacked again, a replaced zip is unpacked again, and a folder the player made himself with the same name is never touched.
size_t unzipIn(const std::string& folder, std::vector<std::string>& log, std::vector<std::string>& warnings) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return 0;
    std::vector<fs::path> zips;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && lower(it->path().extension().string()) == ".zip") zips.push_back(it->path());
    }
    std::sort(zips.begin(), zips.end());
    size_t n = 0;
    for (const fs::path& z : zips) {
        const std::string stem = z.stem().string();
        if (stem.empty() || stem[0] == '.') continue;
        std::error_code e2;
        const fs::path dest = fs::path(folder) / stem;
        const std::string stamp = std::to_string(fs::file_size(z, e2)) + ":" + std::to_string(static_cast<long long>(fs::last_write_time(z, e2).time_since_epoch().count()));
        const fs::path stampFile = dest / ".zipsrc";
        if (fs::exists(dest, e2)) {
            if (!fs::exists(stampFile, e2)) {
                warnings.push_back(z.filename().string() + ": a folder named " + stem + " already exists, the zip is ignored");
                continue;
            }
            if (readText(stampFile) == stamp) continue;   // already unpacked, unchanged
            fs::remove_all(dest, e2);                     // the zip was replaced: ours to redo
        }
        zipread::ExtractStats st;
        std::string err;
        if (!zipread::extract(z.string(), dest.string(), st, true, &err)) {
            fs::remove_all(dest, e2);
            warnings.push_back(z.filename().string() + ": " + err);
            continue;
        }
        writeText(stampFile, stamp);
        ++n;
        log.push_back("unzipped " + z.filename().string() + " -> " + stem + "/ (" + std::to_string(st.files) + " file(s)" +
                      (st.strippedFolder.empty() ? "" : ", folder " + st.strippedFolder + " taken off") + ")");
        for (const std::string& p : st.problems) warnings.push_back(z.filename().string() + ": " + p);
    }
    return n;
}

// What a resource pack's changes.lua may ask the loader (hasMod / getModVersion) and where its scratch files go.
struct PackEnv {
    std::string gameDir;
    std::string cacheRoot;                           // <mods>/.cache/changes
    std::map<std::string, double> versions;          // lowercase id / name of every mod, pack and stage -> version (1 when it has none)
    std::map<std::string, std::set<std::string>> mixed;   // layer id ("pack:<id>") -> normalised game paths its files were MIXED into (not replaced)
};

std::string safeName(std::string s) {
    for (char& c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) c = '_';
    return s.empty() ? std::string("pack") : s;
}

// The text of a pack's pack.ini (or mod.ini), also when it sits in the only folder inside a wrapper folder.
std::string packIniText(const std::string& dir) {
    for (const char* n : {"pack.ini", "mod.ini"}) {
        const std::string t = readText(fs::path(dir) / n);
        if (!t.empty()) return t;
    }
    std::error_code ec;
    std::string only;
    int folders = 0;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        const std::string n = de.path().filename().string();
        if (de.is_directory(ec) && !n.empty() && n[0] != '.') {
            only = de.path().string();
            ++folders;
        }
    }
    if (folders != 1) return std::string();
    for (const char* n : {"pack.ini", "mod.ini"}) {
        const std::string t = readText(fs::path(only) / n);
        if (!t.empty()) return t;
    }
    return std::string();
}

std::string baseOf(const std::string& path) {
    const size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// The files mountByName() would take from a pack folder: {path relative to the pack, real path}.
std::vector<std::pair<std::string, std::string>> packFiles(const std::string& dir) {
    static const std::set<std::string> skipDirs = {"extract", "build", "analysis", "analysis_v2", "skgsteam", "mods"};
    static const std::vector<std::string> skipExt = {".foit", ".art", ".art-pt"};
    static const std::set<std::string> skipNames = {"pack.ini", "mod.ini", "readme.txt", "readme.md", "changes.lua"};
    std::vector<std::pair<std::string, std::string>> r;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (it->is_directory(ec2)) {
            if (skipDirs.count(lower(it->path().filename().string()))) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec2)) continue;
        const std::string rel = fs::relative(it->path(), dir, ec2).generic_string();
        if (ec2 || rel.empty()) continue;
        const std::string low = lower(rel);
        bool skip = skipNames.count(baseOf(low)) > 0;
        for (const std::string& e : skipExt)
            if (low.size() >= e.size() && low.compare(low.size() - e.size(), e.size(), e) == 0) skip = true;
        if (!skip) r.emplace_back(rel, it->path().string());
    }
    std::sort(r.begin(), r.end());
    return r;
}

// One pack file `over` that answers for the game path `rel` (by name: for every game file of that name).  When the pack's rules say to MIX
// it (pack_merge.h) and a game file is there to mix into, the combined text is written under `work` and one override per game path
// is added to `out`.  The text under it is read now, so call this BEFORE the pack's own files are mounted.  Returns the files mixed.
size_t planMerge(const Vfs& vfs, const packmerge::Config& cfg, const std::string& rel, const std::string& over, bool byName,
                 const fs::path& work, Report& rep, const std::string& packId, std::vector<Vfs::Override>& out, std::set<std::string>& mixedRel) {
    const packmerge::Rule rule = packmerge::ruleFor(cfg, rel);
    if (rule.how == packmerge::How::Replace) return 0;
    const std::vector<std::string> targets = vfs.logicalPaths(byName ? baseOf(rel) : rel);
    if (targets.empty()) return 0;   // nothing below it: the pack's file is simply new
    std::map<std::string, std::vector<std::string>> byReal;   // what is below -> the game paths that show it
    for (const std::string& t : targets) {
        std::string real;
        if (vfs.resolve(t, &real)) byReal[real].push_back(t);
    }
    const std::string overText = readText(over);
    size_t n = 0;
    for (const auto& [real, games] : byReal) {
        std::string base, merged, err;
        std::vector<uint8_t> raw;
        if (!readFile(real, raw)) continue;
        base.assign(raw.begin(), raw.end());
        if (!packmerge::merge(rel, rule, base, overText, merged, &err)) {
            rep.warnings.push_back("pack " + packId + ": " + rel + " not mixed (" + err + "): its file replaces the other whole");
            continue;
        }
        std::error_code ec;
        fs::create_directories(work, ec);
        const fs::path file = work / (std::to_string(out.size()) + "_" + safeName(baseOf(rel)));
        std::FILE* f = std::fopen(file.string().c_str(), "wb");
        if (!f) continue;
        const bool ok = std::fwrite(merged.data(), 1, merged.size(), f) == merged.size();
        std::fclose(f);
        if (!ok) continue;
        for (const std::string& g : games) {
            out.push_back({g, file.string()});
            mixedRel.insert(g);
        }
        ++n;
    }
    if (n) rep.log.push_back("pack " + packId + ": " + rel + " mixed (" + packmerge::howName(rule.how) + ") into " + std::to_string(n) + " file(s)");
    return n;
}

// A resource pack, mounted one of two ways (the game's own files are never written to, either way):
//   changes.lua  -> "rules": only what its rules say changes (packchanges::run), as overrides in the Vfs
//   no changes.lua -> "name": every file replaces the game's files of the same name; .foit / .art / .art-pt (mods) and the pack's own
//                     metadata are left out
// Returns the files / rules that took effect (0 = nothing mounted).
size_t mountPack(Vfs& vfs, const mods::ModInfo& m, Item* item, Report& rep, PackEnv& env, const packmerge::Config& cfg, std::vector<modcheck::Layer>& layers) {
    const fs::path changes = fs::path(m.dir) / "changes.lua";
    std::error_code ec;
    const fs::path mixDir = fs::path(env.cacheRoot) / safeName(fs::path(m.dir).filename().string()) / "merge";
    fs::remove_all(mixDir, ec);
    std::vector<Vfs::Override> mixed;
    std::set<std::string> mixedRel;
    if (fs::is_regular_file(changes, ec)) {
        if (item) item->mode = "rules";
        const fs::path work = fs::path(env.cacheRoot) / safeName(fs::path(m.dir).filename().string());
        packchanges::Context ctx;
        ctx.packDir = m.dir;
        ctx.tempDir = (work / "tmp").string();
        ctx.outDir = (work / "out").string();
        ctx.base = &vfs;
        ctx.gameDir = env.gameDir;
        ctx.modVersion = [&env](const std::string& n) {
            const auto f = env.versions.find(lower(n));
            return f == env.versions.end() ? 0.0 : f->second;
        };
        const packchanges::Result r = packchanges::run(changes.string(), ctx);
        for (const std::string& l : r.log) rep.log.push_back("pack " + m.id + ": " + l);
        for (const std::string& w : r.warnings) rep.warnings.push_back("pack " + m.id + ": " + w);
        if (item && !fs::exists(fs::path(m.dir) / "pack.ini", ec) && !fs::exists(fs::path(m.dir) / "mod.ini", ec)) {
            // no ini of its own: what the script says about itself (ModInfo) is the pack's name / version / author
            if (!r.name.empty()) item->name = r.name;
            if (!r.version.empty()) item->version = r.version;
            if (!r.author.empty()) item->author = r.author;
            if (!r.description.empty()) item->description = r.description;
        }
        if (!r.ok) {
            if (item) {
                item->blocked = true;
                item->problem = "changes.lua: " + r.error;
            }
            rep.warnings.push_back("pack " + m.id + ": changes.lua failed, nothing of this pack is applied: " + r.error);
            return 0;
        }
        std::vector<Vfs::Override> rules;
        for (const packchanges::Rule& x : r.rules) rules.push_back({x.game, x.real});
        size_t merged = 0;
        for (const packchanges::Rule& x : r.rules) merged += planMerge(vfs, cfg, x.game, x.real, false, mixDir, rep, m.id, mixed, mixedRel);
        const size_t n = vfs.mountOverrides(rules);
        if (n) rep.log.push_back("pack " + m.id + " (changes.lua): " + std::to_string(n) + " rule(s)");
        vfs.mountOverrides(mixed);   // after the rules, so what was mixed is what answers
        if (item) item->merged = merged;
        if (merged) env.mixed["pack:" + m.id] = mixedRel;
        return n;
    }
    if (item) item->mode = "name";
    size_t merged = 0;
    for (const auto& [rel, real] : packFiles(m.dir)) merged += planMerge(vfs, cfg, rel, real, true, mixDir, rep, m.id, mixed, mixedRel);   // reads what is below first
    std::vector<std::string> skipped;
    const size_t files = vfs.mountByName(m.dir, {".foit", ".art", ".art-pt", "pack.ini", "mod.ini", "readme.txt", "readme.md", "changes.lua"}, &skipped);
    std::string ignored;
    size_t mods = 0;
    for (const std::string& s : skipped) {
        const std::string l = lower(s);
        if (!packchanges::isModFile(l)) continue;
        if (mods++ < 3) ignored += (ignored.empty() ? "" : ", ") + s;
    }
    if (mods)
        rep.warnings.push_back("pack " + m.id + ": " + std::to_string(mods) + " mod file(s) left out (" + ignored + (mods > 3 ? ", ..." : "") +
                               "): .foit / .art / .art-pt are mods, put them in mods/modscustom");
    vfs.mountOverrides(mixed);   // after the pack's files, so what was mixed is what answers
    if (item) item->merged = merged;
    if (merged) env.mixed["pack:" + m.id] = mixedRel;
    if (files) {
        layers.push_back({"pack:" + m.id, m.dir});
        rep.log.push_back("pack " + m.id + " (by name): " + std::to_string(files) + " file(s)" + (merged ? ", " + std::to_string(merged) + " mixed" : ""));
    }
    return files;
}

// Lists every folder of `set` for the Mods screen and, when `mount`, mounts the enabled ones (priority order).
size_t mountSet(Vfs& vfs, const mods::ModSet& set, const char* kind, Report& rep, bool mount, std::vector<modcheck::Layer>& layers,
                std::vector<StageMod>* stageOut = nullptr, PackEnv* env = nullptr) {
    const size_t first = rep.items.size();
    for (const mods::ModInfo& m : set.mods) {
        Item it;
        it.kind = kind;
        it.id = m.id;
        it.name = m.name;
        it.version = m.version;
        it.author = m.author;
        it.description = m.description;
        it.dir = m.dir;
        it.priority = m.priority;
        it.files = m.fileCount;
        it.enabled = m.enabled;
        rep.items.push_back(std::move(it));
    }
    if (!mount) return 0;
    size_t n = 0;
    const std::vector<mods::ModInfo> order = mods::loadOrder(set);
    std::map<std::string, int> on;   // lowercase id of every pack that is switched on -> its priority
    for (const mods::ModInfo& m : order) on[lower(m.id)] = m.priority;
    for (mods::ModInfo m : order) {
        static const std::vector<std::string> kModExt = {".foit", ".art", ".art-pt"};   // not used by a resource pack, so they cannot block it
        if (!mods::vet(m, kind, &rep.log, std::string(kind) == "pack" ? kModExt : std::vector<std::string>())) {   // damaged / altering folders are never mounted
            for (size_t i = first; i < rep.items.size(); ++i)
                if (rep.items[i].dir == m.dir) {
                    rep.items[i].blocked = true;
                    rep.items[i].problem = m.problem;
                }
            rep.warnings.push_back(std::string(kind) + " " + m.id + ": " + m.problem);
            continue;
        }
        size_t files;
        if (env && std::string(kind) == "pack") {
            Item* self = nullptr;
            for (size_t i = first; i < rep.items.size(); ++i)
                if (rep.items[i].dir == m.dir) self = &rep.items[i];
            std::vector<std::string> cfgWarn;
            const packmerge::Config cfg = packmerge::parseConfig(packIniText(m.dir), &cfgWarn);
            for (const std::string& w : cfgWarn) rep.warnings.push_back("pack " + m.id + ": pack.ini " + w);
            std::string why;
            for (const std::string& r : cfg.requires_) {
                const auto f = on.find(r);
                if (f == on.end()) why += (why.empty() ? "needs pack " : ", ") + r + " (not installed or switched off)";
                else if (f->second > m.priority)
                    rep.warnings.push_back("pack " + m.id + ": needs " + r + " but loads before it (priority " + std::to_string(m.priority) + " < " + std::to_string(f->second) + "): raise its priority");
            }
            for (const std::string& c : cfg.conflicts)
                if (c != lower(m.id) && on.count(c)) why += (why.empty() ? "cannot be used together with " : ", ") + c;
            if (!why.empty()) {
                if (self) self->problem = why;
                rep.warnings.push_back("pack " + m.id + " NOT loaded: " + why);
                continue;
            }
            files = mountPack(vfs, m, self, rep, *env, cfg, layers);
            if (files == 0) continue;
            ++n;
            if (self) self->mounted = true;
            continue;   // mountPack logged and registered the layer
        }
        files = vfs.mount(m.dir);
        if (files == 0) continue;
        ++n;
        layers.push_back({std::string(kind) + ":" + m.id, m.dir});
        for (size_t i = first; i < rep.items.size(); ++i)
            if (rep.items[i].dir == m.dir) rep.items[i].mounted = true;
        if (stageOut) {
            StageMod sm;
            sm.id = fs::path(m.dir).filename().string();
            sm.dir = m.dir;
            const auto ini = readIni(fs::path(m.dir) / "stage.ini");
            const auto it = ini.find("name");
            sm.name = (it != ini.end() && !it->second.empty()) ? it->second : m.name;
            stageOut->push_back(std::move(sm));
        }
        rep.log.push_back(std::string(kind) + " " + m.id + " (prio " + std::to_string(m.priority) + "): " + std::to_string(files) + " file(s)");
    }
    return n;
}

}  // namespace

Layout layout(const std::string& gameDir) {
    const fs::path r = fs::path(gameDir) / "mods";
    Layout l;
    l.root = r.string();
    l.pal = (r / "pal").string();
    l.custom = (r / "modscustom").string();
    l.stages = (r / "stages").string();
    l.jpVoice = (r / "sdvoice_japanese").string();
    l.packs = (r / "resourcepacks").string();
    l.cache = (r / ".cache" / "pal").string();
    return l;
}

bool ensure(const std::string& gameDir, std::vector<std::string>* log) {
    if (gameDir.empty()) return false;
    const Layout l = layout(gameDir);
    std::error_code ec;
    const struct { const std::string* dir; const char* readme; } dirs[] = {
        {&l.pal, kPalReadme}, {&l.custom, kCustomReadme}, {&l.stages, kStagesReadme}, {&l.jpVoice, kVoiceReadme}, {&l.packs, kPacksReadme}};
    bool ok = true;
    for (const auto& d : dirs) {
        const bool existed = fs::is_directory(*d.dir, ec);
        fs::create_directories(*d.dir, ec);
        if (!fs::is_directory(*d.dir, ec)) {
            ok = false;
            continue;
        }
        writeIfMissing(fs::path(*d.dir) / "README.txt", d.readme);
        if (!existed && log) log->push_back("created " + *d.dir);
    }
    if (ok) writeIfMissing(fs::path(l.root) / "README.txt", kRootReadme);
    for (const std::string& name : palmod::rosterFolders()) {   // mods/pal/<Character>/ so the palettes are kept apart by character
        const fs::path d = fs::path(l.pal) / name;
        if (!fs::is_directory(d, ec)) fs::create_directories(d, ec);
    }
    return ok;
}

bool setItemEnabled(const std::string& gameDir, const Item& item, bool enabled) {
    const Layout l = layout(gameDir);
    const fs::path iniPath = fs::path(l.root) / "mods.ini";
    std::string text = readText(iniPath);
    // mods.ini "disabled = a, b": the single place a folder is switched off from, so the folder itself stays as the author made it
    std::vector<std::string> ids;
    {
        const auto cfg = readIni(iniPath);
        const auto f = cfg.find("disabled");
        if (f != cfg.end()) {
            size_t pos = 0;
            const std::string& v = f->second;
            while (pos <= v.size()) {
                size_t c = v.find(',', pos);
                if (c == std::string::npos) c = v.size();
                const std::string t = trimmed(v.substr(pos, c - pos));
                if (!t.empty() && lower(t) != lower(item.id)) ids.push_back(t);
                pos = c + 1;
            }
        }
    }
    if (!enabled) ids.push_back(item.id);
    std::string joined;
    for (const std::string& s : ids) joined += (joined.empty() ? "" : ", ") + s;
    std::error_code ec;
    fs::create_directories(l.root, ec);
    text = iniWith(text, "disabled", joined, true);
    bool ok = writeText(iniPath, text);
    if (enabled) {   // a mod.ini / pack.ini with "enabled = 0" also has to go back on
        for (const char* name : {"mod.ini", "pack.ini"}) {
            const fs::path p = fs::path(item.dir) / name;
            if (!fs::exists(p, ec)) continue;
            const auto cfg = readIni(p);
            const auto f = cfg.find("enabled");
            if (f == cfg.end()) continue;
            const std::string v = lower(f->second);
            if (v == "0" || v == "false" || v == "no" || v == "off") ok = writeText(p, iniWith(readText(p), "enabled", "1")) && ok;
        }
    }
    return ok;
}

Report mountAll(Vfs& vfs, const std::string& gameDir, bool japaneseVoices) {
    Report rep;
    rep.layoutOk = ensure(gameDir, &rep.log);
    const Layout l = layout(gameDir);
    const std::string ini = (fs::path(l.root) / "mods.ini").string();
    const auto cfg = readIni(ini);
    bool enabled = true;
    if (const auto it = cfg.find("enabled"); it != cfg.end()) {
        const std::string v = lower(it->second);
        enabled = !(v == "0" || v == "false" || v == "no" || v == "off");
    }
    if (const auto it = cfg.find("japanese_voices"); it != cfg.end()) {
        const std::string v = lower(it->second);
        if (v == "1" || v == "true" || v == "yes" || v == "on") japaneseVoices = true;
        else if (v == "0" || v == "false" || v == "no" || v == "off") japaneseVoices = false;
    }
    // palettes are plain data (they do not override files), but "enabled = 0" switches them off as well
    if (enabled) {
        bool organise = true;
        if (const auto it = cfg.find("organize_pal"); it != cfg.end()) {
            const std::string v = lower(it->second);
            organise = !(v == "0" || v == "false" || v == "no" || v == "off");
        }
        if (organise) palmod::organise(l.pal, &rep.log);   // loose *.pal files go to mods/pal/<Character>/
        rep.pal = palmod::scan(l.pal, l.cache);
    }
    for (const std::string& w : rep.pal.warnings) rep.warnings.push_back("pal: " + w);
    rep.allEnabled = enabled;
    rep.root = l.root;
    if (!enabled) rep.warnings.push_back("mods/mods.ini has enabled = 0: mods, packs, stages and palettes are off");
    if (enabled)   // .zip mods / packs / stages become folders first (zipread.h)
        for (const std::string& d : {l.packs, l.custom, l.stages}) unzipIn(d, rep.log, rep.warnings);
    const mods::ModSet packs = mods::scanFolder(l.packs, ini);
    const mods::ModSet custom = mods::scanFolder(l.custom, ini);
    const mods::ModSet stages = mods::scanFolder(l.stages, ini);
    for (const auto* s : {&packs, &custom, &stages})
        for (const std::string& w : s->warnings)
            if (w.find("enabled=0") == std::string::npos) rep.warnings.push_back(w);
    std::vector<modcheck::Layer> layers;   // mount order, later wins
    PackEnv env;
    env.gameDir = gameDir;
    env.cacheRoot = (fs::path(l.root) / ".cache" / "changes").string();
    for (const auto* s : {&packs, &custom, &stages})
        for (const mods::ModInfo& m : s->mods) {
            double v = std::atof(m.version.c_str());
            if (v <= 0) v = 1;
            env.versions[lower(m.id)] = v;
            env.versions[lower(m.name)] = v;
        }
    rep.packs = mountSet(vfs, packs, "pack", rep, enabled, layers, nullptr, &env);
    rep.mods = mountSet(vfs, custom, "mod", rep, enabled, layers);
    rep.stages = mountSet(vfs, stages, "stage", rep, enabled, layers, &rep.stageMods);
    if (!enabled) return rep;
    if (layers.size() > 1) rep.conflicts = modcheck::conflicts(layers);
    for (modcheck::Conflict& c : rep.conflicts) {
        const auto f = env.mixed.find(c.winner);
        if (f == env.mixed.end()) continue;
        const std::string p = normalizeKey(c.path);
        for (const std::string& g : f->second)
            if (g == p || (g.size() > p.size() && g.compare(g.size() - p.size(), p.size(), p) == 0 && g[g.size() - p.size() - 1] == '/') ||
                (p.size() > g.size() && p.compare(p.size() - g.size(), g.size(), g) == 0 && p[p.size() - g.size() - 1] == '/')) c.mixed = true;
    }
    if (japaneseVoices) {
        Vfs probe;  // README.txt does not count (and is not mounted when it is the only thing there)
        probe.mount(l.jpVoice);
        for (const std::string& f : probe.files())
            if (lower(fs::path(f).filename().string()) != "readme.txt") ++rep.voiceFiles;
        if (rep.voiceFiles) {
            vfs.mount(l.jpVoice);
            rep.log.push_back("sdvoice_japanese: " + std::to_string(rep.voiceFiles) + " file(s)");
        }
    }
    return rep;
}

}  // namespace modsdir
}  // namespace zengine
