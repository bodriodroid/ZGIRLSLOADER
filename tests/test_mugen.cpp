// Tests for the MUGEN -> ZGirls Loader sprite conversion: rle, .art planes, palette textures, .sff / .act / .air / .def readers.
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "art.h"
#include "art_write.h"
#include "foit_write.h"
#include "mugen.h"
#include "mugen_foit.h"

using namespace zengine;

static int g_fail = 0, g_n = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++g_n;                                                         \
        if (!(c)) {                                                    \
            ++g_fail;                                                  \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c);        \
        }                                                              \
    } while (0)

static void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}
static void put16(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    v[at] = static_cast<uint8_t>(x);
    v[at + 1] = static_cast<uint8_t>(x >> 8);
}

int main() {
    // ---- rle: every run length class, literals longer than 128, random data
    {
        std::vector<std::vector<uint8_t>> cases;
        cases.push_back({});
        cases.push_back({7});
        cases.push_back(std::vector<uint8_t>(2, 5));
        for (size_t n : {3u, 124u, 125u, 126u, 255u, 256u, 300u, 65535u, 65536u, 70000u}) cases.push_back(std::vector<uint8_t>(n, 9));
        std::mt19937 rng(7);
        std::vector<uint8_t> noise(1000), mixed;
        for (uint8_t& b : noise) b = static_cast<uint8_t>(rng());
        cases.push_back(noise);
        for (int i = 0; i < 40; ++i) {
            mixed.insert(mixed.end(), static_cast<size_t>(rng() % 300), static_cast<uint8_t>(rng() % 3));
            mixed.push_back(static_cast<uint8_t>(rng()));
        }
        cases.push_back(mixed);
        for (const auto& c : cases) {
            const std::vector<uint8_t> e = rleEncode(c.data(), c.size());
            std::vector<uint8_t> back;
            CHECK(rleDecode(e.data(), e.size(), 0, e.size(), c.size() + 1, back) && back == c);
        }
    }

    // ---- an image through encodeArtImage and the engine's own decoder: sizes that are not a multiple of 16, empty cells, empty images
    {
        std::mt19937 rng(11);
        for (int t = 0; t < 12; ++t) {
            const int w = 20 + static_cast<int>(rng() % 70), h = 20 + static_cast<int>(rng() % 70);   // big enough that something is drawn
            std::vector<uint8_t> band(static_cast<size_t>(w) * h, 0), grad(band.size(), 0);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (x > w / 4 && y > h / 3 && ((x * 7 + y * 3) % 5) != 0) {   // leaves empty cells at the left / top
                        band[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(1 + (x + y) % 200);
                        grad[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>((x % 4) << 1);
                    }
            std::vector<uint8_t> art(5, 0xEE);   // an offset other than 0
            foit::Image im;
            CHECK(encodeArtImage(w, h, band, grad, art, im));
            CHECK(im.w == (w + 15) / 16 && im.h == (h + 15) / 16 && im.cells == im.w * im.h && im.off == 5 && im.external && im.hasData);
            ArtPlanes pl;
            CHECK(foit::decodeImage(im, art.data(), art.size(), pl));
            bool same = pl.width == im.w * 16 && pl.height == im.h * 16;
            for (int y = 0; y < pl.height && same; ++y)
                for (int x = 0; x < pl.width; ++x) {
                    const bool in = x < w && y < h;
                    const uint8_t wb = in ? band[static_cast<size_t>(y) * w + x] : 0, wg = in ? grad[static_cast<size_t>(y) * w + x] : 0;
                    if (pl.band[static_cast<size_t>(y) * pl.width + x] != wb || pl.grad[static_cast<size_t>(y) * pl.width + x] != wg) {
                        same = false;
                        break;
                    }
                }
            CHECK(same);
        }
        std::vector<uint8_t> art, band(40 * 20, 0), grad(40 * 20, 0);
        foit::Image im;
        CHECK(encodeArtImage(40, 20, band, grad, art, im) && im.n == 0 && art.empty() && im.placeholder());   // nothing drawn, nothing stored
        CHECK(!encodeArtImage(0, 5, band, grad, art, im) && !encodeArtImage(40, 20, band, std::vector<uint8_t>(3), art, im));
    }

    // ---- the palette texture: what the engine reads back is what was written
    {
        std::vector<uint8_t> rgba(256 * 16 * 4);
        for (int row = 0; row < 256; ++row)
            for (int t = 0; t < 16; ++t) {
                uint8_t* p = &rgba[(static_cast<size_t>(row) * 16 + t) * 4];
                p[0] = static_cast<uint8_t>(row);
                p[1] = static_cast<uint8_t>(255 - row);
                p[2] = static_cast<uint8_t>(t * 16);
                p[3] = 255;
            }
        const std::vector<uint8_t> dds = makePaletteDds(16, 256, rgba);
        Palette pal;
        CHECK(readDds(dds.data(), dds.size(), pal) && pal.tones == 16 && pal.bands == 256);
        CHECK(pal.at(77, 5)[0] == 77 && pal.at(77, 5)[1] == 178 && pal.at(77, 5)[2] == 80 && pal.litAt(77, 5));
        const std::vector<uint8_t> tb = buildRgbaTables(pal);
        CHECK(tb[(static_cast<size_t>(9) * 4 + 0) * 256 + 0] == 9 && tb[(static_cast<size_t>(9) * 4 + 3) * 256 + 0] == 255);   // band 9, grad 0
        CHECK(tb[(static_cast<size_t>(0) * 4 + 3) * 256 + 0] == 0);                                                             // band 0 / grad 0 = nothing
    }

    // ---- .sff v1 (a made-up file of two sprites and a link), .act, .air, .def
    {
        // PCX 3x2: indices 0 1 2 / 3 3 3 (bytes per line 4); the 3 3 3 line uses a run
        std::vector<uint8_t> pcx(128, 0);
        pcx[0] = 10; pcx[2] = 1; pcx[3] = 8; pcx[65] = 1;
        put16(pcx, 8, 2); put16(pcx, 10, 1); put16(pcx, 66, 4);
        const uint8_t body[] = {0, 1, 2, 0, 0xC3, 3, 0};   // line 1: 0 1 2 pad; line 2: 3 3 3 + pad (run of 3 then a literal 0)
        pcx.insert(pcx.end(), body, body + sizeof body);
        std::vector<uint8_t> sff(512, 0);
        std::memcpy(sff.data(), "ElecbyteSpr", 11);
        sff[15] = 1;
        put32(sff, 16, 1); put32(sff, 20, 2); put32(sff, 24, 512); put32(sff, 28, 32);
        const size_t s0 = sff.size();
        sff.resize(s0 + 32, 0);
        put32(sff, s0 + 0, static_cast<uint32_t>(s0 + 32 + pcx.size()));
        put32(sff, s0 + 4, static_cast<uint32_t>(pcx.size()));
        put16(sff, s0 + 8, 1); put16(sff, s0 + 10, 2);   // axis
        put16(sff, s0 + 12, 7); put16(sff, s0 + 14, 3);   // group, image
        sff.insert(sff.end(), pcx.begin(), pcx.end());
        const size_t s1 = sff.size();
        sff.resize(s1 + 32, 0);
        put32(sff, s1 + 0, 0); put32(sff, s1 + 4, 0);      // last, length 0 = linked
        put16(sff, s1 + 8, 5); put16(sff, s1 + 10, 6); put16(sff, s1 + 12, 8); put16(sff, s1 + 14, 0); put16(sff, s1 + 16, 0);
        std::vector<mugen::Sprite> v;
        std::string err;
        CHECK(mugen::readSff1(sff.data(), sff.size(), v, &err));
        CHECK(v.size() == 2 && v[0].group == 7 && v[0].image == 3 && v[0].axisX == 1 && v[0].axisY == 2 && v[0].w == 3 && v[0].h == 2);
        CHECK(v[0].px == std::vector<uint8_t>({0, 1, 2, 3, 3, 3}));
        CHECK(v[1].linkedTo == 0 && v[1].px == v[0].px && v[1].axisX == 5 && v[1].group == 8);
        std::vector<uint8_t> bad = sff;
        bad[15] = 2;
        CHECK(!mugen::readSff1(bad.data(), bad.size(), v, &err) && !err.empty());
        bad = sff;
        put32(bad, 512 + 4, 99999);
        CHECK(!mugen::readSff1(bad.data(), bad.size(), v, &err));
        CHECK(!mugen::readSff1(sff.data(), 100, v, &err));

        std::vector<uint8_t> act(768);
        for (int i = 0; i < 768; ++i) act[static_cast<size_t>(i)] = static_cast<uint8_t>(i / 3);   // entry k of the file holds (k,k,k)
        std::array<uint8_t, 768> rgb{};
        CHECK(mugen::readAct(act.data(), act.size(), rgb) && rgb[0] == 255 && rgb[3 * 255] == 0 && rgb[3 * 10 + 2] == 245);
        CHECK(!mugen::readAct(act.data(), 100, rgb));

        const std::string air =
            "; comment\n[Begin Action 0]\nClsn2Default: 1\n Clsn2[0] = -10,-50, 10, 0\n0,0, 1,2, 7\nClsn1: 1\n Clsn1[0] = 5,-40, 30,-20\n"
            "LoopStart\n0,1, 0,0, 3, H\nClsn2: 0\n0,2, 0,0, -1\n[Begin Action 20]\n1,0, 0,0, 4, V\n";
        const std::vector<mugen::AirAction> a = mugen::parseAir(air);
        CHECK(a.size() == 2 && a[0].id == 0 && a[1].id == 20 && a[0].frames.size() == 3 && a[0].loopStart == 1);
        CHECK(a[0].frames[0].x == 1 && a[0].frames[0].y == 2 && a[0].frames[0].time == 7 && a[0].frames[0].clsn2.size() == 1 && a[0].frames[0].clsn1.empty());
        CHECK(a[0].frames[1].clsn1.size() == 1 && a[0].frames[1].clsn1[0].x2 == 30 && a[0].frames[1].flipH && a[0].frames[1].clsn2.size() == 1);   // default Clsn2 stays
        CHECK(a[0].frames[2].clsn1.empty() && a[0].frames[2].clsn2.empty() && a[0].frames[2].time == -1);                                          // Clsn2: 0 clears it
        CHECK(a[1].frames[0].flipV && a[1].frames[0].clsn2.empty());

        const mugen::Def d = mugen::parseDef("[Info]\nname = \"Test - One\"\ndisplayname = \"One\"\n[Files]\nsprite = one.sff ; c\npal1 = pal/one1.act\n");
        CHECK(d.name == "Test - One" && d.displayName == "One" && d.files.at("sprite") == "one.sff" && d.files.at("pal1") == "pal/one1.act");
    }

    // ---- scaling, and a .foit built from two sprites and three actions
    {
        mugen::Sprite sp;
        sp.w = 2; sp.h = 1; sp.px = {1, 2}; sp.axisX = 1; sp.axisY = 1;
        mugen::scaleSprite(sp, 3);
        CHECK(sp.w == 6 && sp.h == 3 && sp.axisX == 3 && sp.axisY == 3 && sp.px.size() == 18 && sp.px[0] == 1 && sp.px[2] == 1 && sp.px[3] == 2 && sp.px[17] == 2);
        mugen::scaleSprite(sp, 1);
        CHECK(sp.w == 6);

        std::vector<mugen::Sprite> spr(2);
        spr[0].group = 0; spr[0].image = 0; spr[0].w = 20; spr[0].h = 18; spr[0].axisX = 5; spr[0].axisY = 18;
        spr[0].px.assign(20 * 18, 0);
        for (int y = 2; y < 18; ++y)
            for (int x = 3; x < 17; ++x) spr[0].px[static_cast<size_t>(y) * 20 + x] = static_cast<uint8_t>(1 + (x + y) % 9);
        spr[1].group = 0; spr[1].image = 1; spr[1].w = 4; spr[1].h = 4; spr[1].axisX = 0; spr[1].axisY = 0; spr[1].px.assign(16, 0);   // an empty picture
        std::vector<uint8_t> art;
        std::vector<foit::Image> imgs(2);
        CHECK(encodeArtImage(spr[0].w, spr[0].h, spr[0].px, std::vector<uint8_t>(spr[0].px.size(), 0), art, imgs[0]) && imgs[0].n > 0);
        CHECK(encodeArtImage(spr[1].w, spr[1].h, spr[1].px, std::vector<uint8_t>(16, 0), art, imgs[1]) && imgs[1].n == 0);
        std::vector<mugen::AirAction> acts = mugen::parseAir(
            "[Begin Action 0]\nClsn2: 1\n Clsn2[0] = -4,-18, 6, 0\n0,0, 0,0, 5\n0,0, 0,0, 7\n"
            "[Begin Action 20]\nClsn1: 1\n Clsn1[0] = 1,-10, 9,-6\n0,0, 2,-3, 4, H\nLoopStart\n0,1, 0,0, 3\n0,0, 0,0, 2\n"
            "[Begin Action 30]\n0,0, 0,0, 6\n0,0, 1,1, -1\n");
        mugen::FoitBuild fb;
        std::string err;
        CHECK(mugen::buildFoit("t", spr, imgs, art, acts, fb, &err));
        CHECK(fb.mirroredImages == 1 && fb.states == 4);   // Stand, Action_20 + its loop state, Action_30
        std::vector<uint8_t> bytes;
        CHECK(foit::write(fb.ch, bytes, &err));
        foit::Character c;
        CHECK(foit::parse(bytes.data(), bytes.size(), c, &err));
        const int st = c.body.findState("Stand"), w = c.body.findState("Action_20"), wl = c.body.findState("Action_20_Loop"), a30 = c.body.findState("Action_30");
        CHECK(st == 0 && w == 1 && wl == 2 && a30 == 3);
        if (c.body.states.size() < 4) {
            std::printf("mugen tests: %d checks, %d failed (stopped: the states are not what the next checks need)\n", g_n, g_fail + 1);
            return 1;
        }
        CHECK(c.body.states[0].next == 0 && c.body.states[0].hdr.nodes.size() == 2 && c.body.states[0].hdr.nodes[0].props[0].dur == 5 && c.body.states[0].hdr.nodes[1].props[0].dur == 7);
        CHECK(c.body.states[1].next == 2 && c.body.states[1].hdr.nodes.size() == 3 && c.body.states[2].next == 2 && c.body.states[2].hdr.nodes.size() == 2);   // the loop state holds the frames from LoopStart on
        CHECK(c.body.states[3].hdr.nodes[1].props[0].dur == 0xFFFF && c.body.states[3].next == 3);
        // frame 0 of Stand: picture at -axis, one hurt box; the empty-picture frame of Action_20 has no image
        const foit::Frame& f0 = c.frames[c.body.states[0].hdr.nodes[0].refs[1] - 1u];
        CHECK((f0.slotMask & 1) && f0.drawX(0) == -5 && f0.drawY(0) == -18 && f0.hurt.size() == 1 && f0.hurt[0].left == -4 && f0.hurt[0].right == 6 && f0.hurt[0].top == -18);
        // the flipped frame: x = -2 - (20 - 5), mirrored hit box, another image than the plain one
        const foit::Frame& f1 = c.frames[c.body.states[1].hdr.nodes[0].refs[1] - 1u];
        CHECK(f1.drawX(0) == -2 - 15 && f1.drawY(0) == -3 - 18 && f1.hit.size() == 1 && f1.hit[0].left == -9 && f1.hit[0].right == -1 && f1.img[0].off != f0.img[0].off);
        ArtPlanes plain, flip;
        CHECK(foit::decodeImage(f0.img[0], art.data(), art.size(), plain) && foit::decodeImage(f1.img[0], art.data(), art.size(), flip));
        bool mirror = true;
        for (int y = 0; y < 18; ++y)
            for (int x = 0; x < 20; ++x)
                if (plain.band[static_cast<size_t>(y) * plain.width + x] != flip.band[static_cast<size_t>(y) * flip.width + (19 - x)]) mirror = false;
        CHECK(mirror);
        const foit::Frame& fe = c.frames[c.body.states[1].hdr.nodes[1].refs[1] - 1u];
        CHECK(fe.slotMask == 0 && fe.src == 0);
        CHECK(c.body.box[2] == -4 && c.body.box[3] == 6 && c.body.box[0] == -18 && c.body.refBe == 0);
        // equal frames share one record: Stand twice + Action_30 frame 0 are the same picture and boxes (the boxes differ, so two records)
        CHECK(c.frames.size() <= 6);
        CHECK(!mugen::buildFoit("t", spr, imgs, art, std::vector<mugen::AirAction>(), fb, &err) && !err.empty());
    }

    std::printf("mugen tests: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
