// pack_merge.cpp - see pack_merge.h.
#include "pack_merge.h"

#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "vfs.h"   // normalizeKey

namespace zengine {
namespace packmerge {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWith(const std::string& s, const std::string& suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }

std::vector<std::string> splitList(const std::string& v) {
    std::vector<std::string> r;
    std::string cur;
    for (char c : v + ",") {
        if (c == ',' || c == ';') {
            const std::string t = lower(trim(cur));
            if (!t.empty()) r.push_back(t);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return r;
}

// "value  # comment" -> "value"
std::string stripComment(const std::string& v) {
    for (size_t i = 0; i < v.size(); ++i)
        if ((v[i] == '#' || v[i] == ';') && i > 0 && std::isspace(static_cast<unsigned char>(v[i - 1]))) return trim(v.substr(0, i));
    return trim(v);
}

// ---- lines ----------------------------------------------------------------------------------------------------------------------------

struct Lines {
    std::vector<std::string> v;
    std::string eol = "\n";
};

Lines splitLines(const std::string& text) {
    Lines l;
    size_t pos = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    if (text.find("\r\n") != std::string::npos) l.eol = "\r\n";
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        l.v.push_back(std::move(line));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return l;
}

std::string joinLines(const std::vector<std::string>& v, const std::string& eol) {
    std::string out;
    for (const std::string& s : v) out += s + eol;
    return out;
}

bool isBlank(const std::string& s) { return trim(s).empty(); }

// "Key: value" or "Key = value" (whichever separator comes first).  Comments and blank lines are not key lines.
bool keyLine(const std::string& line, std::string* key, std::string* value = nullptr) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') return false;
    const size_t p = t.find_first_of(":=");
    if (p == std::string::npos || p == 0) return false;
    const std::string k = lower(trim(t.substr(0, p)));
    if (k.empty() || k.find_first_of(" \t") != std::string::npos) return false;
    if (key) *key = k;
    if (value) *value = stripComment(t.substr(p + 1));
    return true;
}

// ---- keys (text) ----------------------------------------------------------------------------------------------------------------------

std::string mergeKeys(const std::string& base, const std::string& over) {
    const Lines b = splitLines(base), o = splitLines(over);
    std::vector<std::string> order;   // keys of `over` in first-appearance order
    std::unordered_map<std::string, std::vector<std::string>> group;
    for (const std::string& line : o.v) {
        std::string k;
        if (!keyLine(line, &k)) continue;
        if (!group.count(k)) order.push_back(k);
        group[k].push_back(line);
    }
    std::vector<std::string> out;
    std::unordered_set<std::string> done;
    for (const std::string& line : b.v) {
        std::string k;
        if (keyLine(line, &k) && group.count(k)) {
            if (done.insert(k).second) out.insert(out.end(), group[k].begin(), group[k].end());   // the group takes the first line's place
            continue;                                                                             // later lines of the same key go
        }
        out.push_back(line);
    }
    for (const std::string& k : order)
        if (!done.count(k)) out.insert(out.end(), group[k].begin(), group[k].end());
    return joinLines(out, b.v.empty() ? o.eol : b.eol);
}

// ---- records (text) -------------------------------------------------------------------------------------------------------------------

struct Block {
    std::vector<std::string> lines;   // the record and the blank lines that follow it
    size_t blanks = 0;                // how many of them are those trailing blank lines
    std::string id;                   // value of the id key ("" = none), lowercase
    bool hasKeys = false;
};

std::vector<Block> splitBlocks(const std::vector<std::string>& lines, const std::string& idKey) {
    std::vector<Block> r;
    Block cur;
    auto flush = [&]() {
        if (!cur.lines.empty()) r.push_back(std::move(cur));
        cur = Block();
    };
    for (const std::string& line : lines) {
        if (isBlank(line)) {
            cur.lines.push_back(line);
            ++cur.blanks;
            continue;
        }
        if (cur.blanks > 0) flush();
        cur.lines.push_back(line);
        std::string k, v;
        if (keyLine(line, &k, &v)) {
            cur.hasKeys = true;
            if (k == idKey && cur.id.empty()) cur.id = lower(v);
        }
    }
    flush();
    return r;
}

void stripTrailingBlanks(Block& b) {
    while (b.blanks > 0 && !b.lines.empty() && isBlank(b.lines.back())) {
        b.lines.pop_back();
        --b.blanks;
    }
}

std::string mergeRecords(const std::string& base, const std::string& over, const std::string& idKey) {
    const Lines bl = splitLines(base), ol = splitLines(over);
    std::vector<Block> blocks = splitBlocks(bl.v, idKey);
    std::unordered_map<std::string, size_t> at;
    for (size_t i = 0; i < blocks.size(); ++i)
        if (!blocks[i].id.empty() && !at.count(blocks[i].id)) at[blocks[i].id] = i;
    for (Block ob : splitBlocks(ol.v, idKey)) {
        if (!ob.hasKeys) continue;   // comments only: nothing to put anywhere
        stripTrailingBlanks(ob);
        const auto f = ob.id.empty() ? at.end() : at.find(ob.id);
        if (f != at.end()) {   // same record: replaced in its place, keeping the blank lines that followed it
            Block& dst = blocks[f->second];
            const size_t keep = dst.blanks;
            ob.lines.insert(ob.lines.end(), keep, std::string());
            ob.blanks = keep;
            dst = std::move(ob);
            continue;
        }
        if (!blocks.empty() && blocks.back().blanks == 0) {
            blocks.back().lines.push_back(std::string());
            blocks.back().blanks = 1;
        }
        if (!ob.id.empty()) at[ob.id] = blocks.size();
        blocks.push_back(std::move(ob));
    }
    std::string out;
    for (const Block& b : blocks) out += joinLines(b.lines, bl.v.empty() ? ol.eol : bl.eol);
    return out;
}

// ---- keys (flat JSON) -----------------------------------------------------------------------------------------------------------------

struct JEntry {
    std::string key;
    int kind = 0;   // 0 string, 1 null, 2 other token (kept as written)
    std::string val;
};

void putUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) {
        o += static_cast<char>(cp);
    } else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18));
        o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool hex4(const std::string& s, size_t i, uint32_t* v) {
    if (i + 4 > s.size()) return false;
    uint32_t r = 0;
    for (size_t k = 0; k < 4; ++k) {
        const char c = s[i + k];
        r <<= 4;
        if (c >= '0' && c <= '9') r |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') r |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') r |= static_cast<uint32_t>(c - 'A' + 10);
        else return false;
    }
    *v = r;
    return true;
}

