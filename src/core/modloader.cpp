#include "modloader.h"

#include "errcode.h"
#include "modcheck.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace zengine {
namespace mods {

namespace {

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string{};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// Flat "key = value" reader.  '#' starts a comment.
std::map<std::string, std::string> readIni(const std::string& path) {
    std::map<std::string, std::string> out;
    std::string text;
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
    std::fclose(fp);

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        const std::string line =
            trim(text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos));
        if (!line.empty() && line[0] != '#' && line[0] != ';') {
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                out[lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
            }
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    // Tolerate a stray UTF-8 BOM on the first key.
    if (!out.empty()) {
        auto it = out.begin();
        const std::string k = it->first;
        if (k.size() > 3 && k.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            out[k.substr(3)] = it->second;
            out.erase(it);
        }
    }
    return out;
}

bool isDir(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> listDir(const std::string& p) {
    std::vector<std::string> out;
    DIR* d = ::opendir(p.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        out.push_back(n);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool writeText(const std::string& path, const std::string& text) {
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), fp) == text.size();
    std::fclose(fp);
    return ok;
}

void mkdirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        if (c == '/' || c == '\\') {
            if (!cur.empty() && !isDir(cur)) ::mkdir(cur.c_str(), 0755);
        }
        cur.push_back(c);
    }
    if (!cur.empty() && !isDir(cur)) ::mkdir(cur.c_str(), 0755);
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t c = s.find(',', pos);
        const std::string t = trim(s.substr(pos, (c == std::string::npos ? s.size() : c) - pos));
        if (!t.empty()) out.push_back(t);
        if (c == std::string::npos) break;
        pos = c + 1;
    }
    return out;
}

}  // namespace

std::string rootDir(const std::string& gameDir) { return join(gameDir, "SKGLoader"); }

ModSet scanFolder(const std::string& modsDir, const std::string& globalIni) {
    ModSet set;
    set.root = modsDir;

    const std::map<std::string, std::string> global = readIni(globalIni);
    if (const auto it = global.find("enabled"); it != global.end()) {
        const std::string v = lower(it->second);
        set.globalEnabled = !(v == "0" || v == "false" || v == "no" || v == "off");
    }
    std::set<std::string> blacklist;
    if (const auto it = global.find("disabled"); it != global.end()) {
        for (const std::string& id : splitCsv(it->second)) blacklist.insert(lower(id));
    }
    if (!set.globalEnabled) set.warnings.push_back(globalIni + " has enabled=0: all mods off");

    if (!isDir(modsDir)) return set;

    for (const std::string& entry : listDir(modsDir)) {
        const std::string dir = join(modsDir, entry);
        if (!isDir(dir)) continue;
        if (!entry.empty() && entry[0] == '.') continue;

        ModInfo m;
        m.id = entry;
        m.dir = dir;
        // mod.ini (SKGLoader mods) or pack.ini (resource packs): same keys
        std::map<std::string, std::string> ini = readIni(join(dir, "mod.ini"));
        if (ini.empty()) ini = readIni(join(dir, "pack.ini"));
        if (ini.empty()) {   // a folder inside a wrapper folder ("mymod/mymod-master/mod.ini"): look one level down, when it is the only folder
            std::string only;
            int folders = 0;
            for (const std::string& sub : listDir(dir))
                if (isDir(join(dir, sub)) && !sub.empty() && sub[0] != '.') {
                    only = sub;
                    ++folders;
                }
            if (folders == 1) {
                ini = readIni(join(join(dir, only), "mod.ini"));
                if (ini.empty()) ini = readIni(join(join(dir, only), "pack.ini"));
            }
        }
        auto get = [&](const char* k, const std::string& def) -> std::string {
            const auto it = ini.find(k);
            return (it == ini.end() || it->second.empty()) ? def : it->second;
        };
        m.name = get("name", entry);
        m.version = get("version", "0.1");
        m.author = get("author", std::string{});
        m.description = get("description", std::string{});
        m.priority = std::atoi(get("priority", "100").c_str());
        if (!get("id", std::string{}).empty()) m.id = get("id", m.id);
        const std::string en = lower(get("enabled", "1"));
        m.enabled = !(en == "0" || en == "false" || en == "no" || en == "off");
        if (blacklist.count(lower(m.id))) {
            m.enabled = false;
            set.warnings.push_back("mod '" + m.id + "' is listed in mods.ini disabled=");
        }

        // Count the payload files and remember which game paths they shadow.
        Vfs probe;
        m.fileCount = probe.mount(dir);
        for (const std::string& f : probe.files()) {
            const auto slash = f.find_last_of("/\\");
            const std::string base =
                (slash == std::string::npos) ? f : f.substr(slash + 1);
            const std::string low = lower(base);
            if (low == "mod.ini" || low == "pack.ini") continue;
            if (low.rfind("readme", 0) == 0) continue;
            if (low == ".ds_store" || low == "thumbs.db") continue;
            if (m.overrides.size() < 24) m.overrides.push_back(f);
        }
        set.mods.push_back(std::move(m));
    }
    return set;
}

