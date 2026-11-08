#include "imgscale.h"

#include <algorithm>
#include <cmath>

namespace zengine {

std::vector<uint8_t> shrinkRgba(const uint8_t* rgba, int w, int h, int factor, int& outW, int& outH) {
    outW = outH = 0;
    if (!rgba || w <= 0 || h <= 0) return {};
    factor = std::max(1, factor);
    if (factor == 1) {
        outW = w;
        outH = h;
        return std::vector<uint8_t>(rgba, rgba + static_cast<size_t>(w) * h * 4);
    }
    outW = (w + factor - 1) / factor;
    outH = (h + factor - 1) / factor;
    std::vector<uint8_t> out(static_cast<size_t>(outW) * outH * 4);
    for (int oy = 0; oy < outH; ++oy) {
        const int y0 = oy * factor, y1 = std::min(h, y0 + factor);
        for (int ox = 0; ox < outW; ++ox) {
            const int x0 = ox * factor, x1 = std::min(w, x0 + factor);
            double sr = 0, sg = 0, sb = 0, sa = 0;
            int n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
                    sr += static_cast<double>(p[0]) * p[3];
                    sg += static_cast<double>(p[1]) * p[3];
                    sb += static_cast<double>(p[2]) * p[3];
                    sa += p[3];
                    ++n;
                }
            uint8_t* o = out.data() + (static_cast<size_t>(oy) * outW + ox) * 4;
            if (sa <= 0 || n == 0) {
                o[0] = o[1] = o[2] = o[3] = 0;
                continue;
            }
            o[0] = static_cast<uint8_t>(std::lround(sr / sa));
            o[1] = static_cast<uint8_t>(std::lround(sg / sa));
            o[2] = static_cast<uint8_t>(std::lround(sb / sa));
            o[3] = static_cast<uint8_t>(std::lround(sa / n));
        }
    }
    return out;
}

namespace {
// Catmull-Rom weights for the two output phases of a 2x enlargement: the sample sits 3/4 and 1/4 of the way between the source
// pixels (offsets -1, 0, +1, +2 from the pixel on the left of the pair).
struct Kernel {
    float w[2][4];
    Kernel() {
        const float ts[2] = {0.75f, 0.25f};
        for (int k = 0; k < 2; ++k) {
            const float t = ts[k], t2 = t * t, t3 = t2 * t;
            w[k][0] = -0.5f * t3 + t2 - 0.5f * t;
            w[k][1] = 1.5f * t3 - 2.5f * t2 + 1.0f;
            w[k][2] = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
            w[k][3] = 0.5f * t3 - 0.5f * t2;
        }
    }
};
inline uint8_t clampByte(float v) { return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255)); }
}  // namespace

std::vector<uint8_t> upscaleCubic2x(const uint8_t* rgba, int w, int h, int64_t maxPixels) {
    if (!rgba || w <= 0 || h <= 0) return {};
    if (static_cast<int64_t>(w) * 2 * h * 2 > maxPixels) return {};
    static const Kernel K;
    const int W = w * 2, H = h * 2;
    // premultiplied floats, 4 per pixel
    std::vector<float> pm(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i) {
        const float a = rgba[i * 4 + 3] / 255.0f;
        pm[i * 4 + 0] = rgba[i * 4 + 0] * a;
        pm[i * 4 + 1] = rgba[i * 4 + 1] * a;
        pm[i * 4 + 2] = rgba[i * 4 + 2] * a;
        pm[i * 4 + 3] = rgba[i * 4 + 3];
    }
    // horizontal pass: w -> 2w
    std::vector<float> hz(static_cast<size_t>(W) * h * 4);
    for (int y = 0; y < h; ++y) {
        const float* row = pm.data() + static_cast<size_t>(y) * w * 4;
        float* orow = hz.data() + static_cast<size_t>(y) * W * 4;
        for (int x = 0; x < w; ++x) {
            for (int phase = 0; phase < 2; ++phase) {
                // phase 0: output 2x sits at x - 0.25 (between x-1 and x, t = 0.75); phase 1: at x + 0.25 (between x and x+1, t = 0.25)
                const int base = phase == 0 ? x - 1 : x;
                const float* wt = K.w[phase];
                float acc[4] = {0, 0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    const int sx = std::clamp(base - 1 + k, 0, w - 1);
                    // base-1+k covers base-1 .. base+2; the weights are ordered the same way
                    for (int c = 0; c < 4; ++c) acc[c] += wt[k] * row[sx * 4 + c];
                }
                float* o = orow + (static_cast<size_t>(x) * 2 + phase) * 4;
                for (int c = 0; c < 4; ++c) o[c] = acc[c];
            }
        }
    }
    // vertical pass: h -> 2h, straight into bytes (un-premultiply)
    std::vector<uint8_t> out(static_cast<size_t>(W) * H * 4);
    for (int y = 0; y < h; ++y) {
        for (int phase = 0; phase < 2; ++phase) {
            const int base = phase == 0 ? y - 1 : y;
            const float* wt = K.w[phase];
            const float* rows[4];
            for (int k = 0; k < 4; ++k) rows[k] = hz.data() + static_cast<size_t>(std::clamp(base - 1 + k, 0, h - 1)) * W * 4;
            uint8_t* orow = out.data() + (static_cast<size_t>(y) * 2 + phase) * W * 4;
            for (int x = 0; x < W; ++x) {
                float v[4];
                for (int c = 0; c < 4; ++c) v[c] = wt[0] * rows[0][x * 4 + c] + wt[1] * rows[1][x * 4 + c] + wt[2] * rows[2][x * 4 + c] + wt[3] * rows[3][x * 4 + c];
                const float a = std::clamp(v[3], 0.0f, 255.0f);
                uint8_t* o = orow + static_cast<size_t>(x) * 4;
                o[3] = clampByte(a);
                if (a < 0.5f) {
                    o[0] = o[1] = o[2] = 0;
                } else {
                    const float inv = 255.0f / a;
                    o[0] = clampByte(v[0] * inv);
                    o[1] = clampByte(v[1] * inv);
                    o[2] = clampByte(v[2] * inv);
                }
            }
        }
    }
    return out;
}