// s[i] == '"' -> the decoded string, i just past the closing quote
bool readString(const std::string& s, size_t& i, std::string& out) {
    out.clear();
    ++i;
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') return true;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= s.size()) return false;
        const char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '/': case '\\': case '"': out += e; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(s, i, &cp)) return false;
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    uint32_t lo;
                    if (hex4(s, i + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                putUtf8(out, cp);
                break;
            }
            default: return false;
        }
    }
    return false;
}

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool parseFlat(const std::string& s, std::vector<JEntry>& out, std::string* err) {
    auto fail = [&](const char* m, size_t at) {
        if (err) *err = std::string(m) + " (byte " + std::to_string(at) + ")";
        return false;
    };
    size_t i = s.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    skipWs(s, i);
    if (i >= s.size() || s[i] != '{') return fail("not a JSON object", i);
    ++i;
    std::unordered_map<std::string, size_t> at;
    while (true) {
        skipWs(s, i);
        if (i < s.size() && s[i] == '}') return true;
        if (i >= s.size() || s[i] != '"') return fail("a key was expected", i);
        JEntry e;
        if (!readString(s, i, e.key)) return fail("bad key", i);
        skipWs(s, i);
        if (i >= s.size() || s[i] != ':') return fail("':' was expected", i);
        ++i;
        skipWs(s, i);
        if (i >= s.size()) return fail("a value was expected", i);
        if (s[i] == '"') {
            e.kind = 0;
            if (!readString(s, i, e.val)) return fail("bad string", i);
        } else if (s[i] == '{' || s[i] == '[') {
            return fail("nested values are not supported", i);
        } else {
            const size_t st = i;
            while (i < s.size() && s[i] != ',' && s[i] != '}') ++i;
            e.val = trim(s.substr(st, i - st));
            e.kind = e.val == "null" ? 1 : 2;
        }
        const auto f = at.find(e.key);
        if (f != at.end()) out[f->second] = std::move(e);
        else {
            at[e.key] = out.size();
            out.push_back(std::move(e));
        }
        skipWs(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            continue;
        }
        if (i < s.size() && s[i] == '}') return true;
        return fail("',' or '}' was expected", i);
    }
}

std::string jsonEscape(const std::string& v) {
    std::string o;
    for (const char c : v) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (u < 0x20) {
            static const char* hx = "0123456789abcdef";
            o += "\\u00";
            o += hx[u >> 4];
            o += hx[u & 15];
        } else o += c;
    }
    return o;
}

