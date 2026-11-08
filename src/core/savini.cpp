// savini.cpp - see savini.h
#include "savini.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace zengine {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

std::string trim(std::string_view v) {
    size_t a = 0, b = v.size();
    while (a < b && (v[a] == ' ' || v[a] == '\t' || v[a] == '\r' || v[a] == '\n')) ++a;
    while (b > a && (v[b - 1] == ' ' || v[b - 1] == '\t' || v[b - 1] == '\r' || v[b - 1] == '\n')) --b;
    return std::string(v.substr(a, b - a));
}

bool isFlagLetter(const std::string& v) {
    return v.size() == 1 && (v[0] == 'I' || v[0] == 'C' || v[0] == 'L' || v[0] == 'U' || v[0] == 'N');
}

bool allDigits(const std::string& v) {
    if (v.empty()) return false;
    for (char c : v)
        if (c < '0' || c > '9') return false;
    return true;
}

bool readAll(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

const std::vector<SavIni::Entry>& noEntries() {
    static const std::vector<SavIni::Entry> e;
    return e;
}

// "pos filia 7" / "done filia" / "flag storydouble" lines of the older zgirls_progress.txt
std::string stripSlash(std::string d) {
    while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    return d;
}

}  // namespace

SavIni& SavIni::shared() {
    static SavIni s;
    return s;
}

// ------------------------------------------------------------------------------------------------------------- sections

SavIni::Section* SavIni::find(const std::string& section) {
    const std::string l = lower(section);
    for (Section& s : sections_)
        if (lower(s.name) == l) return &s;
    return nullptr;
}

const SavIni::Section* SavIni::find(const std::string& section) const {
    const std::string l = lower(section);
    for (const Section& s : sections_)
        if (lower(s.name) == l) return &s;
    return nullptr;
}

SavIni::Section& SavIni::make(const std::string& section) {
    if (Section* s = find(section)) return *s;
    sections_.push_back(Section{section, {}, {}});
    return sections_.back();
}

bool SavIni::has(const std::string& section, const std::string& key) const {
    const Section* s = find(section);
    return s && s->index.count(lower(key)) != 0;
}

std::string SavIni::get(const std::string& section, const std::string& key, const std::string& fallback) const {
    const Section* s = find(section);
    if (!s) return fallback;
    const auto it = s->index.find(lower(key));
    return it == s->index.end() ? fallback : s->items[it->second].value;
}

int SavIni::getInt(const std::string& section, const std::string& key, int fallback) const {
    const std::string v = get(section, key);
    if (v.empty()) return fallback;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    return (end && *end == 0) ? static_cast<int>(n) : fallback;
}

void SavIni::set(const std::string& section, const std::string& key, const std::string& value) {
    if (key.empty()) return;
    Section& s = make(section);
    const std::string lk = lower(key);
    const auto it = s.index.find(lk);
    if (it != s.index.end()) {
        if (s.items[it->second].value == value) return;   // nothing changed: no disk write
        s.items[it->second].value = value;
    } else {
        s.index[lk] = s.items.size();
        s.items.push_back(Entry{key, value});
    }
    if (autosave_) save();
}

bool SavIni::erase(const std::string& section, const std::string& key) {
    Section* s = find(section);
    if (!s) return false;
    const auto it = s->index.find(lower(key));
    if (it == s->index.end()) return false;
    s->items.erase(s->items.begin() + static_cast<std::ptrdiff_t>(it->second));
    s->index.clear();
    for (size_t i = 0; i < s->items.size(); ++i) s->index[lower(s->items[i].key)] = i;
    if (autosave_) save();
    return true;
}

const std::vector<SavIni::Entry>& SavIni::entries(const std::string& section) const {
    const Section* s = find(section);
    return s ? s->items : noEntries();
}

void SavIni::clear() { sections_.clear(); }

// ------------------------------------------------------------------------------------------------------------- text

void SavIni::parse(std::string_view text) {
    clear();
    Section* cur = nullptr;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close == std::string::npos || close < 2) continue;
            cur = &make(trim(std::string_view(line).substr(1, close - 1)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || !cur) continue;
        const std::string key = trim(std::string_view(line).substr(0, eq));
        if (key.empty()) continue;
        const std::string lk = lower(key);
        const std::string val = trim(std::string_view(line).substr(eq + 1));
        const auto it = cur->index.find(lk);
        if (it != cur->index.end()) {
            cur->items[it->second].value = val;   // a repeated key: the last one wins
        } else {
            cur->index[lk] = cur->items.size();
            cur->items.push_back(Entry{key, val});
        }
        if (pos > text.size()) break;
    }
}

