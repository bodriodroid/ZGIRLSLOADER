#include "palfx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace zengine {
namespace palfx {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string lowerCopy(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

std::vector<std::string> tokens(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        const size_t b = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        if (i > b) out.emplace_back(s.substr(b, i - b));
    }
    return out;
}

// "a" or "a/b" (a divided by b); false when it is not a number.
bool parseNum(const std::string& t, float& out) {
    if (t.empty()) return false;
    char* end = nullptr;
    const double a = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    double v = a;
    if (*end == '/') {
        char* end2 = nullptr;
        const double b = std::strtod(end + 1, &end2);
        if (end2 == end + 1) return false;
        if (b != 0.0) v = a / b;
    } else if (*end != '\0') {
        return false;
    }
    if (!std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

// One colour of a frame: NORMAL, PAL n, or three numbers.  Advances `i`.
bool parseColour(const std::vector<std::string>& t, size_t& i, Colour& c) {
    if (i >= t.size()) return false;
    const std::string k = lowerCopy(t[i]);
    if (k == "normal") {
        c.actual = true;
        ++i;
        return true;
    }
    if (k == "pal") {
        float n = 0;
        if (i + 1 >= t.size() || !parseNum(t[i + 1], n) || n < 0 || n > 255) return false;
        c.pal = static_cast<int>(n);
        i += 2;
        return true;
    }
    if (i + 2 >= t.size()) return false;
    for (int k2 = 0; k2 < 3; ++k2) {
        float v = 0;
        if (!parseNum(t[i + static_cast<size_t>(k2)], v)) return false;
        c.rgb[k2] = v / 255.0f;
    }
    i += 3;
    return true;
}

bool parseFrame(std::string_view line, Frame& f, bool& loop) {
    const size_t close = line.find(']');
    if (line.empty() || line[0] != '[' || close == std::string_view::npos) return false;
    const std::vector<std::string> in = tokens(line.substr(1, close - 1));
    size_t i = 0;
    if (!parseColour(in, i, f.hi) || !parseColour(in, i, f.lo) || i != in.size()) return false;
    f.strength = f.hi.actual ? 0.0f : 1.0f;
    const std::vector<std::string> rest = tokens(line.substr(close + 1));
    loop = false;
    for (size_t k = 0; k < rest.size(); ++k) {
        const std::string w = lowerCopy(rest[k]);
        float v = 0;
        if (w == "loop") {
            loop = true;
        } else if (w == "himult" || w == "lomult") {
            if (k + 1 < rest.size() && parseNum(rest[k + 1], v)) {
                (w == "himult" ? f.hiMult : f.loMult) = v;
                ++k;
            }
        } else if (parseNum(rest[k], v)) {
            f.strength = v / 255.0f;
        }
    }
    return true;
}

}  // namespace

uint32_t nameHash(std::string_view name) {
    uint32_t h = 5381;
    for (char c : name) {
        const uint32_t u = static_cast<uint32_t>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c) & 0xFFu;
        h = u ^ (h * 33u);
    }
    return h;
}

int Effect::frameAt(int t) const {
    const int n = static_cast<int>(frames.size());
    if (t < 0 || n == 0) return -1;
    if (t < n) return t;
    if (loopFrom >= 0 && loopFrom < n) return loopFrom + (t - n) % (n - loopFrom);
    return -1;
}

size_t Library::parse(std::string_view text) {
    size_t added = 0;
    Effect cur;
    bool open = false;
    auto finish = [&]() {
        if (open && !cur.frames.empty()) {
            cur.hash = nameHash(cur.name);
            auto it = byHash_.find(cur.hash);
            if (it != byHash_.end()) {
                if (fx_[it->second].name != cur.name && lowerCopy(fx_[it->second].name) != lowerCopy(cur.name)) ++collisions_;
                fx_[it->second] = cur;   // a later definition of the same effect replaces the earlier one
            } else {
                byHash_[cur.hash] = fx_.size();
                fx_.push_back(cur);
            }
            ++added;
        }
        cur = Effect();
        open = false;
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t hash = line.find('#');
        if (hash != std::string_view::npos) line = line.substr(0, hash);
        const size_t sl = line.find("//");
        if (sl != std::string_view::npos) line = line.substr(0, sl);
        size_t b = 0, e = line.size();
        while (b < e && isSpace(line[b])) ++b;
        while (e > b && isSpace(line[e - 1])) --e;
        line = line.substr(b, e - b);
        if (line.empty()) continue;

        const std::string low = lowerCopy(line);
        if (low == "end") {
            finish();
        } else if (line[0] == '[') {
            if (!open) continue;
            Frame f;
            bool loop = false;
            if (!parseFrame(line, f, loop)) continue;
            if (loop) cur.loopFrom = static_cast<int>(cur.frames.size());
            cur.frames.push_back(f);
        } else if (low == "nolight") {
            if (open) cur.noLight = true;
        } else if (low.compare(0, 4, "mask") == 0 && (low.size() == 4 || isSpace(low[4]))) {
            if (!open) continue;
            const std::vector<std::string> t = tokens(line);
            for (size_t k = 1; k < t.size() && cur.mask.size() < 4; ++k) {
                float v = 0;
                if (parseNum(t[k], v) && v >= 0 && v <= 255) cur.mask.push_back(static_cast<int>(v));
            }
        } else {
            finish();   // a name line: whatever was open ends here (a table that forgot an END keeps what it has)
            cur.name = tokens(line)[0];
            open = true;
        }
    }
    finish();
    return added;
}

const Effect* Library::find(uint32_t hash) const {
    const auto it = byHash_.find(hash);
    return it == byHash_.end() ? nullptr : &fx_[it->second];
}

namespace {
void fillEnd(const Colour& c, const PalLookup& pal, float out[3]) {
    if (c.actual) {
        out[0] = out[1] = out[2] = 0.5f;
        return;
    }
    if (c.pal >= 0) {
        float v[3];
        if (pal && pal(c.pal, v)) {
            for (int i = 0; i < 3; ++i) out[i] = v[i];
        } else {
            out[0] = out[1] = out[2] = 0.5f;
        }
        return;
    }
    for (int i = 0; i < 3; ++i) out[i] = c.rgb[i];
}
}  // namespace

Tint resolve(const Effect& fx, const Frame& f, const PalLookup& pal) {
    Tint t;
    fillEnd(f.hi, pal, t.hi);
    fillEnd(f.lo, pal, t.lo);
    t.aHi = f.hi.actual ? 0.0f : f.strength * f.hiMult;
    t.aLo = f.lo.actual ? 0.0f : f.strength * f.loMult;
    t.noLight = fx.noLight;
    t.maskCount = static_cast<int>(std::min<size_t>(4, fx.mask.size()));
    for (int i = 0; i < t.maskCount; ++i) t.mask[i] = fx.mask[static_cast<size_t>(i)];
    return t;
}

Flat flatten(const Tint& t) {
    Flat o;
    const float wl = std::clamp(t.aLo, 0.0f, 1.0f), wh = std::clamp(t.aHi, 0.0f, 1.0f);
    const float sum = wl + wh;
    if (sum <= 1e-6f) {
        o.a = 0;
        return o;
    }
    for (int i = 0; i < 3; ++i) o.rgb[i] = std::clamp((t.lo[i] * wl + t.hi[i] * wh) / sum, 0.0f, 1.0f);
    o.a = std::clamp(sum * 0.5f, 0.0f, 1.0f);
    return o;
}

void Layer::update(uint32_t hash, const Library& lib) {
    if (hash == 0) {
        reset();
        return;
    }
    if (!started_ || hash != hash_) {
        started_ = true;
        hash_ = hash;
        fx_ = lib.find(hash);
        tick_ = 0;
    } else {
        ++tick_;
    }
    frame_ = fx_ ? fx_->frameAt(tick_) : -1;
}

bool Layer::tint(const PalLookup& pal, Tint& out) const {
    if (!active()) return false;
    out = resolve(*fx_, fx_->frames[static_cast<size_t>(frame_)], pal);
    return true;
}

void FighterFx::update(const std::array<uint32_t, kLayers>& hashes, const Library& lib) {
    for (int i = 0; i < kLayers; ++i) layers_[i].update(hashes[static_cast<size_t>(i)], lib);
}

void FighterFx::reset() {
    for (Layer& l : layers_) l.reset();
}

bool FighterFx::any() const {
    for (const Layer& l : layers_)
        if (l.active()) return true;
    return false;
}

bool FighterFx::primary(const PalLookup& pal, Tint& out) const {
    for (int id : {kHit, kSelf, kStatus})
        if (layers_[id].tint(pal, out)) return true;
    return false;
}

bool FighterFx::secondary(const PalLookup& pal, Tint& out) const {
    Tint t;
    if (!layers_[kSelf2].tint(pal, t) || t.maskCount == 0) return false;
    out = t;
    return true;
}

Flat FighterFx::flat(const PalLookup& pal) const {
    Flat acc;
    float a0 = 0;
    // bottom to top: status, self, self2 (only when it has no mask: a masked one cannot be drawn on a whole sprite), hit
    for (int id : {kStatus, kSelf, kSelf2, kHit}) {
        Tint t;
        if (!layers_[id].tint(pal, t)) continue;
        if (id == kSelf2 && t.maskCount > 0) continue;
        const Flat f = flatten(t);
        if (f.a <= 0) continue;
        const float a = f.a + a0 * (1.0f - f.a);
        for (int i = 0; i < 3; ++i) acc.rgb[i] = (f.rgb[i] * f.a + acc.rgb[i] * a0 * (1.0f - f.a)) / a;
        a0 = a;
    }
    acc.a = a0;
    return acc;
}

}  // namespace palfx
}  // namespace zengine
