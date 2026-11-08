// sg_loader core - .dds decoding (see dds.h). Pure C++17, no platform dependencies.
#include "dds.h"

#include <algorithm>
#include <cstring>

namespace zengine {
namespace {

constexpr uint32_t kMagic = 0x20534444u;  // 'DDS '
constexpr uint32_t kDdpfFourcc = 0x4u;
constexpr uint32_t kDdpfRgb = 0x40u;
constexpr uint32_t kDdpfLuminance = 0x20000u;
constexpr uint32_t kDdpfAlphaPixels = 0x1u;  // DDPF_ALPHAPIXELS: the format has an alpha channel (with RGB: 0x41)
constexpr uint32_t kDdpfAlpha = 0x2u;        // DDPF_ALPHA: alpha-only surface
constexpr uint32_t kCapsComplex = 0x8u;
constexpr uint32_t kCapsMipmap = 0x400000u;
constexpr uint32_t kCaps2Cube = 0x200u;

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

DdsFormat fourccFormat(uint32_t cc) {
    switch (cc) {
        case 0x31545844u: return DdsFormat::DXT1;  // 'DXT1'
        case 0x33545844u: return DdsFormat::DXT3;
        case 0x35545844u: return DdsFormat::DXT5;
        case 0x32545844u: return DdsFormat::DXT3;  // DXT2 = DXT3 with premultiplied alpha
        case 0x34545844u: return DdsFormat::DXT5;  // DXT4 = DXT5 with premultiplied alpha
        default: return DdsFormat::Unknown;
    }
}

int blockBytes(DdsFormat f) { return f == DdsFormat::DXT1 ? 8 : 16; }

// Bytes of one mip level (block-compressed formats count whole 4x4 blocks).
size_t levelSize(DdsFormat f, int w, int h) {
    if (f == DdsFormat::DXT1 || f == DdsFormat::DXT3 || f == DdsFormat::DXT5) {
        const int bw = (w + 3) / 4, bh = (h + 3) / 4;
        return static_cast<size_t>(bw) * bh * blockBytes(f);
    }
    int bits = 32;
    switch (f) {
        case DdsFormat::RGB565: bits = 16; break;
        case DdsFormat::BGR8: bits = 24; break;
        case DdsFormat::L8:
        case DdsFormat::A8: bits = 8; break;
        default: bits = 32; break;
    }
    return static_cast<size_t>(((static_cast<size_t>(w) * bits + 7) / 8)) * h;
}

void unpack565(uint16_t v, uint8_t* rgb) {
    rgb[0] = static_cast<uint8_t>((v >> 8) & 0xF8);
    rgb[1] = static_cast<uint8_t>((v >> 3) & 0xFC);
    rgb[2] = static_cast<uint8_t>((v << 3) & 0xF8);
    // bit-expand so 0x1F -> 0xFF (same trick the 5/6-bit channels need)
    rgb[0] |= rgb[0] >> 5;
    rgb[1] |= rgb[1] >> 6;
    rgb[2] |= rgb[2] >> 5;
}

// One 4x4 block -> 16 RGBA pixels, written into a width*4-pitch row buffer at (bx,by).
void decodeBlock(const uint8_t* src, DdsFormat fmt, uint8_t* dst, int dstW, int dstH, int bx, int by) {
    uint8_t pal[4][4];
    const int off = (fmt == DdsFormat::DXT1) ? 0 : 8;  // DXT3/5: 8 alpha bytes first
    const uint16_t c0 = rd16(src + off), c1 = rd16(src + off + 2);
    unpack565(c0, pal[0]);
    unpack565(c1, pal[1]);
    const bool alphaBlocks = (fmt == DdsFormat::DXT3 || fmt == DdsFormat::DXT5);
    if (!alphaBlocks && c0 <= c1) {
        // 3-colour DXT1 block: the 4th entry is *transparent black*, not a blend.
        for (int i = 0; i < 3; ++i)
            pal[2][i] = static_cast<uint8_t>((static_cast<int>(pal[0][i]) * 2 + pal[1][i]) / 3);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    } else {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = static_cast<uint8_t>((static_cast<int>(pal[0][i]) * 2 + pal[1][i]) / 3);
            pal[3][i] = static_cast<uint8_t>((static_cast<int>(pal[0][i]) + pal[1][i] * 2) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    }
    uint32_t idx = 0;
    for (int i = 0; i < 4; ++i) idx |= static_cast<uint32_t>(src[off + 4 + i]) << (8 * i);

    uint64_t alpha = 0;
    uint8_t aPal[8];
    if (fmt == DdsFormat::DXT5) {
        aPal[0] = src[0];
        aPal[1] = src[1];
        if (aPal[0] > aPal[1]) {
            // 8 alphas: a0, a1, then (6a0+a1)/7 ... (a0+6a1)/7
            for (int i = 0; i < 6; ++i) aPal[2 + i] = static_cast<uint8_t>(((6 - i) * aPal[0] + (i + 1) * aPal[1]) / 7);
        } else {
            // 6 alphas: a0, a1, (4a0+a1)/5 ... (a0+4a1)/5, then 0 and 255
            for (int i = 0; i < 4; ++i) aPal[2 + i] = static_cast<uint8_t>(((4 - i) * aPal[0] + (i + 1) * aPal[1]) / 5);
            aPal[6] = 0;
            aPal[7] = 255;
        }
        for (int i = 0; i < 6; ++i) alpha |= static_cast<uint64_t>(src[2 + i]) << (8 * i);
    }
    // DXT3 alpha: 8 bytes = one 4-bit nibble per pixel, row-major (pixel y*4+x -> nibble y*4+x).

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int px = bx + x, py = by + y;
            if (px >= dstW || py >= dstH) continue;  // the last block row/column of a non-multiple-of-4 image is partial
            uint8_t* d = dst + (static_cast<size_t>(py) * dstW + px) * 4;
            const int sel = static_cast<int>((idx >> (2 * (y * 4 + x))) & 3);
            d[0] = pal[sel][0];
            d[1] = pal[sel][1];
            d[2] = pal[sel][2];
            if (fmt == DdsFormat::DXT1) {
                d[3] = (!alphaBlocks && c0 <= c1 && sel == 3) ? 0 : 255;
            } else if (fmt == DdsFormat::DXT5) {
                const int ai = static_cast<int>((alpha >> (3 * (y * 4 + x))) & 7);
                d[3] = aPal[ai];
            } else {  // DXT3
                const int nibble = y * 4 + x;
                const uint8_t b = src[nibble / 2];
                d[3] = static_cast<uint8_t>(((nibble & 1) ? (b >> 4) : (b & 0xF)) * 17);
            }
        }
    }
}

void decodeUncompressed(const uint8_t* src, DdsFormat fmt, uint8_t* dst, int w, int h) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* d = dst + (static_cast<size_t>(y) * w + x) * 4;
            switch (fmt) {
                case DdsFormat::BGRA8: {
                    const uint8_t* s = src + (static_cast<size_t>(y) * w + x) * 4;
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = s[3];
                    break;
                }
                case DdsFormat::RGBA8: {
                    const uint8_t* s = src + (static_cast<size_t>(y) * w + x) * 4;
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    d[3] = s[3];
                    break;
                }
                case DdsFormat::BGR8: {
                    const uint8_t* s = src + (static_cast<size_t>(y) * w + x) * 3;
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = 255;
                    break;
                }
                case DdsFormat::RGB565: {
                    const uint8_t* s = src + (static_cast<size_t>(y) * w + x) * 2;
                    unpack565(rd16(s), d);
                    d[3] = 255;
                    break;
                }
                case DdsFormat::L8: {
                    const uint8_t v = src[static_cast<size_t>(y) * w + x];
                    d[0] = d[1] = d[2] = v;
                    d[3] = 255;
                    break;
                }
                case DdsFormat::A8: {
                    const uint8_t v = src[static_cast<size_t>(y) * w + x];
                    d[0] = d[1] = d[2] = 255;
                    d[3] = v;
                    break;
                }
                default: d[0] = d[1] = d[2] = d[3] = 0; break;
            }
        }
    }
}

}  // namespace