bool mergeJson(const std::string& base, const std::string& over, std::string& out, std::string* err) {
    std::vector<JEntry> b, o;
    std::string e;
    if (!parseFlat(base, b, &e)) {
        if (err) *err = "the file below: " + e;
        return false;
    }
    if (!parseFlat(over, o, &e)) {
        if (err) *err = "the pack's file: " + e;
        return false;
    }
    std::unordered_map<std::string, size_t> at;
    for (size_t i = 0; i < b.size(); ++i) at[b[i].key] = i;
    for (JEntry& x : o) {
        if (x.kind == 1) continue;   // null = "no text": never blanks what is there
        const auto f = at.find(x.key);
        if (f != at.end()) {
            b[f->second] = std::move(x);
        } else {
            at[x.key] = b.size();
            b.push_back(std::move(x));
        }
    }
    out = "{\n";
    for (size_t i = 0; i < b.size(); ++i) {
        out += " \"" + jsonEscape(b[i].key) + "\": ";
        out += b[i].kind == 0 ? "\"" + jsonEscape(b[i].val) + "\"" : b[i].val;
        out += i + 1 < b.size() ? ",\n" : "\n";
    }
    out += "}\n";
    return true;
}

bool matches(const std::string& pat, const std::string& path) {
    if (pat.empty()) return false;
    if (pat[0] == '*') return endsWith(path, pat.substr(1));
    return path == pat || endsWith(path, "/" + pat);
}

}  // namespace

const char* howName(How h) {
    switch (h) {
        case How::Keys: return "keys";
        case How::Records: return "records";
        case How::Append: return "append";
        default: return "replace";
    }
}

Config parseConfig(const std::string& iniText, std::vector<std::string>* warnings) {
    Config c;
    std::string section;
    for (const std::string& raw : splitLines(iniText).v) {
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            const size_t e = line.find(']');
            section = lower(trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = stripComment(line.substr(eq + 1));
        if (section != "merge") {
            const std::string k = lower(key);
            if (k == "requires") for (const std::string& s : splitList(val)) c.requires_.push_back(s);
            else if (k == "conflicts") for (const std::string& s : splitList(val)) c.conflicts.push_back(s);
            continue;
        }
        Rule r;
        r.pattern = lower(key);
        if (r.pattern != "loc" && r.pattern != "ini") r.pattern = normalizeKey(r.pattern);
        std::string how = lower(val), id;
        const size_t sp = how.find_first_of(" \t:,");
        if (sp != std::string::npos) {
            id = trim(how.substr(sp + 1));
            how = how.substr(0, sp);
        }
        if (how == "keys") r.how = How::Keys;
        else if (how == "replace") r.how = How::Replace;
        else if (how == "append") r.how = How::Append;
        else if (how == "records") r.how = How::Records;
        else {
            if (warnings) warnings->push_back("[merge] " + key + ": unknown rule '" + val + "' (replace, keys, records <Key>, append)");
            continue;
        }
        if (r.how == How::Records) {
            if (id.empty()) {
                if (warnings) warnings->push_back("[merge] " + key + ": 'records' needs the key that names a record (records ChalName)");
                continue;
            }
            r.idKey = id;
        }
        c.rules.push_back(std::move(r));
    }
    return c;
}

Rule ruleFor(const Config& cfg, const std::string& pathIn) {
    const std::string path = normalizeKey(pathIn);
    for (const Rule& r : cfg.rules)
        if (r.pattern != "loc" && r.pattern != "ini" && matches(r.pattern, path)) return r;
    for (const Rule& r : cfg.rules) {
        if (r.pattern == "loc" && endsWith(path, "_loc.json")) return r;
        if (r.pattern == "ini" && endsWith(path, ".ini")) return r;
    }
    Rule d;
    d.pattern = "default";
    if (endsWith(path, "_loc.json")) d.how = How::Keys;
    return d;
}

bool merge(const std::string& path, const Rule& r, const std::string& base, const std::string& over, std::string& out, std::string* err) {
    const bool json = endsWith(normalizeKey(path), ".json");
    switch (r.how) {
        case How::Replace:
            out = over;
            return true;
        case How::Append: {
            if (json) {
                if (err) *err = "append does not make sense for a .json file (use keys)";
                return false;
            }
            out = base;
            if (!out.empty() && out.back() != '\n') out += splitLines(base).eol;
            out += over;
            return true;
        }
        case How::Keys:
            if (json) return mergeJson(base, over, out, err);
            out = mergeKeys(base, over);
            return true;
        case How::Records:
            if (json) {
                if (err) *err = "records is for the game's .ini files, not .json (use keys)";
                return false;
            }
            out = mergeRecords(base, over, r.idKey);
            return true;
    }
    return false;
}

}  // namespace packmerge
}  // namespace zengine
