#include "vault.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>

#include "../fx/aead.h"
#include "vfs.h"

namespace zengine {
namespace vault {
namespace fs = std::filesystem;

namespace {

constexpr const char* kKeyFile = "pafx.key";
constexpr const char kVirtualPrefix[] = "vault:";

std::string lowerCopy(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// What makes a folder "a Core": the shaders folder, or the stage / music / table-of-contents ini files.
bool looksLikeCore(const fs::path& p) {
    std::error_code ec;
    if (isDir(p / "shaders")) return true;
    for (const char* n : {"stages.ini", "music.ini", "toc.ini"})
        if (fs::is_regular_file(p / n, ec)) return true;
    return false;
}

bool readWhole(const std::string& path, std::vector<uint8_t>& out) { return readFile(path, out); }

bool writeAtomic(const fs::path& p, const std::vector<uint8_t>& bytes, std::string* err) {
    const fs::path tmp = fs::path(p.string() + ".tmp");
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        const bool closed = std::fclose(f) == 0;
        if (!ok || !closed) {
            std::error_code ec;
            fs::remove(tmp, ec);
            if (err) *err = "cannot write " + tmp.string() + " (disk full?)";
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (err) *err = "cannot replace " + p.string();
        return false;
    }
    return true;
}

// ---- registry of opened vaults (what the virtual paths point at)
std::mutex g_mu;
std::vector<std::shared_ptr<const Vault>> g_vaults;   // id = index + 1

// The subfolder of `p` named "core" (any case), or an empty path.
fs::path coreChild(const fs::path& p) {
    std::error_code ec;
    for (fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2) && lowerCopy(it->path().filename().string()) == "core") return it->path();
    }
    return {};
}

}  // namespace

std::string findCoreRoot(const std::string& dir) {
    const fs::path d(dir);
    if (!isDir(d)) return {};
    if (looksLikeCore(d)) return d.string();
    // one or two levels of "core" below (the way extractions nest it), whatever the case of the folder name
    const fs::path c1 = coreChild(d);
    if (c1.empty()) return {};
    if (looksLikeCore(c1)) return c1.string();
    const fs::path c2 = coreChild(c1);
    if (!c2.empty() && looksLikeCore(c2)) return c2.string();
    return {};
}

bool build(const std::string& dir, const std::string& storeDir, BuildReport* rep, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (storeDir.empty()) return fail("no private folder to keep the vault in");
    const std::string root = findCoreRoot(dir);
    if (root.empty()) return fail("that folder does not hold a Core (no shaders folder and no stages.ini / music.ini / toc.ini)");

    // collect first (sorted, so the vault of the same Core is laid out the same way every time), then read
    std::vector<std::pair<std::string, fs::path>> files;   // {entry name, real path}
    uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const uint64_t sz = static_cast<uint64_t>(it->file_size(e2));
        if (e2) continue;
        if (sz > kMaxFileBytes) return fail("a file in that folder is far too big for a Core: " + it->path().filename().string());
        total += sz;
        if (total > kMaxTotalBytes) return fail("that folder is far too big for a Core (wrong folder? pick the folder named core)");
        std::string rel = fs::relative(it->path(), root, e2).generic_string();
        if (e2 || rel.empty()) continue;
        files.emplace_back(std::move(rel), it->path());
    }
    if (files.empty()) return fail("that Core folder is empty");
    std::sort(files.begin(), files.end());

    fx::PafxWriter w;
    uint64_t bytes = 0;
    for (const auto& f : files) {
        std::vector<uint8_t> data;
        if (!readWhole(f.second.string(), data)) return fail("cannot read " + f.first);
        bytes += data.size();
        w.add(f.first, fx::PafxKind::Raw, data);
    }

    fs::create_directories(storeDir, ec);
    fx::AeadKey key{};
    std::string kerr;
    if (!fx::loadOrCreatePafxKey((fs::path(storeDir) / kKeyFile).string(), key, &kerr)) return fail(kerr);
    std::array<uint8_t, 16> id{};
    if (!fx::secureRandom(id.data(), id.size())) return fail("no secure random source");
    std::vector<uint8_t> blob;
    std::string berr;
    if (!w.build(key, id, blob, &berr)) return fail(berr);
    if (!writeAtomic(fs::path(storeDir) / kVaultFile, blob, err)) return false;
    if (rep) {
        rep->files = files.size();
        rep->bytes = bytes;
        rep->root = root;
    }
    return true;
}

