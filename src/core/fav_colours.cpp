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
std::string stemExt(const std::string& zgcol, const char* ext) {
    fs::path p(zgcol);
    p.replace_extension(ext);
    return p.string();
}
std::string stemDds(const std::string& zgcol) {
    fs::path p(zgcol);
    p.replace_extension(".dds");
    return p.string();
}
}  // namespace

bool toPalFile(const Scheme& s, palmod::PalFile& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    const std::vector<int>* counts = palmod::regionCounts(s.character);
    if (!counts) return fail("no region table for " + s.character);
    if (counts->size() != s.regions.size()) return fail("the region table of " + s.character + " has another number of regions");
    const std::string* header = nullptr;
    for (const std::string& r : palmod::rosterFolders())
        if (palmod::normaliseName(r) == s.character) header = &r;
    if (!header) return fail(s.character + " is not in the roster");
    out = palmod::PalFile();
    out.character = *header;
    const bool haveLit = s.litAlpha.size() == (s.regions.size() + 1) * static_cast<size_t>(s.tones);
    for (size_t r = 0; r < s.regions.size(); ++r) {
        const std::vector<Rgb>& keys = s.regions[r].key;
        if (static_cast<int>(keys.size()) != (*counts)[r]) return fail("region " + std::to_string(r + 1) + " has another number of keys than the table");
        const int n = static_cast<int>(keys.size());
        for (int i = 0; i < n; ++i) {
            // the first key sits at the lightest tone, the last at tone 0 (palmod.h)
            const int tone = n == 1 ? s.tones - 1 : (s.tones - 1) * (n - 1 - i) / (n - 1);
            const uint32_t a = haveLit ? s.litAlpha[(r + 1) * static_cast<size_t>(s.tones) + static_cast<size_t>(tone)] : 0xFFu;
            out.colors.push_back((a << 24) | (static_cast<uint32_t>(keys[static_cast<size_t>(i)][0]) << 16) |
                                 (static_cast<uint32_t>(keys[static_cast<size_t>(i)][1]) << 8) | keys[static_cast<size_t>(i)][2]);
        }
    }
    out.numColors = static_cast<uint32_t>(out.colors.size()) + 1;
    const bool black = s.outline[0] == 0 && s.outline[1] == 0 && s.outline[2] == 0;
    out.line = black ? 0u : (0xFF000000u | (static_cast<uint32_t>(s.outline[0]) << 16) | (static_cast<uint32_t>(s.outline[1]) << 8) | s.outline[2]);
    return true;
}

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
    out.pal.clear();
    palmod::PalFile pf;
    const std::string ppath = stemExt(file, ".pal");
    if (toPalFile(s, pf)) {   // a small extra: the same colours as a community .pal (best effort: the favourite is saved either way)
        const std::vector<uint8_t> pb = palmod::serialize(pf);
        if (writeFile(ppath, pb.data(), pb.size())) out.pal = ppath;
    } else {
        fs::remove(ppath, ec);   // an older .pal of this favourite no longer matches
    }
    return true;
}

bool Favourites::remove(const Fav& f) const {
    std::error_code ec;
    bool ok = true;
    if (!f.file.empty()) ok = fs::remove(f.file, ec) && ok;
    if (!f.dds.empty()) fs::remove(f.dds, ec);
    if (!f.file.empty()) fs::remove(stemExt(f.file, ".pal"), ec);
    return ok;
}

}  // namespace coledit
}  // namespace zengine
