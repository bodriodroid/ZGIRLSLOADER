#include "shadow_layer.h"

#include <algorithm>
#include <cmath>

namespace zengine {

namespace {
constexpr int kMaxSide = 2048;   // a layer larger than this is a mistake (a shadow is a few hundred pixels across)

Uint8 byteOf(float a) { return static_cast<Uint8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f)); }
}  // namespace

void ShadowLayer::release(bool rendererAlive) {
    if (tex_ && rendererAlive) SDL_DestroyTexture(tex_);
    tex_ = nullptr;
    w_ = h_ = 0;
    failed_ = false;
}

int ShadowLayer::draw(SDL_Renderer* r, const std::vector<Piece>& pieces, const std::vector<Stop>& stops, float resolution) {
    if (!r || pieces.empty() || stops.size() < 2 || failed_) return -1;
    if (!(resolution > 0.0f) || resolution > 1.0f) resolution = 0.5f;
    {   // the enlargement is only soft where the renderer filters it (the GPU ones); the software renderer would show 2x2 blocks
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(r, &info) == 0 && (info.flags & SDL_RENDERER_SOFTWARE)) resolution = 1.0f;
    }
    // the area the silhouettes cover
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
    for (const Piece& p : pieces) {
        if (!p.tex) return -1;
        const float v[4] = {p.x0, p.x1, p.yTop, p.yBot};
        for (float f : v)
            if (!std::isfinite(f)) return -1;
        bx0 = std::min({bx0, p.x0, p.x1});
        bx1 = std::max({bx1, p.x0, p.x1});
        by0 = std::min({by0, p.yTop, p.yBot});
        by1 = std::max({by1, p.yTop, p.yBot});
    }
    for (const Stop& s : stops)
        if (!std::isfinite(s.y) || !std::isfinite(s.alpha)) return -1;
    bx0 = std::floor(bx0) - 2;
    by0 = std::floor(by0) - 2;
    bx1 = std::ceil(bx1) + 2;
    by1 = std::ceil(by1) + 2;
    const float areaW = bx1 - bx0, areaH = by1 - by0;
    if (!(areaW > 0) || !(areaH > 0)) return -1;
    const int lw = static_cast<int>(std::ceil(areaW * resolution)), lh = static_cast<int>(std::ceil(areaH * resolution));
    if (lw > kMaxSide || lh > kMaxSide) return -1;

    // a layer at least that big (rounded up, so that a fighter's changing frames do not recreate it every tick)
    if (!tex_ || w_ < lw || h_ < lh) {
        const int nw = std::max(w_, (lw + 63) / 64 * 64), nh = std::max(h_, (lh + 63) / 64 * 64);
        SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, nw, nh);
        if (!t) {
            failed_ = true;
            return -1;
        }
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        if (tex_) SDL_DestroyTexture(tex_);
        tex_ = t;
        w_ = nw;
        h_ = nh;
    }

    SDL_Texture* const prev = SDL_GetRenderTarget(r);
    if (SDL_SetRenderTarget(r, tex_) != 0) {
        failed_ = true;
        return -1;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    // 1. every silhouette, opaque black, into the layer.  SDL's software renderer treats four equal vertex colours as a plain modulation of the
    // texture and leaves it set (the next draw of that texture would come out black): one step of difference keeps it on the per-vertex path.
    int drawn = 0;
    static const int quad[6] = {0, 1, 2, 2, 1, 3};
    for (const Piece& p : pieces) {
        SDL_Vertex v[4];
        const float xa = (p.x0 - bx0) * resolution, xb = (p.x1 - bx0) * resolution;
        const float ya = (p.yTop - by0) * resolution, yb = (p.yBot - by0) * resolution;
        v[0].position = {xa, ya};
        v[1].position = {xb, ya};
        v[2].position = {xa, yb};
        v[3].position = {xb, yb};
        v[0].tex_coord = {p.u0, 0.0f};
        v[1].tex_coord = {p.u1, 0.0f};
        v[2].tex_coord = {p.u0, 1.0f};
        v[3].tex_coord = {p.u1, 1.0f};
        for (int i = 0; i < 4; ++i) v[i].color = SDL_Color{0, 0, 0, static_cast<Uint8>(i < 2 ? 254 : 255)};
        if (SDL_RenderGeometry(r, p.tex, v, 4, quad, 6) == 0) ++drawn;
    }
    SDL_SetRenderTarget(r, prev);
    if (drawn == 0) return 0;

    // 2. the layer over the picture, once: rows of vertices carry the opacity (linear between the stops, constant beyond the first / last one), the
    // texture is the layer's used corner
    auto alphaAt = [&](float y) {
        if (y <= stops.front().y) return stops.front().alpha;
        for (size_t k = 1; k < stops.size(); ++k) {
            if (y <= stops[k].y) {
                const float span = stops[k].y - stops[k - 1].y;
                const float t = span > 1e-6f ? (y - stops[k - 1].y) / span : 1.0f;
                return stops[k - 1].alpha + (stops[k].alpha - stops[k - 1].alpha) * t;
            }
        }
        return stops.back().alpha;
    };
    std::vector<float> rows{by0};
    for (const Stop& s : stops)
        if (s.y > by0 + 0.01f && s.y < by1 - 0.01f) rows.push_back(s.y);
    rows.push_back(by1);
    std::sort(rows.begin(), rows.end());
    std::vector<SDL_Vertex> vs;
    std::vector<int> ix;
    vs.reserve(rows.size() * 2);
    const float tw = static_cast<float>(w_), th = static_cast<float>(h_);
    Uint8 prevA = 0;
    for (size_t k = 0; k < rows.size(); ++k) {
        const float y = rows[k];
        Uint8 a = byteOf(alphaAt(y));
        if (k > 0 && a == prevA) a = a > 0 ? static_cast<Uint8>(a - 1) : static_cast<Uint8>(a + 1);   // never four equal colours (see above)
        prevA = a;
        const float tv = (y - by0) * resolution / th;
        SDL_Vertex l, rt;
        l.position = {bx0, y};
        rt.position = {bx1, y};
        l.tex_coord = {0.0f, tv};
        rt.tex_coord = {areaW * resolution / tw, tv};
        l.color = rt.color = SDL_Color{255, 255, 255, a};
        vs.push_back(l);
        vs.push_back(rt);
        if (k > 0) {
            const int b0 = static_cast<int>((k - 1) * 2);
            ix.insert(ix.end(), {b0, b0 + 1, b0 + 2, b0 + 2, b0 + 1, b0 + 3});
        }
    }
    SDL_SetTextureColorMod(tex_, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex_, 255);
    SDL_RenderGeometry(r, tex_, vs.data(), static_cast<int>(vs.size()), ix.data(), static_cast<int>(ix.size()));
    return drawn;
}

}  // namespace zengine
