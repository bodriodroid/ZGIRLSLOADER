// zdroid_foit - inspect the user's own character data: <char>.foit + <char>.art + <char>.art-pt (+ _1p.dds palette).
//
//   zdroid_foit <file.foit | folder> [more ...] [options]
//
//   --art FILE / --art-pt FILE / --pal FILE   override the files found next to the .foit (<name>.art, <name>.art-pt,
//                                             <name>_1p.dds).  With a folder, each character uses its own siblings.
//   --states                list every state: id, name, nodes, ticks, next state, transitions
//   --frames 1,137,...      write PNGs of those frame ids (boxes + pivot drawn)           (needs --out)
//   --state NAME            write a PNG strip with the frames a state plays, in order     (needs --out)
//   --chains                print the decoded script chain of a state header (with --state)
//   --out DIR               where PNGs go
//   --boxes 0               do not draw hit/hurt/push boxes
//
// What it checks: the .foit parses to its last byte; every frame image that points into the .art / .art-pt is
// decodable (three RLE planes of the expected size).  Nothing is modified and nothing is bundled.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "art.h"
#include "dds.h"
#include "foit.h"
#include "vfs.h"

using namespace zengine;
namespace fs = std::filesystem;

static bool writeFile(const std::string& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    return static_cast<bool>(f);
}

struct Opts {
    std::string art, artPt, pal, out, state;
    bool states = false, chains = false, boxes = true, children = false, stats = false;
    int child = -1;   // --child N: --state / --states look into that child object instead of the body
    std::vector<int> frames;
};

struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // RGBA
    void init(int W, int H) {
        w = W;
        h = H;
        px.assign(static_cast<size_t>(W) * H * 4, 0);
        for (size_t i = 0; i < px.size(); i += 4) {  // dark checker so transparent parts are visible
            const int x = static_cast<int>((i / 4) % W), y = static_cast<int>((i / 4) / W);
            const uint8_t c = (((x >> 4) + (y >> 4)) & 1) ? 46 : 38;
            px[i] = px[i + 1] = px[i + 2] = c;
            px[i + 3] = 255;
        }
    }
    void put(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        p[0] = static_cast<uint8_t>((r * a + p[0] * (255 - a)) / 255);
        p[1] = static_cast<uint8_t>((g * a + p[1] * (255 - a)) / 255);
        p[2] = static_cast<uint8_t>((b * a + p[2] * (255 - a)) / 255);
    }
    void rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
        for (int x = x0; x <= x1; ++x) {
            put(x, y0, r, g, b);
            put(x, y1, r, g, b);
        }
        for (int y = y0; y <= y1; ++y) {
            put(x0, y, r, g, b);
            put(x1, y, r, g, b);
        }
    }
};

struct CharFiles {
    foit::Character ch;
    std::vector<uint8_t> art, artPt;
    std::vector<uint8_t> tables;
    std::string name;
    const uint8_t* srcFor(const foit::Frame& f, size_t* n) const {
        if (f.src == 5) { *n = art.size(); return art.data(); }
        if (f.src == 4) { *n = artPt.size(); return artPt.data(); }
        *n = 0;
        return nullptr;
    }
};

// frame bounding box relative to its pivot (images and optionally boxes)
static void frameBounds(const CharFiles& cf, const foit::Frame& f, bool boxes, int* x0, int* y0, int* x1, int* y1) {
    for (int s = 0; s < foit::kSlots; ++s) {
        const foit::Image& im = f.img[s];
        if (im.placeholder()) continue;
        *x0 = std::min(*x0, f.drawX(s));
        *y0 = std::min(*y0, f.drawY(s));
        *x1 = std::max(*x1, f.drawX(s) + im.w * 16);
        *y1 = std::max(*y1, f.drawY(s) + im.h * 16);
    }
    if (boxes)
        for (const std::vector<foit::Box>* l : {&f.hit, &f.hurt, &f.push})
            for (const foit::Box& b : *l) {
                *x0 = std::min(*x0, b.left);
                *x1 = std::max(*x1, b.right);
                *y0 = std::min(*y0, b.top);
                *y1 = std::max(*y1, b.bottom);
            }
    (void)cf;
}

