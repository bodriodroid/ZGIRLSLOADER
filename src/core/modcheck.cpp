#include "modcheck.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>

#include "dds.h"
#include "foit.h"
#include "gbs.h"
#include "palmod.h"
#include "vfs.h"

namespace zengine {
namespace modcheck {

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kFullParseLimit = 96ull * 1024 * 1024;   // .foit / .gbs are parsed completely up to this size

std::string lowerExt(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return e;
}

std::string slashes(std::string s) {
    for (char& c : s)
        if (c == '\\') c = '/';
    return s;
}

uint32_t le32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }

bool fail(std::string* what, const std::string& why) {
    if (what) *what = why;
    return false;
}

// Random access to the bytes of a file or of a memory block, so the big media files are checked by seeking instead of reading them whole.
using ReadAt = std::function<bool(uint64_t off, size_t len, uint8_t* dst)>;

ReadAt memoryReader(const uint8_t* d, size_t n) {
    return [d, n](uint64_t off, size_t len, uint8_t* dst) {
        if (off + len > n) return false;
        std::memcpy(dst, d + off, len);
        return true;
    };
}

bool checkDds(const uint8_t* hdr, size_t hdrLen, uint64_t fileSize, std::string* what) {
    DdsInfo info;
    std::string e;
    if (!ddsHeader(hdr, hdrLen, info, &e)) return fail(what, "not a valid DDS header" + (e.empty() ? std::string() : " (" + e + ")"));
    if (info.width < 1 || info.height < 1 || info.width > 16384 || info.height > 16384) return fail(what, "DDS size out of range");
    uint64_t need = 0;
    const uint64_t w = static_cast<uint64_t>(info.width), h = static_cast<uint64_t>(info.height);
    const uint64_t blocks = ((w + 3) / 4) * ((h + 3) / 4);
    switch (info.format) {
        case DdsFormat::DXT1: need = blocks * 8; break;
        case DdsFormat::DXT3:
        case DdsFormat::DXT5: need = blocks * 16; break;
        case DdsFormat::BGRA8:
        case DdsFormat::RGBA8: need = w * h * 4; break;
        case DdsFormat::BGR8: need = w * h * 3; break;
        case DdsFormat::RGB565: need = w * h * 2; break;
        case DdsFormat::L8:
        case DdsFormat::A8: need = w * h; break;
        default: break;   // unknown pixel layout: the header was fine, nothing more can be said
    }
    if (need && fileSize < 128 + need)
        return fail(what, "DDS cut short (" + std::to_string(fileSize) + " bytes, needs at least " + std::to_string(128 + need) + ")");
    return true;
}

bool checkWav(uint64_t n, const ReadAt& rd, std::string* what) {
    uint8_t head[12];
    if (n < 12 || !rd(0, 12, head) || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
        return fail(what, "not a RIFF/WAVE file");
    uint64_t pos = 12;
    bool fmt = false, data = false;
    while (pos + 8 <= n) {
        uint8_t ch[8];
        if (!rd(pos, 8, ch)) break;
        const uint32_t sz = le32(ch + 4);
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            if (sz < 16 || pos + 8 + 16 > n) return fail(what, "WAV fmt chunk is cut short");
            fmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            if (pos + 8 + sz > n) return fail(what, "WAV audio is cut short");
            data = true;
        }
        const uint64_t next = pos + 8 + sz + (sz & 1u);
        if (next > n) break;
        pos = next;
    }
    if (!fmt) return fail(what, "WAV has no fmt chunk");
    if (!data) return fail(what, "WAV has no audio data");
    return true;
}

bool checkPng(uint64_t n, const ReadAt& rd, std::string* what) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    static const uint8_t iend[8] = {0, 0, 0, 0, 'I', 'E', 'N', 'D'};
    uint8_t head[16], tail[12];
    if (n < 8 + 25 + 12 || !rd(0, 16, head) || std::memcmp(head, sig, 8) != 0) return fail(what, "not a PNG");
    if (std::memcmp(head + 12, "IHDR", 4) != 0) return fail(what, "PNG has no header chunk");
    if (!rd(n - 12, 12, tail) || std::memcmp(tail, iend, 8) != 0) return fail(what, "PNG cut short (no end chunk)");
    return true;
}

bool checkIni(const uint8_t* d, size_t n, std::string* what) {
    if (std::memchr(d, 0, n) != nullptr) return fail(what, "text file holds binary data");
    return true;
}

