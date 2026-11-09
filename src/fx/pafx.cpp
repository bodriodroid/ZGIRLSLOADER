#include "pafx.h"

#include <cstdio>
#include <cstring>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace zengine {
namespace fx {
namespace {

void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
void put64(std::vector<uint8_t>& v, uint64_t x) { put32(v, static_cast<uint32_t>(x)); put32(v, static_cast<uint32_t>(x >> 32)); }
uint32_t get16(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8); }
uint32_t get32(const uint8_t* p) { return get16(p) | (get16(p + 2) << 16); }
uint64_t get64(const uint8_t* p) { return get32(p) | (static_cast<uint64_t>(get32(p + 4)) << 32); }

AeadKey subKey(const AeadKey& key, const std::array<uint8_t, 16>& id) {
    AeadNonce n;
    std::memcpy(n.data(), id.data(), 12);
    uint8_t blk[64];
    chacha20Block(key, get32(id.data() + 12), n, blk);
    AeadKey k;
    std::memcpy(k.data(), blk, 32);
    return k;
}

AeadNonce entryNonce(uint32_t index) {
    AeadNonce n{};
    n[8] = static_cast<uint8_t>(index);
    n[9] = static_cast<uint8_t>(index >> 8);
    n[10] = static_cast<uint8_t>(index >> 16);
    n[11] = static_cast<uint8_t>(index >> 24);
    return n;
}

constexpr uint32_t kTableIndex = 0xFFFFFFFFu;
}  // namespace

void PafxWriter::add(const std::string& name, PafxKind kind, const std::vector<uint8_t>& bytes) { entries_.push_back({name, kind, bytes}); }
void PafxWriter::addText(const std::string& name, PafxKind kind, const std::string& text) {
    entries_.push_back({name, kind, std::vector<uint8_t>(text.begin(), text.end())});
}

bool PafxWriter::build(const AeadKey& key, const std::array<uint8_t, 16>& archiveId, std::vector<uint8_t>& out, std::string* err) const {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name.empty() || entries_[i].name.size() > 0xFFFF) return fail("bad entry name");
        if (entries_[i].data.size() > 0x7FFFFFFFu) return fail("entry too large");
        for (size_t j = 0; j < i; ++j)
            if (entries_[j].name == entries_[i].name) return fail("duplicate entry name");
    }
    const AeadKey sk = subKey(key, archiveId);

    // seal the entries first (their sizes are needed by the table), offsets are filled in once the table size is known
    std::vector<std::vector<uint8_t>> sealed(entries_.size());
    std::vector<uint8_t> header(kPafxHeaderSize, 0);
    std::memcpy(header.data(), "PAFX", 4);
    header[4] = static_cast<uint8_t>(kPafxVersion);
    std::memcpy(header.data() + 8, archiveId.data(), 16);
    {
        std::vector<uint8_t> t;
        put32(t, static_cast<uint32_t>(entries_.size()));
        std::memcpy(header.data() + 24, t.data(), 4);
    }

    // table size = sum of record sizes + tag; each record is 2 + name + 1 + 1 + 2 + 8 + 4 + 4
    size_t tablePlain = 0;
    for (const E& e : entries_) tablePlain += 2 + e.name.size() + 1 + 1 + 2 + 8 + 4 + 4;
    const size_t tableSealed = tablePlain + kAeadTagSize;
    {
        std::vector<uint8_t> t;
        put32(t, static_cast<uint32_t>(tableSealed));
        put64(t, kPafxHeaderSize);
        std::memcpy(header.data() + 28, t.data(), 12);
    }
    uint64_t total = kPafxHeaderSize + tableSealed;

    std::vector<std::vector<uint8_t>> records(entries_.size());
    // the AAD of an entry is header || record, and the header carries the total size, which depends on the sealed sizes: those
    // are plain + 16 so they can be computed up front.
    uint64_t off = total;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const E& e = entries_[i];
        std::vector<uint8_t>& r = records[i];
        put16(r, static_cast<uint32_t>(e.name.size()));
        r.insert(r.end(), e.name.begin(), e.name.end());
        r.push_back(static_cast<uint8_t>(e.kind));
        r.push_back(0);
        put16(r, 0);
        put64(r, off);
        put32(r, static_cast<uint32_t>(e.data.size() + kAeadTagSize));
        put32(r, static_cast<uint32_t>(e.data.size()));
        off += e.data.size() + kAeadTagSize;
    }
    total = off;
    {
        std::vector<uint8_t> t;
        put64(t, total);
        std::memcpy(header.data() + 40, t.data(), 8);
    }

    for (size_t i = 0; i < entries_.size(); ++i) {
        std::vector<uint8_t> aad = header;
        aad.insert(aad.end(), records[i].begin(), records[i].end());
        aeadSeal(sk, entryNonce(static_cast<uint32_t>(i)), aad.data(), aad.size(), entries_[i].data.data(), entries_[i].data.size(), sealed[i]);
    }
    std::vector<uint8_t> tablePlainBytes;
    for (const auto& r : records) tablePlainBytes.insert(tablePlainBytes.end(), r.begin(), r.end());
    std::vector<uint8_t> tableSealedBytes;
    aeadSeal(sk, entryNonce(kTableIndex), header.data(), header.size(), tablePlainBytes.data(), tablePlainBytes.size(), tableSealedBytes);

    out = header;
    out.insert(out.end(), tableSealedBytes.begin(), tableSealedBytes.end());
    for (const auto& s : sealed) out.insert(out.end(), s.begin(), s.end());
    return true;
}

