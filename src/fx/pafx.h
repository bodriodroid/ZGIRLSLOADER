// pafx.h - PAFX, the "Paradox Archive FX" container: the render-effects pack of the Android build.
//
// A .pafx is NOT a game archive and never carries game art: it holds the effect programs (GLSL ES sources written for this
// engine, see fx_glsl.h), the tuning numbers the effects read (fx_tuning.h, extracted at pack time from the player's own Core
// shaders) and a small manifest.  It is built on the player's device from the player's own files (tools/zdroid_pafx.cpp) and
// sealed with a per-install key that is generated on that device and never leaves it - so there is nothing to ship, and a pack
// built on one install does not open on another.  The repository, the public zips and the APK contain no .pafx.
//
// Layout (all integers little-endian):
//   header, 64 bytes, plain
//     0  "PAFX"
//     4  u16 version (1)          6  u16 flags (0)
//     8  u8[16] archive id (random per pack)
//     24 u32 entry count          28 u32 sealed table size (bytes, tag included)
//     32 u64 table offset (= 64)  40 u64 total file size
//     48 u8[16] zero
//   sealed table (at 64): ChaCha20-Poly1305 of the entry records, then the sealed entries back to back.
//     record: u16 name length, name bytes, u8 kind, u8 0, u16 0, u64 offset, u32 sealed size, u32 plain size
//   Keys: subKey = first 32 bytes of ChaCha20Block(key, counter = archiveId[12..16], nonce = archiveId[0..12]) - every pack has
//   its own sub key.  The table uses nonce {0,..,0xFF,0xFF,0xFF,0xFF}, entry i uses nonce {0,..,i}; the AAD is the 64 header bytes
//   (plus the entry's record for entries), so entries cannot be moved between packs or swapped inside one.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "aead.h"

namespace zengine {
namespace fx {

constexpr uint16_t kPafxVersion = 1;
constexpr size_t kPafxHeaderSize = 64;

enum class PafxKind : uint8_t { Raw = 0, GlslVertex = 1, GlslFragment = 2, Text = 3 };

struct PafxEntryInfo {
    std::string name;
    PafxKind kind = PafxKind::Raw;
    uint32_t size = 0;  // plain bytes
};

class PafxWriter {
public:
    void add(const std::string& name, PafxKind kind, const std::vector<uint8_t>& bytes);
    void addText(const std::string& name, PafxKind kind, const std::string& text);
    // `archiveId` = 16 random bytes (the caller supplies them so tests are deterministic; tools use secureRandom).
    bool build(const AeadKey& key, const std::array<uint8_t, 16>& archiveId, std::vector<uint8_t>& out, std::string* err = nullptr) const;
    size_t size() const { return entries_.size(); }

private:
    struct E {
        std::string name;
        PafxKind kind;
        std::vector<uint8_t> data;
    };
    std::vector<E> entries_;
};

class PafxArchive {
public:
    // Opens `data` (kept by the caller, must outlive the archive) with `key`.  Fails with a message on a wrong key, a damaged
    // or truncated file, an unknown version or a malformed table.
    bool open(const uint8_t* data, size_t size, const AeadKey& key, std::string* err = nullptr);
    const std::vector<PafxEntryInfo>& entries() const { return info_; }
    bool has(const std::string& name) const { return find(name) >= 0; }
    // Decrypts and verifies one entry.
    bool read(const std::string& name, std::vector<uint8_t>& out, std::string* err = nullptr) const;
    bool readText(const std::string& name, std::string& out, std::string* err = nullptr) const;
    const std::array<uint8_t, 16>& archiveId() const { return id_; }

private:
    struct Rec {
        uint64_t off = 0;
        uint32_t sealed = 0;
        std::vector<uint8_t> raw;  // the table record bytes (AAD of the entry)
    };
    int find(const std::string& name) const;
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    AeadKey sub_{};
    std::array<uint8_t, 16> id_{};
    std::vector<PafxEntryInfo> info_;
    std::vector<Rec> recs_;
};

// The per-install key: 32 random bytes in `path`, created on first use.  Returns false (with *err) when the file can neither be
// read nor created.  The file is the only secret; keep it in the app's private storage and out of any backup / repo.
bool loadOrCreatePafxKey(const std::string& path, AeadKey& key, std::string* err = nullptr);

}  // namespace fx
}  // namespace zengine
