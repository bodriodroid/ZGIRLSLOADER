#include "vfs.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace zengine {

namespace {

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lowerCopy(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = lowerAscii(c);
    return r;
}

const std::set<std::string>& skipDirs() {
    static const std::set<std::string> s = {"extract", "build", "analysis", "analysis_v2", "skgsteam", "mods"};
    return s;
}

std::string baseName(const std::string& key) {
    size_t p = key.rfind('/');
    return p == std::string::npos ? key : key.substr(p + 1);
}

}  // namespace

std::string normalizeKey(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        c = lowerAscii(c);
        if (c == '/' && !out.empty() && out.back() == '/') continue;  // collapse "//"
        out.push_back(c);
    }
    while (out.rfind("./", 0) == 0) out.erase(0, 2);
    while (!out.empty() && out.front() == '/') out.erase(0, 1);
    return out;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

size_t Vfs::mount(const std::string& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    const int order = static_cast<int>(mounts_.size());
    mounts_.push_back(dir);

    // Collect first and sort so the index does not depend on directory iteration order.
    std::vector<std::pair<std::string, std::string>> found;  // {relative key path, real path}
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& de = *it;
        std::error_code ec2;
        if (de.is_directory(ec2)) {
            if (skipDirs().count(lowerCopy(de.path().filename().string()))) it.disable_recursion_pending();
            continue;
        }
        if (!de.is_regular_file(ec2)) continue;
        std::string rel = fs::relative(de.path(), dir, ec2).generic_string();
        if (ec2 || rel.empty()) continue;
        found.emplace_back(std::move(rel), de.path().string());
    }
    std::sort(found.begin(), found.end());

    for (const auto& [rel, real] : found) {
        files_.push_back(real);
        const std::string key = normalizeKey(rel);
        // index under every path suffix: "a/b/c.txt", "b/c.txt", "c.txt"
        size_t parts = static_cast<size_t>(std::count(key.begin(), key.end(), '/')) + 1;
        size_t pos = 0;
        while (true) {
            // depth = total path parts of the FILE, so shallower files win for a shared suffix
            const std::string suffix = key.substr(pos);
            auto ins = bySuffix_.emplace(suffix, Entry{real, order, parts});
            if (!ins.second) {
                Entry& e = ins.first->second;
                if (order > e.mountOrder || (order == e.mountOrder && parts < e.depth)) {
                    e.real = real;
                    e.mountOrder = order;
                    e.depth = parts;
                }
            }
            size_t slash = key.find('/', pos);
            if (slash == std::string::npos) break;
            pos = slash + 1;
        }
        byName_[baseName(key)].push_back(real);
    }
    return found.size();
}

bool Vfs::resolve(std::string_view path, std::string* realPath) const {
    auto it = bySuffix_.find(normalizeKey(path));
    if (it == bySuffix_.end()) return false;
    if (realPath) *realPath = it->second.real;
    return true;
}

bool Vfs::read(std::string_view path, std::vector<uint8_t>& out) const {
    std::string real;
    if (!resolve(path, &real)) return false;
    return readFile(real, out);
}

bool Vfs::readText(std::string_view path, std::string& out) const {
    std::vector<uint8_t> buf;
    if (!read(path, buf)) return false;
    out.assign(buf.begin(), buf.end());
    return true;
}

std::vector<std::string> Vfs::listByExt(std::string_view ext) const {
    const std::string e = lowerCopy(ext);
    std::vector<std::string> r;
    for (const std::string& f : files_) {
        if (f.size() >= e.size() && lowerCopy(std::string_view(f).substr(f.size() - e.size())) == e) r.push_back(f);
    }
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    return r;
}

std::vector<std::string> Vfs::findByName(std::string_view name) const {
    auto it = byName_.find(normalizeKey(name));
    if (it == byName_.end()) return {};
    std::vector<std::string> r = it->second;
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    return r;
}

bool Vfs::pick(std::string_view name, std::string* realPath) const {
    std::vector<std::string> c = findByName(name);
    if (c.empty()) return false;
    std::stable_sort(c.begin(), c.end(), [](const std::string& a, const std::string& b) {
        const bool ca = lowerCopy(a).find("characters") != std::string::npos;
        const bool cb = lowerCopy(b).find("characters") != std::string::npos;
        if (ca != cb) return ca;
        return a.size() < b.size();
    });
    if (realPath) *realPath = c.front();
    return true;
}

}  // namespace zengine
