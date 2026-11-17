#include "colour_edit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace zengine {
namespace coledit {

namespace {
float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
uint8_t clampByte(double v) { return static_cast<uint8_t>(std::min(255.0, std::max(0.0, std::floor(v + 0.5)))); }

void wr32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

// Solves the n x n system m * x = b in place (Gaussian elimination, partial pivoting).  A pivot that is nearly zero leaves its unknown at 0.
void solve(std::vector<double>& m, std::vector<double>& b, int n) {
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r)
            if (std::fabs(m[static_cast<size_t>(r) * n + c]) > std::fabs(m[static_cast<size_t>(piv) * n + c])) piv = r;
        if (std::fabs(m[static_cast<size_t>(piv) * n + c]) < 1e-12) continue;
        if (piv != c) {
            for (int k = 0; k < n; ++k) std::swap(m[static_cast<size_t>(c) * n + k], m[static_cast<size_t>(piv) * n + k]);
            std::swap(b[static_cast<size_t>(c)], b[static_cast<size_t>(piv)]);
        }
        for (int r = c + 1; r < n; ++r) {
            const double f = m[static_cast<size_t>(r) * n + c] / m[static_cast<size_t>(c) * n + c];
            if (f == 0) continue;
            for (int k = c; k < n; ++k) m[static_cast<size_t>(r) * n + k] -= f * m[static_cast<size_t>(c) * n + k];
            b[static_cast<size_t>(r)] -= f * b[static_cast<size_t>(c)];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        double s = b[static_cast<size_t>(r)];
        for (int k = r + 1; k < n; ++k) s -= m[static_cast<size_t>(r) * n + k] * b[static_cast<size_t>(k)];
        const double d = m[static_cast<size_t>(r) * n + r];
        b[static_cast<size_t>(r)] = std::fabs(d) < 1e-12 ? 0.0 : s / d;
    }
}

// Least-squares keys (darkest first in `k`, as the interpolation counts) for the tones `row`; returns the keys lightest first.
Region fit(const std::vector<Rgb>& row, int n) {
    const int T = static_cast<int>(row.size());
    Region reg;
    if (n <= 1) {
        double s[3] = {0, 0, 0};
        for (const Rgb& c : row)
            for (int i = 0; i < 3; ++i) s[i] += c[static_cast<size_t>(i)];
        reg.key.push_back({clampByte(s[0] / T), clampByte(s[1] / T), clampByte(s[2] / T)});
        return reg;
    }
    std::vector<double> ata(static_cast<size_t>(n) * n, 0.0);
    std::vector<double> aty[3] = {std::vector<double>(static_cast<size_t>(n), 0.0), std::vector<double>(static_cast<size_t>(n), 0.0),
                                  std::vector<double>(static_cast<size_t>(n), 0.0)};
    for (int t = 0; t < T; ++t) {
        const double x = T > 1 ? t * (n - 1) / static_cast<double>(T - 1) : 0.0;
        const int i = std::min(static_cast<int>(x), n - 2);
        const double f = x - i;
        const double w[2] = {1.0 - f, f};
        for (int a = 0; a < 2; ++a) {
            for (int b = 0; b < 2; ++b) ata[static_cast<size_t>(i + a) * n + (i + b)] += w[a] * w[b];
            for (int c = 0; c < 3; ++c) aty[c][static_cast<size_t>(i + a)] += w[a] * row[static_cast<size_t>(t)][static_cast<size_t>(c)];
        }
    }
    for (int i = 0; i < n; ++i) ata[static_cast<size_t>(i) * n + i] += 1e-9;
    std::vector<double> sol[3];
    for (int c = 0; c < 3; ++c) {
        std::vector<double> m = ata, b = aty[c];
        solve(m, b, n);
        sol[c] = b;
    }
    reg.key.resize(static_cast<size_t>(n));
    for (int p = 0; p < n; ++p)   // p = 0 darkest
        reg.key[static_cast<size_t>(n - 1 - p)] = {clampByte(sol[0][static_cast<size_t>(p)]), clampByte(sol[1][static_cast<size_t>(p)]),
                                                    clampByte(sol[2][static_cast<size_t>(p)])};
    return reg;
}

int maxError(const Region& r, const std::vector<Rgb>& row) {
    std::vector<Rgb> ramp;
    rampOf(r, static_cast<int>(row.size()), ramp);
    int worst = 0;
    for (size_t t = 0; t < row.size(); ++t)
        for (size_t c = 0; c < 3; ++c) worst = std::max(worst, std::abs(static_cast<int>(ramp[t][c]) - static_cast<int>(row[t][c])));
    return worst;
}

std::string hex(const Rgb& c) {
    char b[8];
    std::snprintf(b, sizeof b, "%02x%02x%02x", c[0], c[1], c[2]);
    return b;
}
bool unhex(const std::string& s, Rgb& c) {
    if (s.size() != 6) return false;
    for (char ch : s)
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    for (int i = 0; i < 3; ++i) c[static_cast<size_t>(i)] = static_cast<uint8_t>(std::stoi(s.substr(static_cast<size_t>(i) * 2, 2), nullptr, 16));
    return true;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace

Hsv toHsv(const Rgb& c) {
    const float r = c[0] / 255.0f, g = c[1] / 255.0f, b = c[2] / 255.0f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    Hsv o;
    o.v = mx;
    o.s = mx > 0 ? d / mx : 0;
    if (d <= 0) {
        o.h = 0;
    } else if (mx == r) {
        o.h = 60.0f * std::fmod((g - b) / d, 6.0f);
    } else if (mx == g) {
        o.h = 60.0f * ((b - r) / d + 2.0f);
    } else {
        o.h = 60.0f * ((r - g) / d + 4.0f);
    }
    if (o.h < 0) o.h += 360.0f;
    return o;
}

Rgb fromHsv(const Hsv& c) {
    const float h = std::fmod(std::fmod(c.h, 360.0f) + 360.0f, 360.0f);
    const float s = clampf(c.s, 0, 1), v = clampf(c.v, 0, 1);
    const float k = v * s;
    const float x = k * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - k;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = k; g = x; }
    else if (h < 120) { r = x; g = k; }
    else if (h < 180) { g = k; b = x; }
    else if (h < 240) { g = x; b = k; }
    else if (h < 300) { r = x; b = k; }
    else { r = k; b = x; }
    return {clampByte((r + m) * 255.0), clampByte((g + m) * 255.0), clampByte((b + m) * 255.0)};
}

void rampOf(const Region& r, int tones, std::vector<Rgb>& out) {
    out.assign(static_cast<size_t>(std::max(tones, 0)), Rgb{0, 0, 0});
    const int n = static_cast<int>(r.key.size());
    if (n == 0 || tones <= 0) return;
    for (int t = 0; t < tones; ++t) {
        if (n == 1) {
            out[static_cast<size_t>(t)] = r.key[0];
            continue;
        }
        const double x = tones > 1 ? t * (n - 1) / static_cast<double>(tones - 1) : 0.0;   // 0 = the darkest key
        const int i = std::min(static_cast<int>(x), n - 2);
        const double f = x - i;
        const Rgb& a = r.key[static_cast<size_t>(n - 1 - i)];   // darker
        const Rgb& b = r.key[static_cast<size_t>(n - 2 - i)];   // lighter
        Rgb px;
        for (size_t c = 0; c < 3; ++c) {
            const double va = a[c], vb = b[c];
            px[c] = static_cast<uint8_t>(static_cast<int>(va + (vb - va) * f));
        }
        out[static_cast<size_t>(t)] = px;
    }
}

bool fromPalette(const Palette& base, const std::vector<int>* counts, Scheme& out) {
    if (base.bands < 2 || base.tones < 2 || base.rgb.size() < static_cast<size_t>(base.bands) * base.tones * 3) return false;
    Scheme s;
    s.tones = base.tones;
    s.outline = {base.at(0, 0)[0], base.at(0, 0)[1], base.at(0, 0)[2]};
    const bool useCounts = counts && counts->size() + 1 == static_cast<size_t>(base.bands);
    for (int r = 1; r < base.bands; ++r) {
        std::vector<Rgb> row(static_cast<size_t>(base.tones));
        for (int t = 0; t < base.tones; ++t) {
            const uint8_t* p = base.at(r, t);
            row[static_cast<size_t>(t)] = {p[0], p[1], p[2]};
        }
        if (useCounts && (*counts)[static_cast<size_t>(r - 1)] >= 1 && (*counts)[static_cast<size_t>(r - 1)] <= 16) {
            Region cand = fit(row, (*counts)[static_cast<size_t>(r - 1)]);
            if (maxError(cand, row) <= 3) {   // the table's key count reproduces the row; if it does not (another build), search below
                s.regions.push_back(std::move(cand));
                continue;
            }
        }
        Region best;
        int bestErr = 1 << 30;
        for (int n = 1; n <= 16 && n <= base.tones; ++n) {
            Region cand = fit(row, n);
            const int e = maxError(cand, row);
            if (e < bestErr) {
                bestErr = e;
                best = std::move(cand);
            }
            if (e <= 3) break;   // the smallest number of keys that reproduces the row
        }
        s.regions.push_back(std::move(best));
    }
    if (base.alpha.size() == static_cast<size_t>(base.bands) * base.tones) s.litAlpha = base.alpha;
    out = std::move(s);
    return true;
}

std::vector<int> keyCountCandidates(const Palette& base, int band, int tol) {
    std::vector<int> out;
    if (band < 0 || band >= base.bands || base.tones < 2 || base.rgb.size() < static_cast<size_t>(base.bands) * base.tones * 3) return out;
    std::vector<Rgb> row(static_cast<size_t>(base.tones));
    for (int t = 0; t < base.tones; ++t) {
        const uint8_t* p = base.at(band, t);
        row[static_cast<size_t>(t)] = {p[0], p[1], p[2]};
    }
    const int top = std::min(16, base.tones);
    for (int n = 1; n < top; ++n)
        if (maxError(fit(row, n), row) <= tol) out.push_back(n);
    if (out.empty()) out.push_back(top);   // 16 keys on 16 tones are an exact copy of the row: only listed when nothing smaller fits
    return out;
}

Palette bake(const Scheme& s) {
    Palette p;
    p.tones = s.tones;
    p.bands = static_cast<int>(s.regions.size()) + 1;
    p.rgb.assign(static_cast<size_t>(p.bands) * p.tones * 3, 0);
    for (int t = 0; t < p.tones; ++t)
        for (size_t c = 0; c < 3; ++c) p.rgb[static_cast<size_t>(t) * 3 + c] = s.outline[c];
    std::vector<Rgb> ramp;
    for (size_t r = 0; r < s.regions.size(); ++r) {
        rampOf(s.regions[r], s.tones, ramp);
        for (int t = 0; t < p.tones; ++t)
            for (size_t c = 0; c < 3; ++c) p.rgb[((r + 1) * static_cast<size_t>(p.tones) + static_cast<size_t>(t)) * 3 + c] = ramp[static_cast<size_t>(t)][c];
    }
    if (s.litAlpha.size() == p.rgb.size() / 3) p.alpha = s.litAlpha;
    return p;
}

std::vector<uint8_t> toDds(const Palette& p) {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'D', 'D', 'S', ' '});
    wr32(out, 124);
    wr32(out, 0x100F);   // caps | height | width | pixel format | pitch
    wr32(out, static_cast<uint32_t>(p.bands));
    wr32(out, static_cast<uint32_t>(p.tones));
    wr32(out, static_cast<uint32_t>(p.tones) * 4);
    wr32(out, 0);
    wr32(out, 1);
    for (int i = 0; i < 11; ++i) wr32(out, 0);
    wr32(out, 32);
    wr32(out, 0x41);   // RGB | ALPHAPIXELS
    wr32(out, 0);
    wr32(out, 32);
    wr32(out, 0x00FF0000);
    wr32(out, 0x0000FF00);
    wr32(out, 0x000000FF);
    wr32(out, 0xFF000000);
    wr32(out, 0x1000);
    for (int i = 0; i < 4; ++i) wr32(out, 0);
    const size_t n = static_cast<size_t>(p.bands) * p.tones;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t a = p.alpha.size() == n ? p.alpha[i] : 255u;
        wr32(out, (a << 24) | (static_cast<uint32_t>(p.rgb[i * 3]) << 16) | (static_cast<uint32_t>(p.rgb[i * 3 + 1]) << 8) | p.rgb[i * 3 + 2]);
    }
    return out;
}