std::string SavIni::toText() const {
    std::string out = "; ZGirls Loader save (.savini). Safe to edit with the game closed; delete it to start from zero.\n";
    for (const Section& s : sections_) {
        out += "\n[" + s.name + "]\n";
        for (const Entry& e : s.items) out += e.key + "=" + e.value + "\n";
    }
    return out;
}

bool SavIni::load(const std::string& path) {
    path_ = path;
    std::string text;
    if (!readAll(path, text)) {
        clear();
        return false;
    }
    parse(text);
    return true;
}

bool SavIni::save() const {
    if (path_.empty()) return false;
    namespace fs = std::filesystem;
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        const std::string t = toText();
        f.write(t.data(), static_cast<std::streamsize>(t.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path_, ec);
    if (ec) {   // some platforms refuse to replace: remove and retry once
        fs::remove(path_, ec);
        ec.clear();
        fs::rename(tmp, path_, ec);
    }
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------------------- open / legacy

void SavIni::open(const std::string& prefsDir) {
    if (prefsDir.empty()) return;
    const std::string dir = stripSlash(prefsDir);
    const std::string path = dir + "/zgirls.savini";
    if (path_ == path) return;   // already open
    dir_ = dir;
    const bool had = load(path);
    if (!had) {
        const bool wasAuto = autosave_;
        autosave_ = false;
        importLegacy(dir);
        autosave_ = wasAuto;
        if (!sections_.empty()) save();
    }
}

void SavIni::importLegacy(const std::string& dir) {
    std::string text;
    if (readAll(dir + "/menu_settings.txt", text)) {   // key=value per line
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            set("options", trim(std::string_view(line).substr(0, eq)), trim(std::string_view(line).substr(eq + 1)));
        }
    }
    if (readAll(dir + "/zgirls_progress.txt", text)) {   // "pos filia 7" / "done filia" / "flag storydouble"
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (const size_t h = line.find('#'); h != std::string::npos) line.erase(h);
            std::istringstream ss(line);
            std::string kind, key;
            if (!(ss >> kind >> key) || key.empty()) continue;
            key = lower(key);
            if (kind == "pos") {
                int v = 0;
                if (ss >> v) set("story", "pos." + key, std::to_string(v));
            } else if (kind == "done") {
                set("story", "done." + key, "1");
                set("progression", "Story" + key, "C");
            } else if (kind == "flag") {
                unlock(key);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------------------- defaults / flags

int SavIni::applyDefaults(std::string_view text) {
    int added = 0;
    size_t pos = 0;
    const bool wasAuto = autosave_;
    autosave_ = false;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const size_t eq = line.find('=');   // "key = value"
        if (eq == std::string::npos || eq == 0) continue;
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string val = trim(std::string_view(line).substr(eq + 1));
        if (key.empty() || val.empty()) continue;
        const char* sec = isFlagLetter(val) ? "progression" : allDigits(val) ? "earned" : nullptr;
        if (!sec) continue;
        Section& s = make(sec);
        const auto it = s.index.find(lower(key));
        if (it != s.index.end()) {
            s.items[it->second].key = key;   // adopt the game's spelling (an imported entry may be all lower case)
            continue;
        }
        s.index[lower(key)] = s.items.size();
        s.items.push_back(Entry{key, val});
        ++added;
    }
    autosave_ = wasAuto;
    if (added > 0 && autosave_) save();
    return added;
}

char SavIni::flagLetter(const std::string& key) const {
    const std::string v = get("progression", key);
    return v.empty() ? 0 : v[0];
}

bool SavIni::isDone(const std::string& key) const {
    const char c = flagLetter(key);
    return c == 'C' || c == 'U';
}

void SavIni::unlock(const std::string& key) {
    switch (flagLetter(key)) {
        case 'C':
        case 'U': return;
        case 'L': set("progression", key, "U"); return;
        default: set("progression", key, "C"); return;   // I, N or absent
    }
}

int SavIni::addCounter(const std::string& key, int by) {
    const int v = counter(key) + by;
    setInt("earned", key, v);
    return v;
}

}  // namespace zengine
