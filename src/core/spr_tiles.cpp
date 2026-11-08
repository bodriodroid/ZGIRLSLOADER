// spr_tiles.cpp - see spr_tiles.h
#include "spr_tiles.h"

#include <algorithm>
#include <cstring>

namespace zengine {

namespace {

struct Rd {
    const std::vector<uint8_t>& d;
    size_t p = 0;
    bool ok = true;
    explicit Rd(const std::vector<uint8_t>& v) : d(v) {}
    bool need(size_t n) {
        if (!ok || d.size() - p < n || p > d.size()) ok = false;
        return ok;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | d[p + static_cast<size_t>(i)];
        p += 8;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = static_cast<uint32_t>(d[p]) << 24 | static_cast<uint32_t>(d[p + 1]) << 16 | static_cast<uint32_t>(d[p + 2]) << 8 | d[p + 3];
        p += 4;
        return v;
    }
    float f32() {
        const uint32_t b = u32();
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    std::string str() {
        const uint64_t n = u64();
        if (!ok || n > 4096 || !need(static_cast<size_t>(n))) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(d.data() + p), static_cast<size_t>(n));
        p += static_cast<size_t>(n);
        return s;
    }
};

bool fail(std::string* err, const char* why) {
    if (err) *err = why;
    return false;
}

}  // namespace

bool parseSprTiles(const std::vector<uint8_t>& data, SprTiles& out, std::string* err) {
    out = SprTiles{};
    Rd r(data);
    SprTiles s;
    const std::string version = r.str();
    s.name = r.str();
    r.u32();  // 4 bytes of zeros between the name and the layout string in the real files
    const std::string layout = r.str();
    if (!r.ok || version != "2.0") return fail(err, "not a tile sprite (header)");
    if (layout.find("tile_x") == std::string::npos) return fail(err, "unknown tile layout");
    const uint64_t bpt = r.u64(), ntiles = r.u64(), nframes = r.u64();
    r.u64();  // unknown (2)
    const uint64_t tw = r.u64(), th = r.u64();
    if (!r.ok || bpt != 4 || tw == 0 || th == 0 || tw > 256 || th > 256) return fail(err, "bad counts");
    if (ntiles > 4000000 || nframes > 100000 || !r.need(static_cast<size_t>(ntiles) * 4 + static_cast<size_t>(nframes) * 20))
        return fail(err, "truncated");
    s.tileW = static_cast<int>(tw);
    s.tileH = static_cast<int>(th);
    s.tiles.resize(static_cast<size_t>(ntiles));
    for (SprTile& t : s.tiles) {
        t.x = data[r.p];
        t.y = data[r.p + 1];
        t.u = data[r.p + 2];
        t.v = data[r.p + 3];
        r.p += 4;
    }
    s.frames.resize(static_cast<size_t>(nframes));
    for (SprFrame& f : s.frames) {
        f.first = r.u32();
        f.count = r.u32();
        f.flags = r.u32();
        f.offsetX = r.f32();
        f.offsetY = r.f32();
        if (static_cast<uint64_t>(f.first) + f.count > ntiles) return fail(err, "frame outside the tile list");
        for (uint32_t i = 0; i < f.count; ++i) {
            const SprTile& t = s.tiles[f.first + i];
            f.cols = std::max(f.cols, static_cast<int>(t.x) + 1);
            f.rows = std::max(f.rows, static_cast<int>(t.y) + 1);
        }
    }
    // animations until the end of the file; a malformed tail only loses the names
    while (r.p < data.size()) {
        SprAnim a;
        a.name = r.str();
        a.first = r.u32();
        a.count = r.u32();
        a.last = static_cast<uint32_t>(r.u64());
        if (!r.ok) break;
        if (a.count == 0 || a.first + a.count > s.frames.size()) break;
        s.anims.push_back(a);
    }
    out = std::move(s);
    return true;
}

bool composeSprFrame(const SprTiles& s, size_t frame, const uint8_t* atlas, int aw, int ah, std::vector<uint8_t>& rgba, int& outW, int& outH) {
    if (frame >= s.frames.size() || !atlas || aw <= 0 || ah <= 0) return false;
    const SprFrame& f = s.frames[frame];
    outW = f.cols * s.tileW;
    outH = f.rows * s.tileH;
    if (outW <= 0 || outH <= 0) return false;
    rgba.assign(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4, 0);
    for (uint32_t i = 0; i < f.count; ++i) {
        const SprTile& t = s.tiles[f.first + i];
        const int sx = t.u * s.tileW, sy = t.v * s.tileH;
        if (sx + s.tileW > aw || sy + s.tileH > ah) continue;
        const int dx = t.x * s.tileW, dy = t.y * s.tileH;
        for (int row = 0; row < s.tileH; ++row)
            std::memcpy(rgba.data() + (static_cast<size_t>(dy + row) * static_cast<size_t>(outW) + static_cast<size_t>(dx)) * 4,
                        atlas + (static_cast<size_t>(sy + row) * static_cast<size_t>(aw) + static_cast<size_t>(sx)) * 4, static_cast<size_t>(s.tileW) * 4);
    }
    return true;
}

}  // namespace zengine