ModSet scan(const std::string& gameDir) {
    ModSet set;
    set.root = rootDir(gameDir);
    if (!isDir(set.root)) return set;
    ModSet s = scanFolder(join(set.root, "mods"), join(set.root, "mods.ini"));
    s.root = set.root;
    return s;
}

bool vet(ModInfo& m, const char* kind, std::vector<std::string>* log) {
    const modcheck::Result r = modcheck::check(m.dir);
    if (!r.blocked()) return true;
    m.blocked = true;
    m.problem = r.firstProblem();
    modcheck::report(std::string(kind) + " " + m.id, r, err::global());
    if (log) log->push_back(std::string(kind) + " " + m.id + " NOT loaded (" + (r.altered() ? "err 5" : "err 4") + "): " + m.problem);
    return false;
}

std::vector<ModInfo> loadOrder(const ModSet& set) {
    std::vector<ModInfo> out;
    if (!set.globalEnabled) return out;
    for (const ModInfo& m : set.mods) {
        if (m.enabled) out.push_back(m);
    }
    std::stable_sort(out.begin(), out.end(), [](const ModInfo& a, const ModInfo& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.id < b.id;
    });
    return out;
}

size_t apply(Vfs& vfs, const std::string& gameDir, const ModSet& set,
             std::vector<std::string>* log) {
    const size_t base = vfs.mount(gameDir);
    size_t mounted = 0;
    for (ModInfo m : loadOrder(set)) {
        if (!vet(m, "mod", log)) continue;
        const size_t n = vfs.mount(m.dir);
        if (n == 0) continue;
        ++mounted;
        if (log) log->push_back("mod " + m.id + " (prio " + std::to_string(m.priority) + "): " +
                                std::to_string(n) + " file(s)");
    }
    (void)base;
    return mounted;
}

size_t apply(Vfs& vfs, const std::string& gameDir, std::vector<std::string>* log) {
    return apply(vfs, gameDir, scan(gameDir), log);
}

Resolution resolve(const std::string& gameDir, const std::string& gamePath) {
    Resolution r;
    Vfs vfs;
    vfs.mount(gameDir);
    std::string real;
    if (!vfs.resolve(gamePath, &real)) return r;
    r.found = true;
    r.realPath = real;

    const ModSet set = scan(gameDir);
    std::string best;
    int bestPrio = -1;
    for (const ModInfo& m : loadOrder(set)) {
        Vfs probe;
        probe.mount(m.dir);
        std::string p;
        if (probe.resolve(gamePath, &p) && m.priority >= bestPrio) {
            bestPrio = m.priority;
            best = m.id;
            r.realPath = p;
        }
    }
    if (!best.empty()) r.modId = best;
    return r;
}

bool setEnabled(const std::string& gameDir, const std::string& id, bool enabled,
                const std::string& name) {
    if (id.empty()) return false;
    const std::string dir = join(join(rootDir(gameDir), "mods"), id);
    mkdirs(dir);
    std::map<std::string, std::string> ini = readIni(join(dir, "mod.ini"));
    ini["id"] = id;
    if (!ini.count("name")) ini["name"] = name.empty() ? id : name;
    if (!ini.count("version")) ini["version"] = "0.1";
    if (!ini.count("priority")) ini["priority"] = "100";
    ini["enabled"] = enabled ? "1" : "0";

    std::string out = "# ZDroid-Engine mod descriptor - edit with any text editor\n";
    static const char* kOrder[] = {"id", "name", "version", "author", "description", "priority",
                                   "enabled"};
    for (const char* k : kOrder) {
        const auto it = ini.find(k);
        if (it != ini.end()) out += std::string(k) + " = " + it->second + "\n";
    }
    for (const auto& kv : ini) {
        bool seen = false;
        for (const char* k : kOrder) {
            if (kv.first == k) seen = true;
        }
        if (!seen) out += kv.first + " = " + kv.second + "\n";
    }
    return writeText(join(dir, "mod.ini"), out);
}

bool createSkeleton(const std::string& gameDir, const std::string& id, const std::string& name) {
    if (id.empty()) return false;
    const std::string dir = join(join(rootDir(gameDir), "mods"), id);
    mkdirs(join(dir, "data"));
    if (!setEnabled(gameDir, id, true, name)) return false;

    const std::string readme =
        "# " + (name.empty() ? id : name) + "\n\n" +
        "ZDroid-Engine / SKGLoader mod.\n\n" +
        "Drop game files into this folder (mirroring the game's own layout, e.g.\n"
        "  " + id + "/data/characters/<name>_1p.dds\n"
        "  " + id + "/data/characters/<name>.foit\n"
        ") and they will override the originals the next time the loader mounts the "
        "game folder.\n\n"
        "mod.ini fields:\n"
        "  name         display name\n"
        "  version      free-form\n"
        "  author       free-form\n"
        "  description  one line\n"
        "  priority     higher wins when two mods replace the same file (default 100)\n"
        "  enabled      1 = on, 0 = off\n\n"
        "Turn every mod off at once with <gameDir>/SKGLoader/mods.ini containing:\n"
        "  enabled = 0\n";
    return writeText(join(dir, "README.txt"), readme);
}

}  // namespace mods
}  // namespace zengine
