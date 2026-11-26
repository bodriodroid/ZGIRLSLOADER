// Tests for foit_write: a character built in code writes and parses back to the same thing, and (when ZDROID_TEST_CHARS names a folder
// with the game's own <char>.foit files) every one of them is written back byte for byte.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "foit.h"
#include "foit_write.h"

using namespace zengine;

static int g_fail = 0, g_n = 0;
#define CHECK(c)                                                \
    do {                                                        \
        ++g_n;                                                  \
        if (!(c)) {                                             \
            ++g_fail;                                           \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
        }                                                       \
    } while (0)

static bool readAll(const std::string& p, std::vector<uint8_t>& b) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    b.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static foit::Frame makeFrame(int px, int py) {
    foit::Frame f;
    f.pivotX = px;
    f.pivotY = py;
    f.src = 5;
    f.entries.push_back({0xAABBCCDDu, 5u, static_cast<uint32_t>(-3)});
    f.slotMask = 1 | 4;
    f.img[0].cells = 4; f.img[0].w = 2; f.img[0].h = 2; f.img[0].n = 3; f.img[0].flags = 0x30;
    f.img[0].mask = {0x0B}; f.img[0].hasData = true; f.img[0].external = true; f.img[0].off = 77; f.img[0].size[0] = 5; f.img[0].size[1] = 6; f.img[0].size[2] = 7;
    f.img[2].cells = 1; f.img[2].w = 1; f.img[2].h = 1; f.img[2].n = 1; f.img[2].flags = 0x14;   // inline planes and the two extra words
    f.img[2].extra[0] = 11; f.img[2].extra[1] = 12; f.img[2].mask = {1}; f.img[2].hasData = true;
    for (int k = 0; k < 3; ++k) f.img[2].inl[k] = std::vector<uint8_t>(static_cast<size_t>(k + 2), static_cast<uint8_t>(0x40 + k));
    for (int i = 0; i < 10; ++i) f.xy[i] = i * 3 - 5;
    f.hit.push_back({-10, 4, 7, 30});
    f.hurt.push_back({-90, 0, -20, 20});
    f.hurt.push_back({-60, -30, -30, 30});
    return f;
}

static foit::ChainNode node(uint8_t op, bool boolean, uint8_t da, uint32_t ra, uint8_t db, uint32_t rb, uint8_t dc, uint32_t rc) {
    foit::ChainNode n;
    n.op = op; n.flags = boolean ? 1 : 0;
    n.a.desc = da; n.a.raw = ra; n.b.desc = db; n.b.raw = rb; n.c.desc = dc; n.c.raw = rc;
    return n;
}

static foit::Character makeChar() {
    foit::Character c;
    c.version = 9;
    c.headerFlags = 0x02;
    c.body.name = "Test";
    c.body.v374 = 99;
    c.body.list8.emplace_back(0x01020304u, 9);
    for (size_t i = 0; i < foit::kSlotCount; ++i) c.body.slotDefaults[i] = static_cast<uint32_t>(i * 0x01010101u);
    c.body.slotDefaultsValid = true;
    c.body.hasChain[1] = true;
    c.body.chains[1] = {node(1, false, 1, 3, 0, 0, 1, 4), node(0x12, true, 1, 5, 0, 0x3F800000u, 0, 0x3F800000u)};
    c.body.chainFlags = 2;
    c.frames = {makeFrame(0, 0), makeFrame(10, 20), makeFrame(-4, 8)};
    c.body.box[0] = -40; c.body.box[1] = 0; c.body.box[2] = -20; c.body.box[3] = 20;
    c.body.refBe = 1; c.body.refBf = 2; c.body.hasRefC0 = true; c.body.refC0 = 3;
    c.body.flagsDc = 4; c.body.x373 = 5;

    foit::State a;   // two nodes, a long one with a hit record, a linked second record, a header script
    a.name = "Stand";
    a.next = 0;
    a.b14 = 3;
    a.f = 2;
    a.hashes = {0x11111111u, 0x22222222u};
    foit::Node n1;
    n1.refs[1] = 1;
    foit::NodeProps p;
    p.b1 = 1; p.dur = 30; p.f2 = 1; p.hasHit = true;
    p.hit.f8 = 3; p.hit.f4 = 0x40000; p.hit.fa = 0x13; p.hit.hasChain[0] = true; p.hit.chains[0] = {node(1, false, 0, 0x3F800000u, 0, 0, 1, 7)};
    p.hit.f0a = 0x1234;
    p.hasCmd = true;
    foit::Command cmd;
    cmd.a = 1; cmd.b = 2; cmd.hasDefault = true; cmd.flags = 2;
    foit::Step st;
    st.b0 = 1; st.w = 6; st.flags = 2; st.chain = {node(0x12, true, 1, 2, 0, 0, 0, 0x3F800000u)};
    cmd.def = st;
    cmd.steps = {st};
    p.cmd = cmd;
    foit::NodeProps p2;
    p2.b1 = 0; p2.f2 = 6;
    p2.sub = {foit::SubItem{7, 8, 9, 0, {}}, foit::SubItem{10, 11, 12, 2, {node(1, false, 0, 0, 0, 0, 1, 2)}}};
    p2.u32s = {1, 2, 3};
    n1.props = {p, p2};
    foit::Node n2;
    n2.refs[0] = 2; n2.refs[2] = 3;
    foit::NodeProps q;
    q.b1 = 1; q.b3 = 0x40; q.x1a = 9; q.dur = 12;
    n2.props = {q};
    a.hdr.nodes = {n1, n2};
    a.hdr.hasChain = true;
    a.hdr.chain = {node(1, false, 0, 0x41200000u, 0, 0, 1, 12)};
    foit::Transition tr;
    tr.target = 1;
    tr.cmd = cmd;
    a.transitions = {tr};
    foit::State b;
    b.name = "Walk";
    b.next = 0;
    b.f = 4; b.hasV18 = true; b.v18 = 77; b.v1c = 0;
    foit::Node n3;
    n3.refs[1] = 3;
    n3.props.resize(1);
    b.hdr.nodes = {n3};
    c.body.states = {a, b};

    foit::Object kid;   // a projectile
    kid.name = "kid";
    kid.v374 = 5;
    for (size_t i = 0; i < foit::kSlotCount; ++i) kid.slotDefaults[i] = static_cast<uint32_t>(i);
    kid.slotDefaultsValid = true;
    kid.childFlags = 1;
    foit::State ks;
    ks.name = "Fly";
    foit::Node kn;
    kn.refs[1] = 2;
    kn.props.resize(1);
    ks.hdr.nodes = {kn};
    kid.states = {ks};
    c.body.children = {kid, kid};
    return c;
}

