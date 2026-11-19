// boot_splash.cpp - see boot_splash.h
#include "boot_splash.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "boot_logo.h"

namespace zengine {

namespace {
std::vector<uint8_t> unbase64(const char* s) {
    static int8_t tab[256];
    static bool init = false;
    if (!init) {
        std::memset(tab, -1, sizeof tab);
        const char* al = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) tab[static_cast<uint8_t>(al[i])] = static_cast<int8_t>(i);
        init = true;
    }
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (; *s; ++s) {
        const int v = tab[static_cast<uint8_t>(*s)];
        if (v < 0) continue;   // padding
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}
}  // namespace

void BootLogo::reset() {
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = nullptr;
    tried_ = false;
}

bool BootLogo::load(SDL_Renderer* r) {
    if (tex_) return true;
    if (tried_ || !r) return false;
    tried_ = true;
    using namespace bootlogo;
    const std::vector<uint8_t> rle = unbase64(kAlphaB64);
    const std::vector<uint8_t> patch = unbase64(kPatchB64);
    std::vector<uint8_t> px(static_cast<size_t>(kCanvasW) * kCanvasH * 4, 0);   // white, alpha from the plane
    auto put = [&](size_t idx, uint8_t level, size_t run) {
        const uint8_t a = static_cast<uint8_t>(level * 17);
        for (size_t k = 0; k < run && idx + k < static_cast<size_t>(kW) * kH; ++k) {
            const size_t i = idx + k;
            const int x = kX + static_cast<int>(i % kW), y = kY + static_cast<int>(i / kW);
            uint8_t* p = &px[(static_cast<size_t>(y) * kCanvasW + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = a;
        }
    };
    size_t at = 0;
    for (size_t i = 0; i < rle.size() && at < static_cast<size_t>(kW) * kH;) {
        const uint8_t b = rle[i++];
        const uint8_t level = b >> 4;
        size_t run = b & 15;
        if (run == 15) {
            size_t extra = 0;
            int sh = 0;
            while (i < rle.size()) {
                const uint8_t e = rle[i++];
                extra |= static_cast<size_t>(e & 0x7F) << sh;
                sh += 7;
                if (!(e & 0x80)) break;
            }
            run = 15 + extra;
        }
        if (level) put(at, level, run);
        at += run;
    }
    if (patch.size() >= static_cast<size_t>(kPatchW) * kPatchH * 4) {   // the coloured pixels (the little face): copied as they are
        for (int y = 0; y < kPatchH; ++y)
            for (int x = 0; x < kPatchW; ++x) {
                const uint8_t* s = &patch[(static_cast<size_t>(y) * kPatchW + x) * 4];
                uint8_t* d = &px[(static_cast<size_t>(kPatchY + y) * kCanvasW + (kPatchX + x)) * 4];
                std::memcpy(d, s, 4);
            }
    }
    tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kCanvasW, kCanvasH);
    if (!tex_) return false;
    SDL_UpdateTexture(tex_, nullptr, px.data(), kCanvasW * 4);
    SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex_, SDL_ScaleModeLinear);
    return true;
}

void BootLogo::draw(SDL_Renderer* r, int x, int y, int w, int h, Uint8 alpha, SDL_Color tint) const {
    if (!tex_) return;
    SDL_SetTextureColorMod(tex_, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex_, alpha);
    const SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(r, tex_, nullptr, &dst);
}

}  // namespace zengine
