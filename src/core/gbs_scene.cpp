// gbs_scene.cpp - see gbs_scene.h.
#include "gbs_scene.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "gbs.h"

namespace zengine {

namespace {

constexpr uint32_t kTagFont = 0x47464E54;   // "TNFG"
constexpr uint32_t kTagSprite = 0x47535052; // "RPSG"
constexpr uint32_t kTagLabel = 0x474C424C;  // "LBLG"
constexpr uint32_t kTagGroup = 0x47475250;  // "PRGG"
constexpr uint32_t kTagAnim = 0x4747414C;   // "LAGG"
constexpr size_t kNodeHeader = 76;          // tag, size, id, name[64]
constexpr size_t kTextureRow = 284;
constexpr int kMaxDepth = 24;

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
float rdf(const uint8_t* p) {
    const uint32_t v = rd32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
std::string cstr(const uint8_t* p, size_t max) {
    const char* s = reinterpret_cast<const char*>(p);
    return std::string(s, strnlen(s, max));
}
bool fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

bool parseFont(const uint8_t* d, size_t pos, size_t size, GbsFont& f) {
    if (size < kNodeHeader + 24 || ((size - kNodeHeader) & 3) != 0) return false;
    f.id = rd32(d + pos + 8);
    f.name = cstr(d + pos + 12, 64);
    const uint8_t* b = d + pos + kNodeHeader;
    const size_t words = (size - kNodeHeader) / 4;
    f.lineHeight = static_cast<int>(rd32(b));
    f.atlasW = static_cast<int>(rd32(b + 4));
    f.atlasH = static_cast<int>(rd32(b + 8));
    f.baseline = static_cast<int>(rd32(b + 12));
    f.pages = static_cast<int>(rd32(b + 16));
    const size_t count = rd32(b + 20);
    if (f.atlasW <= 0 || f.atlasH <= 0 || f.atlasW > 16384 || f.atlasH > 16384) return false;
    if (count > (words - 6) / 10) return false;
    f.glyphs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* r = b + 24 + i * 40;
        GbsGlyph g;
        g.cp = rd32(r);
        g.page = static_cast<int>(rd32(r + 36));   // the atlas page is the LAST word of the row (word 1 is always 0)
        g.x = static_cast<int>(rd32(r + 8));
        g.y = static_cast<int>(rd32(r + 12));
        g.w = static_cast<int>(rd32(r + 16));
        g.h = static_cast<int>(rd32(r + 20));
        g.bearingY = static_cast<int>(rd32(r + 24));
        g.advance = static_cast<int>(rd32(r + 28));
        g.bearingX = static_cast<int>(rd32(r + 32));
        f.glyphs.push_back(g);
    }
    std::stable_sort(f.glyphs.begin(), f.glyphs.end(), [](const GbsGlyph& a, const GbsGlyph& b2) { return a.cp < b2.cp; });
    return true;
}

// Parses one node chunk at [pos, pos+size) inside [pos, end).  Returns false when sizes do not add up.
bool parseNode(const uint8_t* d, size_t pos, size_t end, int depth, GbsNode& n, std::string* err) {
    if (depth > kMaxDepth) return fail(err, "node tree too deep");
    if (pos + kNodeHeader + 40 > end) return fail(err, "node header truncated");
    const uint32_t tag = rd32(d + pos);
    const size_t size = rd32(d + pos + 4);
    if (size < kNodeHeader + 40 || pos + size > end) return fail(err, "node size out of range");
    n.tag.assign(reinterpret_cast<const char*>(d + pos), 4);
    n.id = rd32(d + pos + 8);
    n.name = cstr(d + pos + 12, 64);
    const uint8_t* b = d + pos + kNodeHeader;
    n.x = rdf(b);
    n.y = rdf(b + 4);
    n.pivotX = rdf(b + 8);
    n.pivotY = rdf(b + 12);
    n.w = rdf(b + 16);
    n.h = rdf(b + 20);
    n.rotation = rdf(b + 24);
    n.anchorFlags = rd32(b + 28);
    n.maskId = static_cast<int32_t>(rd32(b + 32));
    if (tag == kTagSprite) {
        n.type = GbsNode::Type::Sprite;
        if (size < 132) return fail(err, "sprite chunk too small");
        n.texId = rd32(d + pos + 112);
        const uint32_t mirror = rd32(d + pos + 124);
        n.flipX = (mirror & 1) != 0;
        n.flipY = (mirror & 2) != 0;
        n.colour = rd32(d + pos + 116);
        n.skew = rdf(d + pos + 120);
    } else if (tag == kTagLabel) {
        n.type = GbsNode::Type::Label;
        if (size < 164) return fail(err, "label chunk too small");
        n.fontId = rd32(d + pos + 112);
        n.scaleX = rdf(d + pos + 120);
        n.scaleY = rdf(d + pos + 124);
        n.hAlign = static_cast<int>(rd32(d + pos + 136));
        n.vCentre = rd32(d + pos + 140) != 0;
        n.vAlign = static_cast<int>(rd32(d + pos + 140));
        n.colour = rd32(d + pos + 116);
        n.skew = rdf(d + pos + 128);
        n.dropShadow = static_cast<int32_t>(rd32(d + pos + 132));
        n.leading = rdf(d + pos + 144);
        n.tracking = static_cast<int32_t>(rd32(d + pos + 148));
        n.textFit = rd32(d + pos + 152);
        if (n.hAlign < 0 || n.hAlign > 2) n.hAlign = 0;
    } else if (tag == kTagGroup) {
        n.type = GbsNode::Type::Group;
        if (size < 116) return fail(err, "group chunk too small");
        const uint32_t count = rd32(d + pos + 112);
        size_t p = pos + 116;
        if (count > (size - 116) / (kNodeHeader + 40)) return fail(err, "group child count impossible");
        n.kids.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (!parseNode(d, p, pos + size, depth + 1, n.kids[i], err)) return false;
            p += rd32(d + p + 4);
        }
        if (p != pos + size) return fail(err, "group children do not tile the group");
    }
    return true;
}

bool tagIs(const uint8_t* p, const char* t) { return std::memcmp(p, t, 4) == 0; }

// One MRFK key: [MRFK][size][tick][modeA][modeB][8 floats][kind tag][block size][payload words]
bool parseKey(const uint8_t* d, size_t pos, size_t end, GbsKey& k, std::string* err) {
    if (pos + 8 + 44 + 8 > end || !tagIs(d + pos, "MRFK")) return fail(err, "key header missing");
    const size_t size = rd32(d + pos + 4);
    if (size < 8 + 44 + 8 || pos + size > end) return fail(err, "key size out of range");
    k.t = static_cast<int>(rd32(d + pos + 8));
    k.modeA = static_cast<int>(rd32(d + pos + 12));
    k.modeB = static_cast<int>(rd32(d + pos + 16));
    const uint8_t* tl = d + pos + 8 + 44;  // the property block
    const size_t blk = rd32(tl + 4);
    if (blk != size - 8 - 44 || blk < 8) return fail(err, "key property block does not fill the key");
    const size_t words = (blk - 8) / 4;
    const uint8_t* pl = tl + 8;
    if (tagIs(tl, "TLPG")) k.kind = 'G';
    else if (tagIs(tl, "TLPS")) k.kind = 'S';
    else if (tagIs(tl, "TLBL")) k.kind = 'L';
    else if (tagIs(tl, "TLRA")) k.kind = 'R';
    else return fail(err, "unknown key block " + cstr(tl, 4));
    const size_t need = k.kind == 'G' ? 7 : k.kind == 'S' ? 8 : 12;  // a few TLPS keys stop after the colour word
    if (words < need) return fail(err, "key payload too short");
    k.rawCount = static_cast<int>(std::min<size_t>(words, 12));
    for (int i = 0; i < k.rawCount; ++i) k.raw[i] = rd32(pl + 4 * static_cast<size_t>(i));
    k.pivotX = rdf(pl);
    k.pivotY = rdf(pl + 4);
    k.x = rdf(pl + 8);
    k.y = rdf(pl + 12);
    k.w = rdf(pl + 16);
    k.h = rdf(pl + 20);
    k.rot = rdf(pl + 24);
    if (k.kind != 'G') k.argb = k.raw[7];
    if (k.kind == 'L') {
        k.scaleX = rdf(pl + 32);
        k.scaleY = rdf(pl + 36);
        k.skew = rdf(pl + 40);
        k.dropShadow = static_cast<int32_t>(k.raw[11]);
    } else if (k.kind == 'S' && words >= 9) {
        k.skew = rdf(pl + 32);
    }
    return true;
}

// The LAGG chunk of one view.
bool parseAnims(const uint8_t* d, size_t pos, size_t end, std::vector<GbsAnim>& out, std::string* err) {
    if (pos + 12 > end || !tagIs(d + pos, "LAGG")) return fail(err, "LAGG header missing");
    const size_t total = rd32(d + pos + 4);
    if (total < 12 || pos + total > end) return fail(err, "LAGG size out of range");
    const size_t count = rd32(d + pos + 8);
    const size_t chunkEnd = pos + total;
    size_t p = pos + 12;
    out.clear();
    out.reserve(std::min<size_t>(count, 4096));
    for (size_t i = 0; i < count; ++i) {
        if (p + 96 > chunkEnd || !tagIs(d + p, "NAGG")) return fail(err, "NAGG header missing");
        const size_t sz = rd32(d + p + 4);
        if (sz < 96 || p + sz > chunkEnd) return fail(err, "NAGG size out of range");
        GbsAnim a;
        a.name = cstr(d + p + 8, 64);
        a.length = std::max<int>(1, static_cast<int>(rd32(d + p + 72)));
        const uint32_t ls = rd32(d + p + 76), le = rd32(d + p + 80);
        a.loopStart = ls == 0xFFFFFFFFu ? -1 : static_cast<int>(ls);
        a.loopEnd = le == 0xFFFFFFFFu ? -1 : static_cast<int>(le);
        const size_t channels = rd32(d + p + 84), cues = rd32(d + p + 88);
        const size_t chanBytes = rd32(d + p + 92);
        size_t q = p + 96;
        if (chanBytes > sz - 96) return fail(err, "NAGG channel bytes out of range: " + a.name);
        const size_t chanEnd = q + chanBytes;
        a.channels.reserve(channels);
        for (size_t c = 0; c < channels; ++c) {
            if (q + 16 > chanEnd || !tagIs(d + q, "CAGG")) return fail(err, "CAGG header missing in " + a.name);
            const size_t cs = rd32(d + q + 4);
            if (cs < 16 || q + cs > chanEnd) return fail(err, "CAGG size out of range in " + a.name);
            GbsChannel ch;
            ch.nodeId = rd32(d + q + 8);
            const size_t keys = rd32(d + q + 12);
            if (keys > (cs - 16) / 60) return fail(err, "CAGG key count impossible in " + a.name);
            size_t r = q + 16;
            ch.keys.resize(keys);
            for (size_t k = 0; k < keys; ++k) {
                if (!parseKey(d, r, q + cs, ch.keys[k], err)) return false;
                r += rd32(d + r + 4);
            }
            if (r != q + cs) return fail(err, "CAGG keys do not fill the channel in " + a.name);
            std::stable_sort(ch.keys.begin(), ch.keys.end(), [](const GbsKey& x, const GbsKey& y) { return x.t < y.t; });
            a.channels.push_back(std::move(ch));
            q += cs;
        }
        if (q != chanEnd) return fail(err, "NAGG channels do not add up in " + a.name);
        // events: `cues` blocks [RFCM][size][name char[64]][tick][count] followed by `count` sub-blocks
        //   "SNDA" [16][sound id][volume 0..100]   play a sound of the scene's table
        //   "MSGA" [12][message id]                a message for the game code (kept as a cue with volume < 0)
        for (size_t c = 0; c < cues; ++c) {
            if (q + 80 > p + sz || !tagIs(d + q, "RFCM")) return fail(err, "RFCM event malformed in " + a.name);
            const size_t es = rd32(d + q + 4);
            if (es < 80 || q + es > p + sz) return fail(err, "RFCM event size out of range in " + a.name);
            const int tick = static_cast<int>(rd32(d + q + 8 + 64));
            const size_t subs = rd32(d + q + 8 + 68);
            size_t r = q + 80;
            for (size_t k = 0; k < subs; ++k) {
                if (r + 12 > q + es) return fail(err, "RFCM sub-block truncated in " + a.name);
                const size_t ss = rd32(d + r + 4);
                if (ss < 12 || r + ss > q + es) return fail(err, "RFCM sub-block size out of range in " + a.name);
                if (tagIs(d + r, "ADNS") && ss >= 16) {  // the file spells it "ADNS"
                    GbsSoundCue sc;
                    sc.t = tick;
                    sc.soundId = rd32(d + r + 8);
                    sc.volume = rdf(d + r + 12) / 100.0f;
                    a.sounds.push_back(sc);
                } else if (tagIs(d + r, "AGSM")) {
                    GbsSoundCue sc;
                    sc.t = tick;
                    sc.soundId = rd32(d + r + 8);
                    sc.volume = -1.0f;
                    a.messages.push_back(sc);
                }
                r += ss;
            }
            if (r != q + es) return fail(err, "RFCM sub-blocks do not fill the event in " + a.name);
            q += es;
        }
        if (q != p + sz) return fail(err, "NAGG has bytes left over: " + a.name);
        out.push_back(std::move(a));
        p += sz;
    }
    if (p != chunkEnd) return fail(err, "LAGG animations do not fill the chunk");
    return true;
}

}  // namespace