int main() {
    // ---- a character made in code: write, parse, write again
    {
        const foit::Character c = makeChar();
        std::vector<uint8_t> a, b;
        std::string err;
        CHECK(foit::write(c, a, &err));
        if (!err.empty()) std::printf("  %s\n", err.c_str());
        foit::Character p;
        CHECK(foit::parse(a.data(), a.size(), p, &err));
        if (!err.empty()) std::printf("  parse: %s\n", err.c_str());
        CHECK(p.frames.size() == 3 && p.body.states.size() == 2 && p.body.children.size() == 2 && p.body.name == "Test");
        CHECK(p.body.states[0].name == "Stand" && p.body.states[0].hdr.nodes.size() == 2 && p.body.states[0].hdr.nodes[0].props.size() == 2);
        CHECK(p.body.states[0].hdr.nodes[0].props[0].hit.f4 == 0x40000 && p.body.states[0].hdr.nodes[0].props[0].hit.f0a == 0x1234);
        CHECK(p.body.states[0].hdr.nodes[0].props[1].sub.size() == 2 && p.body.states[0].hdr.nodes[0].props[1].u32s.size() == 3);
        CHECK(p.frames[1].pivotX == 10 && p.frames[1].xy[9] == 22 && p.frames[0].hurt.size() == 2 && p.frames[0].img[2].extra[1] == 12);
        CHECK(p.frames[0].img[0].off == 77 && p.frames[0].entries[0][0] == 0xAABBCCDDu && p.body.children[1].states[0].name == "Fly");
        CHECK(foit::write(p, b, &err) && a == b);
        CHECK(p.body.slotDefaults[100] == c.body.slotDefaults[100] && p.body.box[0] == -40);
        // refusals
        foit::Character bad = c;
        bad.body.states[0].hdr.nodes[0].refs[1] = 99;
        CHECK(!foit::write(bad, b, &err) && !err.empty() && b.empty());
        bad = c;
        bad.body.hasChain[1] = true;
        bad.body.chains[1].clear();
        CHECK(!foit::write(bad, b, &err));
        bad = c;
        bad.frames.clear();
        CHECK(!foit::write(bad, b, &err));
    }

    // ---- the game's own files, byte for byte
    if (const char* dir = std::getenv("ZDROID_TEST_CHARS")) {
        int files = 0;
        for (const char* n : {"annie", "beowulf", "bigband", "blackdahlia", "bossmarie", "cerebella", "double", "eliza", "filia", "fukua", "hitspark",
                              "marie", "msfortune", "painwheel", "parasoul", "peacock", "robofortune", "squigly", "umbrella", "valentine"}) {
            std::vector<uint8_t> src, out;
            if (!readAll(std::string(dir) + "/" + n + ".foit", src)) continue;
            ++files;
            foit::Character c;
            std::string err;
            const bool parsed = foit::parse(src.data(), src.size(), c, &err);
            CHECK(parsed);
            if (!parsed) continue;
            const bool wrote = foit::write(c, out, &err);
            CHECK(wrote);
            size_t at = 0;
            while (at < src.size() && at < out.size() && src[at] == out[at]) ++at;
            const bool same = wrote && src == out;
            CHECK(same);
            std::printf("  %-12s %s (%zu bytes%s)\n", n, same ? "identical" : "DIFFERENT", src.size(),
                        same ? "" : (", first difference at " + std::to_string(at) + (wrote ? ", written " + std::to_string(out.size()) : ", " + err)).c_str());
        }
        if (!files) std::printf("  (ZDROID_TEST_CHARS set but no .foit found there: real-data block skipped)\n");
    }

    std::printf("foit_write tests: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
