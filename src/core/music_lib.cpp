// music_lib.cpp - see music_lib.h
#include "music_lib.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

#include "text_formats.h"

namespace zengine {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string baseName(const std::string& path) {
    const size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string stemOf(const std::string& file) {
    const std::string b = baseName(file);
    const size_t d = b.find_last_of('.');
    return d == std::string::npos ? b : b.substr(0, d);
}

bool endsWith(const std::string& s, const char* suf) {
    const std::string x = suf;
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

bool startsWith(const std::string& s, const std::string& pre) { return s.rfind(pre, 0) == 0; }

std::string normKey(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return o;
}

// "skull02_1" -> names the Vfs finds ("skull02_1.wav"); "" when there is no such file.  A name written the way a person types it
// ("Hymn For The Weekend Final Remake Intro") also finds "Hymn For The Weekend Final Remake Intro .wav": same letters and digits,
// whatever the case, spaces and punctuation (checked against every .wav only when the exact name is not there).
struct WavFinder {
    const Vfs& vfs;
    std::map<std::string, std::string> byNorm;
    bool built = false;
    std::string find(const std::string& stem) {
        if (stem.empty()) return {};
        const std::string cand[] = {stem + ".wav", stem};
        for (const std::string& c : cand)
            if (c.find('.') != std::string::npos && vfs.exists(c)) return c;
        if (!built) {
            built = true;
            for (const std::string& real : vfs.listByExt(".wav")) byNorm.emplace(normKey(stemOf(real)), baseName(real));
        }
        const auto it = byNorm.find(normKey(stem));
        return it == byNorm.end() ? std::string() : it->second;
    }
};

std::string titleCase(std::string s) {
    bool up = true;
    for (char& c : s) {
        if (c == '_') c = ' ';
        c = static_cast<char>(up ? std::toupper(static_cast<unsigned char>(c)) : c);
        up = c == ' ';
    }
    return s;
}

// Level (.lvl) stems that differ from the world names of music.ini.
const std::map<std::string, std::string>& levelAliases() {
    static const std::map<std::string, std::string> m = {
        {"arena", "arena"},           {"arenaevo", "arena"},        {"arenasgcs", "arena"},       {"arenasgcs2024", "arena"},
        {"arenaempty", "emptyarena"}, {"asglabs", "asglab"},        {"asglabsempty", "emptylab8"}, {"atrium", "glasscanopy"},
        {"bathhouseblood", "bathhouse"}, {"bathhousesekh", "bathhouse"}, {"cathedralempty", "emptycathedral"},
        {"medicioffice", "medicitower"}, {"riverkingcasino", "riverking"}, {"casino", "riverking"},
        {"rooftopsday", "meridianrooftops"}, {"nightmarecrest", "maplecrestmare"},
        {"training", "trainingstage"},   // training_3d.lvl; stages.ini calls it Training_Stage
    };
    return m;
}

}  // namespace

std::string MusicLibrary::normalise(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return o;
}

std::string MusicLibrary::levelWorld(const std::string& levelName) {
    std::string n = lower(levelName);
    const size_t p3d = n.find("_3d");
    if (p3d != std::string::npos) n = n.substr(0, p3d);
    const std::string k = normalise(n);
    const auto it = levelAliases().find(k);
    return it != levelAliases().end() ? it->second : k;
}

// Themes the loader itself places, written in music.ini's own format.  They only count when their files are in the user's data
// (nothing is bundled): "Final Remake" is the music of the test level - Training Stage, and the game's test1 / test2.  Any music.ini
// the user adds (a pack, a mod folder) overrides it for the same stage.
static const char* const kBonusMusicIni =
    "Track: Hymn For The Weekend (Final Remake)\n"
    "Stage: Training_Stage, Test1, Test2\n"
    "Files: Hymn For The Weekend Final Remake Intro, Hymn For The Weekend Final Remake Loop\n";

void MusicLibrary::scan(const Vfs& vfs) {
    entries_.clear();
    unsupported_ = 0;
    std::set<std::string> used;   // loop / intro stems music.ini already names (letters and digits only)
    WavFinder wavs{vfs, {}, false};
    auto addTracks = [&](const std::string& ini, int rank, bool onlyIfPresent) {
        for (const MusicTrack& t : parseMusic(ini)) {
            if (t.files.empty()) continue;
            MusicEntry e;
            e.title = trimmed(t.name);
            e.kind = e.title.find("(cutscene)") != std::string::npos ? MusicEntry::Kind::Cutscene : MusicEntry::Kind::Stage;
            e.rank = rank;
            for (const std::string& s : t.stages) e.stages.push_back(normalise(s));
            e.loopStem = trimmed(t.files.back());
            if (t.files.size() > 1) e.introStem = trimmed(t.files.front());
            if (e.introStem == "blankmusic") e.introStem.clear();
            e.loopFile = wavs.find(e.loopStem);
            if (e.loopFile.empty() && (vfs.exists(e.loopStem + ".ogg") || vfs.exists(e.loopStem + ".mp3"))) ++unsupported_;
            if (!e.introStem.empty()) e.introFile = wavs.find(e.introStem);
            if (onlyIfPresent && !e.present()) continue;
            used.insert(normKey(e.loopStem));
            if (!e.introStem.empty()) used.insert(normKey(e.introStem));
            bool dup = false;   // the same file read from two copies of music.ini
            for (const MusicEntry& o : entries_)
                if (o.loopStem == e.loopStem && o.introStem == e.introStem && o.stages == e.stages) dup = true;
            if (!dup) entries_.push_back(std::move(e));
        }
    };
    // Every music.ini the data has, not just the one the Vfs resolves (a pack's copy shadows the game's): the resolved one - the
    // highest-priority mount - ranks highest, the others next, the loader's own bonus themes last.
    std::string winner, ini;
    vfs.resolve("music.ini", &winner);
    std::vector<std::string> others = vfs.findByName("music.ini");
    std::sort(others.begin(), others.end());
    for (const std::string& path : others) {
        if (path == winner) continue;
        std::vector<uint8_t> bytes;
        if (readFile(path, bytes)) addTracks(std::string(bytes.begin(), bytes.end()), 0, false);
    }
    if (!winner.empty()) {
        std::vector<uint8_t> bytes;
        if (readFile(winner, bytes)) addTracks(std::string(bytes.begin(), bytes.end()), 1, false);
    }
    addTracks(kBonusMusicIni, -1, true);
    // every other long .wav that is not a sound effect or a voice: grouped by name into intro + loop
    struct Group { std::string intro, loop, any; };
    std::map<std::string, Group> groups;
    std::error_code ec;
    for (const std::string& real : vfs.listByExt(".wav")) {
        const std::string low = lower(real);
        if (low.find("uisound") != std::string::npos || low.find("story_vo") != std::string::npos || low.find("/lang_") != std::string::npos ||
            low.find("\\lang_") != std::string::npos || low.find("/sfx") != std::string::npos || low.find("/voices") != std::string::npos)
            continue;
        const std::string stem = lower(stemOf(real));
        if (used.count(normKey(stem))) continue;
        if (endsWith(stem, "_en") || endsWith(stem, "_jp") || low.find("/vo/") != std::string::npos || low.find("_vo/") != std::string::npos)
            continue;   // a voice line ("AnnieFight1_Intro_0_EN.wav")
        const auto sz = std::filesystem::file_size(real, ec);
        if (ec || sz < 200u * 1024u) continue;   // a jingle, an effect or a voice
        std::string base = stem;
        bool isIntro = false, isLoop = false;
        if (endsWith(stem, "_intro")) { base = stem.substr(0, stem.size() - 6); isIntro = true; }
        else if (endsWith(stem, "_loop")) { base = stem.substr(0, stem.size() - 5); isLoop = true; }
        else if (endsWith(stem, "_1")) { base = stem.substr(0, stem.size() - 2); isIntro = true; }
        else if (endsWith(stem, "_2")) { base = stem.substr(0, stem.size() - 2); isLoop = true; }
        Group& g = groups[base];
        if (isIntro) g.intro = stem;
        else if (isLoop) g.loop = stem;
        else g.any = stem;
    }
    for (const auto& kv : groups) {
        const Group& g = kv.second;
        MusicEntry e;
        e.loopStem = !g.loop.empty() ? g.loop : (!g.any.empty() ? g.any : g.intro);
        if (!g.loop.empty()) e.introStem = !g.intro.empty() ? g.intro : g.any;   // "sg_attract_c.wav" + "sg_attract_c_loop.wav"
        e.loopFile = wavs.find(e.loopStem);
        if (!e.introStem.empty()) e.introFile = wavs.find(e.introStem);
        if (e.introStem == e.loopStem) e.introStem.clear(), e.introFile.clear();
        e.title = titleCase(kv.first);
        e.kind = kv.first == "menu" ? MusicEntry::Kind::Menu : kv.first == "select" ? MusicEntry::Kind::Select : MusicEntry::Kind::Other;
        entries_.push_back(std::move(e));
    }
    std::printf("[music] %s\n", summary().c_str());
}

size_t MusicLibrary::presentCount() const {
    size_t n = 0;
    for (const MusicEntry& e : entries_) n += e.present() ? 1 : 0;
    return n;
}

std::string MusicLibrary::summary() const {
    std::string s = std::to_string(entries_.size()) + " themes known, " + std::to_string(presentCount()) + " with files on disk";
    std::string missing;
    int n = 0;
    for (const MusicEntry& e : entries_)
        if (!e.present() && n++ < 6) missing += (missing.empty() ? "" : ", ") + e.loopStem;
    if (n > 0) s += " (missing: " + missing + (n > 6 ? ", ..." : "") + ")";
    if (unsupported_ > 0) s += "; " + std::to_string(unsupported_) + " are .ogg / .mp3 files, which this player cannot read (convert them to .wav)";
    return s;
}

std::string MusicLibrary::stems(size_t max) const {
    std::string s;
    size_t n = 0;
    for (const MusicEntry& e : entries_) {
        if (!e.present()) continue;
        if (n++ >= max) {
            s += ", ...";
            break;
        }
        s += (s.empty() ? "" : ", ") + e.loopStem;
    }
    return s.empty() ? std::string("none") : s;
}

int MusicLibrary::forLevel(const std::string& levelName) const {
    const std::string world = levelWorld(levelName);
    int best = -1;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const MusicEntry& e = entries_[i];
        if (e.kind != MusicEntry::Kind::Stage || !e.present()) continue;
        for (const std::string& s : e.stages)
            if (s == world && (best < 0 || e.rank >= entries_[static_cast<size_t>(best)].rank)) best = static_cast<int>(i);
    }
    return best;
}

int MusicLibrary::forFight(const std::string& levelName) const {
    const int exact = forLevel(levelName);
    if (exact >= 0) return exact;
    std::vector<int> pool;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].kind == MusicEntry::Kind::Stage && entries_[i].present() && entries_[i].rank >= 0) pool.push_back(static_cast<int>(i));   // the loader's own test-level theme is never a stand-in
    if (pool.empty()) return select();   // no stage theme on disk at all: the character-select theme, not silence
    uint32_t h = 2166136261u;   // FNV-1a: the same stage always gets the same stand-in
    for (char c : levelWorld(levelName)) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return pool[h % pool.size()];
}