// the structured formats are parsed from their bytes
bool checkStructured(const std::string& ext, const uint8_t* data, size_t size, std::string* what) {
    if (ext == ".foit") {
        foit::Character c;
        std::string e;
        if (!foit::parse(data, size, c, &e)) return fail(what, "character data does not parse" + (e.empty() ? std::string() : " (" + e + ")"));
        return true;
    }
    if (ext == ".gbs") {
        Gbs g;
        std::string e;
        if (!parseGbs(data, size, g, &e)) return fail(what, "UI scene does not parse" + (e.empty() ? std::string() : " (" + e + ")"));
        return true;
    }
    if (ext == ".pal") {
        palmod::PalFile p;
        std::string e;
        if (!palmod::parse(data, size, p, &e)) return fail(what, "palette does not parse" + (e.empty() ? std::string() : " (" + e + ")"));
        return true;
    }
    if (ext == ".ini") return checkIni(data, size, what);
    return true;
}

}  // namespace

bool isCodeExtension(const std::string& ext) {
    static const std::set<std::string> kCode = {".so", ".dll", ".exe", ".dylib", ".apk", ".dex", ".jar", ".bat", ".cmd",
                                                ".sh", ".ps1", ".msi", ".vbs", ".com", ".scr", ".bin"};
    return kCode.count(ext) != 0;
}

bool checkBytes(const std::string& ext, const uint8_t* data, size_t size, std::string* what) {
    if (ext == ".dds") return checkDds(data, size, size, what);
    if (ext == ".wav") return checkWav(size, memoryReader(data, size), what);
    if (ext == ".png") return checkPng(size, memoryReader(data, size), what);
    return checkStructured(ext, data, size, what);
}

bool Result::damaged() const {
    for (const Issue& i : issues)
        if (i.sev == Severity::Damaged) return true;
    return false;
}

bool Result::altered() const {
    for (const Issue& i : issues)
        if (i.sev == Severity::Altered) return true;
    return false;
}

std::string Result::firstProblem() const {
    for (Severity s : {Severity::Altered, Severity::Damaged})
        for (const Issue& i : issues)
            if (i.sev == s) return i.file + ": " + i.what;
    return std::string();
}

Result check(const std::string& dir, size_t maxFiles) {
    Result res;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return res;
    const fs::path root = fs::weakly_canonical(fs::path(dir), ec);
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        std::error_code e2;
        const fs::file_status ls = fs::symlink_status(p, e2);
        const std::string rel = slashes(fs::relative(p, fs::path(dir), e2).string());
        if (fs::is_symlink(ls)) {
            const fs::path target = fs::weakly_canonical(p, e2);
            const std::string t = target.string(), r = root.string();
            const bool inside = !e2 && t.size() >= r.size() && t.compare(0, r.size(), r) == 0 && (t.size() == r.size() || t[r.size()] == fs::path::preferred_separator);
            if (!inside) res.issues.push_back({Severity::Altered, rel, "link that leaves the mod folder"});
            if (fs::is_directory(ls)) it.disable_recursion_pending();
            continue;
        }
        if (!fs::is_regular_file(ls)) continue;
        if (res.files >= maxFiles) {
            res.scanCut = true;
            break;
        }
        ++res.files;
        const std::string ext = lowerExt(p);
        const uint64_t size = static_cast<uint64_t>(fs::file_size(p, e2));
        if (e2) {
            res.issues.push_back({Severity::Damaged, rel, "cannot read the file"});
            continue;
        }
        res.bytes += size;
        if (isCodeExtension(ext)) {
            res.issues.push_back({Severity::Altered, rel, "native code / script inside a mod (" + ext + ")"});
            continue;
        }
        const bool textLike = ext == ".txt" || ext == ".md" || ext == ".ini";
        if (size == 0) {
            if (!textLike) res.issues.push_back({Severity::Damaged, rel, "empty file"});
            continue;
        }
        const bool structured = ext == ".foit" || ext == ".gbs" || ext == ".pal" || ext == ".ini";
        const bool media = ext == ".dds" || ext == ".wav" || ext == ".png";
        if (!structured && !media) continue;
        if (structured && ext != ".ini" && size > kFullParseLimit) continue;
        std::ifstream f(p, std::ios::binary);
        if (!f) {
            res.issues.push_back({Severity::Damaged, rel, "cannot read the file"});
            continue;
        }
        std::string why;
        bool ok = true;
        if (structured) {
            std::vector<uint8_t> buf(static_cast<size_t>(size));
            f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
            if (static_cast<uint64_t>(f.gcount()) != size) ok = fail(&why, "cannot read the file");
            else ok = checkStructured(ext, buf.data(), buf.size(), &why);
        } else {
            const ReadAt rd = [&f](uint64_t off, size_t len, uint8_t* dst) {
                f.clear();
                f.seekg(static_cast<std::streamoff>(off));
                f.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(len));
                return static_cast<size_t>(f.gcount()) == len;
            };
            if (ext == ".dds") {
                uint8_t hdr[128];
                const size_t got = size < 128 ? static_cast<size_t>(size) : 128;
                if (!rd(0, got, hdr)) ok = fail(&why, "cannot read the file");
                else ok = checkDds(hdr, got, size, &why);
            } else if (ext == ".wav") {
                ok = checkWav(size, rd, &why);
            } else {
                ok = checkPng(size, rd, &why);
            }
        }
        if (!ok) res.issues.push_back({Severity::Damaged, rel, why});
    }
    return res;
}