bool PafxArchive::open(const uint8_t* data, size_t size, const AeadKey& key, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        info_.clear();
        recs_.clear();
        return false;
    };
    info_.clear();
    recs_.clear();
    if (size < kPafxHeaderSize || std::memcmp(data, "PAFX", 4) != 0) return fail("not a PAFX file");
    if (get16(data + 4) != kPafxVersion) return fail("unsupported PAFX version");
    const uint32_t count = get32(data + 24), tableSize = get32(data + 28);
    const uint64_t tableOff = get64(data + 32), total = get64(data + 40);
    if (total != size) return fail("PAFX size does not match its header (truncated or padded)");
    if (tableOff != kPafxHeaderSize || tableOff + tableSize > size || tableSize < kAeadTagSize) return fail("bad PAFX table");
    std::memcpy(id_.data(), data + 8, 16);
    sub_ = subKey(key, id_);
    std::vector<uint8_t> table;
    if (!aeadOpen(sub_, entryNonce(kTableIndex), data, kPafxHeaderSize, data + tableOff, tableSize, table))
        return fail("wrong key or damaged PAFX (the table does not verify)");
    data_ = data;
    size_ = size;
    size_t p = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t start = p;
        if (p + 2 > table.size()) return fail("PAFX table is cut short");
        const size_t nl = get16(&table[p]);
        p += 2;
        if (p + nl + 1 + 1 + 2 + 8 + 4 + 4 > table.size()) return fail("PAFX table is cut short");
        PafxEntryInfo e;
        e.name.assign(reinterpret_cast<const char*>(&table[p]), nl);
        p += nl;
        e.kind = static_cast<PafxKind>(table[p]);
        p += 4;
        Rec r;
        r.off = get64(&table[p]);
        p += 8;
        r.sealed = get32(&table[p]);
        p += 4;
        e.size = get32(&table[p]);
        p += 4;
        if (r.sealed != static_cast<uint64_t>(e.size) + kAeadTagSize || r.off < tableOff + tableSize || r.off + r.sealed > size)
            return fail("PAFX entry points outside the file");
        r.raw.assign(table.begin() + static_cast<std::ptrdiff_t>(start), table.begin() + static_cast<std::ptrdiff_t>(p));
        info_.push_back(std::move(e));
        recs_.push_back(std::move(r));
    }
    if (p != table.size()) return fail("PAFX table has trailing bytes");
    return true;
}

int PafxArchive::find(const std::string& name) const {
    for (size_t i = 0; i < info_.size(); ++i)
        if (info_[i].name == name) return static_cast<int>(i);
    return -1;
}

bool PafxArchive::read(const std::string& name, std::vector<uint8_t>& out, std::string* err) const {
    out.clear();
    const int i = find(name);
    if (i < 0) {
        if (err) *err = "no such entry: " + name;
        return false;
    }
    const Rec& r = recs_[static_cast<size_t>(i)];
    std::vector<uint8_t> aad(data_, data_ + kPafxHeaderSize);
    aad.insert(aad.end(), r.raw.begin(), r.raw.end());
    if (!aeadOpen(sub_, entryNonce(static_cast<uint32_t>(i)), aad.data(), aad.size(), data_ + r.off, r.sealed, out)) {
        if (err) *err = "entry does not verify (damaged): " + name;
        return false;
    }
    return true;
}

bool PafxArchive::readText(const std::string& name, std::string& out, std::string* err) const {
    std::vector<uint8_t> b;
    if (!read(name, b, err)) return false;
    out.assign(b.begin(), b.end());
    return true;
}

bool loadOrCreatePafxKey(const std::string& path, AeadKey& key, std::string* err) {
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        const size_t n = std::fread(key.data(), 1, key.size(), f);
        std::fclose(f);
        if (n == key.size()) return true;
        if (err) *err = "key file is too short: " + path;
        return false;
    }
    if (!secureRandom(key.data(), key.size())) {
        if (err) *err = "no secure random source";
        return false;
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "cannot create key file: " + path;
        return false;
    }
    const bool ok = std::fwrite(key.data(), 1, key.size(), f) == key.size();
    std::fclose(f);
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
    if (!ok && err) *err = "cannot write key file: " + path;
    return ok;
}

}  // namespace fx
}  // namespace zengine
