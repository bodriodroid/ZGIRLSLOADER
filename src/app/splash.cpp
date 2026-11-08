// splash.cpp - see splash.h
#include "splash.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "dds.h"

namespace zengine {

namespace {
constexpr int kBombFrameMs = 110;     // 9 frames: the "LOADING..." text flickers, the bomb sways
constexpr int kSlideMs = 1300;        // one line drawing
constexpr int kSlideFadeMs = 450;     // cross-fade between two drawings
constexpr float kSlideAlpha = 1.0f;   // the PC game draws the line art at full brightness
constexpr float kSlideScale = 1.5f;   // PC: the 688x336 drawing fills ~1030x505 of the 1280x720 canvas
constexpr float kSlideX = 120.0f, kSlideY = 14.0f;   // top-left of the scaled drawing (measured on the PC build)
constexpr int kSlideDelayMs = 650;    // first the bomb and "LOADING..." alone on black, then the first drawing comes up
constexpr float kBombX = 758.0f, kBombY = 525.0f;    // top-left of the 512x240 "LOADING..." + bomb frame (lower right)
}  // namespace

void LoadingSplash::reset() {
    for (SDL_Texture* t : tex_)
        if (t) SDL_DestroyTexture(t);
    tex_.clear();
    atlas_.clear();
    atlas_.shrink_to_fit();
    spr_ = SprTiles{};
    ok_ = false;
    name_.clear();
}

bool LoadingSplash::load(SDL_Renderer* r, const Vfs& vfs, const std::string& lang) {
    reset();
    r_ = r;
    std::vector<std::string> cands;
    if (!lang.empty() && lang != "jp") cands.push_back("loading_" + lang);
    cands.push_back("loading_en");
    for (const std::string& stem : cands) {
        std::vector<uint8_t> msb, dds;
        if (!vfs.read(stem + ".spr.msb", msb) || !vfs.read(stem + ".dds", dds)) continue;
        SprTiles s;
        DdsImage img;
        std::string err;
        if (!parseSprTiles(msb, s, &err) || !decodeDds(dds, img) || img.empty()) {
            std::printf("[splash] %s: %s\n", stem.c_str(), err.empty() ? "cannot decode" : err.c_str());
            continue;
        }
        const SprAnim* bomb = nullptr;
        for (const SprAnim& a : s.anims)
            if (a.name.rfind("bomb", 0) == 0) bomb = &a;
        const SprAnim* slides = s.anim("slideshow");
        if (!bomb) continue;
        spr_ = std::move(s);
        atlas_ = std::move(img.rgba);
        atlasW_ = img.width;
        atlasH_ = img.height;
        bombFirst_ = static_cast<int>(bomb->first);
        bombCount_ = static_cast<int>(bomb->count);
        slideFirst_ = slides ? static_cast<int>(slides->first) : 0;
        slideCount_ = slides ? static_cast<int>(slides->count) : 0;
        tex_.assign(spr_.frames.size(), nullptr);
        name_ = stem;
        ok_ = true;
        std::printf("[splash] %s: %zu frames (%d bomb, %d slideshow), atlas %dx%d\n", stem.c_str(), spr_.frames.size(), bombCount_, slideCount_,
                    atlasW_, atlasH_);
        return true;
    }
    return false;
}

SDL_Texture* LoadingSplash::frameTex(size_t k) const {
    if (k >= tex_.size()) return nullptr;
    if (tex_[k]) return tex_[k];
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!composeSprFrame(spr_, k, atlas_.data(), atlasW_, atlasH_, px, w, h)) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    tex_[k] = t;
    return t;
}

void LoadingSplash::draw(GameUi& ui, int ms) const {
    ui.fillRect(0, 0, 1280, 720, Col{0, 0, 0, 255});
    if (!ok_) return;
    // slideshow: the frame's own offset places it (it is 688x336 at (320,154): the middle of the screen)
    if (slideCount_ > 0) {
        const int cur = (ms / kSlideMs) % slideCount_;
        const int into = ms % kSlideMs;
        const float fadeOut = into > kSlideMs - kSlideFadeMs ? static_cast<float>(into - (kSlideMs - kSlideFadeMs)) / kSlideFadeMs : 0.0f;
        const float fadeIn = std::clamp(static_cast<float>(ms - kSlideDelayMs) / 600.0f, 0.0f, 1.0f);  // the first drawing comes up from black
        for (int pass = 0; pass < 2; ++pass) {
            const int k = pass == 0 ? cur : (cur + 1) % slideCount_;
            const float a = (pass == 0 ? 1.0f - fadeOut : fadeOut) * kSlideAlpha * fadeIn;
            if (a <= 0.01f) continue;
            const size_t fi = static_cast<size_t>(slideFirst_ + k);
            if (SDL_Texture* t = frameTex(fi)) {
                const SprFrame& f = spr_.frames[fi];
                (void)f;
                ui.drawTexture(t, kSlideX, kSlideY, static_cast<float>(f.cols * spr_.tileW) * kSlideScale,
                               static_cast<float>(f.rows * spr_.tileH) * kSlideScale, 0, 0, 0,
                               false, false, Col{255, 255, 255, 255}, a, true);   // additive: the black of the .dds must not cover what is behind
            }
        }
    }
    // bomb + "LOADING...": lower right corner, like the PC game
    if (bombCount_ > 0) {
        const size_t fi = static_cast<size_t>(bombFirst_ + (ms / kBombFrameMs) % bombCount_);
        if (SDL_Texture* t = frameTex(fi)) {
            const SprFrame& f = spr_.frames[fi];
            const float w = static_cast<float>(f.cols * spr_.tileW), h = static_cast<float>(f.rows * spr_.tileH);
            ui.drawTexture(t, kBombX, kBombY, w, h, 0, 0, 0, false, false, Col{255, 255, 255, 255}, 1.0f, true);
        }
    }
}

}  // namespace zengine
