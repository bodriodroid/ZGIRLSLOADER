#include "art.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace zengine {

namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

int popcount8(uint8_t x) {
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
}

}  // namespace

// Assembles the 16x16 blocks of the decoded strips into full planes (sg_extract.place).
void placeCells(int w, int h, int n, const uint8_t* mask, size_t maskLen, const std::vector<uint8_t>& grad,
                const std::vector<uint8_t>& band, ArtPlanes& out) {
    const int W = w * 16, H = h * 16, sw = n * 16;
    out.width = W;
    out.height = H;
    out.band.assign(static_cast<size_t>(W) * H, 0);
    out.grad.assign(static_cast<size_t>(W) * H, 0);
    int k = 0;
    const int cells = w * h;
    for (int j = 0; j < cells; ++j) {
        if (static_cast<size_t>(j >> 3) >= maskLen) break;
        if (!((mask[j >> 3] >> (j & 7)) & 1)) continue;
        if (k >= n) break;
        const int cy = j / w, cx = j % w;
        const size_t base = static_cast<size_t>(cy) * 16 * W + static_cast<size_t>(cx) * 16;
        const int s0 = k * 16;
        for (int row = 0; row < 16; ++row) {
            const size_t s = static_cast<size_t>(row) * sw + s0;
            const size_t p = base + static_cast<size_t>(row) * W;
            std::memcpy(&out.band[p], &band[s], 16);
            std::memcpy(&out.grad[p], &grad[s], 16);
        }
        ++k;
    }
}

namespace {
void place(const ArtRecord& r, const std::vector<uint8_t>& grad, const std::vector<uint8_t>& band, ArtPlanes& out) {
    placeCells(r.w, r.h, r.n, r.mask.data(), r.mask.size(), grad, band, out);
}
}  // namespace

bool rleDecode(const uint8_t* src, size_t srcLen, size_t start, size_t end, size_t limit, std::vector<uint8_t>& out) {
    out.clear();
    if (end > srcLen || start > end) return false;
    size_t i = start;
    while (i < end) {
        const uint8_t c = src[i];
        size_t adv, len;
        if (c < 0x80) {  // literals
            len = static_cast<size_t>(c) + 1;
            adv = 2 + c;
            if (i + adv > end) return false;
            if (out.size() + len > limit) return false;
            out.insert(out.end(), src + i + 1, src + i + 1 + len);
            i += adv;
            continue;
        }
        uint8_t val;
        if (c < 0xFD) {
            len = (c & 0x7F) + 1;
            adv = 2;
            if (i + adv > end) return false;
            val = src[i + 1];
        } else if (c == 0xFD) {
            adv = 3;
            if (i + adv > end) return false;
            len = src[i + 1];
            val = src[i + 2];
        } else if (c == 0xFE) {
            adv = 4;
            if (i + adv > end) return false;
            len = (static_cast<size_t>(src[i + 1]) << 8) | src[i + 2];
            val = src[i + 3];
        } else {
            adv = 5;
            if (i + adv > end) return false;
            len = (static_cast<size_t>(src[i + 1]) << 16) | (static_cast<size_t>(src[i + 2]) << 8) | src[i + 3];
            val = src[i + 4];
        }
        if (out.size() + len > limit) return false;
        out.insert(out.end(), len, val);
        i += adv;
    }
    return i == end;
}

