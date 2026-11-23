#include "zipread.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "gfs.h"

namespace zengine {
namespace zipread {

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kMaxEntry = 1ull << 30;    // one file may not expand past 1 GiB
constexpr uint64_t kMaxTotal = 4ull << 30;    // nor the whole zip past 4 GiB

// ---- inflate (RFC 1951): canonical Huffman decoding by code length counts
struct Bits {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool bad = false;
    int get(int need) {
        uint32_t v = buf;
        while (cnt < need) {
            if (pos >= n) {
                bad = true;
                return 0;
            }
            v |= uint32_t(p[pos++]) << cnt;
            cnt += 8;
        }
        buf = v >> need;
        cnt -= need;
        return int(v & ((1u << need) - 1));
    }
};

struct Huff {
    uint16_t count[16];
    uint16_t symbol[320];
};

// 0 = complete, > 0 = incomplete, < 0 = over-subscribed
int construct(Huff& h, const uint16_t* length, int n) {
    for (int l = 0; l <= 15; ++l) h.count[l] = 0;
    for (int s = 0; s < n; ++s) ++h.count[length[s]];
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l <= 15; ++l) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return left;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = uint16_t(offs[l] + h.count[l]);
    for (int s = 0; s < n; ++s)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = uint16_t(s);
    return left;
}

int decode(Bits& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= s.get(1);
        if (s.bad) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t kLen[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLenExt[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDist[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint16_t kDistExt[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// 0 = ok, otherwise an error text
const char* codes(Bits& s, std::vector<uint8_t>& out, size_t maxOut, const Huff& lc, const Huff& dc) {
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0) return "damaged data";
        if (sym < 256) {
            if (out.size() >= maxOut) return "data is larger than its header says";
            out.push_back(uint8_t(sym));
        } else if (sym == 256) {
            return nullptr;
        } else {
            sym -= 257;
            if (sym >= 29) return "damaged data";
            const int len = kLen[sym] + s.get(kLenExt[sym]);
            const int ds = decode(s, dc);
            if (ds < 0 || ds >= 30) return "damaged data";
            const size_t dist = size_t(kDist[ds]) + size_t(s.get(kDistExt[ds]));
            if (s.bad) return "damaged data";
            if (dist > out.size()) return "damaged data";
            if (out.size() + size_t(len) > maxOut) return "data is larger than its header says";
            size_t from = out.size() - dist;
            for (int i = 0; i < len; ++i) out.push_back(out[from++]);
        }
    }
}

}  // namespace

bool inflate(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOut, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    out.clear();
    Bits s{src, srcLen};
    int last;
    do {
        last = s.get(1);
        const int type = s.get(2);
        if (s.bad) return fail("damaged data");
        if (type == 0) {
            s.buf = 0;
            s.cnt = 0;
            if (s.pos + 4 > s.n) return fail("damaged data");
            const unsigned len = s.p[s.pos] | (s.p[s.pos + 1] << 8);
            const unsigned nlen = s.p[s.pos + 2] | (s.p[s.pos + 3] << 8);
            s.pos += 4;
            if (len != (~nlen & 0xFFFF)) return fail("damaged data");
            if (s.pos + len > s.n) return fail("damaged data");
            if (out.size() + len > maxOut) return fail("data is larger than its header says");
            out.insert(out.end(), s.p + s.pos, s.p + s.pos + len);
            s.pos += len;
        } else if (type == 1) {
            static Huff lc, dc;
            static bool built = false;
            if (!built) {
                uint16_t l[288];
                int i = 0;
                for (; i < 144; ++i) l[i] = 8;
                for (; i < 256; ++i) l[i] = 9;
                for (; i < 280; ++i) l[i] = 7;
                for (; i < 288; ++i) l[i] = 8;
                construct(lc, l, 288);
                uint16_t d[30];
                for (i = 0; i < 30; ++i) d[i] = 5;
                construct(dc, d, 30);
                built = true;
            }
            if (const char* e = codes(s, out, maxOut, lc, dc)) return fail(e);
        } else if (type == 2) {
            const int nlen = s.get(5) + 257, ndist = s.get(5) + 1, ncode = s.get(4) + 4;
            if (s.bad || nlen > 286 || ndist > 30) return fail("damaged data");
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint16_t lengths[320];
            int idx = 0;
            for (; idx < ncode; ++idx) lengths[order[idx]] = uint16_t(s.get(3));
            for (; idx < 19; ++idx) lengths[order[idx]] = 0;
            Huff lc, dc;
            if (s.bad || construct(lc, lengths, 19) != 0) return fail("damaged data");
            idx = 0;
            while (idx < nlen + ndist) {
                int sym = decode(s, lc);
                if (sym < 0) return fail("damaged data");
                if (sym < 16) {
                    lengths[idx++] = uint16_t(sym);
                } else {
                    int rep, val = 0;
                    if (sym == 16) {
                        if (idx == 0) return fail("damaged data");
                        val = lengths[idx - 1];
                        rep = 3 + s.get(2);
                    } else if (sym == 17) {
                        rep = 3 + s.get(3);
                    } else {
                        rep = 11 + s.get(7);
                    }
                    if (idx + rep > nlen + ndist) return fail("damaged data");
                    while (rep--) lengths[idx++] = uint16_t(val);
                }
            }
            if (s.bad || lengths[256] == 0) return fail("damaged data");
            int e1 = construct(lc, lengths, nlen);
            if (e1 < 0 || (e1 > 0 && nlen != lc.count[0] + lc.count[1])) return fail("damaged data");
            int e2 = construct(dc, lengths + nlen, ndist);
            if (e2 < 0 || (e2 > 0 && ndist != dc.count[0] + dc.count[1])) return fail("damaged data");
            if (const char* e = codes(s, out, maxOut, lc, dc)) return fail(e);
        } else {
            return fail("damaged data");
        }
        if (s.bad) return fail("damaged data");
    } while (!last);
    return true;
}

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        built = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
}  // namespace

bool list(const std::string& zipFile, std::vector<Entry>& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    out.clear();
    std::error_code ec;
    const uint64_t size = fs::file_size(zipFile, ec);
    std::ifstream f(zipFile, std::ios::binary);
    if (ec || !f) return fail("cannot open the zip");
    if (size < 22) return fail("not a zip file");
    const uint64_t tail = std::min<uint64_t>(size, 22 + 65535);
    std::vector<uint8_t> t(static_cast<size_t>(tail));
    f.seekg(static_cast<std::streamoff>(size - tail));
    f.read(reinterpret_cast<char*>(t.data()), static_cast<std::streamsize>(t.size()));
    if (static_cast<size_t>(f.gcount()) != t.size()) return fail("cannot read the zip");
    size_t e = std::string::npos;
    for (size_t i = t.size() - 22 + 1; i-- > 0;)
        if (le32(&t[i]) == 0x06054b50u) {
            e = i;
            break;
        }
    if (e == std::string::npos) return fail("not a zip file");
    const uint32_t cdSize = le32(&t[e + 12]), cdOff = le32(&t[e + 16]);
    const uint16_t total = le16(&t[e + 10]);
    if (total == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) return fail("ZIP64 files are not supported");
    if (uint64_t(cdOff) + cdSize > size) return fail("the zip is damaged (directory beyond its end)");
    std::vector<uint8_t> cd(cdSize);
    f.seekg(cdOff);
    f.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cd.size()));
    if (static_cast<size_t>(f.gcount()) != cd.size()) return fail("cannot read the zip");
    size_t pos = 0;
    for (uint16_t i = 0; i < total; ++i) {
        if (pos + 46 > cd.size() || le32(&cd[pos]) != 0x02014b50u) return fail("the zip is damaged (directory)");
        Entry en;
        en.flags = le16(&cd[pos + 8]);
        en.method = le16(&cd[pos + 10]);
        en.crc = le32(&cd[pos + 16]);
        en.csize = le32(&cd[pos + 20]);
        en.usize = le32(&cd[pos + 24]);
        const size_t nl = le16(&cd[pos + 28]), xl = le16(&cd[pos + 30]), cl = le16(&cd[pos + 32]);
        en.localOffset = le32(&cd[pos + 42]);
        if (pos + 46 + nl + xl + cl > cd.size()) return fail("the zip is damaged (directory)");
        en.name.assign(reinterpret_cast<const char*>(&cd[pos + 46]), nl);
        pos += 46 + nl + xl + cl;
        out.push_back(std::move(en));
    }
    return true;
}

