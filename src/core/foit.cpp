#include "foit.h"
#include "foit_names.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace zengine {
namespace foit {

namespace {

// Bounds-checked little-endian reader. Reading past the end sets `ok = false` and returns zeros, so the
// parsers can keep a simple shape and test `ok` where it matters (loops with counts read from the file).
struct Rd {
    const uint8_t* d;
    size_t n;
    size_t o;
    bool ok = true;
    Rd(const uint8_t* data, size_t size, size_t pos = 0) : d(data), n(size), o(pos) {}
    size_t left() const { return o <= n ? n - o : 0; }
    void fail() {
        ok = false;
        o = n;
    }
    uint8_t u8() {
        if (o + 1 > n) { fail(); return 0; }
        return d[o++];
    }
    uint16_t u16() {
        if (o + 2 > n) { fail(); return 0; }
        const uint16_t v = static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
        o += 2;
        return v;
    }
    uint32_t u32() {
        if (o + 4 > n) { fail(); return 0; }
        const uint32_t v = static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
                           (static_cast<uint32_t>(d[o + 2]) << 16) | (static_cast<uint32_t>(d[o + 3]) << 24);
        o += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    const uint8_t* take(size_t k) {
        if (k > left()) { fail(); return nullptr; }
        const uint8_t* p = d + o;
        o += k;
        return p;
    }
};

constexpr int kMaxDepth = 8;

// ---- scripts ---------------------------------------------------------------------------------------------
bool readChain(Rd& r, Chain& out) {
    out.clear();
    for (int guard = 0; guard < 20000; ++guard) {
        ChainNode nd;
        nd.a.desc = r.u8();
        nd.b.desc = r.u8();
        nd.c.desc = r.u8();
        const uint8_t fl = r.u8();
        nd.op = r.u8();
        nd.flags = fl & 1;
        nd.a.raw = (nd.a.desc & 1) ? r.u8() : r.u32();
        nd.b.raw = (nd.b.desc & 1) ? r.u8() : r.u32();
        nd.c.raw = (nd.c.desc & 1) ? r.u8() : r.u32();
        out.push_back(nd);
        if (!r.ok) return false;
        if (!(fl & 2)) return true;
    }
    return false;
}

bool readStep(Rd& r, Step& s) {
    s.b0 = r.u8();
    s.b1 = r.u8();
    s.b2 = r.u8();
    s.w = r.u16();
    s.flags = r.u8();
    if (s.flags & 2) {
        if (!readChain(r, s.chain)) return false;
    }
    const uint8_t* t = r.take(15);
    if (t) std::memcpy(s.t, t, 15);
    return r.ok;
}

bool readCommand(Rd& r, Command& c) {
    c.a = r.u16();
    c.b = r.u16();
    const uint16_t ns = r.u16();
    c.flags = r.u8();
    if (!r.ok) return false;
    if (c.flags & 2) {
        c.hasDefault = true;
        if (!readStep(r, c.def)) return false;
    }
    if (static_cast<size_t>(ns) * 19 > r.left()) return false;  // a step is at least 19 bytes
    c.steps.resize(ns);
    for (uint16_t i = 0; i < ns; ++i)
        if (!readStep(r, c.steps[i])) return false;
    return true;
}

bool readHit(Rd& r, HitData& h) {
    h.f8 = r.u16();
    h.f4 = r.u32();
    h.f13 = r.u8();
    h.fc = r.u16();
    h.fe = r.u16();
    h.f18 = r.u8();
    h.f2c = r.u32();
    h.f10 = r.u16();
    h.f12 = r.u8();
    h.f0 = r.u32();
    h.f14 = r.u8();
    h.f15 = r.u8();
    h.f16 = r.u8();
    h.f17 = r.u8();
    h.fa = r.u8();
    h.fb = r.u8();
    for (int k = 0; k < 4; ++k)
        if (h.fa & (1 << k)) {
            h.hasChain[k] = true;
            if (!readChain(r, h.chains[k])) return false;
        }
    if (h.fa & 0x10) h.f0a = r.u16();
    return r.ok;
}

bool readSub(Rd& r, std::vector<SubItem>& out) {
    for (int guard = 0; guard < 20000; ++guard) {
        SubItem o;
        o.a = r.u16();
        o.b = r.u32();
        o.c = r.u32();
        o.flags = r.u8();
        if (o.flags & 2)
            if (!readChain(r, o.chain)) return false;
        const uint8_t nxt = r.u8();
        out.push_back(std::move(o));
        if (!r.ok) return false;
        if (!nxt) return true;
    }
    return false;
}

bool readNodeProps(Rd& r, std::vector<NodeProps>& items) {
    for (int guard = 0; guard < 20000; ++guard) {
        NodeProps n;
        n.b1 = r.u8();
        n.b2 = r.u8();
        n.b3 = r.u8();
        if (n.b3 & 0x80)
            if (!readChain(r, n.chain)) return false;
        n.dur = (n.b1 & 1) ? r.u16() : 1;
        if (n.b3 & 0x40) n.x1a = r.u8();
        n.f2 = r.u8();
        if (n.f2) {
            if (n.f2 & 1) {
                n.hasHit = true;
                if (!readHit(r, n.hit)) return false;
            }
            if (n.f2 & 2)
                if (!readSub(r, n.sub)) return false;
            if (n.f2 & 4) {
                const uint8_t c = r.u8();
                if (static_cast<size_t>(c) * 4 > r.left()) return false;
                for (uint8_t i = 0; i < c; ++i) n.u32s.push_back(r.u32());
            }
        }
        const uint8_t f3 = r.u8();
        if (!r.ok) return false;
        if (f3) {
            n.hasCmd = true;
            if (!readCommand(r, n.cmd)) return false;
        }
        items.push_back(std::move(n));
        if (!f3) return true;
    }
    return false;
}

bool readStateHeader(Rd& r, size_t nframes, StateHeader& h) {
    const uint16_t cnt = r.u16();
    if (static_cast<size_t>(cnt) * 9 > r.left()) return false;  // refs (6) + props (>= 3)
    h.nodes.resize(cnt);
    for (uint16_t i = 0; i < cnt; ++i) {
        Node& nd = h.nodes[i];
        for (int k = 0; k < 3; ++k) {
            nd.refs[k] = r.u16();
            if (nd.refs[k] > nframes) return false;
        }
        if (!readNodeProps(r, nd.props)) return false;
    }
    if (r.u8()) {
        h.hasCmd = true;
        if (!readCommand(r, h.cmd)) return false;
        if (!readNodeProps(r, h.cmdNode)) return false;
    }
    if (r.u8()) {
        h.hasChain = true;
        if (!readChain(r, h.chain)) return false;
    }
    return r.ok;
}

bool readStates(Rd& r, size_t nframes, Object& o) {
    o.flagX = r.u8();
    const uint16_t n = r.u16();
    if (static_cast<size_t>(n) * 10 > r.left()) return false;
    o.states.resize(n);
    for (uint16_t i = 0; i < n; ++i) {
        State& s = o.states[i];
        s.f = r.u8();
        if (s.f & 4) {
            s.hasV18 = true;
            s.v18 = r.u32();
            s.v1c = 0;
        } else if (s.f & 8) {
            s.hasV18 = true;
            s.v18 = r.u32();
            s.v1c = 1;
        }
        s.b14 = r.u8();
        if (s.f & 2) {
            const uint8_t c = r.u8();
            if (static_cast<size_t>(c) * 4 > r.left()) return false;
            for (uint8_t k = 0; k < c; ++k) s.hashes.push_back(r.u32());
        }
        const uint16_t na = r.u16();
        s.next = r.u16();
        if (!r.ok) return false;
        if (!readStateHeader(r, nframes, s.hdr)) return false;
        if (static_cast<size_t>(na) * 9 > r.left()) return false;
        s.transitions.resize(na);
        for (uint16_t k = 0; k < na; ++k) {
            s.transitions[k].target = r.u16();
            if (!readCommand(r, s.transitions[k].cmd)) return false;
        }
    }
    return r.ok;
}

// the part shared by the main object and every child: slot defaults, optional chains
bool readTailCommon(Rd& r, Object& o) {
    const uint8_t c = r.u8();
    if (static_cast<size_t>(c) * 5 > r.left()) return false;
    for (uint8_t i = 0; i < c; ++i) {
        const uint32_t a = r.u32();
        const uint8_t b = r.u8();
        o.list8.emplace_back(a, b);
    }
    for (size_t i = 0; i < kSlotCount; ++i) o.slotDefaults[i] = r.u32();
    o.slotDefaultsValid = true;
    o.chainFlags = r.u8();
    for (int k = 0; k < 3; ++k)
        if (o.chainFlags & (1 << k)) {
            o.hasChain[k] = true;
            if (!readChain(r, o.chains[k])) return false;
        }
    return r.ok;
}

bool readAfterFrames(Rd& r, size_t nframes, Object& o, uint32_t& nchild) {
    if (!readStates(r, nframes, o)) return false;
    o.refBe = r.u16();
    o.refBf = r.u16();
    if (r.u8()) {
        o.hasRefC0 = true;
        o.refC0 = r.u16();
    }
    o.flagsDc = r.u8();
    o.x373 = r.u8();
    for (int k = 0; k < 4; ++k) o.box[k] = r.i32();
    nchild = r.u32();
    return r.ok;
}

bool readNames(Rd& r, Object& o) {
    for (State& s : o.states) {
        const uint8_t k = r.u8();
        if (k) {
            const uint8_t* p = r.take(k);
            if (!p) return false;
            s.name.assign(reinterpret_cast<const char*>(p), k);
        }
    }
    return r.ok;
}

bool readChild(Rd& r, size_t nframes, Object& c, int depth, std::string* err);

bool readChildren(Rd& r, size_t nframes, Object& o, uint32_t nchild, int depth, std::string* err) {
    if (depth >= kMaxDepth) {
        if (err) *err = "children nested too deep";
        return false;
    }
    if (static_cast<size_t>(nchild) * 13 > r.left()) {
        if (err) *err = "child count larger than the file";
        return false;
    }
    o.children.resize(nchild);
    for (uint32_t i = 0; i < nchild; ++i)
        if (!readChild(r, nframes, o.children[i], depth + 1, err)) return false;
    return true;
}

bool readChild(Rd& r, size_t nframes, Object& c, int depth, std::string* err) {
    const size_t hdrPos = r.o;
    const uint32_t off = r.u32();
    const uint8_t* mag = r.take(4);
    const uint32_t ver = r.u32();
    const uint8_t fl = r.u8();
    if (!r.ok || !mag || std::memcmp(mag, "VRSN", 4) != 0 || ver != 9) {
        if (err) *err = "bad child header at " + std::to_string(hdrPos);
        return false;
    }
    if (fl & 8) {
        if (err) *err = "child with animation summary not supported (at " + std::to_string(hdrPos) + ")";
        return false;
    }
    if (off >= r.n) return false;
    r.o = off;
    const uint8_t len = r.u8();
    if (len) {
        const uint8_t* p = r.take(len);
        if (!p) return false;
        c.name.assign(reinterpret_cast<const char*>(p), len);
    }
    c.v374 = r.u32();
    if (r.u8()) {
        if (err) *err = "child with layer object not supported (at " + std::to_string(hdrPos) + ")";
        return false;
    }
    if (!readTailCommon(r, c)) return false;
    if (r.u8() != 0) {
        if (err) *err = "child with its own frame list not supported (at " + std::to_string(hdrPos) + ")";
        return false;
    }
    uint32_t nchild = 0;
    if (!readAfterFrames(r, nframes, c, nchild)) return false;
    if (!readChildren(r, nframes, c, nchild, depth, err)) return false;
    return readNames(r, c);
}

// ---- frames ----------------------------------------------------------------------------------------------
bool readImage(Rd& r, Image& im) {
    im.cells = r.u16();
    im.w = r.u16();
    im.h = r.u16();
    im.n = r.u16();
    im.flags = r.u8();
    if (im.flags & 4) {
        r.u32();
        r.u32();
    }
    if (im.flags & 0x10) {
        im.hasData = true;
        const size_t ml = (static_cast<size_t>(im.cells) + 7) / 8;
        const uint8_t* m = r.take(ml);
        if (!m) return false;
        im.mask.assign(m, m + ml);
        if (im.flags & 0x20) {
            im.external = true;
            im.off = r.u32();
            for (int k = 0; k < 3; ++k) im.size[k] = r.u32();
        } else {
            for (int k = 0; k < 3; ++k) {
                const uint32_t len = r.u32();
                const uint8_t* p = r.take(len);
                if (!p) return false;
                im.inl[k].assign(p, p + len);
            }
        }
    }
    return r.ok;
}

bool readBoxList(Rd& r, std::vector<Box>& out) {
    const uint16_t c = r.u16();
    if (static_cast<size_t>(c) * 16 > r.left()) return false;
    out.resize(c);
    for (uint16_t i = 0; i < c; ++i) {
        out[i].top = r.i32();
        out[i].bottom = r.i32();
        out[i].left = r.i32();
        out[i].right = r.i32();
    }
    return r.ok;
}

bool readFrame(Rd& r, Frame& f) {
    f.pivotX = r.i32();
    f.pivotY = r.i32();
    f.src = r.u8();
    const uint8_t n = r.u8();
    if (static_cast<size_t>(n) * 12 > r.left()) return false;
    f.entries.resize(n);
    for (uint8_t i = 0; i < n; ++i) {
        f.entries[i][0] = r.u32();
        f.entries[i][1] = r.u32();
        f.entries[i][2] = r.u32();
    }
    f.slotMask = r.u8();
    for (int b = 0; b < kSlots; ++b)
        if (f.slotMask & (1 << b))
            if (!readImage(r, f.img[b])) return false;
    for (int i = 0; i < 10; ++i) f.xy[i] = r.i32();
    if (!readBoxList(r, f.hit)) return false;
    if (!readBoxList(r, f.hurt)) return false;
    if (!readBoxList(r, f.push)) return false;
    return r.ok;
}

// ---- the layer object (colour palettes) ------------------------------------------------------------------
// The text a script chain was compiled from (length-prefixed); only the tools use it.
bool skipScriptText(Rd& r) {
    const uint32_t n = r.u32();
    return r.take(n) != nullptr;
}

// A script chain followed by its source text and a validity byte.
bool skipScripted(Rd& r) {
    Chain c;
    if (!readChain(r, c)) return false;
    if (!skipScriptText(r)) return false;
    r.u8();
    return r.ok;
}

bool readLayerObject(Rd& r, LayerObject& L) {
    L = LayerObject{};
    const uint16_t numLayers = r.u16();
    const uint8_t numGroups = r.u8();
    L.groups.resize(numGroups);
    for (ColorGroup& g : L.groups) {
        g.count = r.u8();
        g.flagA = r.u8();
        g.flagB = r.u8();
        const uint8_t n = r.u8();
        const uint8_t* p = r.take(n);
        if (!r.ok) return false;
        if (n) g.name.assign(reinterpret_cast<const char*>(p), n);
        L.totalColors += g.count;
    }
    const uint8_t numRanges = r.u8();
    for (uint8_t i = 0; i < numRanges; ++i) {
        const uint32_t last = r.u32();
        const uint32_t group = r.u32();
        L.ranges.emplace_back(last, group);
    }
    if (!r.ok || static_cast<size_t>(numLayers) * (static_cast<size_t>(L.totalColors) * 4 + 20) > r.left()) return false;
    L.palettes.resize(numLayers);
    for (Palette& pal : L.palettes) {
        pal.colors.resize(L.totalColors);
        for (uint32_t& c : pal.colors) c = r.u32();
        pal.edge[0] = r.u32();
        pal.edge[1] = r.u32();
        r.u8();  // two flag bytes
        r.u8();
        const uint8_t numBytes = r.u8();
        if (!r.take(numBytes)) return false;
        // rules: (u32, u32, u8) + optional script
        for (uint8_t n = r.u8(), i = 0; i < n; ++i) {
            r.u32();
            r.u32();
            r.u8();
            if (r.u8() && !skipScripted(r)) return false;
        }
        // named values: name, hash, small payload, a few scalars, a text, optional script
        for (uint8_t n = r.u8(), i = 0; i < n; ++i) {
            const uint32_t nameLen = r.u32();
            if (!r.take(nameLen)) return false;
            r.u32();
            if (!r.take(r.u8())) return false;
            r.u8();
            r.u8();
            if (!r.take(4)) return false;
            r.u8();
            r.u32();
            r.u32();
            r.u32();
            if (!skipScriptText(r)) return false;
            if (r.u8() && !skipScripted(r)) return false;
        }
        r.u32();
        for (uint8_t n = r.u8(), i = 0; i < n; ++i) {
            r.u32();
            if (!skipScripted(r)) return false;
        }
        for (uint8_t n = r.u8(), i = 0; i < n; ++i) r.u16();
        for (uint8_t n = r.u8(), i = 0; i < n; ++i) {
            r.u32();
            r.u32();
            Chain c;
            if (!readChain(r, c) || !skipScriptText(r)) return false;
        }
        if (!r.ok) return false;
    }
    return r.ok;
}

// Everything from the slot-default table to the end of the file.  `r` is positioned at the table.
bool parseBody(Rd& r, Object& body, std::vector<Frame>& frames, size_t& frameListPos, std::string* err) {
    if (!readTailCommon(r, body)) return false;
    if (r.u8() != 1) return false;  // frame list flag: this object owns the frames
    frameListPos = r.o;
    const uint32_t nframes = r.u32();
    if (nframes == 0 || nframes > 100000 || static_cast<size_t>(nframes) * 30 > r.left()) return false;
    frames.assign(nframes, Frame{});
    for (uint32_t i = 0; i < nframes; ++i)
        if (!readFrame(r, frames[i])) return false;
    uint32_t nchild = 0;
    if (!readAfterFrames(r, nframes, body, nchild)) return false;
    if (!readChildren(r, nframes, body, nchild, 0, err)) return false;
    if (!readNames(r, body)) return false;
    return r.ok && r.o == r.n;
}

}  // namespace

size_t Character::countChildren() const {
    size_t n = 0;
    struct Local {
        static void walk(const Object& o, size_t& n) {
            n += o.children.size();
            for (const Object& c : o.children) walk(c, n);
        }
    };
    Local::walk(body, n);
    return n;
}

size_t Character::countStates() const {
    size_t n = 0;
    struct Local {
        static void walk(const Object& o, size_t& n) {
            n += o.states.size();
            for (const Object& c : o.children) walk(c, n);
        }
    };
    Local::walk(body, n);
    return n;
}

bool parse(const uint8_t* data, size_t size, Character& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    out = Character{};
    out.fileSize = size;
    Rd r(data, size, 0);
    const uint32_t off = r.u32();
    const uint8_t* mag = r.take(4);
    out.version = r.u32();
    out.headerFlags = r.u8();
    if (!r.ok || !mag || std::memcmp(mag, "VRSN", 4) != 0) return fail("not a .foit (missing VRSN)");
    if (out.version != 9) return fail("unsupported .foit version " + std::to_string(out.version));
    if (off >= size) return fail("body offset outside the file");
    r.o = off;
    const uint8_t len = r.u8();
    Object body;
    if (len) {
        const uint8_t* p = r.take(len);
        if (!p) return fail("truncated name");
        body.name.assign(reinterpret_cast<const char*>(p), len);
    }
    body.v374 = r.u32();
    const uint8_t hasLayer = r.u8();
    if (!r.ok) return fail("truncated body header");

    std::string childErr;
    out.hasLayer = hasLayer != 0;
    out.layerPos = r.o;
    if (hasLayer && !readLayerObject(r, out.layer)) return fail("bad layer object (palettes) at " + std::to_string(out.layerPos));
    out.tailPos = r.o;
    std::vector<Frame> frames;
    if (!parseBody(r, body, frames, out.frameListPos, &childErr))
        return fail("character body does not match the grammar" + (childErr.empty() ? "" : ": " + childErr));
    out.body = std::move(body);
    out.frames = std::move(frames);
    return true;
}

// ---- pixels ----------------------------------------------------------------------------------------------
namespace {
int popcount(const std::vector<uint8_t>& m) {
    int c = 0;
    for (uint8_t b : m)
        while (b) {
            c += b & 1;
            b >>= 1;
        }
    return c;
}

bool decodePlanes(const Image& im, const uint8_t* src, size_t srcLen, std::vector<uint8_t>* grad,
                  std::vector<uint8_t>* band) {
    if (!im.hasData || im.placeholder()) return false;
    if (im.w == 0 || im.h == 0 || im.cells != static_cast<uint32_t>(im.w) * im.h) return false;
    if (im.n == 0 || im.n > im.cells || popcount(im.mask) != im.n) return false;
    const size_t P = static_cast<size_t>(im.n) * 256;
    std::vector<uint8_t> scratch;
    if (im.external) {
        const uint64_t total = static_cast<uint64_t>(im.off) + im.size[0] + im.size[1] + im.size[2];
        if (!src || total > srcLen || !im.size[0] || !im.size[1] || !im.size[2]) return false;
        const size_t o = im.off;
        if (!rleDecode(src, srcLen, o, o + im.size[0], P, scratch) || scratch.size() != P) return false;
        std::vector<uint8_t> g, b;
        if (!rleDecode(src, srcLen, o + im.size[0], o + im.size[0] + im.size[1], P, g) || g.size() != P) return false;
        if (!rleDecode(src, srcLen, o + im.size[0] + im.size[1], static_cast<size_t>(total), P, b) || b.size() != P) return false;
        if (im.flags & 0x40) interpolateOddToneRows(g, static_cast<size_t>(im.n) * 16);  // see art.h (tag bit 6)
        if (grad) *grad = std::move(g);
        if (band) *band = std::move(b);
        return true;
    }
    std::vector<uint8_t> g, b;
    if (!rleDecode(im.inl[0].data(), im.inl[0].size(), 0, im.inl[0].size(), P, scratch) || scratch.size() != P) return false;
    if (!rleDecode(im.inl[1].data(), im.inl[1].size(), 0, im.inl[1].size(), P, g) || g.size() != P) return false;
    if (!rleDecode(im.inl[2].data(), im.inl[2].size(), 0, im.inl[2].size(), P, b) || b.size() != P) return false;
    if (im.flags & 0x40) interpolateOddToneRows(g, static_cast<size_t>(im.n) * 16);
    if (grad) *grad = std::move(g);
    if (band) *band = std::move(b);
    return true;
}
}  // namespace

bool decodeImage(const Image& im, const uint8_t* src, size_t srcLen, ArtPlanes& out) {
    std::vector<uint8_t> grad, band;
    if (!decodePlanes(im, src, srcLen, &grad, &band)) return false;
    placeCells(im.w, im.h, im.n, im.mask.data(), im.mask.size(), grad, band, out);
    return true;
}

bool validateImage(const Image& im, const uint8_t* src, size_t srcLen) {
    return decodePlanes(im, src, srcLen, nullptr, nullptr);
}

// ---- debugging text --------------------------------------------------------------------------------------
namespace {
const char* opName(const ChainNode& n) {
    static const char* arith[] = {"nop", "set", "add", "sub", "mul", "div", "abs", "trunc", "default", "sin", "cos",
                                  "max", "min", "atan2", "set-if", "set-ifnot"};
    if (!n.boolean() && n.op < 16) return arith[n.op];
    if (n.boolean()) {
        switch (n.op) {
            case 0x10: return "<";
            case 0x11: return ">";
            case 0x12: return "==";
            case 0x13: return ">=";
            case 0x14: return "<=";
            case 0x15: return "!=";
            case 0x16: return "==";
            case 0x17: return "!=";
            case 0x18: return "true";
            default: break;
        }
    }
    return nullptr;
}

std::string operandText(const Operand& o) {
    std::string pre;
    if ((o.desc & 0x90) == 0x90) pre = "PRNT.";
    else if ((o.desc & 0x18) == 0x18) pre = "PT.";
    else if (o.desc & 8) pre = "L.";
    else if (o.desc & 0x10) pre = "E.";
    else if (o.desc & 0x40) pre = "T.";
    else if (o.desc & 0x80) pre = "A.";
    char buf[48];
    if (o.desc & 1) {
        const char* nm = (o.desc & 2) ? nullptr : slotName(static_cast<int>(o.raw & 0xFF));
        if (nm) std::snprintf(buf, sizeof buf, "%s%s", pre.c_str(), nm);
        else std::snprintf(buf, sizeof buf, "%s%s[%u]", pre.c_str(), (o.desc & 2) ? "G" : "", o.raw & 0xFF);
    } else {
        float f;
        std::memcpy(&f, &o.raw, 4);
        if (o.desc & 0x20) std::snprintf(buf, sizeof buf, "%sFLAG%d", pre.c_str(), static_cast<int>(f));
        else if (o.desc & 4) std::snprintf(buf, sizeof buf, "RANDOM(%d)", static_cast<int>(o.raw));
        else std::snprintf(buf, sizeof buf, "%s%g", pre.c_str(), static_cast<double>(f));
    }
    return buf;
}
}  // namespace

std::string describeChain(const Chain& c) {
    std::string s;
    for (const ChainNode& n : c) {
        if (!s.empty()) s += "; ";
        char buf[40];
        const char* nm = opName(n);
        if (nm) std::snprintf(buf, sizeof buf, "%s", nm);
        else std::snprintf(buf, sizeof buf, "op%u", n.op);
        if (n.boolean()) {
            // the VM compares A with operand C (see vm::testNode); B is printed too when it holds something else
            s += "if " + operandText(n.a) + " " + buf + " " + operandText(n.c);
            if (n.b.desc != n.c.desc || n.b.raw != n.c.raw) s += " {b=" + operandText(n.b) + "}";
        }
        else s += operandText(n.c) + " = " + buf + "(" + operandText(n.a) + ", " + operandText(n.b) + ")";
    }
    return s;
}

}  // namespace foit
}  // namespace zengine