bool ArtFile::load(std::vector<uint8_t> foit, std::vector<uint8_t> art, std::vector<uint8_t> artPt) {
    foit_ = std::move(foit);
    art_ = std::move(art);
    artPt_ = std::move(artPt);
    records_.clear();
    const size_t nf = foit_.size();
    if (nf < 11) return false;

    std::vector<uint8_t> scratch;
    size_t i = 10;
    while (true) {
        const uint8_t* hit = static_cast<const uint8_t*>(std::memchr(foit_.data() + i, 0x41, nf - i));
        if (!hit) break;
        i = static_cast<size_t>(hit - foit_.data());
        size_t nxt = i + 1;
        const uint8_t pre = foit_[i - 2];
        if (foit_[i - 1] == 0 && (pre == 4 || pre == 5) && i + 10 <= nf) {
            const uint32_t cells = rd16(&foit_[i + 1]);
            const uint32_t w = rd16(&foit_[i + 3]);
            const uint32_t h = rd16(&foit_[i + 5]);
            const uint32_t n = rd16(&foit_[i + 7]);
            const uint8_t tag = foit_[i + 9];
            if ((tag & kTagSignatureMask) == kTagColor && w && h && cells == w * h && n > 0 && n <= cells) {
                const size_t ml = (cells + 7) / 8;
                const size_t e = i + 10 + ml;
                if (e + 16 <= nf) {
                    int pc = 0;
                    for (size_t m = 0; m < ml; ++m) pc += popcount8(foit_[i + 10 + m]);
                    if (pc == static_cast<int>(n)) {
                        const uint32_t off = rd32(&foit_[e]);
                        const uint32_t a = rd32(&foit_[e + 4]);
                        const uint32_t b = rd32(&foit_[e + 8]);
                        const uint32_t c = rd32(&foit_[e + 12]);
                        const std::vector<uint8_t>& src = (pre == 5) ? art_ : artPt_;
                        const size_t P = static_cast<size_t>(n) * 256;
                        const uint64_t total = static_cast<uint64_t>(off) + a + b + c;
                        if (!src.empty() && a && b && c && total <= src.size()) {
                            const size_t o = off;
                            bool ok = rleDecode(src.data(), src.size(), o, o + a, P, scratch) && scratch.size() == P;
                            ok = ok && rleDecode(src.data(), src.size(), o + a, o + a + b, P, scratch) && scratch.size() == P;
                            ok = ok && rleDecode(src.data(), src.size(), o + a + b, o + a + b + c, P, scratch) &&
                                 scratch.size() == P;
                            if (ok) {
                                ArtRecord r;
                                r.src = (pre == 5) ? ArtRecord::Src::Art : ArtRecord::Src::ArtPt;
                                r.tag = tag;
                                r.w = static_cast<uint16_t>(w);
                                r.h = static_cast<uint16_t>(h);
                                r.n = static_cast<uint16_t>(n);
                                r.mask.assign(foit_.begin() + i + 10, foit_.begin() + i + 10 + ml);
                                r.A = rd32(&foit_[i - 10]);
                                r.B = rd32(&foit_[i - 6]);
                                r.foitOff = static_cast<uint32_t>(i);
                                r.off = off;
                                r.sizeNormal = a;
                                r.sizeGrad = b;
                                r.sizeBand = c;
                                records_.push_back(std::move(r));
                                nxt = e + 16;
                            }
                        }
                    }
                }
            }
        }
        if (nxt >= nf) break;
        i = nxt;
    }
    return !records_.empty();
}

// Tag bit 6 (0x7A / 0xFA records): the encoder stored only the even rows' tone bits; rows 1,3,..,13 of every 16-row tile hold
// just the line level (bits 7..5).  The game (and the Sprite Workshop decoder) rebuilds the tone of those rows as the average of
// the rows above and below.  Row 15 is stored.  `sw` = strip width in bytes (n*16).
void interpolateOddToneRows(std::vector<uint8_t>& grad, size_t sw) {
    for (size_t y = 1; y < 14; y += 2) {
        uint8_t* row = &grad[y * sw];
        const uint8_t* up = &grad[(y - 1) * sw];
        const uint8_t* dn = &grad[(y + 1) * sw];
        for (size_t x = 0; x < sw; ++x) row[x] = static_cast<uint8_t>((row[x] & 0xE0) | (((up[x] & 31) + (dn[x] & 31)) / 2));
    }
}