Rgb mainKey(const Region& r) {
    if (r.key.empty()) return {0, 0, 0};
    return r.key[(r.key.size() - 1) / 2];
}

void adjust(Region& r, float dHue, float satMul, float valMul) {
    for (Rgb& k : r.key) {
        Hsv h = toHsv(k);
        h.h = std::fmod(h.h + dHue + 720.0f, 360.0f);
        h.s = clampf(h.s * satMul, 0, 1);
        h.v = clampf(h.v * valMul, 0, 1);
        k = fromHsv(h);
    }
}

void setMain(Region& r, const Rgb& c) {
    if (r.key.empty()) return;
    const size_t mi = (r.key.size() - 1) / 2;
    const Hsv old = toHsv(r.key[mi]), neu = toHsv(c);
    if (old.s < 0.04f || old.v < 0.02f) {   // a grey (or black) main colour has no hue to move from: every key takes the new hue and saturation
        const float vMul = old.v > 0.02f ? neu.v / old.v : 1.0f;
        for (size_t i = 0; i < r.key.size(); ++i) {
            Hsv h = toHsv(r.key[i]);
            h.h = neu.h;
            h.s = neu.s;
            h.v = clampf(old.v > 0.02f ? h.v * vMul : neu.v * (r.key.size() > 1 ? 0.4f + 0.6f * (1.0f - static_cast<float>(i) / (r.key.size() - 1)) : 1.0f), 0, 1);
            r.key[i] = fromHsv(h);
        }
    } else {
        const float sMul = neu.s > 0 ? clampf(neu.s / old.s, 0, 8) : 0;
        adjust(r, neu.h - old.h, sMul, clampf(neu.v / old.v, 0, 8));
    }
    r.key[mi] = c;
}