const char* DdsInfo::formatName() const {
    switch (format) {
        case DdsFormat::DXT1: return "DXT1";
        case DdsFormat::DXT3: return "DXT3";
        case DdsFormat::DXT5: return "DXT5";
        case DdsFormat::BGRA8: return "BGRA8";
        case DdsFormat::RGBA8: return "RGBA8";
        case DdsFormat::BGR8: return "BGR8";
        case DdsFormat::RGB565: return "RGB565";
        case DdsFormat::L8: return "L8";
        case DdsFormat::A8: return "A8";
        default: return "unknown";
    }
}

bool ddsHeader(const uint8_t* data, size_t size, DdsInfo& out, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (!data || size < 128) return fail("not a DDS file (too small)");
    if (rd32(data) != kMagic) return fail("not a DDS file (bad magic)");
    if (rd32(data + 4) != 124) return fail("bad DDS header size");
    const uint32_t flags = rd32(data + 8);
    const uint32_t h = rd32(data + 12), w = rd32(data + 16);
    const uint32_t mips = rd32(data + 28);
    const uint32_t caps = rd32(data + 108), caps2 = rd32(data + 112);
    const uint32_t pfSize = rd32(data + 76), pfFlags = rd32(data + 80), fourcc = rd32(data + 84);
    const uint32_t bpp = rd32(data + 88);
    const uint32_t rMask = rd32(data + 92), gMask = rd32(data + 96), bMask = rd32(data + 100), aMask = rd32(data + 104);
    if (w == 0 || h == 0) return fail("empty image");
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.mipLevels = (caps & kCapsMipmap) && (caps & kCapsComplex) && mips ? static_cast<int>(mips) : 1;
    out.cubeMap = (caps2 & kCaps2Cube) != 0;

    if (pfFlags & kDdpfFourcc) {
        out.format = fourccFormat(fourcc);
        if (out.format == DdsFormat::Unknown) return fail("unsupported fourcc");
        return true;
    }
    if (pfFlags & kDdpfLuminance) {
        out.format = DdsFormat::L8;
        if (bpp != 8 && bpp != 16) return fail("unsupported luminance layout");
        return true;
    }
    if ((pfFlags & kDdpfAlpha) || ((pfFlags & kDdpfAlphaPixels) && !(pfFlags & kDdpfRgb))) {  // alpha only (not RGB + alpha, that is 0x41)
        if (bpp == 8) {
            out.format = DdsFormat::A8;
            return true;
        }
        return fail("unsupported alpha-only layout");
    }
    if (pfFlags & kDdpfRgb) {
        if (bpp == 32) {
            out.format = (rMask == 0xFF) ? DdsFormat::RGBA8 : DdsFormat::BGRA8;
            (void)aMask;
            return true;
        }
        if (bpp == 24) {
            out.format = DdsFormat::BGR8;
            return true;
        }
        if (bpp == 16) {
            out.format = DdsFormat::RGB565;
            return true;
        }
        return fail("unsupported RGB bit count");
    }
    (void)flags;
    (void)pfSize;
    (void)gMask;
    (void)bMask;
    return fail("unknown pixel format");
}