const GbsGlyph* GbsFont::find(uint32_t cp) const {
    auto it = std::lower_bound(glyphs.begin(), glyphs.end(), cp, [](const GbsGlyph& g, uint32_t v) { return g.cp < v; });
    return it != glyphs.end() && it->cp == cp ? &*it : nullptr;
}

static const GbsNode* findIn(const GbsNode& n, const std::string& name) {
    if (n.name == name) return &n;
    for (const GbsNode& k : n.kids)
        if (const GbsNode* r = findIn(k, name)) return r;
    return nullptr;
}

const GbsNode* GbsSceneView::find(const std::string& nodeName) const {
    for (const GbsNode& n : nodes)
        if (const GbsNode* r = findIn(n, nodeName)) return r;
    return nullptr;
}

const GbsAnim* GbsSceneView::anim(const std::string& animName) const {
    for (const GbsAnim& a : anims)
        if (a.name == animName) return &a;
    return nullptr;
}

const GbsFont* GbsScene::fontById(uint32_t id) const {
    for (const GbsFont& f : fonts)
        if (f.id == id) return &f;
    return nullptr;
}
const GbsFont* GbsScene::fontByName(const std::string& name) const {
    for (const GbsFont& f : fonts)
        if (f.name == name) return &f;
    return nullptr;
}
const GbsSceneView* GbsScene::view(const std::string& name) const {
    for (const GbsSceneView& v : views)
        if (v.name == name) return &v;
    return nullptr;
}
const std::string& GbsScene::textureName(uint32_t id) const {
    static const std::string none;
    if (id == kGbsNoTexture) return none;
    for (const GbsTexture& t : textures)
        if (t.id == id) return t.name;
    return none;
}

