// test_vault.cpp - the sealed copy of the player's Core (src/core/vault.*): build, open, tamper, the virtual file system on top of it,
// the effects pack reading its shaders out of it, and the staging import the Android shell leaves behind.
// Everything runs on synthetic files; no game data is read.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "aead.h"
#include "fx_store.h"
#include "pafx.h"
#include "vault.h"
#include "vfs.h"

using namespace zengine;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) {                                                               \
            ++g_fail;                                                             \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);              \
        }                                                                         \
    } while (0)

static void put(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}
static std::vector<uint8_t> slurpBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}
static void writeBytes(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
static bool contains(const std::vector<uint8_t>& hay, const std::string& needle) {
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}
static std::string asString(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

static const char* kMarker = "MARKER-THAT-MUST-NOT-APPEAR-IN-THE-VAULT-FILE";

// A small Core: shaders, ini files, a binary scene with every byte value, an empty file.
static std::string makeCore(const fs::path& core) {
    put(core / "shaders" / "TiledSprite_fp.fx", std::string("// tiled sprite\n") + kMarker + "\n");
    put(core / "shaders" / "Otter_vp.fx", "// otter vertex\n");
    put(core / "shaders" / "Otter_fp.fx", "// otter fragment\n");
    put(core / "shaders" / "pssl" / "other.fx", "x");
    put(core / "stages.ini", "[stage]\nname=one\n");
    put(core / "music.ini", "[song]\nname=two\n");
    std::string all;
    for (int i = 0; i < 256; ++i) all.push_back(static_cast<char>(i));
    for (int i = 0; i < 4; ++i) all += all;   // 1 KiB, repeated bytes
    put(core / "scenes" / "Menu.GBS", all);
    put(core / "empty.bin", "");
    return all;
}

static fs::path tempRoot(const char* tag) {
    const fs::path root = fs::temp_directory_path() / (std::string("zd_vault_") + tag + "_" + std::to_string(static_cast<long>(std::rand())) + "_" +
                                                       std::to_string(static_cast<long>(std::time(nullptr))));
    fs::remove_all(root);
    return root;
}

static void testFindRoot() {
    const fs::path root = tempRoot("root");
    makeCore(root / "a");
    CHECK(vault::findCoreRoot((root / "a").string()) == (root / "a").string());   // the Core itself
    makeCore(root / "b" / "core");
    CHECK(vault::findCoreRoot((root / "b").string()) == (root / "b" / "core").string());   // the folder that holds it
    makeCore(root / "c" / "Core" / "core");
    CHECK(vault::findCoreRoot((root / "c").string()) == (root / "c" / "Core" / "core").string());   // nested twice, any case
    put(root / "d" / "sprites" / "x.dds", "no");
    CHECK(vault::findCoreRoot((root / "d").string()).empty());
    CHECK(vault::findCoreRoot((root / "nothing").string()).empty());
    CHECK(vault::findCoreRoot("").empty());
    put(root / "e" / "stages.ini", "[a]\n");   // an ini alone is enough to be recognised
    CHECK(vault::findCoreRoot((root / "e").string()) == (root / "e").string());
    fs::remove_all(root);
}

static void testBuildOpen() {
    const fs::path root = tempRoot("build");
    const std::string all = makeCore(root / "in");
    const fs::path store = root / "private";
    vault::BuildReport rep;
    std::string err;
    CHECK(vault::build(root.string() + "/in", store.string(), &rep, &err));
    CHECK(rep.files == 8 && rep.root == (root / "in").string());
    CHECK(fs::exists(store / vault::kVaultFile) && fs::exists(store / "pafx.key") && fs::file_size(store / "pafx.key") == 32);
    CHECK(!fs::exists(store / (std::string(vault::kVaultFile) + ".tmp")));   // written through a temporary file that is gone

    // nothing readable inside the file on disk: not the marker, not an entry name
    const std::vector<uint8_t> blob = slurpBytes(store / vault::kVaultFile);
    CHECK(blob.size() > 1000 && std::memcmp(blob.data(), "PAFX", 4) == 0);
    CHECK(!contains(blob, kMarker) && !contains(blob, "TiledSprite") && !contains(blob, "stages.ini") && !contains(blob, "otter"));

    vault::Vault v;
    CHECK(v.open(store.string(), &err));
    CHECK(v.entries().size() == 8 && v.has("shaders/TiledSprite_fp.fx") && v.has("scenes/Menu.GBS") && !v.has("tiledsprite_fp.fx"));
    std::vector<uint8_t> out;
    CHECK(v.read("scenes/Menu.GBS", out) && asString(out) == all);
    std::string text;
    CHECK(v.readText("shaders/TiledSprite_fp.fx", text) && text.find(kMarker) != std::string::npos);
    CHECK(v.read("empty.bin", out) && out.empty());
    CHECK(!v.read("no/such/file", out, &err) && !err.empty());

    // the same Core sealed again: the key stays, the vault is a different file (fresh archive id)
    const std::vector<uint8_t> key1 = slurpBytes(store / "pafx.key");
    CHECK(vault::build((root / "in").string(), store.string(), nullptr, &err));
    CHECK(slurpBytes(store / "pafx.key") == key1);
    CHECK(slurpBytes(store / vault::kVaultFile) != blob);
    vault::Vault v2;
    CHECK(v2.open(store.string(), &err) && v2.read("scenes/Menu.GBS", out) && asString(out) == all);

    // not a Core, empty folder, no store: refused with a reason, nothing written, the vault that is there stays
    const std::vector<uint8_t> before = slurpBytes(store / vault::kVaultFile);
    put(root / "junk" / "readme.txt", "hello");
    CHECK(!vault::build((root / "junk").string(), store.string(), nullptr, &err) && err.find("does not hold a Core") != std::string::npos);
    CHECK(!vault::build((root / "missing").string(), store.string(), nullptr, &err));
    CHECK(!vault::build((root / "in").string(), "", nullptr, &err) && !err.empty());
    CHECK(slurpBytes(store / vault::kVaultFile) == before);

    fs::remove_all(root);
}

static void testTamperAndKeys() {
    const fs::path root = tempRoot("tamper");
    makeCore(root / "in");
    const fs::path store = root / "private";
    std::string err;
    CHECK(vault::build((root / "in").string(), store.string(), nullptr, &err));
    const std::vector<uint8_t> blob = slurpBytes(store / vault::kVaultFile);
    const std::vector<uint8_t> key = slurpBytes(store / "pafx.key");

    // another install: same vault file, its own key -> refused, never read
    {
        const fs::path other = root / "other";
        fs::create_directories(other);
        writeBytes(other / vault::kVaultFile, blob);
        std::vector<uint8_t> k2 = key;
        k2[0] ^= 0x01;
        writeBytes(other / "pafx.key", k2);
        vault::Vault v;
        CHECK(!v.open(other.string(), &err) && err.find("wrong key") != std::string::npos);
    }
    // no key: refused, and no key is invented for it
    {
        const fs::path nokey = root / "nokey";
        fs::create_directories(nokey);
        writeBytes(nokey / vault::kVaultFile, blob);
        vault::Vault v;
        CHECK(!v.open(nokey.string(), &err) && err.find("key is missing") != std::string::npos);
        CHECK(!fs::exists(nokey / "pafx.key"));
        writeBytes(nokey / "pafx.key", std::vector<uint8_t>(10, 7));   // too short
        CHECK(!v.open(nokey.string(), &err));
    }
    // nothing there at all
    {
        vault::Vault v;
        CHECK(!v.open((root / "empty").string(), &err) && err == "no Core vault yet");
    }
    // a damaged table (byte 70 is inside the sealed table) and a truncated file never open
    {
        std::vector<uint8_t> b = blob;
        b[70] ^= 0x80;
        vault::Vault v;
        fx::AeadKey k;
        std::memcpy(k.data(), key.data(), 32);
        CHECK(!v.openBytes(b, k, &err));
        b = blob;
        b.resize(b.size() - 1);
        CHECK(!v.openBytes(b, k, &err));
        b = blob;
        b.push_back(0);
        CHECK(!v.openBytes(b, k, &err));
        CHECK(!v.openBytes(std::vector<uint8_t>(10, 0), k, &err));
    }
    // a flipped byte inside ONE entry: the vault opens, that entry is refused, the others still read
    {
        vault::Vault good;
        fx::AeadKey k;
        std::memcpy(k.data(), key.data(), 32);
        CHECK(good.openBytes(blob, k, &err));
        // entries are sealed back to back at the end of the file: damage the last byte of the file (inside the last entry's tag)
        std::vector<uint8_t> b = blob;
        b.back() ^= 0x01;
        vault::Vault v;
        CHECK(v.openBytes(b, k, &err));
        int bad = 0, ok = 0;
        for (const fx::PafxEntryInfo& e : v.entries()) {
            std::vector<uint8_t> out;
            (v.read(e.name, out) ? ok : bad)++;
        }
        CHECK(bad == 1 && ok == static_cast<int>(v.entries().size()) - 1);
    }
    fs::remove_all(root);
}

static void testVfs() {
    vault::clearRegistry();
    const fs::path root = tempRoot("vfs");
    const std::string all = makeCore(root / "in");
    const fs::path store = root / "private";
    std::string err;
    CHECK(vault::build((root / "in").string(), store.string(), nullptr, &err));
    auto v = std::make_shared<vault::Vault>();
    CHECK(v->open(store.string(), &err));

    Vfs vfs;
    CHECK(vfs.mountVault(v) == 8);
    CHECK(vfs.mounts().size() == 1 && vault::isVirtualPath(vfs.mounts()[0]));
    std::string real;
    CHECK(vfs.resolve("shaders/TiledSprite_fp.fx", &real) && vault::isVirtualPath(real) && real.find("vault:1/") == 0);
    CHECK(vfs.exists("SHADERS\\TILEDSPRITE_FP.FX") && vfs.exists("tiledsprite_fp.fx") && vfs.exists("pssl/other.fx") && !vfs.exists("nope.fx"));
    std::vector<uint8_t> out;
    CHECK(vfs.read("MENU.gbs", out) && asString(out) == all);          // any case, any suffix
    CHECK(readFile(real, out) && asString(out).find(kMarker) != std::string::npos);   // the global reader understands the virtual path
    CHECK(vfs.listByExt(".gbs").size() == 1 && vfs.pick("Menu.GBS", &real) && vault::isVirtualPath(real));
    std::string text;
    CHECK(vfs.readText("stages.ini", text) && text.find("name=one") != std::string::npos);

    // virtual paths that make no sense are plain misses, not crashes
    CHECK(!readFile("vault:99/x", out) && !readFile("vault:/x", out) && !readFile("vault:abc", out) && !readFile("vault:1/no/such", out));
    CHECK(!readFile("vault:", out) && !readFile("vault:0/stages.ini", out) && !readFile("vault:-1/stages.ini", out));
    std::shared_ptr<const vault::Vault> rv;
    std::string name;
    CHECK(vault::resolveVirtual("vault:1", rv, name) && rv && name.empty());
    CHECK(vault::resolveVirtual("vault:1/shaders", rv, name) && name == "shaders");
    CHECK(!vault::resolveVirtual("vault:1x/shaders", rv, name) && !rv);
    CHECK(!vault::resolveVirtual("/tmp/vault:1/shaders", rv, name));

    // mounting the same vault again (a rescan) keeps its id
    Vfs again;
    CHECK(again.mountVault(v) == 8 && again.mounts()[0] == vfs.mounts()[0]);

    // layering: whatever is mounted later wins, in both orders
    put(root / "over" / "stages.ini", "[stage]\nname=override\n");
    Vfs a;
    a.mountVault(v);
    CHECK(a.mount((root / "over").string()) == 1);
    CHECK(a.readText("stages.ini", text) && text.find("override") != std::string::npos);
    CHECK(a.readText("music.ini", text) && text.find("name=two") != std::string::npos);   // the rest still comes from the vault
    Vfs b;
    b.mount((root / "over").string());
    b.mountVault(v);
    CHECK(b.readText("stages.ini", text) && text.find("name=one") != std::string::npos);

    // a vault that no longer opens (tamper after mounting is not possible: it is in memory), an empty one mounts nothing
    CHECK(vfs.mountVault(nullptr) == 0);
    fs::remove_all(root);
    vault::clearRegistry();
}

static void testShadersFromVault() {
    vault::clearRegistry();
    const fs::path root = tempRoot("shaders");
    makeCore(root / "in");
    const fs::path store = root / "private";
    std::string err;
    CHECK(vault::build((root / "in").string(), store.string(), nullptr, &err));
    auto v = std::make_shared<vault::Vault>();
    CHECK(v->open(store.string(), &err));
    Vfs vfs;
    vfs.mountVault(v);

    // the folder Play derives from the virtual path of a shader source is "vault:<id>/shaders"
    std::string real;
    CHECK(vfs.pick("TiledSprite_fp.fx", &real));
    const std::string vdir = fs::path(real).parent_path().string();
    CHECK(vdir == "vault:1/shaders");

    fx::CoreShaderInputs plain, sealed, rootIn;
    CHECK(fx::gatherCoreShaders((root / "in").string(), plain, &err));
    CHECK(fx::gatherCoreShaders(vdir, sealed, &err));
    CHECK(sealed.fingerprint == plain.fingerprint && sealed.fileCount == plain.fileCount && plain.fileCount == 4);   // same Core, same fingerprint
    CHECK(sealed.tiledSpriteFp == plain.tiledSpriteFp && sealed.otterVp == plain.otterVp && sealed.otterFp == plain.otterFp);
    CHECK(fx::gatherCoreShaders("vault:1", rootIn, &err) && rootIn.fingerprint == plain.fingerprint);   // the vault root: shaders/ is found
    CHECK(!fx::gatherCoreShaders("vault:1/scenes", rootIn, &err));   // a folder without shader sources
    CHECK(!fx::gatherCoreShaders("vault:77/shaders", rootIn, &err) && !err.empty());

    // the pack is built from the sealed shaders and is not rebuilt when the same Core is later read from a plain folder
    fx::FxPack pk;
    fx::FxStoreInfo inf;
    CHECK(fx::loadOrBuildFxPack((root / "fxstore").string(), vdir, pk, &inf, &err));
    CHECK(inf.built && inf.fingerprint == plain.fingerprint);
    fx::FxPack pk2;
    fx::FxStoreInfo inf2;
    CHECK(fx::loadOrBuildFxPack((root / "fxstore").string(), (root / "in").string(), pk2, &inf2, &err));
    CHECK(!inf2.built && inf2.persisted && inf2.fingerprint == plain.fingerprint);
    fs::remove_all(root);
    vault::clearRegistry();
}

static void testImport() {
    const fs::path root = tempRoot("import");
    const fs::path store = root / "private", staging = root / "core_import";
    std::string err;

    // nothing staged: nothing happens
    vault::ImportResult r = vault::importPending(staging.string(), store.string());
    CHECK(!r.attempted && !r.ok);
    CHECK(!vault::importPending("", store.string()).attempted);

    // a Core staged by the shell (nested the way a picked folder arrives): sealed, plain copy deleted, status written
    const std::string all = makeCore(staging / "core");
    r = vault::importPending(staging.string(), store.string());
    CHECK(r.attempted && r.ok && r.files == 8 && r.bytes > all.size());
    CHECK(!fs::exists(staging));
    vault::Vault v;
    CHECK(v.open(store.string(), &err) && v.has("shaders/Otter_fp.fx"));
    CHECK(asString(slurpBytes(store / vault::kImportStatusFile)).rfind("ok ", 0) == 0);

    // a wrong folder staged next: refused with a reason, deleted, the good vault is untouched, the status says why
    const std::vector<uint8_t> goodVault = slurpBytes(store / vault::kVaultFile);
    put(staging / "photos" / "cat.jpg", "meow");
    r = vault::importPending(staging.string(), store.string());
    CHECK(r.attempted && !r.ok && !r.message.empty());
    CHECK(!fs::exists(staging));
    CHECK(slurpBytes(store / vault::kVaultFile) == goodVault);
    CHECK(asString(slurpBytes(store / vault::kImportStatusFile)).rfind("error ", 0) == 0);

    // a second Core replaces the first and keeps the same key
    const std::vector<uint8_t> key = slurpBytes(store / "pafx.key");
    put(staging / "stages.ini", "[stage]\nname=second\n");
    r = vault::importPending(staging.string(), store.string());
    CHECK(r.ok && r.files == 1 && slurpBytes(store / "pafx.key") == key);
    vault::Vault v2;
    std::string t;
    CHECK(v2.open(store.string(), &err) && v2.readText("stages.ini", t) && t.find("second") != std::string::npos && !v2.has("shaders/Otter_fp.fx"));
    fs::remove_all(root);
}

// With ZDROID_TEST_DATA pointing at a folder that holds a real Core: seal it, mount the sealed copy next to the plain folder and check
// that every file reads back byte for byte the same through the virtual file system.
static void testRealCore() {
    const char* d = std::getenv("ZDROID_TEST_DATA");
    if (!d || !*d) {
        std::printf("  (no ZDROID_TEST_DATA: skipping the real-Core round trip)\n");
        return;
    }
    std::stringstream ss(d);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        const std::string coreRoot = vault::findCoreRoot(dir);
        if (coreRoot.empty()) continue;
        vault::clearRegistry();
        const fs::path root = tempRoot("real");
        const fs::path store = root / "private";
        vault::BuildReport rep;
        std::string err;
        CHECK(vault::build(dir, store.string(), &rep, &err));
        auto v = std::make_shared<vault::Vault>();
        CHECK(v->open(store.string(), &err));
        Vfs plain, sealed;
        plain.mount(coreRoot);
        sealed.mountVault(v);
        CHECK(sealed.fileCount() == rep.files && plain.fileCount() >= rep.files);
        size_t same = 0, bad = 0;
        for (const fx::PafxEntryInfo& e : v->entries()) {
            std::vector<uint8_t> a, b;
            if (readFile((fs::path(coreRoot) / e.name).string(), a) && sealed.read(e.name, b) && a == b) ++same;
            else ++bad;
        }
        CHECK(bad == 0 && same == rep.files);
        std::printf("  real Core: %zu files, %llu bytes sealed into %llu bytes, every file identical\n", rep.files,
                    static_cast<unsigned long long>(rep.bytes), static_cast<unsigned long long>(fs::file_size(store / vault::kVaultFile)));
        fs::remove_all(root);
        vault::clearRegistry();
        return;
    }
    std::printf("  (ZDROID_TEST_DATA holds no Core: skipping the real-Core round trip)\n");
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    std::printf("[vault] finding the Core folder\n");
    testFindRoot();
    std::printf("[vault] sealing and opening\n");
    testBuildOpen();
    std::printf("[vault] keys and tampering\n");
    testTamperAndKeys();
    std::printf("[vault] virtual file system\n");
    testVfs();
    std::printf("[vault] effects pack from the vault\n");
    testShadersFromVault();
    std::printf("[vault] import from the Android staging folder\n");
    testImport();
    std::printf("[vault] real Core\n");
    testRealCore();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