bool read(const std::string& zipFile, const Entry& e, std::vector<uint8_t>& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    out.clear();
    if (e.flags & 1) return fail("encrypted entries are not supported");
    if (e.method != 0 && e.method != 8) return fail("compression method " + std::to_string(e.method) + " is not supported");
    if (e.usize > kMaxEntry || e.csize > kMaxEntry) return fail("entry too large");
    std::ifstream f(zipFile, std::ios::binary);
    if (!f) return fail("cannot open the zip");
    uint8_t lh[30];
    f.seekg(static_cast<std::streamoff>(e.localOffset));
    f.read(reinterpret_cast<char*>(lh), 30);
    if (f.gcount() != 30 || le32(lh) != 0x04034b50u) return fail("the zip is damaged (local header)");
    f.seekg(static_cast<std::streamoff>(e.localOffset + 30 + le16(lh + 26) + le16(lh + 28)));
    std::vector<uint8_t> raw(static_cast<size_t>(e.csize));
    f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (static_cast<size_t>(f.gcount()) != raw.size()) return fail("the zip is cut off");
    if (e.method == 0) {
        if (e.csize != e.usize) return fail("damaged data");
        out = std::move(raw);
    } else if (!inflate(raw.data(), raw.size(), out, static_cast<size_t>(e.usize), err)) {
        return false;
    }
    if (out.size() != e.usize) return fail("damaged data (size)");
    if (crc32(out.data(), out.size()) != e.crc) return fail("damaged data (CRC)");
    return true;
}