static int drawFrame(const CharFiles& cf, const foit::Frame& f, Canvas& cv, int ox, int oy, bool boxes) {
    int drawn = 0;
    for (int s = 0; s < foit::kSlots; ++s) {
        const foit::Image& im = f.img[s];
        if (im.placeholder()) continue;
        size_t n = 0;
        const uint8_t* src = cf.srcFor(f, &n);
        ArtPlanes pl;
        if (!foit::decodeImage(im, src, n, pl)) continue;
        const std::vector<uint8_t> rgba = toRgba(pl, cf.tables);
        const int dx = ox + f.drawX(s), dy = oy + f.drawY(s);
        for (int y = 0; y < pl.height; ++y)
            for (int x = 0; x < pl.width; ++x) {
                const uint8_t* p = &rgba[(static_cast<size_t>(y) * pl.width + x) * 4];
                if (p[3]) cv.put(dx + x, dy + y, p[0], p[1], p[2], p[3]);
            }
        ++drawn;
    }
    if (boxes) {
        for (const foit::Box& b : f.push) cv.rect(ox + b.left, oy + b.top, ox + b.right, oy + b.bottom, 70, 130, 255);
        for (const foit::Box& b : f.hurt) cv.rect(ox + b.left, oy + b.top, ox + b.right, oy + b.bottom, 60, 220, 90);
        for (const foit::Box& b : f.hit) cv.rect(ox + b.left, oy + b.top, ox + b.right, oy + b.bottom, 255, 60, 60);
    }
    for (int i = -6; i <= 6; ++i) {  // pivot cross
        cv.put(ox + i, oy, 255, 0, 255);
        cv.put(ox, oy + i, 255, 0, 255);
    }
    return drawn;
}

static bool writePng(const Canvas& cv, const std::string& path) {
    return writeFile(path, encodePngRgba(cv.px.data(), cv.w, cv.h));
}

static std::vector<int> parseList(const char* s) {
    std::vector<int> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) v.push_back(std::atoi(tok.c_str()));
    }
    return v;
}


// ---- --stats: structural totals of the parsed model, compared by tools/validate_foit_ksy.py against the numbers
// the Kaitai Struct description (docs/foit.ksy) produces for the same file.
struct Stats {
    size_t objects = 0, states = 0, nodes = 0, scriptNodes = 0, props = 0, chains = 0, chainNodes = 0, hits = 0, subs = 0,
           commands = 0, steps = 0, transitions = 0, u32s = 0, hashes = 0;
};
static void statChain(Stats& s, const foit::Chain& c) {
    if (c.empty()) return;
    ++s.chains;
    s.chainNodes += c.size();
}
static void statCmd(Stats& s, const foit::Command& c) {
    ++s.commands;
    if (c.hasDefault) {
        ++s.steps;
        statChain(s, c.def.chain);
    }
    for (const foit::Step& st : c.steps) {
        ++s.steps;
        statChain(s, st.chain);
    }
}
static void statProps(Stats& s, const std::vector<foit::NodeProps>& v) {
    for (const foit::NodeProps& p : v) {
        ++s.props;
        statChain(s, p.chain);
        if (p.hasHit) {
            ++s.hits;
            for (int k = 0; k < 4; ++k) statChain(s, p.hit.chains[k]);
        }
        for (const foit::SubItem& si : p.sub) {
            ++s.subs;
            statChain(s, si.chain);
        }
        s.u32s += p.u32s.size();
        if (p.hasCmd) statCmd(s, p.cmd);
    }
}
static void statObject(Stats& s, const foit::Object& o) {
    ++s.objects;
    for (int k = 0; k < 3; ++k) statChain(s, o.chains[k]);
    for (const foit::State& st : o.states) {
        ++s.states;
        s.hashes += st.hashes.size();
        for (const foit::Node& nd : st.hdr.nodes) {
            ++s.nodes;
            s.scriptNodes += nd.isScript();
            statProps(s, nd.props);
        }
        if (st.hdr.hasCmd) {
            statCmd(s, st.hdr.cmd);
            statProps(s, st.hdr.cmdNode);
        }
        statChain(s, st.hdr.chain);
        for (const foit::Transition& tr : st.transitions) {
            ++s.transitions;
            statCmd(s, tr.cmd);
        }
    }
    for (const foit::Object& c : o.children) statObject(s, c);
}
static void printStats(const std::string& name, const foit::Character& ch) {
    Stats s;
    statObject(s, ch.body);
    size_t imgs = 0, hit = 0, hurt = 0, push = 0, entries = 0;
    for (const foit::Frame& f : ch.frames) {
        for (int b = 0; b < foit::kSlots; ++b) imgs += (f.slotMask >> b) & 1;
        hit += f.hit.size();
        hurt += f.hurt.size();
        push += f.push.size();
        entries += f.entries.size();
    }
    std::printf("stats %s objects=%zu states=%zu nodes=%zu script_nodes=%zu props=%zu chains=%zu chain_nodes=%zu hits=%zu subs=%zu "
                "commands=%zu steps=%zu transitions=%zu u32s=%zu hashes=%zu frames=%zu images=%zu hit_boxes=%zu hurt_boxes=%zu "
                "push_boxes=%zu entries=%zu list8=%zu\n",
                name.c_str(), s.objects, s.states, s.nodes, s.scriptNodes, s.props, s.chains, s.chainNodes, s.hits, s.subs,
                s.commands, s.steps, s.transitions, s.u32s, s.hashes, ch.frames.size(), imgs, hit, hurt, push, entries,
                ch.body.list8.size());
}

