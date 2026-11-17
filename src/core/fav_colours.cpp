#include "fav_colours.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace zengine {
namespace coledit {

namespace fs = std::filesystem;

namespace {
bool readText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
bool writeFile(const std::string& path, const void* data, size_t n) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}
std::string stemDds(const std::string& zgcol) {
    fs::path p(zgcol);
    p.replace_extension(".dds");
    return p.string();
}
}  // namespace

bool Favourites::load(const std::string& file, Scheme& out, std::string* err) {
    std::string text;
    if (!readText(file, text)) {
        if (err) *err = "cannot read " + file;
        return false;
    }
    return parse(text, out, err);
}

std::vector<Fav> Favourites::list(const std::string& character, const Palette* litBase) const {
    std::vector<Fav> v;
    if (dir_.empty() || character.empty()) return v;
    std::error_code ec;
    const fs::path d = fs::path(dir_) / character;
    if (!fs::is_directory(d, ec)) return v;
    std::vector<std::string> files;
    for (const fs::directory_entry& e : fs::directory_iterator(d, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".zgcol") files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        Scheme s;
        if (!load(f, s) || s.character != character) continue;
        Fav fav;
        fav.character = character;
        fav.file = f;
        fav.name = s.name.empty() ? fs::path(f).stem().string() : s.name;
        fav.dds = stemDds(f);
        if (!fs::exists(fav.dds, ec)) {
            if (!litBase || litBase->bands != static_cast<int>(s.regions.size()) + 1) continue;   // cannot bake it (another build of the data)
            if (litBase->alpha.size() == static_cast<size_t>(litBase->bands) * litBase->tones) s.litAlpha = litBase->alpha;
            const std::vector<uint8_t> dds = toDds(bake(s));
            if (!writeFile(fav.dds, dds.data(), dds.size())) continue;
        }
        v.push_back(std::move(fav));
    }
    return v;
}

bool Favourites::save(const Scheme& s, const Palette& baked, const std::string& existingFile, Fav& out, std::string* err) const {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (dir_.empty()) return fail("no folder for favourites");
    if (!s.valid() || s.character.empty()) return fail("nothing to save");
    std::error_code ec;
    const fs::path d = fs::path(dir_) / s.character;
    fs::create_directories(d, ec);
    if (ec) return fail("cannot create " + d.string());
    std::string file = existingFile;
    if (file.empty()) {
        const std::string base = slug(s.name);
        file = (d / (base + ".zgcol")).string();
        for (int n = 2; fs::exists(file, ec) && n < 1000; ++n) file = (d / (base + "_" + std::to_string(n) + ".zgcol")).string();
    }
    const std::string text = serialize(s);
    const std::vector<uint8_t> dds = toDds(baked);
    if (!writeFile(file, text.data(), text.size())) return fail("cannot write " + file);
    const std::string dpath = stemDds(file);
    if (!writeFile(dpath, dds.data(), dds.size())) return fail("cannot write " + dpath);
    out.character = s.character;
    out.name = s.name;
    out.file = file;
    out.dds = dpath;
    return true;
}

bool Favourites::remove(const Fav& f) const {
    std::error_code ec;
    bool ok = true;
    if (!f.file.empty()) ok = fs::remove(f.file, ec) && ok;
    if (!f.dds.empty()) fs::remove(f.dds, ec);
    return ok;
}

}  // namespace coledit
}  // namespace zengine
