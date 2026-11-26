// art_write.cpp - see art_write.h.
#include "art_write.h"

#include <algorithm>
#include <cstring>

namespace zengine {

std::vector<uint8_t> rleEncode(const uint8_t* d, size_t n) {
    std::vector<uint8_t> o;
    size_t i = 0;
    auto flushLit = [&](size_t from, size_t to) {   // literals [from, to), at most 128 per code
        while (from < to) {
            const size_t k = std::min<size_t>(128, to - from);
            o.push_back(static_cast<uint8_t>(k - 1));
            o.insert(o.end(), d + from, d + from + k);
            from += k;
        }
    };
    size_t litStart = 0;
    while (i < n) {
        size_t r = 1;
        while (i + r < n && d[i + r] == d[i] && r < 0xFFFFFF) ++r;
        if (r >= 3) {
            flushLit(litStart, i);
            size_t left = r;
            while (left) {
                const size_t k = std::min<size_t>(left, 0xFFFFFF);
                if (k <= 125) {
                    o.push_back(static_cast<uint8_t>(0x80 | (k - 1)));
                } else if (k <= 255) {
                    o.push_back(0xFD);
                    o.push_back(static_cast<uint8_t>(k));
                } else if (k <= 0xFFFF) {
                    o.push_back(0xFE);
                    o.push_back(static_cast<uint8_t>(k >> 8));
                    o.push_back(static_cast<uint8_t>(k));
                } else {
                    o.push_back(0xFF);
                    o.push_back(static_cast<uint8_t>(k >> 16));
                    o.push_back(static_cast<uint8_t>(k >> 8));
                    o.push_back(static_cast<uint8_t>(k));
                }
                o.push_back(d[i]);
                left -= k;
            }
            i += r;
            litStart = i;
        } else {
            i += r;
        }
    }
    flushLit(litStart, n);
    return o;
}

bool encodeArtImage(int width, int height, const std::vector<uint8_t>& band, const std::vector<uint8_t>& grad, std::vector<uint8_t>& art,
                    foit::Image& out) {
    out = foit::Image();
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    const size_t px = static_cast<size_t>(width) * height;
    if (band.size() != px || grad.size() != px) return false;
    const int cw = (width + 15) / 16, ch = (height + 15) / 16;
    out.w = static_cast<uint16_t>(cw);
    out.h = static_cast<uint16_t>(ch);
    out.cells = static_cast<uint16_t>(cw * ch);
    out.mask.assign((static_cast<size_t>(cw) * ch + 7) / 8, 0);
    out.hasData = true;
    out.external = true;
    out.flags = 0x30;
    std::vector<int> kept;   // cell indices, row-major
    auto cellAt = [&](const std::vector<uint8_t>& pl, int cx, int cy, int row, int col) -> uint8_t {
        const int x = cx * 16 + col, y = cy * 16 + row;
        return (x < width && y < height) ? pl[static_cast<size_t>(y) * width + x] : 0;
    };
    for (int cy = 0; cy < ch; ++cy)
        for (int cx = 0; cx < cw; ++cx) {
            bool any = false;
            for (int row = 0; row < 16 && !any; ++row)
                for (int col = 0; col < 16; ++col)
                    if (cellAt(band, cx, cy, row, col) || cellAt(grad, cx, cy, row, col)) {
                        any = true;
                        break;
                    }
            if (!any) continue;
            const int j = cy * cw + cx;
            out.mask[static_cast<size_t>(j >> 3)] |= static_cast<uint8_t>(1u << (j & 7));
            kept.push_back(j);
        }
    out.n = static_cast<uint16_t>(kept.size());
    if (kept.empty()) return true;   // nothing to draw
    const size_t n = kept.size(), sw = n * 16;
    std::vector<uint8_t> sg(sw * 16), sb(sw * 16), sn(sw * 16, 0);
    for (size_t k = 0; k < n; ++k) {
        const int cx = kept[k] % cw, cy = kept[k] / cw;
        for (int row = 0; row < 16; ++row)
            for (int col = 0; col < 16; ++col) {
                sg[static_cast<size_t>(row) * sw + k * 16 + col] = cellAt(grad, cx, cy, row, col);
                sb[static_cast<size_t>(row) * sw + k * 16 + col] = cellAt(band, cx, cy, row, col);
            }
    }
    const std::vector<uint8_t> en = rleEncode(sn.data(), sn.size()), eg = rleEncode(sg.data(), sg.size()), eb = rleEncode(sb.data(), sb.size());
    out.off = static_cast<uint32_t>(art.size());
    out.size[0] = static_cast<uint32_t>(en.size());
    out.size[1] = static_cast<uint32_t>(eg.size());
    out.size[2] = static_cast<uint32_t>(eb.size());
    art.insert(art.end(), en.begin(), en.end());
    art.insert(art.end(), eg.begin(), eg.end());
    art.insert(art.end(), eb.begin(), eb.end());
    return true;
}

std::vector<uint8_t> makePaletteDds(int tones, int bands, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> o(128 + static_cast<size_t>(tones) * bands * 4, 0);
    auto w32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) o[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    std::memcpy(o.data(), "DDS ", 4);
    w32(4, 124);
    w32(8, 0x100F);   // caps | height | width | pitch | pixelformat
    w32(12, static_cast<uint32_t>(bands));
    w32(16, static_cast<uint32_t>(tones));
    w32(20, static_cast<uint32_t>(tones) * 4);
    w32(76, 32);      // pixel format
    w32(80, 0x41);    // rgb | alpha pixels
    w32(88, 32);
    w32(92, 0x00FF0000);
    w32(96, 0x0000FF00);
    w32(100, 0x000000FF);
    w32(104, 0xFF000000);
    w32(108, 0x1000);
    const size_t n = static_cast<size_t>(tones) * bands;
    for (size_t i = 0; i < n && i * 4 + 3 < rgba.size(); ++i) {
        o[128 + i * 4 + 0] = rgba[i * 4 + 2];   // B
        o[128 + i * 4 + 1] = rgba[i * 4 + 1];   // G
        o[128 + i * 4 + 2] = rgba[i * 4 + 0];   // R
        o[128 + i * 4 + 3] = rgba[i * 4 + 3];   // A
    }
    return o;
}

}  // namespace zengine
