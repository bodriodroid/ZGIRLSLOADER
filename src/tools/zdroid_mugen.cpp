// zdroid_mugen - turns the sprites of a MUGEN character into the sprite data of a ZGirls Loader character and checks the result.
//
//   zdroid_mugen <character folder with the .def> <out folder> [--scale N] [--preview]
//
//   --scale N    enlarge every sprite N times (whole numbers, nearest neighbour; MUGEN pixel art is tiny next to the game's characters)
//   --preview    also write preview/<state>/<k>.png (what the engine draws for every node of every state) + preview/index.json
//
// Writes   <out>/<name>.art              the sprite planes (art_write.h), every image of the .sff
//          <out>/<name>_<N>p.dds         one palette texture per .act (16 tones x 256 rows; row i = colour i, so a pixel of palette index i is
//                                        band i / tone 0 and the palette textures swap the colours exactly like the .act files did)
//          <out>/sprites/<group>_<image>.png   every sprite as the engine's own decoder draws it from those planes (palette 1;
//                                        sprites_p2/ ... for the other palettes)
//          <out>/manifest.json           sprites (axis, size, where in the .art) and animations (frames, timing, boxes) for the next steps
//          <out>/<name>.foit             frames, boxes and one animation state per .air action (mugen_foit.h)
// It decodes every image back through the engine's decoder, comparing each pixel with the .sff under every palette, and then parses the
// .foit it wrote and checks every .air frame in it: sprite pixels, position, timing and box counts.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "art.h"
#include "art_write.h"
#include "foit.h"
#include "foit_write.h"
#include "mugen.h"
#include "mugen_foit.h"

namespace fs = std::filesystem;
using namespace zengine;

