// lesson_data.cpp - see lesson_data.h.
#include "lesson_data.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "story.h"
#include "text_formats.h"
#include "vfs.h"

namespace zengine {
namespace lessons {

namespace {

std::string lowerOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimOf(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> commaList(const std::string& v) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= v.size()) {
        size_t c = v.find(',', pos);
        if (c == std::string::npos) c = v.size();
        const std::string item = trimOf(v.substr(pos, c - pos));
        if (!item.empty()) out.push_back(item);
        pos = c + 1;
    }
    return out;
}

std::string stripComment(const std::string& line) {
    const size_t h = line.find('#');
    return trimOf(h == std::string::npos ? line : line.substr(0, h));
}

void gauge(const std::string& v, Challenge::Gauge& g) {
    const std::vector<std::string> p = commaList(v);
    if (p.size() >= 1) g.playerPct = std::atoi(p[0].c_str());
    if (p.size() >= 2) g.playerMod = p[1];
    if (p.size() >= 3) g.enemyPct = std::atoi(p[2].c_str());
    if (p.size() >= 4) g.enemyMod = p[3];
}

}  // namespace

bool parseFightIni(std::string_view text, FightIni& out) {
    out = FightIni();
    int side = -1, fighters = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string line = stripComment(std::string(text.substr(pos, nl - pos)));
        pos = nl + 1;
        if (line.empty()) continue;
        const std::vector<std::string> w = splitWords(line);
        if (w.empty()) continue;
        const std::string k = lowerOf(w[0]);
        const std::string rest = w.size() > 1 ? trimOf(line.substr(line.find(w[0]) + w[0].size())) : std::string();
        if (k == "player" && w.size() > 1) {
            side = std::atoi(w[1].c_str()) - 1;
            if (side < 0 || side > 1) side = -1;
        } else if (k == "fighter" && side >= 0 && w.size() > 1) {
            if (out.fighter[side].empty()) {   // a team lists more fighters: the duel plays the first
                out.fighter[side] = w[1];
                ++fighters;
            }
        } else if (k == "color" && side >= 0 && w.size() > 1) {
            out.color[side] = std::max(1, std::atoi(w[1].c_str()));
        } else if (k == "trainingmode" && w.size() > 1) {
            out.training = std::atoi(w[1].c_str()) != 0;
        } else if (k == "useai" && w.size() > 1) {
            out.useAI = std::atoi(w[1].c_str()) != 0;
        } else if (k == "numrounds" && w.size() > 1) {
            out.rounds = std::max(1, std::atoi(w[1].c_str()));
        } else if (k == "luafile" && w.size() > 1) {
            out.luaFile = w[1];
        } else if (k == "world" && w.size() > 1) {
            out.world = w[1];
        }
    }
    return fighters >= 2;
}

std::string Chapter::progressKey(const Entry& e) const {
    std::string f = lowerOf(e.file);
    for (char& c : f)
        if (c == '\\') c = '/';
    return "lesson." + f;
}

std::vector<Chapter> loadChapters(const Vfs& vfs, const std::string& toc) {
    std::vector<Chapter> out;
    std::string text;
    if (!vfs.readText(toc, text)) return out;
    for (const std::string& stm : parseToc(text)) {
        std::string body;
        if (!vfs.readText(stm, body)) continue;
        const StmFile f = parseStm(body);
        Chapter ch;
        ch.stem = stm.substr(0, stm.find_last_of('.'));
        ch.name = f.charName;
        for (const StmNode& n : f.nodes) {
            if (n.kind != "StoryNode") continue;
            const std::string* type = n.get("NodeType");
            const std::string* file = n.get("FileName");
            if (!type || *type != "Fight" || !file) continue;
            Entry e;
            e.file = *file;
            if (const std::string* t = n.get("DisplayName")) e.title = *t;
            if (const std::string* t = n.get("DisplayText")) e.text = *t;
            if (const std::string* t = n.get("NodeUnlockedImg")) e.icon = *t;
            if (const std::string* t = n.get("NodeCompletedImg")) e.doneIcon = *t;
            if (const std::string* t = n.get("TimelinePos")) e.pos = std::atoi(t->c_str());
            ch.entries.push_back(std::move(e));
        }
        std::stable_sort(ch.entries.begin(), ch.entries.end(), [](const Entry& a, const Entry& b) { return a.pos < b.pos; });
        if (!ch.entries.empty()) out.push_back(std::move(ch));
    }
    return out;
}

std::vector<Challenge> parseChallenges(std::string_view text) {
    std::vector<Challenge> out;
    Challenge cur;
    bool open = false;
    auto flush = [&]() {
        if (open && !cur.name.empty()) out.push_back(cur);
        cur = Challenge();
        open = false;
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string line = stripComment(std::string(text.substr(pos, nl - pos)));
        pos = nl + 1;
        const size_t colon = line.find(':');
        if (line.empty() || colon == std::string::npos) continue;
        const std::string k = lowerOf(trimOf(line.substr(0, colon)));
        const std::string v = trimOf(line.substr(colon + 1));
        if (k == "chalname") {
            flush();
            cur.name = v;
            open = true;
            continue;
        }
        if (!open) continue;
        if (k == "timelimit") cur.timeLimit = std::max(1, std::atoi(v.c_str()));
        else if (k == "wincond") cur.winCond = v;
        else if (k == "world") cur.world = v;
        else if (k == "playerteam") {
            cur.playerTeam = commaList(v);
            if (cur.playerTeam.size() == 1 && lowerOf(cur.playerTeam[0]) == "any") cur.playerTeam.clear();
        } else if (k == "enemyteam") cur.enemyTeam = commaList(v);
        else if (k == "playerassist") cur.playerAssist = commaList(v);
        else if (k == "enemyassist") cur.enemyAssist = commaList(v);
        else if (k == "playercolor" || k == "enemycolor") {
            std::vector<int>& dst = k == "playercolor" ? cur.playerColor : cur.enemyColor;
            dst.clear();
            for (const std::string& c : commaList(v)) dst.push_back(std::max(1, std::atoi(c.c_str())));
        } else if (k == "health") gauge(v, cur.health);
        else if (k == "meter") gauge(v, cur.meter);
        else if (k == "drama") gauge(v, cur.drama);
        else if (k == "playerlocked" || k == "enemylocked") {
            std::vector<std::string>& dst = k == "playerlocked" ? cur.playerLocked : cur.enemyLocked;
            dst.clear();
            for (const std::string& c : commaList(v))
                if (lowerOf(c) != "none") dst.push_back(c);
        } else if (k == "playerallowdamage" || k == "enemyallowdamage") {
            std::vector<std::string>& dst = k == "playerallowdamage" ? cur.playerAllowDamage : cur.enemyAllowDamage;
            dst.clear();
            for (const std::string& c : commaList(v))
                if (lowerOf(c) != "all") dst.push_back(c);
        } else if (k == "aifile") cur.aiFile = v;
        else if (k == "ailevel") cur.aiLevel = std::max(1, std::atoi(v.c_str()));
    }
    flush();
    return out;
}

}  // namespace lessons
}  // namespace zengine