bool decodeDds(const std::vector<uint8_t>& data, DdsImage& out, std::string* err, int maxSize) {
    DdsInfo info;
    if (!ddsHeader(data.data(), data.size(), info, err)) return false;
    static_cast<DdsInfo&>(out) = info;

    // Pick the mip level to decode: mip 0 unless maxSize asks for a cheaper preview, in which
    // case we go down the chain while the level is still at least maxSize big.
    int level = 0, lw = info.width, lh = info.height;
    size_t offset = 128;
    // Without a maxSize the caller wants the full-resolution level 0.
    for (int l = 1; l < info.mipLevels && maxSize > 0; ++l) {
        const int w = std::max(1, info.width >> l), h = std::max(1, info.height >> l);
        if (maxSize > 0 && std::max(w, h) < maxSize) break;  // this level is already too small
        offset += levelSize(info.format, lw, lh);
        lw = w;
        lh = h;
        level = l;
    }
    const size_t need = levelSize(info.format, lw, lh);
    if (offset + need > data.size()) {
        if (err) *err = "file is truncated";
        return false;
    }
    out.width = lw;
    out.height = lh;
    out.rgba.assign(static_cast<size_t>(lw) * lh * 4, 0);

    const uint8_t* src = data.data() + offset;
    if (info.format == DdsFormat::DXT1 || info.format == DdsFormat::DXT3 || info.format == DdsFormat::DXT5) {
        const int bw = (lw + 3) / 4, bh = (lh + 3) / 4;
        for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx)
                decodeBlock(src + (static_cast<size_t>(by) * bw + bx) * blockBytes(info.format), info.format,
                            out.rgba.data(), lw, lh, bx * 4, by * 4);
    } else {
        decodeUncompressed(src, info.format, out.rgba.data(), lw, lh);
    }
    (void)level;
    return true;
}

std::vector<uint8_t> resampleRgba(const uint8_t* rgba, int w, int h, int nw, int nh) {
    if (nw <= 0 || nh <= 0 || w <= 0 || h <= 0) return {};
    if (nw == w && nh == h) return std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(w) * h * 4);
    std::vector<uint8_t> out(static_cast<size_t>(nw) * nh * 4, 0);
    for (int y = 0; y < nh; ++y) {
        const int y0 = y * h / nh, y1 = std::max(y0 + 1, (y + 1) * h / nh);
        for (int x = 0; x < nw; ++x) {
            const int x0 = x * w / nw, x1 = std::max(x0 + 1, (x + 1) * w / nw);
            uint32_t s[4] = {0, 0, 0, 0};
            uint32_t n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t* row = rgba + (static_cast<size_t>(yy) * w) * 4;
                for (int xx = x0; xx < x1; ++xx) {
                    const uint8_t* p = row + static_cast<size_t>(xx) * 4;
                    s[0] += p[0];
                    s[1] += p[1];
                    s[2] += p[2];
                    s[3] += p[3];
                    ++n;
                }
            }
            uint8_t* d = out.data() + (static_cast<size_t>(y) * nw + x) * 4;
            for (int c = 0; c < 4; ++c) d[c] = static_cast<uint8_t>(s[c] / std::max<uint32_t>(1, n));
        }
    }
    return out;
}

void fitInto(int w, int h, int boxW, int boxH, int& outW, int& outH) {
    if (w <= 0 || h <= 0 || boxW <= 0 || boxH <= 0) {
        outW = outH = 0;
        return;
    }
    double s = std::min(static_cast<double>(boxW) / w, static_cast<double>(boxH) / h);
    if (s > 1.0) s = 1.0;  // never upscale for previews
    outW = std::max(1, static_cast<int>(w * s));
    outH = std::max(1, static_cast<int>(h * s));
}

}  // namespace zengine