bool Vault::openBytes(std::vector<uint8_t> blob, const fx::AeadKey& key, std::string* err) {
    blob_ = std::move(blob);
    ar_ = fx::PafxArchive();
    return ar_.open(blob_.data(), blob_.size(), key, err);
}

bool Vault::open(const std::string& storeDir, std::string* err) {
    const fs::path vp = fs::path(storeDir) / kVaultFile, kp = fs::path(storeDir) / kKeyFile;
    std::vector<uint8_t> blob;
    if (!readWhole(vp.string(), blob)) {
        if (err) *err = "no Core vault yet";
        return false;
    }
    // the key is only ever read here: a missing key means the vault can never open, and a new key must not be invented for it
    fx::AeadKey key{};
    std::vector<uint8_t> k;
    if (!readWhole(kp.string(), k) || k.size() < key.size()) {
        if (err) *err = "the vault's key is missing";
        return false;
    }
    std::copy(k.begin(), k.begin() + static_cast<std::ptrdiff_t>(key.size()), key.begin());
    return openBytes(std::move(blob), key, err);
}

// ---- virtual paths
bool isVirtualPath(std::string_view path) { return path.substr(0, sizeof(kVirtualPrefix) - 1) == kVirtualPrefix; }

int registerVault(std::shared_ptr<const Vault> v) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (size_t i = 0; i < g_vaults.size(); ++i)
        if (g_vaults[i] == v) return static_cast<int>(i) + 1;   // a re-mount (rescan) of the same vault keeps its id
    g_vaults.push_back(std::move(v));
    return static_cast<int>(g_vaults.size());
}

std::string virtualPath(int id, const std::string& name) { return std::string(kVirtualPrefix) + std::to_string(id) + "/" + name; }

bool resolveVirtual(std::string_view path, std::shared_ptr<const Vault>& v, std::string& name) {
    v.reset();
    name.clear();
    if (!isVirtualPath(path)) return false;
    path.remove_prefix(sizeof(kVirtualPrefix) - 1);
    size_t i = 0;
    int id = 0;
    while (i < path.size() && path[i] >= '0' && path[i] <= '9' && i < 6) id = id * 10 + (path[i++] - '0');
    if (i == 0 || id <= 0) return false;
    if (i < path.size()) {
        if (path[i] != '/') return false;
        ++i;
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (static_cast<size_t>(id) > g_vaults.size()) return false;
        v = g_vaults[static_cast<size_t>(id) - 1];
    }
    name.assign(path.substr(i));
    return v != nullptr;
}

bool readVirtual(std::string_view path, std::vector<uint8_t>& out) {
    std::shared_ptr<const Vault> v;
    std::string name;
    if (!resolveVirtual(path, v, name)) return false;
    return v->read(name, out, nullptr);
}

void clearRegistry() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_vaults.clear();
}

// ---- the import the Android shell leaves behind
ImportResult importPending(const std::string& importDir, const std::string& storeDir) {
    ImportResult r;
    if (importDir.empty() || !isDir(importDir)) return r;
    r.attempted = true;
    BuildReport rep;
    std::string err;
    r.ok = build(importDir, storeDir, &rep, &err);
    r.files = rep.files;
    r.bytes = rep.bytes;
    r.message = r.ok ? "Core sealed: " + std::to_string(rep.files) + " files" : err;
    // the plaintext copy goes either way: a good one is now inside the vault, a bad one would only be picked up and fail again
    std::error_code ec;
    fs::remove_all(importDir, ec);
    if (!storeDir.empty()) {
        const fs::path st = fs::path(storeDir) / kImportStatusFile;
        if (std::FILE* f = std::fopen(st.string().c_str(), "wb")) {
            const std::string line = std::string(r.ok ? "ok " : "error ") + r.message + "\n";
            std::fwrite(line.data(), 1, line.size(), f);
            std::fclose(f);
        }
    }
    return r;
}

}  // namespace vault
}  // namespace zengine