const GbsTexture* GbsScene::textureById(uint32_t id) const {
    if (id == kGbsNoTexture) return nullptr;
    for (const GbsTexture& t : textures)
        if (t.id == id) return &t;
    return nullptr;
}

const GbsTexture* GbsScene::textureByName(const std::string& name) const {
    auto norm = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* ext : {".png", ".dds"})
            if (s.size() > 4 && s.compare(s.size() - 4, 4, ext) == 0) {
                s.resize(s.size() - 4);
                break;
            }
        return s;
    };
    const std::string k = norm(name);
    for (const GbsTexture& t : textures)
        if (norm(t.name) == k) return &t;
    return nullptr;
}

const std::string& GbsScene::soundName(uint32_t id) const {
    static const std::string none;
    for (const GbsSound& t : sounds)
        if (t.id == id) return t.name;
    return none;
}

size_t countNodes(const GbsNode& n) {
    size_t c = 1;
    for (const GbsNode& k : n.kids) c += countNodes(k);
    return c;
}

bool parseGbsScene(const uint8_t* d, size_t size, GbsScene& out, std::string* err) {
    out = GbsScene{};
    Gbs g;
    if (!parseGbs(d, size, g, err)) return false;
    out.sceneId = g.sceneId;
    const size_t base = 0x38;
    const size_t payloadEnd = base + g.payloadSize;

    // fonts
    size_t p = base;
    const size_t fontsEnd = base + g.off0;
    if (fontsEnd > payloadEnd) return fail(err, "font section beyond payload");
    for (uint32_t i = 0; i < g.count0; ++i) {
        if (p + kNodeHeader > fontsEnd || rd32(d + p) != kTagFont) return fail(err, "expected TNFG font chunk");
        const size_t sz = rd32(d + p + 4);
        if (sz < kNodeHeader || p + sz > fontsEnd) return fail(err, "font chunk size out of range");
        GbsFont f;
        if (!parseFont(d, p, sz, f)) return fail(err, "font chunk malformed: " + cstr(d + p + 12, 64));
        out.fonts.push_back(std::move(f));
        p += sz;
    }
    if (p != fontsEnd) return fail(err, "font chunks do not tile the font section");

    // textures
    const size_t texBegin = fontsEnd;
    if (texBegin + static_cast<size_t>(g.textureCount) * kTextureRow > base + g.off1 || base + g.off1 > payloadEnd)
        return fail(err, "texture table does not fit its section");
    for (uint32_t i = 0; i < g.textureCount; ++i) {
        const uint8_t* e = d + texBegin + static_cast<size_t>(i) * kTextureRow;
        GbsTexture t;
        t.id = rd32(e);
        t.name = cstr(e + 4, 260);
        t.atlasId = rd32(e + 264);
        auto f32 = [&](size_t off) {
            const uint32_t w = rd32(e + off);
            float f;
            std::memcpy(&f, &w, 4);
            return f;
        };
        t.u0 = f32(268);
        t.v0 = f32(272);
        t.u1 = f32(276);
        t.v1 = f32(280);
        if (!(t.u0 >= 0 && t.v0 >= 0 && t.u1 <= 1.0001f && t.v1 <= 1.0001f)) t.atlasId = 0;   // not a rectangle: a plain row
        out.textures.push_back(std::move(t));
    }

    // sounds: count2 rows of 264 bytes between the texture table and the views
    if (g.count2 > 0 && base + g.off2 <= payloadEnd && g.off2 >= g.off1) {
        const size_t bytes = g.off2 - g.off1;
        if (bytes % g.count2 == 0 && bytes / g.count2 >= 8) {
            const size_t row = bytes / g.count2;
            for (uint32_t i = 0; i < g.count2; ++i) {
                const uint8_t* e = d + base + g.off1 + static_cast<size_t>(i) * row;
                GbsSound sn;
                sn.id = rd32(e);
                sn.name = cstr(e + 4, std::min<size_t>(row - 4, 260));
                out.sounds.push_back(std::move(sn));
            }
        }
    }

    // views
    for (const ::zengine::GbsView& hv : g.views) {
        GbsSceneView v;
        v.name = hv.name;
        const size_t vo = hv.offset, vend = hv.offset + hv.size;
        if (vo + 140 > vend) return fail(err, "view header truncated: " + v.name);
        v.width = rdf(d + vo + 92);
        v.height = rdf(d + vo + 96);
        const size_t tc = rd32(d + vo + 112), ac = rd32(d + vo + 116), nc = rd32(d + vo + 120);
        if (tc > 8192 || ac > 8192 || nc > 65536 || vo + 140 + 4 * (tc + ac) > vend)
            return fail(err, "view id lists out of range: " + v.name);
        for (size_t i = 0; i < tc; ++i) v.textureIds.push_back(rd32(d + vo + 140 + 4 * i));
        for (size_t i = 0; i < ac; ++i) v.animationIds.push_back(rd32(d + vo + 140 + 4 * (tc + i)));
        size_t q = vo + 140 + 4 * (tc + ac);
        v.nodes.resize(nc);
        for (size_t i = 0; i < nc; ++i) {
            std::string e;
            if (!parseNode(d, q, vend, 0, v.nodes[i], &e)) return fail(err, "view '" + v.name + "': " + e);
            q += rd32(d + q + 4);
        }
        if (q + 8 <= vend && rd32(d + q) == kTagAnim) {
            v.animOffset = static_cast<uint32_t>(q);
            v.animSize = rd32(d + q + 4);
            std::string e;
            if (!parseAnims(d, q, vend, v.anims, &e)) return fail(err, "view '" + v.name + "': " + e);
            q += v.animSize;
        }
        if (q != vend) return fail(err, "view '" + v.name + "': nodes and animation chunk do not fill the view");
        out.views.push_back(std::move(v));
    }
    return true;
}

}  // namespace zengine
