#include "text_formats.h"

#include <cctype>

namespace zengine {

namespace {
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

bool endsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

// Splits on runs of whitespace.
std::vector<std::string> splitWs(std::string_view s) {
    std::vector<std::string> r;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        size_t j = i;
        while (j < s.size() && !isSpace(s[j])) ++j;
        if (j > i) r.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return r;
}

// "Key: value" -> value (trimmed) if the line starts with `key`.
bool takeKeyColon(const std::string& line, std::string_view key, std::string* value) {
    if (!startsWith(line, key)) return false;
    std::string_view rest = std::string_view(line).substr(key.size());
    if (rest.empty() || rest[0] != ':') return false;
    *value = trim(rest.substr(1));
    return true;
}
}  // namespace

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    size_t i = 0;
    std::string cur;
    while (i < text.size()) {
        char c = text[i++];
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else if (c == '\r') {
            if (i < text.size() && text[i] == '\n') ++i;
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

std::vector<std::string> splitCommaList(std::string_view s) {
    std::vector<std::string> r;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string_view::npos) j = s.size();
        std::string part = trim(s.substr(i, j - i));
        if (!part.empty()) r.push_back(std::move(part));
        i = j + 1;
    }
    return r;
}

std::vector<StageEntry> parseStages(std::string_view text) {
    std::vector<StageEntry> out;
    for (const std::string& raw : splitLines(text)) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> t = splitWs(line);
        if (t.size() < 3) continue;
        StageEntry s;
        s.locId = t[0];
        s.level = t[1];
        s.picture = t[2];
        for (size_t i = 3; i < t.size(); ++i) s.options.push_back(t[i]);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<MusicTrack> parseMusic(std::string_view text) {
    std::vector<MusicTrack> out;
    for (const std::string& raw : splitLines(text)) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        std::string v;
        if (takeKeyColon(line, "Track", &v)) {
            out.emplace_back();
            out.back().name = v;
        } else if (!out.empty() && takeKeyColon(line, "Stage", &v)) {
            out.back().stages = splitCommaList(v);
        } else if (!out.empty() && takeKeyColon(line, "Files", &v)) {
            out.back().files = splitCommaList(v);
        }
    }
    return out;
}

std::vector<std::string> parseToc(std::string_view text) {
    std::vector<std::string> out;
    for (const std::string& raw : splitLines(text)) {
        std::string line = trim(raw), v;
        if (line.empty() || line[0] == '#') continue;
        if (takeKeyColon(line, "File", &v) && !v.empty()) out.push_back(v);
    }
    return out;
}

std::vector<CompileJob> parseCompile(std::string_view text) {
    std::vector<CompileJob> out;
    std::string pendingSpt;
    for (const std::string& raw : splitLines(text)) {
        std::string line = trim(raw);
        if (endsWith(line, "#input")) {
            pendingSpt = trim(std::string_view(line).substr(0, line.size() - 6));
        } else if (endsWith(line, "#output") && !pendingSpt.empty()) {
            out.push_back({pendingSpt, trim(std::string_view(line).substr(0, line.size() - 7))});
            pendingSpt.clear();
        }
    }
    return out;
}

const std::string* StmNode::get(std::string_view key) const {
    for (const auto& kv : fields)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

StmFile parseStm(std::string_view text) {
    StmFile f;
    StmNode* cur = nullptr;
    for (const std::string& raw : splitLines(text)) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        size_t sp = 0;
        while (sp < line.size() && !isSpace(line[sp])) ++sp;
        std::string key = line.substr(0, sp);
        std::string rest = trim(std::string_view(line).substr(sp));
        // Inline comment: whitespace followed by '#' (Squigly.stm has "FileName x.ini<TAB>#leftover.ini").
        for (size_t k = 1; k < rest.size(); ++k)
            if (rest[k] == '#' && isSpace(rest[k - 1])) { rest = trim(std::string_view(rest).substr(0, k)); break; }
        if (key == "ArcadeNode" || key == "StoryNode") {
            f.nodes.emplace_back();
            cur = &f.nodes.back();
            cur->kind = key;
            cur->arg = rest;
        } else if (key == "CharName" && !cur) {
            f.charName = rest;
        } else if (key == "CharIcon" && !cur) {
            f.charIcon = rest;
        } else if (cur) {
            cur->fields.emplace_back(key, rest);
        }
    }
    return f;
}

}  // namespace zengine
