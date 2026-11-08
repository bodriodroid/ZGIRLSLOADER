#include "gamedata.h"

#include "modloader.h"

#include <algorithm>
#include <map>
#include <set>

namespace zengine {

std::string normName(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) r.push_back(c);
    }
    return r;
}

namespace {

std::string baseOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string stemOf(const std::string& file) {
    size_t p = file.rfind('.');
    return p == std::string::npos ? file : file.substr(0, p);
}

std::string lowerOf(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool hasExt(const std::string& path, const char* ext) {
    const std::string l = lowerOf(path);
    const std::string e = ext;
    return l.size() >= e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0;
}

// remainder must fully match  p?\d{1,2}p?   -> returns the digits ("" if no match)
std::string paletteNumber(const std::string& rest) {
    size_t i = 0;
    if (i < rest.size() && rest[i] == 'p') ++i;
    size_t d0 = i;
    while (i < rest.size() && rest[i] >= '0' && rest[i] <= '9') ++i;
    const size_t nd = i - d0;
    if (nd < 1 || nd > 2) return "";
    std::string digits = rest.substr(d0, nd);
    if (i < rest.size() && rest[i] == 'p') ++i;
    return i == rest.size() ? digits : "";
}

}  // namespace

size_t GameData::mountMods(const std::string& gameDir, std::vector<std::string>* log) {
    return mods::apply(vfs_, gameDir, log);
}

modsdir::Report GameData::mountModsDir(const std::string& gameDir, bool japaneseVoices) {
    modsdir::Report r = modsdir::mountAll(vfs_, gameDir, japaneseVoices);
    palLib_ = r.pal;
    stageMods_ = r.stageMods;
    return r;
}

Inventory GameData::scan() const {
    Inventory inv;
    inv.files = vfs_.fileCount();
    for (const std::string& f : vfs_.files()) {
        if (hasExt(f, ".lua")) ++inv.luaFiles;
        else if (hasExt(f, ".ini")) ++inv.iniFiles;
        else if (hasExt(f, ".stm")) ++inv.stmFiles;
        else if (hasExt(f, ".seq")) ++inv.seqFiles;
        else if (hasExt(f, ".glsl")) ++inv.glslFiles;
        else if (hasExt(f, ".dds")) ++inv.ddsFiles;
        else if (hasExt(f, ".pcx")) ++inv.pcxFiles;
        else if (hasExt(f, ".gfs")) ++inv.gfsFiles;
    }

    // ---- characters: every .foit, matched with its .art / .art-pt / palettes
    std::set<std::string> seen;
    const std::vector<std::string> foits = vfs_.listByExt(".foit");
    const std::vector<std::string> dds = vfs_.listByExt(".dds");
    for (const std::string& fp : foits) {
        const std::string name = lowerOf(stemOf(baseOf(fp)));
        if (!seen.insert(name).second) continue;
        CharacterAssets c;
        c.name = name;
        vfs_.pick(name + ".foit", &c.foitPath);
        if (!vfs_.pick(name + ".art", &c.artPath))
            inv.warnings.push_back(name + ": .foit found but no " + name + ".art");
        vfs_.pick(name + ".art-pt", &c.artPtPath);

        const std::string n = normName(name);
        std::map<int, std::pair<std::string, std::string>> byNum;  // number -> {label, path}
        for (const std::string& dp : dds) {
            const std::string stem = normName(stemOf(baseOf(dp)));
            if (stem.compare(0, n.size(), n) != 0 || stem.size() == n.size()) continue;
            const std::string digits = paletteNumber(stem.substr(n.size()));
            if (digits.empty()) continue;
            const int num = std::stoi(digits);
            auto it = byNum.find(num);
            if (it == byNum.end() || dp.size() < it->second.second.size())
                byNum[num] = {std::to_string(num) + "p", dp};
        }
        for (auto& kv : byNum) c.palettes.push_back(kv.second);
        if (c.palettes.empty()) inv.warnings.push_back(name + ": no <name>_<N>p.dds palette found");
        // mods/pal/*.pal baked into palette textures: listed after the game's own colours, labelled "mod:<file name>"
        for (const palmod::Entry* e : palLib_.forCharacter(name)) c.palettes.push_back({"mod:" + e->label, e->dds});
        inv.characters.push_back(std::move(c));
    }

    // ---- text tables
    std::string text;
    if (vfs_.readText("stages.ini", text)) inv.stages = parseStages(text);
    if (vfs_.readText("music.ini", text)) inv.music = parseMusic(text);
    if (vfs_.readText("compile.ini", text)) inv.compileJobs = parseCompile(text);
    if (vfs_.readText("toc.ini", text)) {
        inv.tocFiles = parseToc(text);
        for (const std::string& f : inv.tocFiles) {
            if (vfs_.exists(f)) continue;
            // The game's own toc.ini says "Ms.Fortune.stm" while the file is "MsFortune.stm": compare names
            // with everything but letters and digits removed before declaring a file missing.
            bool loose = false;
            const std::string want = normName(stemOf(f));
            for (const std::string& real : vfs_.listByExt(".stm"))
                if (normName(stemOf(baseOf(real))) == want) { loose = true; break; }
            if (!loose) inv.warnings.push_back("toc.ini lists " + f + " but it was not found");
        }
    }

    // ---- UI scenes
    for (const std::string& gp : vfs_.listByExt(".gbs")) {
        SceneInfo s;
        s.path = gp;
        std::vector<uint8_t> buf;
        if (!readFile(gp, buf)) s.error = "cannot read file";
        else if (!parseGbs(buf.data(), buf.size(), s.gbs, &s.error)) {
            if (s.error.empty()) s.error = "parse failed";
        }
        if (!s.error.empty()) inv.warnings.push_back(baseOf(gp) + ": " + s.error);
        inv.scenes.push_back(std::move(s));
    }

    // ---- stages (*.lvl): bounds, spawn points, ambient light and the 2D backdrop texture
    for (const std::string& lp : vfs_.listByExt(".lvl")) {
        // a stage mod that ships its own "arena.lvl" shadows the game's: only the file the Vfs resolves counts (one entry per stage)
        std::string winner;
        if (vfs_.resolve(baseOf(lp), &winner) && !winner.empty() && winner != lp) continue;
        std::vector<uint8_t> buf;
        if (!readFile(lp, buf)) {
            inv.warnings.push_back(baseOf(lp) + ": cannot read file");
            continue;
        }
        LevelInfo lv = parseLevel(std::string(reinterpret_cast<const char*>(buf.data()), buf.size()),
                                  stemOf(baseOf(lp)), lp);
        if (lv.width <= 0 || lv.height <= 0) inv.warnings.push_back(baseOf(lp) + ": no StageSize");
        if (lv.start1 == 0 && lv.start2 == 0) inv.warnings.push_back(baseOf(lp) + ": no Start1/Start2");
        inv.levels.push_back(std::move(lv));
    }
    return inv;
}

bool GameData::loadCharacter(const CharacterAssets& c, ArtFile& out, std::string* err) const {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    std::vector<uint8_t> foit, art, pt;
    if (c.foitPath.empty() || !readFile(c.foitPath, foit)) return fail("cannot read " + c.foitPath);
    if (!c.artPath.empty()) readFile(c.artPath, art);
    if (!c.artPtPath.empty()) readFile(c.artPtPath, pt);
    // A character is usable as soon as ONE of the two data files is present: records that point into a
    // missing file are simply not validated (ArtFile::hasArt()/hasArtPt() tell the caller what it got).
    if (art.empty() && pt.empty()) return fail("neither .art nor .art-pt could be read for " + c.name);
    if (!out.load(std::move(foit), std::move(art), std::move(pt))) return fail("no image records found in the .foit");
    return true;
}

bool GameData::loadPalette(const std::string& ddsPath, Palette& out, std::string* err) {
    std::vector<uint8_t> buf;
    if (!readFile(ddsPath, buf)) {
        if (err) *err = "cannot read " + ddsPath;
        return false;
    }
    return readDds(buf.data(), buf.size(), out, err);
}

void diagnose(const Inventory& inv, const Vfs& vfs, bool audioDeviceOk, err::Registry& reg) {
    // audio: voices / sound effects are .wav files, the themes come from music.ini
    const size_t wavs = vfs.listByExt(".wav").size();
    if (wavs == 0) reg.raise(err::kNoAudio, "no .wav files: voices and sound effects are not used");
    if (inv.music.empty()) reg.raise(err::kNoAudio, "no music tracks found (music.ini / theme files)");
    if (!audioDeviceOk) reg.raise(err::kNoAudio, "no audio output device: the mixer is silent");
    // everything else the fights and menus are built from
    if (inv.characters.empty()) reg.raise(err::kNoData, "no characters (.foit / .art): the fighters are not available");
    if (inv.scenes.empty()) reg.raise(err::kNoData, "no UI scenes (.gbs): menus and HUD use the built-in layout");
    if (inv.levels.empty() && inv.stages.empty()) reg.raise(err::kNoData, "no stages (.lvl): matches use a generated stage");
    if (inv.ddsFiles == 0) reg.raise(err::kNoData, "no textures (.dds): art is not available");
}

}  // namespace zengine
