#include "fx_store.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "aead.h"
#include "pafx.h"
#include "vault.h"

namespace zengine {
namespace fx {
namespace fs = std::filesystem;

namespace {

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeAtomic(const fs::path& p, const std::vector<uint8_t>& bytes) {
    const fs::path tmp = fs::path(p.string() + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool hasEveryProgram(const FxPack& pack) {
    for (const FxProgramSource& s : fxProgramSources())
        if (!pack.program(s.name)) return false;
    return true;
}

// The same job for a Core that lives sealed in the vault: `dir` is "vault:<id>/<folder>" (what Vfs hands out for a vault entry).
bool gatherFromVault(const std::string& dir, CoreShaderInputs& in, std::string* err) {
    std::shared_ptr<const vault::Vault> v;
    std::string base;
    if (!vault::resolveVirtual(dir, v, base)) {
        if (err) *err = "no such vault: " + dir;
        return false;
    }
    while (!base.empty() && base.back() == '/') base.pop_back();
    auto under = [&](const std::string& folder, const std::string& name) { return (folder.empty() ? std::string() : folder + "/") + name; };
    auto hasUnder = [&](const std::string& folder) {
        const std::string pre = folder.empty() ? std::string() : folder + "/";
        for (const PafxEntryInfo& e : v->entries())
            if (e.name.compare(0, pre.size(), pre) == 0) return true;
        return false;
    };
    // the vault root, like a Core folder, keeps its sources in shaders/
    std::string sd = base;
    if (sd.empty() && hasUnder("shaders")) sd = "shaders";
    if (!hasUnder(sd)) {
        if (err) *err = "no shaders folder in " + dir;
        return false;
    }
    const std::string pre = sd.empty() ? std::string() : sd + "/";
    std::vector<std::pair<std::string, uint64_t>> files;
    for (const PafxEntryInfo& e : v->entries())
        if (e.name.compare(0, pre.size(), pre) == 0) files.emplace_back(e.name.substr(pre.size()), static_cast<uint64_t>(e.size));
    in.fileCount = static_cast<int>(files.size());
    in.fingerprint = coreFingerprint(files);
    v->readText(under(sd, "TiledSprite_fp.fx"), in.tiledSpriteFp);
    v->readText(under(sd, "Otter_vp.fx"), in.otterVp);
    v->readText(under(sd, "Otter_fp.fx"), in.otterFp);
    if (in.tiledSpriteFp.empty() && in.otterVp.empty() && in.otterFp.empty()) {
        if (err) *err = "none of TiledSprite_fp.fx / Otter_vp.fx / Otter_fp.fx found in " + dir;
        return false;
    }
    return true;
}

}  // namespace

bool gatherCoreShaders(const std::string& dir, CoreShaderInputs& in, std::string* err) {
    in = CoreShaderInputs{};
    if (vault::isVirtualPath(dir)) return gatherFromVault(dir, in, err);
    const fs::path core(dir);
    std::error_code ec;
    const fs::path sd = fs::is_directory(core / "shaders", ec) ? core / "shaders" : core;
    if (!fs::is_directory(sd, ec)) {
        if (err) *err = "no shaders folder in " + dir;
        return false;
    }
    std::vector<std::pair<std::string, uint64_t>> files;
    for (auto it = fs::recursive_directory_iterator(sd, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        files.emplace_back(fs::relative(it->path(), sd).generic_string(), static_cast<uint64_t>(it->file_size(e2)));
    }
    in.fileCount = static_cast<int>(files.size());
    in.fingerprint = coreFingerprint(files);
    readAll(sd / "TiledSprite_fp.fx", in.tiledSpriteFp);
    readAll(sd / "Otter_vp.fx", in.otterVp);
    readAll(sd / "Otter_fp.fx", in.otterFp);
    if (in.tiledSpriteFp.empty() && in.otterVp.empty() && in.otterFp.empty()) {
        if (err) *err = "none of TiledSprite_fp.fx / Otter_vp.fx / Otter_fp.fx found in " + sd.string();
        return false;
    }
    return true;
}

bool loadOrBuildFxPack(const std::string& storeDir, const std::string& coreDir, FxPack& out, FxStoreInfo* info, std::string* err) {
    FxStoreInfo local;
    FxStoreInfo& inf = info ? *info : local;
    inf = FxStoreInfo{};

    const bool keep = !storeDir.empty();
    AeadKey key{};
    fs::path packPath, keyPath;
    if (keep) {
        std::error_code ec;
        fs::create_directories(storeDir, ec);
        packPath = fs::path(storeDir) / kFxPackFile;
        keyPath = fs::path(storeDir) / kFxKeyFile;
        if (!loadOrCreatePafxKey(keyPath.string(), key, err)) return false;
    } else if (!secureRandom(key.data(), key.size())) {
        if (err) *err = "no secure random source";
        return false;
    }

    CoreShaderInputs core;
    const bool haveCore = !coreDir.empty() && gatherCoreShaders(coreDir, core, nullptr);

    if (keep) {
        std::string raw;
        if (readAll(packPath, raw)) {
            FxPack existing;
            std::string e;
            if (!existing.load(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), key, &e)) {
                inf.why = "unreadable";   // damaged, or sealed with another key: rebuild
            } else if (!hasEveryProgram(existing)) {
                inf.why = "built before this version";
            } else if (haveCore && existing.coreFingerprint() != core.fingerprint) {
                inf.why = "the Core changed";
            } else {
                out = std::move(existing);
                inf.fromCore = out.tuningFromCore();
                inf.fingerprint = out.coreFingerprint();
                inf.persisted = true;
                return true;
            }
        } else {
            inf.why = "created";
        }
    } else {
        inf.why = "created";
    }

    std::array<uint8_t, 16> id{};
    if (!secureRandom(id.data(), id.size())) {
        if (err) *err = "no secure random source";
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!buildFxPack(haveCore ? core : CoreShaderInputs{}, key, id, bytes, nullptr, err)) return false;
    FxPack made;
    if (!made.load(bytes.data(), bytes.size(), key, err)) return false;
    inf.built = true;
    if (keep) inf.persisted = writeAtomic(packPath, bytes);   // a failed write is not fatal: the pack still works from memory
    out = std::move(made);
    inf.fromCore = out.tuningFromCore();
    inf.fingerprint = out.coreFingerprint();
    return true;
}

}  // namespace fx
}  // namespace zengine
