// foit_write.cpp - see foit_write.h.  Mirrors the reader in foit.cpp function by function.
#include "foit_write.h"

#include <cstring>

namespace zengine {
namespace foit {

namespace {

struct Wr {
    std::vector<uint8_t>& o;
    std::string err;
    explicit Wr(std::vector<uint8_t>& out) : o(out) {}
    bool bad(const std::string& m) {
        if (err.empty()) err = m;
        return false;
    }
    void u8(uint32_t v) { o.push_back(static_cast<uint8_t>(v)); }
    void u16(uint32_t v) {
        u8(v);
        u8(v >> 8);
    }
    void u32(uint32_t v) {
        u16(v);
        u16(v >> 16);
    }
    void bytes(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        o.insert(o.end(), b, b + n);
    }
    // a count that has to fit a field of `bits` bits
    bool fits(size_t n, size_t max, const char* what) {
        if (n > max) return bad(std::string("too many ") + what);
        return true;
    }
};

bool putChain(Wr& w, const Chain& c) {
    if (c.empty()) return w.bad("a script chain needs at least one node");
    for (size_t i = 0; i < c.size(); ++i) {
        const ChainNode& n = c[i];
        w.u8(n.a.desc);
        w.u8(n.b.desc);
        w.u8(n.c.desc);
        w.u8((n.flags & 1) | (i + 1 < c.size() ? 2 : 0));
        w.u8(n.op);
        for (const Operand* x : {&n.a, &n.b, &n.c}) {
            if (x->desc & 1) w.u8(x->raw);
            else w.u32(x->raw);
        }
    }
    return true;
}

bool putStep(Wr& w, const Step& s) {
    w.u8(s.b0);
    w.u8(s.b1);
    w.u8(s.b2);
    w.u16(s.w);
    const uint8_t fl = static_cast<uint8_t>((s.flags & ~2) | (s.chain.empty() ? 0 : 2));
    w.u8(fl);
    if (fl & 2)
        if (!putChain(w, s.chain)) return false;
    w.bytes(s.t, 15);
    return true;
}

bool putCommand(Wr& w, const Command& c) {
    if (!w.fits(c.steps.size(), 0xFFFF, "command steps")) return false;
    w.u16(c.a);
    w.u16(c.b);
    w.u16(static_cast<uint32_t>(c.steps.size()));
    w.u8((c.flags & ~2) | (c.hasDefault ? 2 : 0));
    if (c.hasDefault)
        if (!putStep(w, c.def)) return false;
    for (const Step& s : c.steps)
        if (!putStep(w, s)) return false;
    return true;
}

bool putHit(Wr& w, const HitData& h) {
    w.u16(h.f8);
    w.u32(h.f4);
    w.u8(h.f13);
    w.u16(h.fc);
    w.u16(h.fe);
    w.u8(h.f18);
    w.u32(h.f2c);
    w.u16(h.f10);
    w.u8(h.f12);
    w.u32(h.f0);
    w.u8(h.f14);
    w.u8(h.f15);
    w.u8(h.f16);
    w.u8(h.f17);
    uint8_t fa = h.fa & ~0x0F;
    for (int k = 0; k < 4; ++k)
        if (h.hasChain[k]) fa |= static_cast<uint8_t>(1 << k);
    w.u8(fa);
    w.u8(h.fb);
    for (int k = 0; k < 4; ++k)
        if (h.hasChain[k])
            if (!putChain(w, h.chains[k])) return false;
    if (fa & 0x10) w.u16(h.f0a);
    return true;
}

bool putSub(Wr& w, const std::vector<SubItem>& items) {
    if (items.empty()) return w.bad("a sub list needs at least one item");
    for (size_t i = 0; i < items.size(); ++i) {
        const SubItem& s = items[i];
        w.u16(s.a);
        w.u32(s.b);
        w.u32(s.c);
        const uint8_t fl = static_cast<uint8_t>((s.flags & ~2) | (s.chain.empty() ? 0 : 2));
        w.u8(fl);
        if (fl & 2)
            if (!putChain(w, s.chain)) return false;
        w.u8(i + 1 < items.size() ? 1 : 0);
    }
    return true;
}

bool putNodeProps(Wr& w, const std::vector<NodeProps>& items) {
    if (items.empty()) return w.bad("a node needs at least one property record");
    for (size_t i = 0; i < items.size(); ++i) {
        const NodeProps& n = items[i];
        const uint8_t b3 = static_cast<uint8_t>((n.b3 & ~0x80) | (n.chain.empty() ? 0 : 0x80));
        const uint8_t b1 = static_cast<uint8_t>((n.b1 & ~1) | (n.dur != 1 ? 1 : (n.b1 & 1)));
        w.u8(b1);
        w.u8(n.b2);
        w.u8(b3);
        if (b3 & 0x80)
            if (!putChain(w, n.chain)) return false;
        if (b1 & 1) w.u16(n.dur);
        if (b3 & 0x40) w.u8(n.x1a);
        const uint8_t f2 = static_cast<uint8_t>((n.f2 & ~7) | (n.hasHit ? 1 : 0) | (n.sub.empty() ? 0 : 2) | (n.u32s.empty() ? 0 : 4));
        w.u8(f2);
        if (f2 & 1)
            if (!putHit(w, n.hit)) return false;
        if (f2 & 2)
            if (!putSub(w, n.sub)) return false;
        if (f2 & 4) {
            if (!w.fits(n.u32s.size(), 255, "values in a node list")) return false;
            w.u8(static_cast<uint32_t>(n.u32s.size()));
            for (uint32_t v : n.u32s) w.u32(v);
        }
        const bool more = i + 1 < items.size();
        w.u8(more ? 1 : 0);
        if (more) {
            if (!items[i].hasCmd) return w.bad("a linked property record needs its command header");
            if (!putCommand(w, n.cmd)) return false;
        }
    }
    return true;
}

bool putStateHeader(Wr& w, size_t nframes, const StateHeader& h) {
    if (!w.fits(h.nodes.size(), 0xFFFF, "nodes in a state")) return false;
    w.u16(static_cast<uint32_t>(h.nodes.size()));
    for (const Node& nd : h.nodes) {
        for (int k = 0; k < 3; ++k) {
            if (nd.refs[k] > nframes) return w.bad("a node points at a frame that does not exist");
            w.u16(nd.refs[k]);
        }
        if (!putNodeProps(w, nd.props)) return false;
    }
    w.u8(h.hasCmd ? 1 : 0);
    if (h.hasCmd) {
        if (!putCommand(w, h.cmd)) return false;
        if (!putNodeProps(w, h.cmdNode)) return false;
    }
    w.u8(h.hasChain ? 1 : 0);
    if (h.hasChain)
        if (!putChain(w, h.chain)) return false;
    return true;
}

bool putStates(Wr& w, size_t nframes, const Object& o) {
    if (!w.fits(o.states.size(), 0xFFFF, "states")) return false;
    w.u8(o.flagX);
    w.u16(static_cast<uint32_t>(o.states.size()));
    for (const State& s : o.states) {
        uint8_t f = s.f & ~(4 | 8 | 2);
        if (s.hasV18) f |= s.v1c ? 8 : 4;
        if (!s.hashes.empty()) f |= 2;
        w.u8(f);
        if (s.hasV18) w.u32(s.v18);
        w.u8(s.b14);
        if (f & 2) {
            if (!w.fits(s.hashes.size(), 255, "state hashes")) return false;
            w.u8(static_cast<uint32_t>(s.hashes.size()));
            for (uint32_t h : s.hashes) w.u32(h);
        }
        if (!w.fits(s.transitions.size(), 0xFFFF, "transitions")) return false;
        w.u16(static_cast<uint32_t>(s.transitions.size()));
        w.u16(s.next);
        if (!putStateHeader(w, nframes, s.hdr)) return false;
        for (const Transition& t : s.transitions) {
            w.u16(t.target);
            if (!putCommand(w, t.cmd)) return false;
        }
    }
    return true;
}

bool putTailCommon(Wr& w, const Object& o) {
    if (!w.fits(o.list8.size(), 255, "list8 entries")) return false;
    w.u8(static_cast<uint32_t>(o.list8.size()));
    for (const auto& p : o.list8) {
        w.u32(p.first);
        w.u8(p.second);
    }
    for (size_t i = 0; i < kSlotCount; ++i) w.u32(o.slotDefaults[i]);
    uint8_t cf = o.chainFlags & ~7;
    for (int k = 0; k < 3; ++k)
        if (o.hasChain[k]) cf |= static_cast<uint8_t>(1 << k);
    w.u8(cf);
    for (int k = 0; k < 3; ++k)
        if (o.hasChain[k])
            if (!putChain(w, o.chains[k])) return false;
    return true;
}

bool putNames(Wr& w, const Object& o) {
    for (const State& s : o.states) {
        if (!w.fits(s.name.size(), 255, "characters in a state name")) return false;
        w.u8(static_cast<uint32_t>(s.name.size()));
        w.bytes(s.name.data(), s.name.size());
    }
    return true;
}

bool putAfterFrames(Wr& w, size_t nframes, const Object& o) {
    if (!putStates(w, nframes, o)) return false;
    w.u16(o.refBe);
    w.u16(o.refBf);
    w.u8(o.hasRefC0 ? 1 : 0);
    if (o.hasRefC0) w.u16(o.refC0);
    w.u8(o.flagsDc);
    w.u8(o.x373);
    for (int k = 0; k < 4; ++k) w.u32(static_cast<uint32_t>(o.box[k]));
    w.u32(static_cast<uint32_t>(o.children.size()));
    return true;
}

bool putChild(Wr& w, size_t nframes, const Object& c, int depth);

bool putChildren(Wr& w, size_t nframes, const Object& o, int depth) {
    if (!o.children.empty() && depth >= 8) return w.bad("children nested too deep");
    for (const Object& c : o.children)
        if (!putChild(w, nframes, c, depth + 1)) return false;
    return true;
}

bool putName(Wr& w, const std::string& n) {
    if (!w.fits(n.size(), 255, "characters in a name")) return false;
    w.u8(static_cast<uint32_t>(n.size()));
    w.bytes(n.data(), n.size());
    return true;
}

bool putChild(Wr& w, size_t nframes, const Object& c, int depth) {
    w.u32(static_cast<uint32_t>(w.o.size() + 13));   // the body follows its header at once
    w.bytes("VRSN", 4);
    w.u32(9);
    w.u8(c.childFlags & ~8);
    if (!putName(w, c.name)) return false;
    w.u32(c.v374);
    w.u8(0);   // no layer object
    if (!putTailCommon(w, c)) return false;
    w.u8(0);   // the frames belong to the character
    if (!putAfterFrames(w, nframes, c)) return false;
    if (!putChildren(w, nframes, c, depth)) return false;
    return putNames(w, c);
}

bool putImage(Wr& w, const Image& im) {
    w.u16(im.cells);
    w.u16(im.w);
    w.u16(im.h);
    w.u16(im.n);
    w.u8(im.flags);
    if (im.flags & 4) {
        w.u32(im.extra[0]);
        w.u32(im.extra[1]);
    }
    if (im.flags & 0x10) {
        const size_t ml = (static_cast<size_t>(im.cells) + 7) / 8;
        if (im.mask.size() != ml) return w.bad("an image mask does not match its cell count");
        w.bytes(im.mask.data(), ml);
        if (im.flags & 0x20) {
            w.u32(im.off);
            for (int k = 0; k < 3; ++k) w.u32(im.size[k]);
        } else {
            for (int k = 0; k < 3; ++k) {
                w.u32(static_cast<uint32_t>(im.inl[k].size()));
                w.bytes(im.inl[k].data(), im.inl[k].size());
            }
        }
    }
    return true;
}

bool putBoxes(Wr& w, const std::vector<Box>& b) {
    if (!w.fits(b.size(), 0xFFFF, "boxes")) return false;
    w.u16(static_cast<uint32_t>(b.size()));
    for (const Box& x : b) {
        w.u32(static_cast<uint32_t>(x.top));
        w.u32(static_cast<uint32_t>(x.bottom));
        w.u32(static_cast<uint32_t>(x.left));
        w.u32(static_cast<uint32_t>(x.right));
    }
    return true;
}

bool putFrame(Wr& w, const Frame& f) {
    w.u32(static_cast<uint32_t>(f.pivotX));
    w.u32(static_cast<uint32_t>(f.pivotY));
    w.u8(f.src);
    if (!w.fits(f.entries.size(), 255, "named points in a frame")) return false;
    w.u8(static_cast<uint32_t>(f.entries.size()));
    for (const auto& e : f.entries) {
        w.u32(e[0]);
        w.u32(e[1]);
        w.u32(e[2]);
    }
    w.u8(f.slotMask);
    for (int b = 0; b < kSlots; ++b)
        if (f.slotMask & (1 << b))
            if (!putImage(w, f.img[b])) return false;
    for (int i = 0; i < 10; ++i) w.u32(static_cast<uint32_t>(f.xy[i]));
    return putBoxes(w, f.hit) && putBoxes(w, f.hurt) && putBoxes(w, f.push);
}

}  // namespace

bool write(const Character& c, std::vector<uint8_t>& out, std::string* err) {
    out.clear();
    Wr w(out);
    auto fail = [&]() {
        if (err) *err = w.err.empty() ? "cannot write this character" : w.err;
        out.clear();
        return false;
    };
    if (c.frames.empty() || c.frames.size() > 100000) {
        w.bad("a character needs between 1 and 100000 frames");
        return fail();
    }
    w.u32(static_cast<uint32_t>(13 + c.preamble.size()));
    w.bytes("VRSN", 4);
    w.u32(c.version ? c.version : 9);
    w.u8(c.headerFlags);
    w.bytes(c.preamble.data(), c.preamble.size());
    if (!putName(w, c.body.name)) return fail();
    w.u32(c.body.v374);
    w.u8(c.hasLayer ? 1 : 0);
    if (c.hasLayer) w.bytes(c.layerRaw.data(), c.layerRaw.size());
    if (!putTailCommon(w, c.body)) return fail();
    w.u8(1);
    w.u32(static_cast<uint32_t>(c.frames.size()));
    for (const Frame& f : c.frames)
        if (!putFrame(w, f)) return fail();
    if (!putAfterFrames(w, c.frames.size(), c.body)) return fail();
    if (!putChildren(w, c.frames.size(), c.body, 0)) return fail();
    if (!putNames(w, c.body)) return fail();
    return true;
}

}  // namespace foit
}  // namespace zengine
