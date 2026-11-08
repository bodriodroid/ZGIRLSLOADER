#include "warp.h"

#include <cstdio>
#include <cstring>

namespace zengine {
namespace warp {

// A string is a 24-bit big-endian length followed by raw bytes.  The three bytes in
// front of the length are zero in every asset we have (the previous field is either
// padding or a zero-valued scalar), so that is used as a cheap false-positive filter
// against the float payloads, which otherwise contain plenty of 00 00 xx runs.
std::vector<StringHit> scanStrings(const std::vector<uint8_t>& d, std::size_t maxLen) {
    std::vector<StringHit> hits;
    if (d.size() < 4) return hits;
    for (std::size_t p = 0; p + 3 < d.size(); ++p) {
        if (d[p] != 0 || d[p + 1] != 0) continue;
        if (p >= 3 && (d[p - 1] != 0 || d[p - 2] != 0 || d[p - 3] != 0)) continue;
        const std::size_t n = (static_cast<std::size_t>(d[p]) << 16) |
                              (static_cast<std::size_t>(d[p + 1]) << 8) |
                              static_cast<std::size_t>(d[p + 2]);
        if (n == 0 || n > maxLen) continue;
        if (p + 3 + n > d.size()) continue;
        bool printable = true;
        for (std::size_t i = 0; i < n; ++i) {
            const uint8_t c = d[p + 3 + i];
            if (c < 0x20 || c >= 0x7F) {
                printable = false;
                break;
            }
        }
        if (!printable) continue;
        // Reject hits that sit inside a previously accepted string's payload.
        if (!hits.empty()) {
            const StringHit& prev = hits.back();
            if (p < prev.data + prev.value.size()) continue;
        }
        hits.push_back({p, p + 3, std::string(reinterpret_cast<const char*>(d.data() + p + 3), n)});
    }
    return hits;
}

namespace {
uint32_t be32(const std::vector<uint8_t>& d, std::size_t p) {
    return (static_cast<uint32_t>(d[p]) << 24) | (static_cast<uint32_t>(d[p + 1]) << 16) |
           (static_cast<uint32_t>(d[p + 2]) << 8) | static_cast<uint32_t>(d[p + 3]);
}
uint64_t be64(const std::vector<uint8_t>& d, std::size_t p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | d[p + i];
    return v;
}
float bef32(const std::vector<uint8_t>& d, std::size_t p) {
    const uint32_t bits = be32(d, p);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
}  // namespace

Skeleton readSkeleton(const std::vector<uint8_t>& d) {
    Skeleton sk;
    sk.fileSize = d.size();
    const std::vector<StringHit> hits = scanStrings(d);
    if (hits.empty()) {
        sk.error = "no length-prefixed strings found";
        return sk;
    }
    sk.version = hits[0].value;
    sk.declaredJoints = (d.size() >= 8) ? be64(d, 0) : 0;

    // hits[0] is the format version; every later hit is a joint name.
    for (std::size_t i = 1; i < hits.size(); ++i) {
        Joint j;
        j.name = hits[i].value;
        j.offset = hits[i].offset;
        const std::size_t dataEnd = hits[i].data + hits[i].value.size();
        const std::size_t next = (i + 1 < hits.size()) ? hits[i + 1].offset : d.size();
        j.payloadBytes = (next > dataEnd) ? (next - dataEnd) : 0;

        // name, then i32 parent, then a 16-float (float44) row-major bind matrix.
        std::size_t q = dataEnd;
        if (q + 4 <= next) {
            j.parent = static_cast<int32_t>(be32(d, q));
            q += 4;
        }
        while (q + 4 <= next && j.matrix.size() < 16) {
            j.matrix.push_back(bef32(d, q));
            q += 4;
        }
        // Anything left before the next joint name is build-specific joint state;
        // keep the raw bytes so the tool can report how much we cannot yet decode.
        const std::size_t room = (next > q) ? (next - q) : 0;
        j.extra.assign(d.begin() + static_cast<long>(q), d.begin() + static_cast<long>(q + room));
        sk.joints.push_back(std::move(j));
    }
    sk.ok = true;
    return sk;
}

Animation readAnimation(const std::vector<uint8_t>& d) {
    Animation an;
    an.fileSize = d.size();
    {
        const std::vector<StringHit> hits = scanStrings(d);
        if (hits.empty()) {
            an.error = "no length-prefixed strings found";
            return an;
        }
        an.version = hits[0].value;
        for (std::size_t i = 1; i < hits.size(); ++i) {
            Track t;
            t.name = hits[i].value;
            t.offset = hits[i].offset;
            const std::size_t dataEnd = hits[i].data + hits[i].value.size();
            const std::size_t next = (i + 1 < hits.size()) ? hits[i + 1].offset : d.size();
            msb::Reader tr(d);
            tr.seek(dataEnd);
            while (tr.offset() + 4 <= next) {
                const float v = tr.f32();
                if (v != v) break;  // NaN: we are off the rails, stop
                t.keys.push_back(v);
            }
            an.tracks.push_back(std::move(t));
        }
        an.ok = true;
    }
    return an;
}

MeshInfo readMeshInfo(const std::vector<uint8_t>& d) {
    MeshInfo mi;
    mi.fileSize = d.size();
    const std::vector<StringHit> hits = scanStrings(d);
    if (hits.empty()) {
        mi.error = "no length-prefixed strings found";
        return mi;
    }
    mi.version = hits[0].value;
    for (const StringHit& h : hits) mi.names.push_back(h.value);
    mi.ok = true;
    return mi;
}

}  // namespace warp
}  // namespace zengine