void report(const std::string& label, const Result& r, err::Registry& reg) {
    int shown = 0;
    for (Severity s : {Severity::Altered, Severity::Damaged}) {
        for (const Issue& i : r.issues) {
            if (i.sev != s) continue;
            reg.raise(s == Severity::Altered ? err::kRuntimeAltered : err::kModDamaged, label + ": " + i.file + ": " + i.what);
            if (++shown >= 3) return;
        }
    }
}

std::vector<Conflict> conflicts(const std::vector<Layer>& layers, size_t maxConflicts) {
    struct Entry {
        size_t layer;
        std::string rel;   // normalised relative path
    };
    std::map<std::string, std::vector<Entry>> byName;   // lowercase file name -> every file of that name
    for (size_t li = 0; li < layers.size(); ++li) {
        std::error_code ec;
        if (!fs::is_directory(layers[li].dir, ec)) continue;
        fs::recursive_directory_iterator it(layers[li].dir, fs::directory_options::skip_permission_denied, ec), end;
        size_t n = 0;
        for (; !ec && it != end && n < 200000; it.increment(ec), ++n) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            const std::string rel = normalizeKey(slashes(fs::relative(it->path(), layers[li].dir, e2).string()));
            const std::string base = rel.substr(rel.find_last_of('/') == std::string::npos ? 0 : rel.find_last_of('/') + 1);
            if (base == "mod.ini" || base == "pack.ini" || base == "stage.ini" || base == "readme.txt") continue;
            byName[base].push_back({li, rel});
        }
    }
    std::vector<Conflict> out;
    auto endsWithPath = [](const std::string& longer, const std::string& shorter) {
        if (longer == shorter) return true;
        return longer.size() > shorter.size() && longer.compare(longer.size() - shorter.size(), shorter.size(), shorter) == 0 &&
               longer[longer.size() - shorter.size() - 1] == '/';
    };
    for (auto& kv : byName) {
        auto& v = kv.second;
        if (v.size() < 2) continue;
        std::stable_sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.rel.size() > b.rel.size(); });
        struct Group {
            std::string anchor;
            std::vector<size_t> layers;
        };
        std::vector<Group> groups;
        for (const Entry& e : v) {
            Group* g = nullptr;
            for (Group& cand : groups)
                if (endsWithPath(cand.anchor, e.rel)) {
                    g = &cand;
                    break;
                }
            if (!g) {
                groups.push_back({e.rel, {}});
                g = &groups.back();
            }
            if (std::find(g->layers.begin(), g->layers.end(), e.layer) == g->layers.end()) g->layers.push_back(e.layer);
        }
        for (Group& g : groups) {
            if (g.layers.size() < 2) continue;
            std::sort(g.layers.begin(), g.layers.end());
            Conflict c;
            c.path = g.anchor;
            c.winner = layers[g.layers.back()].id;
            for (size_t i = 0; i + 1 < g.layers.size(); ++i) c.overridden.push_back(layers[g.layers[i]].id);
            out.push_back(std::move(c));
            if (out.size() >= maxConflicts) break;
        }
        if (out.size() >= maxConflicts) break;
    }
    std::sort(out.begin(), out.end(), [](const Conflict& a, const Conflict& b) { return a.path < b.path; });
    return out;
}

}  // namespace modcheck
}  // namespace zengine