void adjustAll(Scheme& s, float dHue, float satMul, float valMul) {
    for (Region& r : s.regions) adjust(r, dHue, satMul, valMul);
}

void randomise(Scheme& s, uint32_t seed) {
    uint32_t st = seed * 2654435761u + 12345u;
    auto next = [&]() {
        st = st * 1664525u + 1013904223u;
        return static_cast<float>((st >> 8) & 0xFFFF) / 65535.0f;
    };
    const float base = next() * 360.0f;
    float shift[3], sat[3], val[3];
    for (int f = 0; f < 3; ++f) {
        shift[f] = base + 120.0f * static_cast<float>(f) + (next() - 0.5f) * 60.0f;
        sat[f] = 0.8f + next() * 0.4f;
        val[f] = 0.92f + next() * 0.16f;
    }
    for (Region& r : s.regions) {
        const Hsv m = toHsv(mainKey(r));
        if (m.s < 0.08f) continue;   // greys, whites and blacks keep their look (skin shadows, outlines, metal)
        const int fam = static_cast<int>(m.h / 120.0f) % 3;
        adjust(r, shift[fam], sat[fam], val[fam]);
    }
}

std::string serialize(const Scheme& s) {
    std::ostringstream o;
    o << "ZGCOL 1\n";
    o << "character " << s.character << "\n";
    o << "name " << s.name << "\n";
    o << "tones " << s.tones << "\n";
    o << "outline " << hex(s.outline) << "\n";
    for (const Region& r : s.regions) {
        o << "r " << r.key.size();
        for (const Rgb& k : r.key) o << ' ' << hex(k);
        o << "\n";
    }
    return o.str();
}

