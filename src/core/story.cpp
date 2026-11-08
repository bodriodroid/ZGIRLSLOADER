#include "story.h"

#include <cctype>
#include <cstdlib>

namespace zengine {

namespace {
bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
std::string trimStr(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isWs(s[a])) ++a;
    while (b > a && isWs(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}
// drops "  # comment" (a '#' that follows whitespace)
std::string stripInlineComment(std::string s) {
    for (size_t k = 0; k < s.size(); ++k)
        if (s[k] == '#' && (k == 0 || isWs(s[k - 1]))) {
            s.resize(k);
            break;
        }
    return trimStr(s);
}
}  // namespace

std::vector<std::string> splitWords(const std::string& arg) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < arg.size()) {
        while (i < arg.size() && isWs(arg[i])) ++i;
        size_t j = i;
        while (j < arg.size() && !isWs(arg[j])) ++j;
        if (j > i) out.push_back(arg.substr(i, j - i));
        i = j;
    }
    return out;
}

CutScript parseCutscene(std::string_view text) {
    CutScript s;
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string line = trimStr(text.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        // "Key: value"; a line whose first word has no colon right after it is a note
        size_t k = 0;
        while (k < line.size() && (std::isalnum(static_cast<unsigned char>(line[k])) || line[k] == '_')) ++k;
        if (k == 0 || k >= line.size() || line[k] != ':') continue;
        CutCmd c;
        c.key = line.substr(0, k);
        c.arg = stripInlineComment(line.substr(k + 1));
        c.line = lineNo;
        if (c.key == "Scene" && s.scene.empty()) s.scene = splitWords(c.arg).empty() ? "" : splitWords(c.arg)[0];
        if (c.key == "View" && s.view.empty()) s.view = splitWords(c.arg).empty() ? "" : splitWords(c.arg)[0];
        s.cmds.push_back(std::move(c));
    }
    return s;
}

std::string FightScript::get(const std::string& key, const std::string& fallback) const {
    const auto it = fields.find(key);
    return it == fields.end() ? fallback : it->second;
}

FightScript parseFightScript(std::string_view text) {
    FightScript f;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string line = stripInlineComment(trimStr(text.substr(pos, nl - pos)));
        pos = nl + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t sp = 0;
        while (sp < line.size() && !isWs(line[sp])) ++sp;
        f.fields[line.substr(0, sp)] = trimStr(std::string_view(line).substr(sp));
    }
    return f;
}

std::vector<StoryNode> storyNodes(const StmFile& stm) {
    std::vector<StoryNode> out;
    for (const StmNode& n : stm.nodes) {
        if (n.kind != "ArcadeNode" && n.kind != "StoryNode") continue;
        StoryNode s;
        const std::string* t = n.get("NodeType");
        if (!t) continue;   // the "ArcadeNode Complete" end marker
        s.type = *t == "NewScene" ? StoryNode::Type::Scene : *t == "Fight" ? StoryNode::Type::Fight : *t == "Credits" ? StoryNode::Type::Credits : StoryNode::Type::Other;
        if (const std::string* f = n.get("FileName")) s.file = *f;
        if (const std::string* p = n.get("TimelinePos")) s.pos = std::atoi(p->c_str());
        if (const std::string* u = n.get("UnlockCondition")) {
            const auto w = splitWords(*u);
            if (w.size() >= 2) {
                s.unlockChar = w[0];
                s.unlockPos = std::atoi(w[1].c_str());
            }
        }
        if (const std::string* g = n.get("ProgessionUnlock")) s.progressionUnlock = *g;
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<std::string> storyVoiceFiles(const std::string& textKey, const std::string& lang, int part) {
    std::vector<std::string> out;
    const std::string key = trimStr(textKey);
    if (key.empty() || key.compare(0, 5, "_RAW_") == 0 || part < 0 || part > 3) return out;
    const bool jp = lang == "jp" || lang == "JP";
    std::string joined;
    for (const std::string& w : splitWords(key))
        for (char c : w)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') joined += c;
    if (joined.empty()) return out;
    std::string bare;
    for (char c : joined)
        if (c != '-' && c != '_') bare += c;
    const std::string dir = std::string("story_vo/lang_") + (jp ? "jp" : "en") + "/";
    const std::string tail = std::string(part == 0 ? "_" : "_" + std::to_string(part) + "_") + (jp ? "JP" : "EN") + ".wav";
    out.push_back(dir + joined + tail);
    if (bare != joined) out.push_back(dir + bare + tail);
    return out;
}

}  // namespace zengine
