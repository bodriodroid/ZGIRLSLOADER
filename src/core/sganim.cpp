#include "sganim.h"

#include <algorithm>
#include <cmath>

#include "msb.h"

namespace zengine {
namespace sganim {

namespace {
constexpr uint64_t kMaxKeys = 1u << 16;

std::string readStr(msb::Reader& r) {
    const uint64_t n = r.u64();
    if (n > r.remaining()) throw msb::Error{msb::Error::Kind::BadLength, r.offset()};
    const std::vector<uint8_t> b = r.raw(static_cast<size_t>(n));
    return std::string(b.begin(), b.end());
}

// plain big-endian 32-bit word (r.u32() is the .msb encoding with padding bytes, which these header words are not)
uint32_t word(msb::Reader& r) {
    const std::vector<uint8_t> b = r.raw(4);
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}

std::vector<float> block(msb::Reader& r, size_t perKey, size_t* keysOut) {
    const uint64_t n = r.u64();
    if (n > kMaxKeys || n * perKey * 4 > r.remaining()) throw msb::Error{msb::Error::Kind::BadLength, r.offset()};
    std::vector<float> v(static_cast<size_t>(n) * perKey);
    for (float& f : v) f = r.f32();
    *keysOut = static_cast<size_t>(n);
    return v;
}
}  // namespace

bool parse(const std::vector<uint8_t>& data, Anim& out, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    out = Anim();
    try {
        msb::Reader r(data);
        const std::string version = readStr(r);
        if (version != "3.0") return fail("unsupported animation version '" + version + "'");
        const uint32_t a = word(r), b = word(r), joints = word(r), c = word(r), tracks = word(r);
        (void)a; (void)b; (void)c;
        if (joints != 0) return fail("skeleton animation");
        if (tracks == 0 || tracks > 16) return fail("implausible track count");
        out.duration = r.f32();
        if (!(out.duration > 0) || !std::isfinite(out.duration)) return fail("bad duration");
        for (uint32_t t = 0; t < tracks; ++t) {
            UvTrack tr;
            tr.name = readStr(r);
            size_t ns = 0, nr = 0, nt = 0;
            tr.scale = block(r, 2, &ns);
            tr.rot = block(r, 1, &nr);
            tr.trans = block(r, 2, &nt);
            if (ns != nr || nr != nt || nr == 0) return fail("track blocks of different length");
            out.tracks.push_back(std::move(tr));
        }
        if (!r.eof()) return fail("bytes left after the last track");
    } catch (const msb::Error& e) {
        return fail(std::string("truncated: ") + e.what());
    }
    // flipbook or scroll?  A scroll moves at a constant speed and covers whole textures per loop (waterfalls: 11 keys, v 0 -> 1 in 0.4 s), so its last key
    // meets its first one.  A flipbook jumps a whole sprite cell (>= 0.1 of the texture) between neighbouring keys, holds frames and does not add up to a
    // whole texture, at a rate a person would call frames (a key at least every second); a slow drift between two far-apart keys is a scroll even though
    // it "jumps".
    const UvTrack& t0 = out.tracks[0];
    const size_t n = t0.keys();
    double biggest = 0;
    for (size_t k = 0; k + 1 < n; ++k)
        biggest = std::max({biggest, static_cast<double>(std::fabs(t0.trans[2 * (k + 1)] - t0.trans[2 * k])),
                            static_cast<double>(std::fabs(t0.trans[2 * (k + 1) + 1] - t0.trans[2 * k + 1]))});
    bool seamless = n > 2;   // constant speed on both axes, a whole number of textures in total, at least one of them moving
    bool moves = false;
    for (int axis = 0; axis < 2 && seamless; ++axis) {
        const double d0 = static_cast<double>(t0.trans[2 + axis]) - static_cast<double>(t0.trans[axis]);
        for (size_t k = 0; k + 1 < n; ++k)
            if (std::fabs((static_cast<double>(t0.trans[2 * (k + 1) + axis]) - static_cast<double>(t0.trans[2 * k + axis])) - d0) > 1e-3) { seamless = false; break; }
        const double total = d0 * static_cast<double>(n - 1);
        if (std::fabs(total - std::round(total)) > 0.01) seamless = false;
        if (std::fabs(std::round(total)) >= 1) moves = true;
    }
    out.stepped = !(seamless && moves) && n > 2 && biggest >= 0.1 && out.duration / static_cast<double>(n - 1) <= 1.0;
    return true;
}

Uv sample(const Anim& a, double seconds) {
    Uv r;
    if (a.tracks.empty()) return r;
    const UvTrack& t = a.tracks[0];
    const size_t n = t.keys();
    if (n == 0) return r;
    auto at = [&](size_t k) {
        Uv u;
        u.su = t.scale[2 * k];
        u.sv = t.scale[2 * k + 1];
        u.rot = t.rot[k];
        u.tu = t.trans[2 * k];
        u.tv = t.trans[2 * k + 1];
        return u;
    };
    if (n == 1) return at(0);
    double x = std::fmod(seconds, static_cast<double>(a.duration));
    if (x < 0) x += a.duration;
    if (a.stepped) {
        const size_t k = std::min(n - 1, static_cast<size_t>(x / a.duration * static_cast<double>(n)));
        return at(k);
    }
    const double f = x / a.duration * static_cast<double>(n - 1);
    const size_t k = std::min(n - 2, static_cast<size_t>(f));
    const float w = static_cast<float>(f - static_cast<double>(k));
    const Uv p = at(k), q = at(k + 1);
    r.su = p.su + (q.su - p.su) * w;
    r.sv = p.sv + (q.sv - p.sv) * w;
    r.rot = p.rot + (q.rot - p.rot) * w;
    r.tu = p.tu + (q.tu - p.tu) * w;
    r.tv = p.tv + (q.tv - p.tv) * w;
    return r;
}

}  // namespace sganim
}  // namespace zengine