bool parse(const std::string& text, Scheme& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    std::istringstream in(text);
    std::string line;
    bool header = false;
    Scheme s;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string word;
        ls >> word;
        if (!header) {
            int ver = 0;
            ls >> ver;
            if (word != "ZGCOL" || ver != 1) return fail("not a ZGCOL 1 file");
            header = true;
            continue;
        }
        if (word == "character") {
            ls >> s.character;
        } else if (word == "name") {
            std::string rest;
            std::getline(ls, rest);
            s.name = trim(rest);
        } else if (word == "tones") {
            ls >> s.tones;
            if (s.tones < 2 || s.tones > 64) return fail("bad tones");
        } else if (word == "outline") {
            std::string h;
            ls >> h;
            if (!unhex(h, s.outline)) return fail("bad outline colour");
        } else if (word == "r") {
            size_t n = 0;
            ls >> n;
            if (n < 1 || n > 16) return fail("bad key count");
            Region r;
            for (size_t i = 0; i < n; ++i) {
                std::string h;
                Rgb c;
                if (!(ls >> h) || !unhex(h, c)) return fail("bad key colour");
                r.key.push_back(c);
            }
            s.regions.push_back(std::move(r));
        }
    }
    if (!header) return fail("empty file");
    if (s.character.empty() || s.regions.empty()) return fail("no character or no regions");
    out = std::move(s);
    return true;
}

std::string slug(const std::string& name) {
    std::string o;
    bool gap = false;
    for (char ch : name) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u)) {
            if (gap && !o.empty()) o += '_';
            o += static_cast<char>(std::tolower(u));
            gap = false;
        } else {
            gap = true;
        }
    }
    return o.empty() ? std::string("colour") : o;
}

}  // namespace coledit
}  // namespace zengine