void sharpenRgba(uint8_t* rgba, int w, int h, float amount) {
    if (!rgba || w < 3 || h < 3 || amount <= 0.0f) return;
    const size_t n = static_cast<size_t>(w) * h;
    // separable [1 2 1]/4 blur of the colour channels
    std::vector<float> tmp(n * 3), blur(n * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int xl = std::max(0, x - 1), xr = std::min(w - 1, x + 1);
            for (int c = 0; c < 3; ++c)
                tmp[(static_cast<size_t>(y) * w + x) * 3 + c] =
                    0.25f * rgba[(static_cast<size_t>(y) * w + xl) * 4 + c] + 0.5f * rgba[(static_cast<size_t>(y) * w + x) * 4 + c] +
                    0.25f * rgba[(static_cast<size_t>(y) * w + xr) * 4 + c];
        }
    for (int y = 0; y < h; ++y) {
        const int yu = std::max(0, y - 1), yd = std::min(h - 1, y + 1);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                blur[(static_cast<size_t>(y) * w + x) * 3 + c] = 0.25f * tmp[(static_cast<size_t>(yu) * w + x) * 3 + c] +
                                                                  0.5f * tmp[(static_cast<size_t>(y) * w + x) * 3 + c] +
                                                                  0.25f * tmp[(static_cast<size_t>(yd) * w + x) * 3 + c];
    }
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c) {
            const float v = rgba[i * 4 + c];
            rgba[i * 4 + c] = clampByte(v + amount * (v - blur[i * 3 + c]));
        }
}

void bleedTransparent(uint8_t* rgba, int w, int h, int passes) {
    if (!rgba || w <= 0 || h <= 0) return;
    std::vector<uint8_t> src;
    for (int pass = 0; pass < passes; ++pass) {
        src.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
        bool any = false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
                if (p[3] != 0) continue;
                int r = 0, g = 0, b = 0, n = 0;
                static const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const uint8_t* q = &src[(static_cast<size_t>(ny) * w + nx) * 4];
                    // a neighbour counts when it is visible or was already coloured by an earlier pass
                    if (q[3] != 0 || q[0] || q[1] || q[2]) {
                        r += q[0];
                        g += q[1];
                        b += q[2];
                        ++n;
                    }
                }
                if (n) {
                    p[0] = static_cast<uint8_t>(r / n);
                    p[1] = static_cast<uint8_t>(g / n);
                    p[2] = static_cast<uint8_t>(b / n);
                    any = true;
                }
            }
        if (!any) break;
    }
}

bool unrotateArgb(uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    const size_t n = static_cast<size_t>(w) * h;
    size_t blue255 = 0;
    for (size_t i = 0; i < n; ++i) blue255 += rgba[i * 4 + 2] == 255;
    if (blue255 * 1000 < n * 999) return false;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* p = rgba + i * 4;
        const uint8_t r = p[0], g = p[1], a = p[3];   // stored: r = G, g = B, b = 255, a = R
        p[0] = a;
        p[1] = r;
        p[2] = g;
        p[3] = 255;
    }
    return true;
}

int spriteLevelFor(double pxPerArtPx, int minLevel) {
    int lv = 0;
    // a half-size texture still has >= 1 texel per screen pixel while pxPerArtPx <= 0.5, and so on
    while (lv < 3 && pxPerArtPx > 0.0 && pxPerArtPx <= 1.0 / static_cast<double>(1 << (lv + 1))) ++lv;
    return std::clamp(std::max(lv, minLevel), 0, 3);
}

}  // namespace zengine