bool ArtFile::decode(size_t idx, ArtPlanes& out) const {
    if (idx >= records_.size()) return false;
    const ArtRecord& r = records_[idx];
    const std::vector<uint8_t>& src = (r.src == ArtRecord::Src::Art) ? art_ : artPt_;
    const size_t P = static_cast<size_t>(r.n) * 256;
    std::vector<uint8_t> grad, band;
    const size_t o = r.off;
    if (!rleDecode(src.data(), src.size(), o + r.sizeNormal, o + r.sizeNormal + r.sizeGrad, P, grad) || grad.size() != P)
        return false;
    if (!rleDecode(src.data(), src.size(), o + r.sizeNormal + r.sizeGrad, o + r.compressedSize(), P, band) ||
        band.size() != P)
        return false;
    if (r.flagged()) interpolateOddToneRows(grad, static_cast<size_t>(r.n) * 16);
    place(r, grad, band, out);
    return true;
}

double ArtFile::coverageArt() const {
    double s = 0;
    for (const ArtRecord& r : records_)
        if (r.src == ArtRecord::Src::Art) s += r.compressedSize();
    return s / static_cast<double>(std::max<size_t>(art_.size(), 1));
}

double ArtFile::coveragePt() const {
    if (artPt_.empty()) return 0;
    double s = 0;
    for (const ArtRecord& r : records_)
        if (r.src == ArtRecord::Src::ArtPt) s += r.compressedSize();
    return s / static_cast<double>(artPt_.size());
}

// ---------------------------------------------------------------- palettes
bool readDds(const uint8_t* d, size_t size, Palette& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (size < 128 || std::memcmp(d, "DDS ", 4) != 0) return fail("not a DDS file");
    const uint32_t h = rd32(d + 12), w = rd32(d + 16);
    const uint32_t flags = rd32(d + 80);
    uint32_t bits = rd32(d + 88), rm = rd32(d + 92), gm = rd32(d + 96), bm = rd32(d + 100);
    bool fourccSet = false;
    for (int i = 0; i < 4; ++i)
        if (d[84 + i] != 0) fourccSet = true;  // python: fourcc.strip(b"\0") is non-empty
    if ((flags & 4) && fourccSet) return fail("compressed DDS not supported");
    if (!w || !h || w > 16384 || h > 16384) return fail("bad DDS dimensions");
    if (128 + static_cast<uint64_t>(w) * h * 4 > size) return fail("DDS pixel data truncated");
    if (bits != 32 || !(rm && gm && bm)) {
        rm = 0xFF0000;
        gm = 0xFF00;
        bm = 0xFF;
    }
    auto shift = [](uint32_t m) {
        int s = 0;
        while (!((m >> s) & 1)) ++s;
        return s;
    };
    const int sr = shift(rm), sg = shift(gm), sb = shift(bm);
    out.tones = static_cast<int>(w);
    out.bands = static_cast<int>(h);
    out.rgb.resize(static_cast<size_t>(w) * h * 3);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t p = rd32(d + 128 + (static_cast<size_t>(y) * w + x) * 4);
            uint8_t* o = &out.rgb[(static_cast<size_t>(y) * w + x) * 3];
            o[0] = static_cast<uint8_t>((p & rm) >> sr);
            o[1] = static_cast<uint8_t>((p & gm) >> sg);
            o[2] = static_cast<uint8_t>((p & bm) >> sb);
        }
    return true;
}

namespace {
void hsvToRgb(double h, double s, double v, double& r, double& g, double& b) {  // python colorsys
    if (s == 0.0) { r = g = b = v; return; }
    int i = static_cast<int>(h * 6.0);
    const double f = (h * 6.0) - i;
    const double p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
    i %= 6;
    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}
}  // namespace