static std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void writeAll(const fs::path& p, const std::vector<uint8_t>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}
static fs::path findCi(const fs::path& dir, const std::string& name) {   // case-insensitive file lookup (MUGEN files are written on Windows)
    std::string want = name;
    std::replace(want.begin(), want.end(), '\\', '/');
    fs::path cur = dir;
    std::stringstream ss(want);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        fs::path hit;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(cur, ec)) {
            std::string a = e.path().filename().string(), b = part;
            for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (a == b) {
                hit = e.path();
                break;
            }
        }
        if (hit.empty()) return fs::path();
        cur = hit;
    }
    return cur;
}
static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: zdroid_mugen <character folder> <out folder>\n");
        return 2;
    }
    const fs::path in = argv[1], out = argv[2];
    int scale = 1;
    bool preview = false;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::max(1, std::min(32, std::atoi(argv[++i])));
        else if (!std::strcmp(argv[i], "--preview")) preview = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    fs::path defPath;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(in, ec))
        if (e.path().extension() == ".def" || e.path().extension() == ".DEF") defPath = e.path();
    if (defPath.empty()) {
        std::fprintf(stderr, "no .def in %s\n", in.string().c_str());
        return 2;
    }
    const std::vector<uint8_t> defBytes = readAll(defPath);
    const mugen::Def def = mugen::parseDef(std::string(defBytes.begin(), defBytes.end()));
    std::string base = defPath.stem().string();
    for (char& c : base) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const fs::path sffPath = findCi(in, def.files.count("sprite") ? def.files.at("sprite") : base + ".sff");
    if (sffPath.empty()) {
        std::fprintf(stderr, "sprite file not found\n");
        return 2;
    }
    const std::vector<uint8_t> sffBytes = readAll(sffPath);
    std::vector<mugen::Sprite> sprites;
    std::string err;
    if (!mugen::readSff1(sffBytes.data(), sffBytes.size(), sprites, &err)) {
        std::fprintf(stderr, "%s: %s\n", sffPath.string().c_str(), err.c_str());
        return 1;
    }
    for (mugen::Sprite& sp : sprites) mugen::scaleSprite(sp, scale);
    // palettes: pal1..palN of the .def, else the one inside the first sprite
    std::vector<std::array<uint8_t, 768>> pals;
    for (int n = 1; n <= 64; ++n) {
        const auto it = def.files.find("pal" + std::to_string(n));
        if (it == def.files.end()) continue;
        const fs::path p = findCi(in, it->second);
        std::array<uint8_t, 768> rgb{};
        if (p.empty()) continue;
        const std::vector<uint8_t> b = readAll(p);
        if (mugen::readAct(b.data(), b.size(), rgb)) pals.push_back(rgb);
    }
    if (pals.empty())
        for (const mugen::Sprite& s : sprites)
            if (s.hasOwnPal) {
                pals.push_back(s.ownPal);
                break;
            }
    if (pals.empty()) {
        std::fprintf(stderr, "no palette (.act or inside the .sff)\n");
        return 1;
    }

    std::vector<uint8_t> art;
    std::vector<foit::Image> images(sprites.size());
    for (size_t i = 0; i < sprites.size(); ++i) {
        const mugen::Sprite& s = sprites[i];
        std::vector<uint8_t> band = s.px, grad(s.px.size(), 0);
        if (!encodeArtImage(s.w, s.h, band, grad, art, images[i])) {
            std::fprintf(stderr, "sprite %d,%d: cannot be encoded (%dx%d)\n", s.group, s.image, s.w, s.h);
            return 1;
        }
    }
    std::vector<Palette> readBack;
    for (size_t k = 0; k < pals.size(); ++k) {
        std::vector<uint8_t> rgba(256 * 16 * 4);
        for (int row = 0; row < 256; ++row)
            for (int t = 0; t < 16; ++t) {
                uint8_t* p = &rgba[(static_cast<size_t>(row) * 16 + t) * 4];
                p[0] = pals[k][static_cast<size_t>(row) * 3];
                p[1] = pals[k][static_cast<size_t>(row) * 3 + 1];
                p[2] = pals[k][static_cast<size_t>(row) * 3 + 2];
                p[3] = 255;
            }
        const std::vector<uint8_t> dds = makePaletteDds(16, 256, rgba);
        writeAll(out / "sprites" / ".." / (base + "_" + std::to_string(k + 1) + "p.dds"), dds);
        Palette pal;
        if (!readDds(dds.data(), dds.size(), pal, &err)) {
            std::fprintf(stderr, "palette %zu: the engine cannot read it back (%s)\n", k + 1, err.c_str());
            return 1;
        }
        readBack.push_back(pal);
    }

    // verify: every image, decoded by the engine's own code, under every palette, pixel for pixel
    size_t checked = 0, bad = 0;
    std::vector<std::vector<uint8_t>> tables;
    for (const Palette& p : readBack) tables.push_back(buildRgbaTables(p));
    for (size_t i = 0; i < sprites.size(); ++i) {
        const mugen::Sprite& s = sprites[i];
        ArtPlanes planes;
        if (images[i].n == 0) {
            planes.width = images[i].w * 16;
            planes.height = images[i].h * 16;
            planes.band.assign(static_cast<size_t>(planes.width) * planes.height, 0);
            planes.grad = planes.band;
        } else if (!foit::decodeImage(images[i], art.data(), art.size(), planes)) {
            std::fprintf(stderr, "sprite %d,%d: the engine cannot decode what was written\n", s.group, s.image);
            return 1;
        }
        for (size_t k = 0; k < tables.size(); ++k) {
            const std::vector<uint8_t> rgba = toRgba(planes, tables[k]);
            for (int y = 0; y < planes.height; ++y)
                for (int x = 0; x < planes.width; ++x) {
                    const uint8_t* o = &rgba[(static_cast<size_t>(y) * planes.width + x) * 4];
                    uint8_t want[4] = {0, 0, 0, 0};
                    if (x < s.w && y < s.h) {
                        const uint8_t idx = s.px[static_cast<size_t>(y) * s.w + x];
                        if (idx) {
                            want[0] = pals[k][idx * 3];
                            want[1] = pals[k][idx * 3 + 1];
                            want[2] = pals[k][idx * 3 + 2];
                            want[3] = 255;
                        }
                    }
                    ++checked;
                    if (want[3] != o[3] || (want[3] && std::memcmp(want, o, 3) != 0)) ++bad;
                }
            {   // the picture the engine draws, cropped to the sprite: sprites/ = palette 1, sprites_p<N>/ = the others
                std::vector<uint8_t> crop(static_cast<size_t>(s.w) * s.h * 4);
                for (int y = 0; y < s.h; ++y) std::memcpy(&crop[static_cast<size_t>(y) * s.w * 4], &rgba[static_cast<size_t>(y) * planes.width * 4], static_cast<size_t>(s.w) * 4);
                const std::string dir = k == 0 ? "sprites" : "sprites_p" + std::to_string(k + 1);
                writeAll(out / dir / (std::to_string(s.group) + "_" + std::to_string(s.image) + ".png"), encodePngRgba(crop.data(), s.w, s.h));
            }
        }
    }

    // animations + manifest
    std::vector<mugen::AirAction> acts;
    {
        const fs::path p = findCi(in, def.files.count("anim") ? def.files.at("anim") : base + ".air");
        if (!p.empty()) {
            const std::vector<uint8_t> b = readAll(p);
            acts = mugen::parseAir(std::string(b.begin(), b.end()));
            mugen::scaleActions(acts, scale);
        }
    }
    std::ostringstream js;
    js << "{\n \"name\": " << jstr(base) << ",\n \"displayName\": " << jstr(def.displayName) << ",\n \"palettes\": " << pals.size() << ",\n \"sprites\": [\n";
    for (size_t i = 0; i < sprites.size(); ++i) {
        const mugen::Sprite& s = sprites[i];
        const foit::Image& im = images[i];
        js << "  {\"g\":" << s.group << ",\"i\":" << s.image << ",\"axis\":[" << s.axisX << "," << s.axisY << "],\"size\":[" << s.w << "," << s.h << "],\"cells\":[" << im.w << "," << im.h
           << "],\"n\":" << im.n << ",\"off\":" << im.off << ",\"planes\":[" << im.size[0] << "," << im.size[1] << "," << im.size[2] << "],\"png\":\"sprites/" << s.group << "_" << s.image << ".png\"}"
           << (i + 1 < sprites.size() ? "," : "") << "\n";
    }
    js << " ],\n \"actions\": [\n";
    for (size_t a = 0; a < acts.size(); ++a) {
        js << "  {\"id\":" << acts[a].id << ",\"loopStart\":" << acts[a].loopStart << ",\"frames\":[";
        for (size_t f = 0; f < acts[a].frames.size(); ++f) {
            const mugen::AirFrame& fr = acts[a].frames[f];
            js << (f ? "," : "") << "{\"g\":" << fr.group << ",\"i\":" << fr.image << ",\"x\":" << fr.x << ",\"y\":" << fr.y << ",\"t\":" << fr.time << ",\"fh\":" << fr.flipH << ",\"fv\":" << fr.flipV << ",\"c1\":[";
            for (size_t b = 0; b < fr.clsn1.size(); ++b) js << (b ? "," : "") << "[" << fr.clsn1[b].x1 << "," << fr.clsn1[b].y1 << "," << fr.clsn1[b].x2 << "," << fr.clsn1[b].y2 << "]";
            js << "],\"c2\":[";
            for (size_t b = 0; b < fr.clsn2.size(); ++b) js << (b ? "," : "") << "[" << fr.clsn2[b].x1 << "," << fr.clsn2[b].y1 << "," << fr.clsn2[b].x2 << "," << fr.clsn2[b].y2 << "]";
            js << "]}";
        }
        js << "]}" << (a + 1 < acts.size() ? "," : "") << "\n";
    }
    js << " ]\n}\n";
    const std::string j = js.str();
    writeAll(out / "manifest.json", std::vector<uint8_t>(j.begin(), j.end()));

    size_t stored = 0;
    for (const foit::Image& im : images) stored += im.n;
    std::printf("%s: %zu sprites, %zu palettes, %zu animations; .art %zu bytes, %zu cells stored (scale x%d)\n", base.c_str(), sprites.size(), pals.size(), acts.size(), art.size(), stored, scale);
    std::printf("verified through the engine's decoder: %zu pixels, %zu different\n", checked, bad);
    if (bad) return 1;

    // ---- the .foit
    mugen::FoitBuild fb;
    if (!mugen::buildFoit(base, sprites, images, art, acts, fb, &err)) {
        std::fprintf(stderr, "foit: %s\n", err.c_str());
        return 1;
    }
    std::vector<uint8_t> foitBytes;
    if (!foit::write(fb.ch, foitBytes, &err)) {
        std::fprintf(stderr, "foit: cannot write (%s)\n", err.c_str());
        return 1;
    }
    foit::Character back;
    if (!foit::parse(foitBytes.data(), foitBytes.size(), back, &err)) {
        std::fprintf(stderr, "foit: the engine's parser rejects what was written (%s)\n", err.c_str());
        return 1;
    }
    writeAll(out / (base + ".art"), art);
    writeAll(out / (base + ".foit"), foitBytes);

    // every .air frame, read back out of the parsed .foit
    std::map<std::pair<int, int>, size_t> byId;
    for (size_t i = 0; i < sprites.size(); ++i) byId.emplace(std::make_pair(sprites[i].group, sprites[i].image), i);
    size_t fchecked = 0, fbad = 0, pixelsChecked = 0;
    auto problem = [&](const std::string& what) {
        if (++fbad <= 10) std::fprintf(stderr, "  .foit check failed: %s\n", what.c_str());
    };
    for (const mugen::AirAction& a : acts) {
        if (a.frames.empty()) continue;
        const std::string nm = a.id == 0 ? "Stand" : "Action_" + std::to_string(a.id);
        const int si = back.body.findState(nm);
        if (si < 0) {
            problem("state " + nm + " is missing");
            continue;
        }
        const foit::State& st = back.body.states[static_cast<size_t>(si)];
        if (st.hdr.nodes.size() != a.frames.size()) {
            problem(nm + ": " + std::to_string(st.hdr.nodes.size()) + " nodes for " + std::to_string(a.frames.size()) + " frames");
            continue;
        }
        const bool holds = a.frames.back().time < 0;
        const size_t loop = std::min<size_t>(static_cast<size_t>(std::max(a.loopStart, 0)), a.frames.size() - 1);
        if (!holds && loop > 0) {
            const int li = back.body.findState(nm + "_Loop");
            if (li < 0 || back.body.states[static_cast<size_t>(li)].hdr.nodes.size() != a.frames.size() - loop || st.next != li) problem(nm + ": the loop state is wrong");
        } else if (st.next != si) {
            problem(nm + ": does not repeat itself");
        }
        for (size_t k = 0; k < a.frames.size(); ++k) {
            ++fchecked;
            const mugen::AirFrame& af = a.frames[k];
            const foit::Node& nd = st.hdr.nodes[k];
            const int wantTicks = af.time < 0 ? 0xFFFF : std::min(std::max(af.time, 1), 0xFFFF);
            if (nd.props.empty() || nd.props[0].dur != wantTicks) problem(nm + ": wrong duration on frame " + std::to_string(k));
            if (nd.refs[1] == 0 || nd.refs[1] > back.frames.size()) {
                problem(nm + ": frame reference outside the list");
                continue;
            }
            const foit::Frame& fr = back.frames[nd.refs[1] - 1u];
            if (fr.hit.size() != af.clsn1.size() || fr.hurt.size() != af.clsn2.size()) problem(nm + ": box count differs on frame " + std::to_string(k));
            const auto it = byId.find(std::make_pair(af.group, af.image));
            const bool drawn = it != byId.end() && images[it->second].n > 0;
            if (!drawn) {
                if (fr.slotMask != 0) problem(nm + ": a picture where there should be none");
                continue;
            }
            const mugen::Sprite& sp = sprites[it->second];
            if (!(fr.slotMask & 1) || fr.src != 5) {
                problem(nm + ": picture missing on frame " + std::to_string(k));
                continue;
            }
            const int wantX = af.flipH ? -af.x - (sp.w - sp.axisX) : af.x - sp.axisX;
            const int wantY = af.flipV ? -af.y - (sp.h - sp.axisY) : af.y - sp.axisY;
            if (fr.drawX(0) != wantX || fr.drawY(0) != wantY) problem(nm + ": wrong position on frame " + std::to_string(k));
            ArtPlanes pl;
            if (!foit::decodeImage(fr.img[0], art.data(), art.size(), pl)) {
                problem(nm + ": picture cannot be decoded on frame " + std::to_string(k));
                continue;
            }
            for (int y = 0; y < pl.height; ++y)
                for (int x = 0; x < pl.width; ++x) {
                    uint8_t want = 0;
                    if (x < sp.w && y < sp.h) want = sp.px[static_cast<size_t>(af.flipV ? sp.h - 1 - y : y) * sp.w + (af.flipH ? sp.w - 1 - x : x)];
                    ++pixelsChecked;
                    if (pl.band[static_cast<size_t>(y) * pl.width + x] != want || pl.grad[static_cast<size_t>(y) * pl.width + x] != 0) {
                        problem(nm + ": wrong pixel on frame " + std::to_string(k));
                        y = pl.height;
                        break;
                    }
                }
        }
    }
    std::printf(".foit: %zu bytes, %zu frames, %zu states, %zu mirrored images added; %zu .air frames checked (%zu pixels), %zu problems\n", foitBytes.size(), fb.frames, fb.states,
                fb.mirroredImages, fchecked, pixelsChecked, fbad);
    if (fb.missingSprites) std::printf("  note: %zu .air frames name a sprite that is not in the .sff (kept as empty frames)\n", fb.missingSprites);

    // preview: what the engine draws for every node of every state
    if (preview) {
        std::ostringstream ix;
        ix << "[\n";
        size_t nStates = 0;
        for (size_t si = 0; si < back.body.states.size(); ++si) {
            const foit::State& st = back.body.states[si];
            struct Piece { ArtPlanes pl; int x = 0, y = 0; bool has = false; int ticks = 1; };
            std::vector<Piece> pieces;
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the origin is always inside
            for (const foit::Node& nd : st.hdr.nodes) {
                Piece pc;
                pc.ticks = nd.ticks();
                if (nd.refs[1] > 0) {
                    const foit::Frame& fr = back.frames[nd.refs[1] - 1u];
                    if ((fr.slotMask & 1) && foit::decodeImage(fr.img[0], art.data(), art.size(), pc.pl)) {
                        pc.has = true;
                        pc.x = fr.drawX(0);
                        pc.y = fr.drawY(0);
                        x0 = std::min(x0, pc.x);
                        y0 = std::min(y0, pc.y);
                        x1 = std::max(x1, pc.x + pc.pl.width);
                        y1 = std::max(y1, pc.y + pc.pl.height);
                    }
                }
                pieces.push_back(std::move(pc));
            }
            const int W = x1 - x0, H = y1 - y0;
            if (W <= 0 || H <= 0 || W > 8192 || H > 8192) continue;
            ix << (nStates++ ? ",\n" : "") << " {\"state\":" << jstr(st.name) << ",\"origin\":[" << -x0 << "," << -y0 << "],\"size\":[" << W << "," << H << "],\"frames\":[";
            for (size_t k = 0; k < pieces.size(); ++k) {
                std::vector<uint8_t> canvas(static_cast<size_t>(W) * H * 4, 0);
                if (pieces[k].has) {
                    const std::vector<uint8_t> rgba = toRgba(pieces[k].pl, tables[0]);
                    for (int y = 0; y < pieces[k].pl.height; ++y)
                        for (int x = 0; x < pieces[k].pl.width; ++x) {
                            const uint8_t* sp = &rgba[(static_cast<size_t>(y) * pieces[k].pl.width + x) * 4];
                            if (!sp[3]) continue;
                            std::memcpy(&canvas[(static_cast<size_t>(y + pieces[k].y - y0) * W + (x + pieces[k].x - x0)) * 4], sp, 4);
                        }
                }
                const std::string rel = "preview/" + st.name + "/" + std::to_string(k) + ".png";
                writeAll(out / rel, encodePngRgba(canvas.data(), W, H));
                ix << (k ? "," : "") << "[" << jstr(rel) << "," << pieces[k].ticks << "]";
            }
            ix << "]}";
        }
        ix << "\n]\n";
        const std::string t = ix.str();
        writeAll(out / "preview" / "index.json", std::vector<uint8_t>(t.begin(), t.end()));
        std::printf("preview: %zu states written to %s\n", nStates, (out / "preview").string().c_str());
    }
    return fbad ? 1 : 0;
}
