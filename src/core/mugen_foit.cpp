// mugen_foit.cpp - see mugen_foit.h.
#include "mugen_foit.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace zengine {
namespace mugen {

namespace {

foit::Box toBox(const Box& b, bool fh, bool fv) {
    int l = std::min(b.x1, b.x2), r = std::max(b.x1, b.x2), t = std::min(b.y1, b.y2), d = std::max(b.y1, b.y2);
    if (fh) {
        const int nl = -r, nr = -l;
        l = nl;
        r = nr;
    }
    if (fv) {
        const int nt = -d, nd = -t;
        t = nt;
        d = nd;
    }
    return foit::Box{t, d, l, r};
}

std::string frameKey(const foit::Frame& f) {
    std::string k = std::to_string(f.src) + "|" + std::to_string(f.slotMask) + "|" + std::to_string(f.img[0].off) + "|" + std::to_string(f.img[0].n) + "|" +
                    std::to_string(f.xy[0]) + "," + std::to_string(f.xy[1]);
    for (const std::vector<foit::Box>* v : {&f.hit, &f.hurt}) {
        k += "|";
        for (const foit::Box& b : *v) k += std::to_string(b.top) + "," + std::to_string(b.bottom) + "," + std::to_string(b.left) + "," + std::to_string(b.right) + ";";
    }
    return k;
}

}  // namespace

bool buildFoit(const std::string& name, const std::vector<Sprite>& sprites, const std::vector<foit::Image>& images, std::vector<uint8_t>& art,
               const std::vector<AirAction>& actions, FoitBuild& out, std::string* err) {
    out = FoitBuild();
    if (sprites.size() != images.size()) {
        if (err) *err = "sprites and images do not match";
        return false;
    }
    std::map<std::pair<int, int>, size_t> byId;
    for (size_t i = 0; i < sprites.size(); ++i) byId.emplace(std::make_pair(sprites[i].group, sprites[i].image), i);

    std::map<std::tuple<size_t, bool, bool>, foit::Image> mirrored;
    auto imageFor = [&](size_t si, bool fh, bool fv) -> const foit::Image* {
        if (!fh && !fv) return &images[si];
        const auto key = std::make_tuple(si, fh, fv);
        auto it = mirrored.find(key);
        if (it != mirrored.end()) return &it->second;
        const Sprite& s = sprites[si];
        std::vector<uint8_t> band(s.px.size()), grad(s.px.size(), 0);
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x)
                band[static_cast<size_t>(y) * s.w + x] = s.px[static_cast<size_t>(fv ? s.h - 1 - y : y) * s.w + (fh ? s.w - 1 - x : x)];
        foit::Image im;
        if (!encodeArtImage(s.w, s.h, band, grad, art, im)) return nullptr;
        ++out.mirroredImages;
        return &mirrored.emplace(key, im).first->second;
    };

    foit::Character& c = out.ch;
    c.version = 9;
    c.body.name = name;
    c.body.slotDefaultsValid = true;   // all slots start at 0.0
    std::map<std::string, int> frameIds;
    auto frameFor = [&](const AirFrame& af) -> int {
        foit::Frame f;
        const auto it = byId.find(std::make_pair(af.group, af.image));
        if (it == byId.end()) {
            if (af.group >= 0) ++out.missingSprites;
        } else {
            const Sprite& s = sprites[it->second];
            const foit::Image* im = imageFor(it->second, af.flipH, af.flipV);
            if (!im) return -1;
            if (im->n > 0) {
                f.src = 5;
                f.slotMask = 1;
                f.img[0] = *im;
                f.xy[0] = af.flipH ? -af.x - (s.w - s.axisX) : af.x - s.axisX;
                f.xy[1] = af.flipV ? -af.y - (s.h - s.axisY) : af.y - s.axisY;
            }
        }
        for (const Box& b : af.clsn1) f.hit.push_back(toBox(b, af.flipH, af.flipV));
        for (const Box& b : af.clsn2) f.hurt.push_back(toBox(b, af.flipH, af.flipV));
        const std::string key = frameKey(f);
        const auto fi = frameIds.find(key);
        if (fi != frameIds.end()) return fi->second;
        const int id = static_cast<int>(c.frames.size());
        c.frames.push_back(std::move(f));
        frameIds.emplace(key, id);
        return id;
    };

    int standState = -1;
    for (const AirAction& a : actions) {
        if (a.frames.empty()) continue;
        std::vector<int> fid;
        for (const AirFrame& af : a.frames) {
            const int id = frameFor(af);
            if (id < 0) {
                if (err) *err = "cannot encode a mirrored sprite";
                return false;
            }
            fid.push_back(id);
        }
        auto nodes = [&](size_t from) {
            std::vector<foit::Node> v;
            for (size_t k = from; k < a.frames.size(); ++k) {
                foit::Node nd;
                nd.refs[1] = static_cast<uint16_t>(fid[k] + 1);
                foit::NodeProps p;
                p.b1 = 1;   // "has a duration"
                p.dur = a.frames[k].time < 0 ? 0xFFFF : static_cast<uint16_t>(std::min(std::max(a.frames[k].time, 1), 0xFFFF));
                nd.props.push_back(p);
                v.push_back(std::move(nd));
            }
            return v;
        };
        const std::string nm = a.id == 0 ? "Stand" : "Action_" + std::to_string(a.id);
        const bool holds = a.frames.back().time < 0;
        const size_t loop = std::min<size_t>(static_cast<size_t>(std::max(a.loopStart, 0)), a.frames.size() - 1);
        const uint16_t self = static_cast<uint16_t>(c.body.states.size());
        foit::State s;
        s.name = nm;
        s.hdr.nodes = nodes(0);
        if (a.id == 0) standState = self;
        if (holds || loop == 0) {
            s.next = self;
            c.body.states.push_back(std::move(s));
        } else {
            s.next = static_cast<uint16_t>(self + 1);
            c.body.states.push_back(std::move(s));
            foit::State l;
            l.name = nm + "_Loop";
            l.hdr.nodes = nodes(loop);
            l.next = static_cast<uint16_t>(self + 1);
            c.body.states.push_back(std::move(l));
        }
    }
    if (c.frames.empty() || c.body.states.empty()) {
        if (err) *err = "the .air has no animation to convert";
        return false;
    }
    if (c.body.states.size() > 0xFFFF || c.frames.size() > 0xFFFE) {
        if (err) *err = "too many states or frames";
        return false;
    }
    // default push box = the standing body
    if (standState >= 0) {
        const foit::Frame& f = c.frames[c.body.states[static_cast<size_t>(standState)].hdr.nodes[0].refs[1] - 1];
        if (!f.hurt.empty()) {
            int t = 0, l = 0, r = 0;
            bool first = true;
            for (const foit::Box& b : f.hurt) {
                if (first) { t = b.top; l = b.left; r = b.right; first = false; }
                t = std::min(t, b.top);
                l = std::min(l, b.left);
                r = std::max(r, b.right);
            }
            c.body.box[0] = t;
            c.body.box[1] = 0;
            c.body.box[2] = l;
            c.body.box[3] = r;
        }
        c.body.refBe = static_cast<uint16_t>(standState);
    }
    out.frames = c.frames.size();
    out.states = c.body.states.size();
    return true;
}

}  // namespace mugen
}  // namespace zengine