Palette fallbackPalette() {
    Palette p;
    p.tones = 16;
    p.bands = 32;
    p.rgb.resize(32 * 16 * 3);
    for (int b = 0; b < 32; ++b) {
        const double hue = std::fmod(b * 0.137, 1.0);
        for (int t = 0; t < 16; ++t) {
            double r, g, bl;
            hsvToRgb(hue, b ? 0.55 : 0.0, 0.25 + 0.75 * t / 15, r, g, bl);
            uint8_t* o = &p.rgb[(static_cast<size_t>(b) * 16 + t) * 3];
            o[0] = static_cast<uint8_t>(static_cast<int>(255 * r));
            o[1] = static_cast<uint8_t>(static_cast<int>(255 * g));
            o[2] = static_cast<uint8_t>(static_cast<int>(255 * bl));
        }
    }
    return p;
}

void keyRgb(const Palette& pal, int band, int g, double out[3]) {
    const int line = g >> 5;
    const double t = (g & 31) / 2.0;
    int t0 = static_cast<int>(t);
    const double fr = t - t0;
    int t1 = std::min(t0 + 1, pal.tones - 1);
    t0 = std::min(t0, pal.tones - 1);
    const int row = std::min(band, pal.bands - 1);
    const double k = 1 - line / 7.0;
    const uint8_t* a = pal.at(row, t0);
    const uint8_t* b = pal.at(row, t1);
    for (int c = 0; c < 3; ++c) out[c] = (a[c] * (1 - fr) + b[c] * fr) * k;
}

std::vector<uint8_t> buildRgbaTables(const Palette& pal) {
    std::vector<uint8_t> t(256u * 4u * 256u, 0);
    auto at = [&](int band, int ch, int g) -> uint8_t& { return t[(static_cast<size_t>(band) * 4 + ch) * 256 + g]; };
    for (int g = 0; g < 256; ++g) at(0, 3, g) = static_cast<uint8_t>((255 * (g >> 5) + 3) / 7);
    const int nb = std::min(pal.bands, 256);
    for (int b = 1; b < nb; ++b)
        for (int g = 0; g < 256; ++g) {
            double c[3];
            keyRgb(pal, b, g, c);
            for (int ch = 0; ch < 3; ++ch) at(b, ch, g) = static_cast<uint8_t>(std::min(255, static_cast<int>(c[ch] + 0.5)));
            at(b, 3, g) = 255;
        }
    const int last = std::max(nb, 1) - 1;
    for (int b = std::max(nb, 1); b < 256; ++b)
        std::memcpy(&t[static_cast<size_t>(b) * 4 * 256], &t[static_cast<size_t>(last) * 4 * 256], 4 * 256);
    return t;
}

std::vector<uint8_t> toRgba(const ArtPlanes& pl, const std::vector<uint8_t>& tables) {
    const size_t n = static_cast<size_t>(pl.width) * pl.height;
    std::vector<uint8_t> out(n * 4);
    for (size_t i = 0; i < n; ++i) {
        const size_t base = static_cast<size_t>(pl.band[i]) * 4 * 256;
        const uint8_t g = pl.grad[i];
        out[i * 4 + 0] = tables[base + 0 * 256 + g];
        out[i * 4 + 1] = tables[base + 1 * 256 + g];
        out[i * 4 + 2] = tables[base + 2 * 256 + g];
        out[i * 4 + 3] = tables[base + 3 * 256 + g];
    }
    return out;
}

// ---------------------------------------------------------------- PNG (stored deflate)
namespace {
uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    be32(png, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    be32(png, crc32(body.data(), body.size()));
}
}  // namespace

std::vector<uint8_t> encodePngRgba(const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + static_cast<size_t>(y) * w * 4, rgba + static_cast<size_t>(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) { s1 = (s1 + b) % 65521; s2 = (s2 + s1) % 65521; }
    do {
        const size_t len = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(len & 0xFF); z.push_back(len >> 8);
        z.push_back(~len & 0xFF); z.push_back((~len >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    } while (pos < raw.size());
    be32(z, (s2 << 16) | s1);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(w));
    be32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

}  // namespace zengine