bool extract(const std::string& zipFile, const std::string& outDir, ExtractStats& st, bool stripWrapper, std::string* err) {
    st = ExtractStats();
    std::vector<Entry> all;
    if (!list(zipFile, all, err)) return false;
    // the safe relative path of every entry ("" = skipped); __MACOSX and .DS_Store never belong to a mod
    std::vector<std::string> rel(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string r = gfs::safePath(all[i].name);
        if (r.empty()) {
            if (!all[i].name.empty() && all[i].name != "/") st.problems.push_back("refused path: " + all[i].name);
            continue;
        }
        if (r.rfind("__MACOSX", 0) == 0 || r.find(".DS_Store") != std::string::npos) continue;
        rel[i] = r;
    }
    std::string wrapper;
    if (stripWrapper) {
        bool same = true, any = false;
        for (size_t i = 0; i < all.size() && same; ++i) {
            if (rel[i].empty()) continue;
            const size_t slash = rel[i].find('/');
            if (slash == std::string::npos) {
                if (!all[i].isDir()) same = false;   // a file at the top: no wrapper
                continue;
            }
            const std::string top = rel[i].substr(0, slash);
            if (!any) wrapper = top, any = true;
            else if (top != wrapper) same = false;
        }
        if (!same || !any) wrapper.clear();
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        if (err) *err = "cannot create " + outDir;
        return false;
    }
    uint64_t total = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        if (rel[i].empty()) continue;
        std::string r = rel[i];
        if (!wrapper.empty()) {
            if (r == wrapper) continue;
            r = r.substr(wrapper.size() + 1);
        }
        const fs::path dest = fs::path(outDir) / fs::path(r);
        if (all[i].isDir()) {
            fs::create_directories(dest, ec);
            continue;
        }
        total += all[i].usize;
        if (total > kMaxTotal) {
            st.problems.push_back("stopped: the zip expands to more than 4 GiB");
            break;
        }
        std::vector<uint8_t> data;
        std::string e;
        if (!read(zipFile, all[i], data, &e)) {
            st.problems.push_back(r + ": " + e);
            continue;
        }
        fs::create_directories(dest.parent_path(), ec);
        std::ofstream o(dest, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        o.close();
        if (!o) {
            st.problems.push_back("could not write: " + r);
            fs::remove(dest, ec);
            continue;
        }
        ++st.files;
        st.bytes += data.size();
    }
    st.strippedFolder = wrapper;
    return true;
}

}  // namespace zipread
}  // namespace zengine
