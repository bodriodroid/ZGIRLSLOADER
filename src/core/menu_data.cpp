// menu_data.cpp - see menu_data.h.
#include "menu_data.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>

namespace zengine {

namespace {

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t j = i;
        while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool listHas(const std::vector<std::string>& v, const std::string& what) {
    const std::string w = lowerStr(what);
    for (const std::string& s : v)
        if (lowerStr(s) == w) return true;
    return false;
}

MenuOptType typeFromString(const std::string& v) {
    const std::string l = lowerStr(v);
    if (l == "advance") return MenuOptType::Advance;
    if (l == "setting") return MenuOptType::Setting;
    if (l == "submenu") return MenuOptType::Submenu;
    if (l == "confirm") return MenuOptType::Confirm;
    return MenuOptType::Unknown;
}

// ' #' (or a leading '#') starts a comment - the same rule text_formats.cpp uses.
std::string stripComment(const std::string& line) {
    for (size_t i = 0; i < line.size(); ++i)
        if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) return line.substr(0, i);
    return line;
}

}  // namespace

bool MenuOption::visibleOn(const std::string& platform) const {
    if (!allowedPlatforms.empty() && !listHas(allowedPlatforms, platform)) return false;
    if (listHas(blockedPlatforms, platform)) return false;
    if (lowerStr(modeLimit) == "betaonly") return false;  // we are never the beta build
    return true;
}

MenuDef parseMenuIni(std::string_view text) {
    MenuDef def;
    MenuOption* cur = nullptr;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string line = trim(stripComment(std::string(text.substr(pos, nl - pos))));
        pos = nl + 1;
        if (line.empty()) continue;

        const size_t colon = line.find(':');
        const std::string key = lowerStr(trim(colon == std::string::npos ? line : line.substr(0, colon)));
        const std::string val = colon == std::string::npos ? std::string() : trim(line.substr(colon + 1));

        if (key == "option") {
            def.options.emplace_back();
            cur = &def.options.back();
            continue;
        }
        if (!cur) {
            if (key == "toptext") def.topText = val;
            continue;
        }
        if (key == "defaultcursorpos") cur->defaultCursor = true;
        else if (key == "type") cur->type = typeFromString(val);
        else if (key == "text") cur->text = val;
        else if (key == "description") cur->description = val;
        else if (key == "keyword") cur->keyword = val;
        else if (key == "choice") cur->choices.push_back(val);
        else if (key == "defaultchoice") {
            cur->defaultChoice = val;
            cur->choices.push_back(val);  // DefaultChoice sits in the choice list at its own position
        } else if (key == "p1lock") cur->p1Lock = lowerStr(val) == "true";
        else if (key == "restart") cur->restart = lowerStr(val) == "yes" || lowerStr(val) == "true";
        else if (key == "allowedplatforms") cur->allowedPlatforms = splitWords(val);
        else if (key == "blockedplatforms") cur->blockedPlatforms = splitWords(val);
        else if (key == "modelimit") cur->modeLimit = val;
        // everything else (TrialModeDefaultChoice, ...) is ignored
    }
    // An entry with neither a label nor an action is junk (e.g. a half-commented block).
    def.options.erase(std::remove_if(def.options.begin(), def.options.end(),
                                     [](const MenuOption& o) { return o.text.empty() && o.keyword.empty(); }),
                      def.options.end());
    return def;
}

// ---------------------------------------------------------------------------------------- JSON

namespace {

struct JsonCursor {
    std::string_view s;
    size_t i = 0;
    std::string err;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    bool fail(const char* m) {
        if (err.empty()) err = std::string(m) + " at byte " + std::to_string(i);
        return false;
    }
    static void putUtf8(std::string& o, uint32_t cp) {
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
    bool hex4(uint32_t& out) {
        if (i + 4 > s.size()) return fail("truncated \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i + k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        i += 4;
        out = v;
        return true;
    }
    bool str(std::string& out) {
        out.clear();
        if (i >= s.size() || s[i] != '"') return fail("expected '\"'");
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        const size_t save = i;
                        i += 2;
                        uint32_t lo = 0;
                        if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else {
                            i = save;
                            err.clear();
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xD800 && cp < 0xE000) {
                        cp = 0xFFFD;
                    }
                    putUtf8(out, cp);
                    break;
                }
                default: out += e; break;  // tolerate unknown escapes
            }
        }
        return fail("unterminated string");
    }
    // Skips any JSON value (used for non-string values, which a loc table should not have).
    bool skip() {
        ws();
        if (i >= s.size()) return fail("unexpected end");
        if (s[i] == '"') {
            std::string tmp;
            return str(tmp);
        }
        if (s[i] == '{' || s[i] == '[') {
            int depth = 0;
            while (i < s.size()) {
                const char c = s[i];
                if (c == '"') {
                    std::string tmp;
                    if (!str(tmp)) return false;
                    continue;
                }
                if (c == '{' || c == '[') ++depth;
                if (c == '}' || c == ']') --depth;
                ++i;
                if (depth == 0) return true;
            }
            return fail("unterminated container");
        }
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') ++i;
        return true;
    }
};

}  // namespace

bool Localization::parseJson(std::string_view json, std::string* err) {
    map_.clear();
    JsonCursor c{json};
    if (c.s.size() >= 3 && static_cast<unsigned char>(c.s[0]) == 0xEF && static_cast<unsigned char>(c.s[1]) == 0xBB &&
        static_cast<unsigned char>(c.s[2]) == 0xBF)
        c.i = 3;
    c.ws();
    auto done = [&](bool ok) {
        if (!ok && err) *err = c.err;
        return ok;
    };
    if (c.i >= c.s.size() || c.s[c.i] != '{') return done(c.fail("expected '{'"));
    ++c.i;
    c.ws();
    if (c.i < c.s.size() && c.s[c.i] == '}') return done(true);
    for (;;) {
        c.ws();
        std::string key;
        if (!c.str(key)) return done(false);
        c.ws();
        if (c.i >= c.s.size() || c.s[c.i] != ':') return done(c.fail("expected ':'"));
        ++c.i;
        c.ws();
        if (c.i < c.s.size() && c.s[c.i] == '"') {
            std::string val;
            if (!c.str(val)) return done(false);
            map_[std::move(key)] = std::move(val);
        } else if (!c.skip()) {
            return done(false);
        }
        c.ws();
        if (c.i >= c.s.size()) return done(c.fail("unexpected end"));
        if (c.s[c.i] == ',') {
            ++c.i;
            continue;
        }
        if (c.s[c.i] == '}') return done(true);
        return done(c.fail("expected ',' or '}'"));
    }
}

std::string Localization::tr(const std::string& key) const { return trOr(key, key); }

std::string Localization::trOr(const std::string& key, const std::string& fallback) const {
    const auto it = map_.find(key);
    return it == map_.end() ? fallback : it->second;
}

}  // namespace zengine
