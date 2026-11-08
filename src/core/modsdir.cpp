#include "modsdir.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>

#include "modloader.h"

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
    "One folder per resource pack, mirroring the game's layout (textures, sounds, text).  pack.ini is optional\n"
    "(name / description / priority / enabled).  Packs are mounted over the game but under modscustom;\n"
    "the original files are never modified - remove the folder and everything is back.\n\n"
    "Una carpeta por pack. Reemplaza contenido sin tocar el core ni los .dds originales: borra la carpeta y vuelve todo.\n";

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

// Lists every folder of `set` for the Mods screen and, when `mount`, mounts the enabled ones (priority order).
size_t mountSet(Vfs& vfs, const mods::ModSet& set, const char* kind, Report& rep, bool mount, std::vector<modcheck::Layer>& layers,
                std::vector<StageMod>* stageOut = nullptr) {
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
    for (mods::ModInfo m : mods::loadOrder(set)) {
        if (!mods::vet(m, kind, &rep.log)) {   // damaged / altering folders are never mounted
            for (size_t i = first; i < rep.items.size(); ++i)
                if (rep.items[i].dir == m.dir) {
                    rep.items[i].blocked = true;
                    rep.items[i].problem = m.problem;
                }
            rep.warnings.push_back(std::string(kind) + " " + m.id + ": " + m.problem);
            continue;
        }
        const size_t files = vfs.mount(m.dir);
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
    const mods::ModSet packs = mods::scanFolder(l.packs, ini);
    const mods::ModSet custom = mods::scanFolder(l.custom, ini);
    const mods::ModSet stages = mods::scanFolder(l.stages, ini);
    for (const auto* s : {&packs, &custom, &stages})
        for (const std::string& w : s->warnings)
            if (w.find("enabled=0") == std::string::npos) rep.warnings.push_back(w);
    std::vector<modcheck::Layer> layers;   // mount order, later wins
    rep.packs = mountSet(vfs, packs, "pack", rep, enabled, layers);
    rep.mods = mountSet(vfs, custom, "mod", rep, enabled, layers);
    rep.stages = mountSet(vfs, stages, "stage", rep, enabled, layers, &rep.stageMods);
    if (!enabled) return rep;
    if (layers.size() > 1) rep.conflicts = modcheck::conflicts(layers);
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