static int inspect(const std::string& foitPath, const Opts& o) {
    std::vector<uint8_t> data;
    if (!readFile(foitPath, data)) {
        std::printf("%s: cannot read\n", foitPath.c_str());
        return 1;
    }
    CharFiles cf;
    cf.name = fs::path(foitPath).stem().string();
    std::string err;
    if (!foit::parse(data.data(), data.size(), cf.ch, &err)) {
        std::printf("%-14s PARSE FAILED: %s\n", cf.name.c_str(), err.c_str());
        return 1;
    }
    const foit::Character& ch = cf.ch;
    if (o.stats) {
        printStats(cf.name, ch);
        return 0;
    }
    size_t bySrc[3] = {0, 0, 0}, imgs = 0, withBoxes = 0, withEntries = 0;
    for (const foit::Frame& f : ch.frames) {
        ++bySrc[f.src == 5 ? 0 : (f.src == 4 ? 1 : 2)];
        for (const foit::Image& im : f.img) imgs += !im.placeholder();
        withBoxes += !f.hit.empty() || !f.hurt.empty() || !f.push.empty();
        withEntries += !f.entries.empty();
    }
    std::printf("%-14s OK  '%s' | %zu frames (art %zu, art-pt %zu, none %zu) | %zu images | %zu states | %zu children"
                " (%zu states in them) | file %zu bytes consumed exactly\n",
                cf.name.c_str(), ch.body.name.c_str(), ch.frames.size(), bySrc[0], bySrc[1], bySrc[2], imgs,
                ch.body.states.size(), ch.countChildren(), ch.countStates() - ch.body.states.size(), ch.fileSize);
    (void)withBoxes;
    (void)withEntries;

    // sibling data files
    const fs::path base = fs::path(foitPath).parent_path() / cf.name;
    const std::string artPath = !o.art.empty() ? o.art : base.string() + ".art";
    const std::string ptPath = !o.artPt.empty() ? o.artPt : base.string() + ".art-pt";
    std::string palPath = !o.pal.empty() ? o.pal : base.string() + "_1p.dds";
    const bool hasArt = readFile(artPath, cf.art), hasPt = readFile(ptPath, cf.artPt);
    Palette pal = fallbackPalette();
    bool realPal = false;
    {
        std::vector<uint8_t> dd;
        std::string perr;
        if (readFile(palPath, dd) && readDds(dd.data(), dd.size(), pal, &perr)) realPal = true;
        else pal = fallbackPalette();
    }
    cf.tables = buildRgbaTables(pal);
    std::printf("               layer object: %s, bytes [%zu, %zu) of %zu | frame list at %zu\n", ch.hasLayer ? "yes" : "no",
                ch.layerPos, ch.tailPos, ch.fileSize, ch.frameListPos);
    std::printf("               files: .art %s | .art-pt %s | palette %s\n", hasArt ? "yes" : "NO", hasPt ? "yes" : "NO",
                realPal ? "yes" : "no (fallback colours)");

    if (hasArt || hasPt) {
        size_t ok[2] = {0, 0}, bad[2] = {0, 0}, missing = 0, firstBad = 0;
        bool haveBad = false;
        for (size_t i = 0; i < ch.frames.size(); ++i) {
            const foit::Frame& f = ch.frames[i];
            for (const foit::Image& im : f.img) {
                if (im.placeholder()) continue;
                size_t n = 0;
                const uint8_t* src = cf.srcFor(f, &n);
                const int k = f.src == 4 ? 1 : 0;
                if (!src || !n) { ++missing; continue; }
                if (foit::validateImage(im, src, n)) ++ok[k];
                else {
                    ++bad[k];
                    if (!haveBad) { haveBad = true; firstBad = i; }
                }
            }
        }
        std::printf("               images decodable: .art %zu ok / %zu bad | .art-pt %zu ok / %zu bad | file missing for %zu\n",
                    ok[0], bad[0], ok[1], bad[1], missing);
        if (haveBad) std::printf("               first undecodable image in frame %zu\n", firstBad);
    }

    if (o.children) {   // the object tree: every child with its first states (projectiles, companions such as Umbrella's Hungern)
        std::function<void(const foit::Object&, int)> walk = [&](const foit::Object& ob, int depth) {
            for (size_t i = 0; i < ob.children.size(); ++i) {
                const foit::Object& c = ob.children[i];
                std::printf("  %*schild %zu '%s' states %zu:", depth * 2, "", i, c.name.c_str(), c.states.size());
                for (size_t k = 0; k < c.states.size() && k < 8; ++k) std::printf(" %s(%dt)", c.states[k].name.c_str(), c.states[k].totalTicks());
                std::printf("%s\n", c.states.size() > 8 ? " ..." : "");
                walk(c, depth + 1);
            }
        };
        walk(ch.body, 0);
    }
    const foit::Object& obj = (o.child >= 0 && static_cast<size_t>(o.child) < ch.body.children.size()) ? ch.body.children[static_cast<size_t>(o.child)] : ch.body;
    if (o.states) {
        const foit::Object& b = obj;
        for (size_t i = 0; i < b.states.size(); ++i) {
            const foit::State& s = b.states[i];
            const std::string nxt = s.next < b.states.size() ? b.states[s.next].name : "?";
            std::printf("  state %3zu %-28s nodes %3zu ticks %3d next %-22s transitions %zu\n", i, s.name.c_str(),
                        s.hdr.nodes.size(), s.totalTicks(), nxt.c_str(), s.transitions.size());
        }
    }

    if (!o.out.empty()) {
        fs::create_directories(o.out);
        for (int id : o.frames) {
            if (id < 0 || static_cast<size_t>(id) >= ch.frames.size()) {
                std::printf("  frame %d out of range\n", id);
                continue;
            }
            const foit::Frame& f = ch.frames[id];
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            frameBounds(cf, f, o.boxes, &x0, &y0, &x1, &y1);
            Canvas cv;
            cv.init(x1 - x0 + 41, y1 - y0 + 41);
            const int n = drawFrame(cf, f, cv, 20 - x0, 20 - y0, o.boxes);
            const std::string p = o.out + "/" + cf.name + "_frame" + std::to_string(id) + ".png";
            writePng(cv, p);
            std::printf("  frame %d: pivot (%d,%d) src %u images drawn %d hit %zu hurt %zu push %zu -> %s\n", id, f.pivotX,
                        f.pivotY, f.src, n, f.hit.size(), f.hurt.size(), f.push.size(), p.c_str());
        }
        if (!o.state.empty()) {
            const int si = obj.findState(o.state);
            if (si < 0) {
                std::printf("  no state named '%s'\n", o.state.c_str());
            } else {
                const foit::State& s = obj.states[si];
                std::vector<int> ids;  // 0-based frame ids in play order
                std::printf("  state %d '%s': next -> %s, %d ticks\n", si, s.name.c_str(),
                            s.next < obj.states.size() ? obj.states[s.next].name.c_str() : "?", s.totalTicks());
                for (size_t k = 0; k < s.hdr.nodes.size(); ++k) {
                    const foit::Node& n = s.hdr.nodes[k];
                    if (n.isScript()) {
                        if (o.chains)
                            for (const foit::NodeProps& p : n.props)
                                if (!p.chain.empty()) std::printf("    node %zu script: %s\n", k, foit::describeChain(p.chain).c_str());
                        continue;
                    }
                    const int ref = n.refs[1] ? n.refs[1] : (n.refs[0] ? n.refs[0] : n.refs[2]);
                    std::printf("    node %zu frame %d for %d tick(s)%s\n", k, ref - 1, n.ticks(),
                                (!n.props.empty() && n.props[0].hasHit) ? "  [hit data]" : "");
                    ids.push_back(ref - 1);
                }
                if (o.chains && s.hdr.hasChain) std::printf("    header script: %s\n", foit::describeChain(s.hdr.chain).c_str());
                if (!ids.empty()) {
                    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                    for (int id : ids) frameBounds(cf, ch.frames[id], o.boxes, &x0, &y0, &x1, &y1);
                    const int cw = x1 - x0 + 20, chh = y1 - y0 + 20;
                    const int per = std::min<int>(static_cast<int>(ids.size()), 8);
                    const int rows = (static_cast<int>(ids.size()) + per - 1) / per;
                    Canvas cv;
                    cv.init(cw * per, chh * rows);
                    for (size_t i = 0; i < ids.size(); ++i) {
                        const int cx = static_cast<int>(i % per) * cw, cy = static_cast<int>(i / per) * chh;
                        drawFrame(cf, ch.frames[ids[i]], cv, cx + 10 - x0, cy + 10 - y0, o.boxes);
                    }
                    const std::string p = o.out + "/" + cf.name + "_state_" + s.name + ".png";
                    writePng(cv, p);
                    std::printf("  strip of %zu frames -> %s\n", ids.size(), p.c_str());
                }
            }
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    Opts o;
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--art") o.art = next();
        else if (a == "--art-pt") o.artPt = next();
        else if (a == "--pal") o.pal = next();
        else if (a == "--out") o.out = next();
        else if (a == "--state") o.state = next();
        else if (a == "--frames") o.frames = parseList(next());
        else if (a == "--states") o.states = true;
        else if (a == "--stats") o.stats = true;
        else if (a == "--children") o.children = true;
        else if (a == "--child") o.child = std::atoi(next());
        else if (a == "--chains") o.chains = true;
        else if (a == "--boxes") o.boxes = std::atoi(next()) != 0;
        else if (a == "-h" || a == "--help") {
            std::printf("usage: zdroid_foit <file.foit|folder> ... [--states] [--stats] [--frames 1,2] [--state NAME] [--chains] [--out DIR]\n"
                        "                   [--art F] [--art-pt F] [--pal F] [--boxes 0]\n");
            return 0;
        } else inputs.push_back(a);
    }
    if (inputs.empty()) {
        std::printf("usage: zdroid_foit <file.foit|folder> ... (see --help)\n");
        return 2;
    }
    std::vector<std::string> files;
    for (const std::string& in : inputs) {
        std::error_code ec;
        if (fs::is_directory(in, ec)) {
            std::vector<std::string> found;
            for (const auto& e : fs::recursive_directory_iterator(in, ec))
                if (e.is_regular_file() && e.path().extension() == ".foit") found.push_back(e.path().string());
            std::sort(found.begin(), found.end());
            files.insert(files.end(), found.begin(), found.end());
        } else {
            files.push_back(in);
        }
    }
    int rc = 0, okCount = 0;
    for (const std::string& f : files) {
        const int r = inspect(f, o);
        rc |= r;
        okCount += r == 0;
    }
    std::printf("\n%d of %zu file(s) parsed\n", okCount, files.size());
    return rc;
}
