#include "bar_fill.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace zengine {

BarFill::Layout BarFill::layout(int outW, int outH, int lw, int lh) {
    Layout l;
    if (outW <= 0 || outH <= 0 || lw <= 0 || lh <= 0) return l;
    l.picH = outH;
    l.picW = std::max(1, static_cast<int>(std::lround(static_cast<double>(outH) * lw / lh)));
    // less than ~3 % of the width as bars is not worth the extra pass
    if (static_cast<double>(outW) < l.picW * 1.03) {
        l.picW = outW;
        return l;
    }
    l.wanted = true;
    l.x = (outW - l.picW) / 2;
    l.visible = std::clamp(static_cast<double>(outH) / outW * lw / lh, 0.05, 1.0);
    return l;
}

void BarFill::release() {
    if (owner_) {   // (a destroyed renderer took its textures with it; the owner calls release() while it is alive)
        if (pic_) SDL_DestroyTexture(pic_);
        if (mid_) SDL_DestroyTexture(mid_);
        if (small_) SDL_DestroyTexture(small_);
    }
    pic_ = mid_ = small_ = nullptr;
    picW_ = picH_ = 0;
    owner_ = nullptr;
    active_ = false;
}

bool BarFill::begin(SDL_Renderer* r, int lw, int lh) {
    active_ = false;
    if (!r) return false;
    if (SDL_GetRenderTarget(r) != nullptr) return false;   // already drawing somewhere else
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(r, &ow, &oh);
    lay_ = layout(ow, oh, lw, lh);
    if (std::getenv("ZDROID_BARFILL_LOG")) std::fprintf(stderr, "[barfill] output %dx%d logical %dx%d -> %s (picture %dx%d at x=%d)\n", ow, oh, lw, lh, lay_.wanted ? "bars filled" : "no bars", lay_.picW, lay_.picH, lay_.x);
    if (!lay_.wanted) {
        if (pic_) release();
        return false;
    }
    if (!pic_ || picW_ != lay_.picW || picH_ != lay_.picH || owner_ != r) {
        if (pic_) release();
        pic_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, lay_.picW, lay_.picH);
        mid_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, std::max(8, lay_.picW / 4), std::max(8, lay_.picH / 4));
        small_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, std::max(8, lay_.picW / 16), std::max(8, lay_.picH / 16));
        if (!pic_ || !mid_ || !small_) {   // no target textures here: draw the old way (black bars)
            owner_ = r;
            release();
            return false;
        }
        for (SDL_Texture* t : {pic_, mid_, small_}) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        picW_ = lay_.picW;
        picH_ = lay_.picH;
        owner_ = r;
    }
    lw_ = lw;
    lh_ = lh;
    if (SDL_SetRenderTarget(r, pic_) != 0) return false;
    SDL_RenderSetScale(r, static_cast<float>(picW_) / static_cast<float>(lw), static_cast<float>(picH_) / static_cast<float>(lh));   // keep drawing in logical units
    active_ = true;
    return true;
}

void BarFill::end(SDL_Renderer* r) {
    if (!active_ || !r || !pic_) return;
    active_ = false;
    // the blurred copy: picture -> 1/4 -> 1/16 (each step is a linear shrink, so the result is a soft smear of the whole picture)
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_SetTextureColorMod(pic_, 255, 255, 255);
    SDL_SetRenderTarget(r, mid_);
    SDL_RenderCopy(r, pic_, nullptr, nullptr);
    SDL_SetRenderTarget(r, small_);
    SDL_RenderCopy(r, mid_, nullptr, nullptr);
    SDL_SetRenderTarget(r, nullptr);   // the window again (SDL brings its logical size back)
    // the window in plain pixels
    SDL_RenderSetLogicalSize(r, 0, 0);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(r, &ow, &oh);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    int sw = 0, sh = 0;
    SDL_QueryTexture(small_, nullptr, nullptr, &sw, &sh);
    const double vis = lay_.visible;
    const SDL_Rect src{0, static_cast<int>(std::lround(sh * (1.0 - vis) / 2.0)), sw, std::max(1, static_cast<int>(std::lround(sh * vis)))};
    SDL_SetTextureColorMod(small_, 120, 120, 120);   // darker than the picture: the sides are a frame, not content
    SDL_RenderCopy(r, small_, &src, nullptr);
    SDL_SetTextureColorMod(small_, 255, 255, 255);
    const SDL_Rect mid{lay_.x, 0, picW_, picH_};
    SDL_RenderCopy(r, pic_, nullptr, &mid);
    SDL_RenderSetLogicalSize(r, lw_, lh_);
}

}  // namespace zengine