// A theme whose file name contains one of the words (in order of the words), among those on disk.
int MusicLibrary::findByTokens(const std::vector<std::string>& wantAny, bool cutscenePreferred) const {
    for (const std::string& w : wantAny) {
        int best = -1;
        for (size_t i = 0; i < entries_.size(); ++i) {
            const MusicEntry& e = entries_[i];
            if (!e.present()) continue;
            if (lower(e.loopStem).find(w) == std::string::npos && normalise(e.title).find(w) == std::string::npos) continue;
            if (best < 0 || (cutscenePreferred && e.kind == MusicEntry::Kind::Cutscene && entries_[static_cast<size_t>(best)].kind != MusicEntry::Kind::Cutscene))
                best = static_cast<int>(i);
        }
        if (best >= 0) return best;
    }
    return -1;
}

int MusicLibrary::forCue(const std::string& codeIn) const {
    const std::string code = lower(trimmed(codeIn));
    if (code.empty() || code == "stop" || code == "off") return -1;
    if (code == "news") return findByTokens({"news"}, false);
    if (code == "boss" || code == "arrhythmia") {
        const int w = forLevel("falling_wreckage");
        return w >= 0 ? w : findByTokens({"boss", "arrhythmia"}, false);
    }
    if (code == "attract") return findByTokens({"attract"}, false);
    if (code == "cat" || code == "cat2" || code == "bloodbath" || code == "annie") return findByTokens({code, code.substr(0, 3)}, false);
    // "02" / "24" / "K" / "R": the file names carry them as sg<code>_..., sg_<code>..., skull<code>_..., skull_<code>_...
    int bestIdx = -1, bestScore = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const MusicEntry& e = entries_[i];
        if (!e.present()) continue;
        const std::string stems[] = {lower(e.loopStem), lower(e.introStem)};
        int score = 0;
        for (const std::string& st : stems) {
            if (st.empty()) continue;
            for (const char* pre : {"sg", "sg_", "skull", "skull_", ""}) {
                const std::string p = std::string(pre) + code;
                if (!startsWith(st, p)) continue;
                const char nx = st.size() > p.size() ? st[p.size()] : '_';
                const bool boundary = nx == '_' || std::isdigit(static_cast<unsigned char>(nx)) || st.size() == p.size();
                if (!boundary) continue;
                // a letter code followed by a digit ("sg_k1") is a title, not a number: fine; a bare prefix-less match is the weakest
                score = std::max(score, pre[0] == 0 ? 2 : 4);
            }
        }
        if (score > 0 && (score > bestScore || (score == bestScore && e.kind == MusicEntry::Kind::Cutscene &&
                                                 entries_[static_cast<size_t>(bestIdx)].kind != MusicEntry::Kind::Cutscene))) {
            bestScore = score;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

int MusicLibrary::menu() const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].kind == MusicEntry::Kind::Menu && entries_[i].present()) return static_cast<int>(i);
    return -1;
}

int MusicLibrary::select() const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].kind == MusicEntry::Kind::Select && entries_[i].present()) return static_cast<int>(i);
    return menu();
}

int MusicLibrary::ending() const {
    const int e = findByTokens({"ending", "final", "outro", "epilog", "thanks", "victory"}, false);
    return e >= 0 ? e : credits();
}

int MusicLibrary::credits() const {
    const int c = findByTokens({"credit"}, false);
    if (c >= 0) return c;
    const int e = findByTokens({"ending", "final", "outro", "epilog"}, false);
    return e >= 0 ? e : menu();
}

int MusicLibrary::gallery() const {
    const int g = findByTokens({"gallery", "extras"}, false);
    return g >= 0 ? g : menu();
}

}  // namespace zengine
