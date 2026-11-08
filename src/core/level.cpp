// level.cpp - see level.h. Tolerant line parser, no exceptions, no I/O.
#include "level.h"

#include "text_formats.h"

#include <cstdlib>

namespace zengine {
namespace {

std::vector<std::string> tokens(std::string_view line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == ',' || c == '\r') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int toInt(const std::string& s, int def = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

double toDbl(const std::string& s, double def = 0) {
    try {
        return std::stod(s);
    } catch (...) {
        return def;
    }
}

}  // namespace

LevelInfo parseLevel(std::string_view text, const std::string& name, const std::string& path) {
    LevelInfo lv;
    lv.name = name;
    lv.path = path;

    for (const std::string& raw : splitLines(text)) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = trim(line.substr(0, hash));
        if (line.empty()) continue;

        // Two shapes: "Key: value value" and "Key value value" ("CAMERA 66", "2D <path>").
        const size_t colon = line.find(':');
        std::vector<std::string> tk;
        std::string key;
        if (colon == std::string::npos) {
            tk = tokens(line);
            key = tk.empty() ? std::string() : tk[0];
            if (!tk.empty()) tk.erase(tk.begin());   // a line of only separators (",") has no tokens at all
        } else {
            key = trim(line.substr(0, colon));
            tk = tokens(line.substr(colon + 1));
        }

        if (key == "StageSize" && tk.size() >= 2) {
            lv.width = toInt(tk[0], lv.width);
            lv.height = toInt(tk[1], lv.height);
        } else if (key == "BottomClearance" && !tk.empty()) {
            lv.bottomClearance = toInt(tk[0], lv.bottomClearance);
        } else if (key == "Start1" && !tk.empty()) {
            lv.start1 = toInt(tk[0], lv.start1);
        } else if (key == "Start2" && !tk.empty()) {
            lv.start2 = toInt(tk[0], lv.start2);
        } else if (key == "ShadowDist" && !tk.empty()) {
            lv.shadowDist = toInt(tk[0], lv.shadowDist);
        } else if (key == "2D" && !tk.empty()) {
            lv.background2D = tk[0];
        } else if (key == "CAMERA" && !tk.empty()) {
            lv.cameraFov = toDbl(tk[0], lv.cameraFov);
        } else if (key == "Light" && tk.size() >= 4) {
            LevelLight l;
            l.kind = tk[0];
            l.r = toInt(tk[1]);
            l.g = toInt(tk[2]);
            l.b = toInt(tk[3]);
            if (tk.size() >= 7) {
                l.x = toDbl(tk[4]);
                l.y = toDbl(tk[5]);
                l.z = toDbl(tk[6]);
            }
            if (tk.size() >= 8) l.range = toDbl(tk[7]);
            if (l.kind == "Amb" || l.kind == "amb") {
                lv.ambient[0] = l.r;
                lv.ambient[1] = l.g;
                lv.ambient[2] = l.b;
            }
            lv.lights.push_back(l);
        }
        // Everything else (3D, ForceReplace, Replace, ReplaceNumIfChar, ...) is 3D-mode or
        // character-specific casting and has no meaning for a 2D renderer.
    }
    return lv;
}

namespace {
// "Rooftops_Day", "rooftops-day", "ROOFTOPS DAY" -> "rooftopsday"
std::string stageKey(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c + 32);
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}
std::string withoutPrefix(const std::string& k, const char* pre) {
    const std::string p = pre;
    return k.compare(0, p.size(), p) == 0 ? k.substr(p.size()) : k;
}
std::string without3d(const std::string& k) { return (k.size() > 2 && k.compare(k.size() - 2, 2, "3d") == 0) ? k.substr(0, k.size() - 2) : k; }
int levelByKey(const std::vector<LevelInfo>& levels, const std::string& k) {
    if (k.empty()) return -1;
    for (size_t i = 0; i < levels.size(); ++i)
        if (stageKey(levels[i].name) == k) return static_cast<int>(i);
    return -1;
}
}  // namespace

int resolveLevel(const std::vector<LevelInfo>& levels, const std::vector<StageEntry>& table, std::string_view want) {
    const std::string k = stageKey(want);
    if (k.empty() || levels.empty()) return -1;
    const std::string bare = withoutPrefix(k, "stage");   // "stage_casino" -> "casino"
    // 1. the .lvl itself
    if (int i = levelByKey(levels, k); i >= 0) return i;
    // 2. a stages.ini row: "<LocId> <file> [options] <picture>"
    for (const StageEntry& e : table) {
        const std::string loc = stageKey(e.locId), file = stageKey(e.level), pic = stageKey(e.picture);
        const std::string picBare = withoutPrefix(pic, "stage");
        if (k == loc || k == file || k == pic || bare == picBare || bare == loc || without3d(k) == without3d(file)) {
            if (int i = levelByKey(levels, file); i >= 0) return i;
        }
    }
    // 3. the same name without the "_3d" suffix
    {
        const std::string kb = without3d(bare);
        for (size_t i = 0; i < levels.size(); ++i)
            if (without3d(stageKey(levels[i].name)) == kb) return static_cast<int>(i);
    }
    // 4. contained in exactly one level (and not an empty / one-letter name)
    if (bare.size() >= 4) {
        int hit = -1, hits = 0;
        for (size_t i = 0; i < levels.size(); ++i) {
            const std::string n = without3d(stageKey(levels[i].name));
            if (n.find(without3d(bare)) != std::string::npos || without3d(bare).find(n) != std::string::npos) {
                hit = static_cast<int>(i);
                ++hits;
            }
        }
        if (hits == 1) return hit;
    }
    return -1;
}

std::vector<int> randomLevels(const std::vector<LevelInfo>& levels, const std::vector<StageEntry>& table) {
    std::vector<int> out;
    for (size_t i = 0; i < levels.size(); ++i) {
        const std::string name = stageKey(levels[i].name);
        bool listed = false, excluded = false;
        for (const StageEntry& e : table) {
            if (stageKey(e.level) != name) continue;
            listed = true;
            for (const std::string& o : e.options) {
                const std::string ok = stageKey(o);
                if (ok == "excludefromrandom" || ok == "betaonly") excluded = true;
            }
        }
        // a level stages.ini does not list is a mod's stage (mods/stages/<id>/): it may come up, the game's test1 / test2 may not
        if (listed ? !excluded : name.compare(0, 4, "test") != 0) out.push_back(static_cast<int>(i));
    }
    if (out.empty())   // nothing qualifies (a mod-only data set): any level is better than none
        for (size_t i = 0; i < levels.size(); ++i) out.push_back(static_cast<int>(i));
    return out;
}

}  // namespace zengine
