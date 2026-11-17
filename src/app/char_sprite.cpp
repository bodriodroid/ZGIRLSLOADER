#include "char_sprite.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "art.h"
#include "dds.h"
#include "spr_tiles.h"

namespace zengine {
namespace menu {

namespace {
constexpr size_t kKeep = 5;   // characters kept composed

// The atlas is not a picture: every 16-bit texel is (tone 5 bits | palette row 6 bits | line alpha 5 bits) = R5 G6 B5, to be looked up in the
// character's palette (same rows and tones as the fight sprites).  Row 0 is the black outline layer.  `texels` RGBA8 texels (as the .dds decodes
// to) are replaced by the colours of `pal`; `raw` and `out` may be the same buffer.  `region` (>= 1) is brightened by `pulse`.
void paintTexels(const Palette& pal, const uint8_t* raw, uint8_t* out, size_t texels, int region = 0, float pulse = 0) {
    std::vector<uint8_t> table(static_cast<size_t>(pal.bands) * 32 * 3);
    for (int b = 1; b < pal.bands; ++b)
        for (int t = 0; t < 32; ++t) {
            double c[3];
            keyRgb(pal, b, t, c);   // grad = tone * 2 with no line darkening
            for (int ch = 0; ch < 3; ++ch)
                table[(static_cast<size_t>(b) * 32 + t) * 3 + ch] = static_cast<uint8_t>(std::min(255, static_cast<int>(c[ch] + 0.5)));
        }
    const float lift = std::max(0.0f, std::min(1.0f, pulse));
    for (size_t i = 0; i < texels; ++i) {
        const uint8_t r0 = raw[i * 4], g0 = raw[i * 4 + 1], b0 = raw[i * 4 + 2];
        uint8_t* px = &out[i * 4];
        const int tone = r0 >> 3, band = g0 >> 2, line = b0 >> 3;
        if (band == 0) {   // outline layer: black (or the palette's outline colour), coverage in the third channel; tone 31 on row 0 is the background key (0xF800 / 0xF81F)
            px[0] = pal.at(0, 0)[0];   // row 0 of the stock palettes is all zero = black; a mods/pal palette may set an outline colour
            px[1] = pal.at(0, 0)[1];
            px[2] = pal.at(0, 0)[2];
            px[3] = tone == 31 ? 0 : static_cast<uint8_t>(std::min(255, 255 * line / 7));
        } else {
            const int row = std::min(band, pal.bands - 1);
            const uint8_t* c = &table[(static_cast<size_t>(row) * 32 + tone) * 3];
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
            px[3] = 255;
            if (region > 0 && band == region && lift > 0)
                for (int k = 0; k < 3; ++k) px[k] = static_cast<uint8_t>(std::min(255.0f, px[k] + (255.0f - px[k]) * 0.55f * lift + 40.0f * lift));
        }
    }
}
}  // namespace

void CharSprites::clear() {
    for (Slot& s : slots_)
        for (CharSpriteFrame& f : s.spr.idle)
            if (f.tex) SDL_DestroyTexture(f.tex);
    slots_.clear();
}

const CharSprite* CharSprites::get(GameUi& gfx, const std::string& name, const std::string& palPath) {
    ++clock_;
    const std::string key = palPath.empty() ? name : name + "|" + palPath;
    for (Slot& s : slots_)
        if (s.key == key) {
            s.used = clock_;
            return s.ok ? &s.spr : nullptr;
        }
    // evict the least recently used one
    if (slots_.size() >= kKeep) {
        auto it = std::min_element(slots_.begin(), slots_.end(), [](const Slot& a, const Slot& b) { return a.used < b.used; });
        for (CharSpriteFrame& f : it->spr.idle)
            if (f.tex) SDL_DestroyTexture(f.tex);
        slots_.erase(it);
    }
    slots_.emplace_back();
    Slot& slot = slots_.back();
    slot.key = key;
    slot.spr.name = name;
    slot.used = clock_;
    SDL_Renderer* r = gfx.renderer();
    const Vfs* vfs = gfx.vfs();
    if (!r || !vfs) return nullptr;
    const std::string stem = "character_select_" + name;
    std::vector<uint8_t> msb, dds;
    if (!vfs->read(stem + ".spr.msb", msb) || !vfs->read(stem + ".dds", dds)) return nullptr;
    SprTiles s;
    DdsImage img;
    std::string err;
    if (!parseSprTiles(msb, s, &err) || !decodeDds(dds, img) || img.empty()) {
        std::printf("[charselect] %s: %s\n", stem.c_str(), err.empty() ? "cannot decode" : err.c_str());
        return nullptr;
    }
    // The atlas is not a picture: every 16-bit texel is (tone 5 bits | palette row 6 bits | line alpha 5 bits) = R5 G6 B5, to be looked up
    // in the character's own <name>_1p.dds palette (same rows and tones as the fight sprites).  Row 0 is the black outline layer.
    std::vector<uint8_t> palFile;
    Palette pal;
    const bool gotPal = palPath.empty() ? vfs->read(name + "_1p.dds", palFile) : readFile(palPath, palFile);
    if (!gotPal || !readDds(palFile.data(), palFile.size(), pal) || pal.bands < 2) {
        std::printf("[charselect] %s: no palette %s_1p.dds\n", stem.c_str(), name.c_str());
        return nullptr;
    }
    paintTexels(pal, img.rgba.data(), img.rgba.data(), static_cast<size_t>(img.width) * img.height);
    const SprAnim* idle = s.anim("idle");
    const uint32_t first = idle ? idle->first : 0, count = idle ? idle->count : static_cast<uint32_t>(s.frames.size());
    for (uint32_t k = 0; k < count && first + k < s.frames.size(); ++k) {
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (!composeSprFrame(s, first + k, img.rgba.data(), img.width, img.height, px, w, h)) continue;
        SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!t) continue;
        SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        const SprFrame& f = s.frames[first + k];
        slot.spr.idle.push_back({t, w, h, f.offsetX, f.offsetY});
    }
    slot.ok = !slot.spr.idle.empty();
    return slot.ok ? &slot.spr : nullptr;
}

bool CharSpriteEdit::open(GameUi& gfx, const std::string& name, const Palette& pal) {
    close();
    SDL_Renderer* r = gfx.renderer();
    const Vfs* vfs = gfx.vfs();
    if (!r || !vfs || pal.bands < 2) return false;
    const std::string stem = "character_select_" + name;
    std::vector<uint8_t> msb, dds;
    if (!vfs->read(stem + ".spr.msb", msb) || !vfs->read(stem + ".dds", dds)) return false;
    SprTiles s;
    DdsImage img;
    std::string err;
    if (!parseSprTiles(msb, s, &err) || !decodeDds(dds, img) || img.empty()) return false;
    const SprAnim* idle = s.anim("idle");
    const uint32_t first = idle ? idle->first : 0;
    if (first >= s.frames.size()) return false;
    int w = 0, h = 0;
    if (!composeSprFrame(s, first, img.rgba.data(), img.width, img.height, raw_, w, h) || w <= 0 || h <= 0) return false;
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) {
        raw_.clear();
        return false;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    tex_ = t;
    w_ = w;
    h_ = h;
    hotX_ = s.frames[first].offsetX;
    hotY_ = s.frames[first].offsetY;
    out_.assign(raw_.size(), 0);
    // visible texels: any palette row, or the outline layer with some coverage (tone 31 on row 0 is the background key)
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t* q = &raw_[(static_cast<size_t>(y) * w + x) * 4];
            const int band = q[1] >> 2;
            const bool visible = band > 0 || ((q[0] >> 3) != 31 && (q[2] >> 3) > 0);
            if (!visible) continue;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    bounds_ = x1 < x0 ? SDL_Rect{0, 0, w, h} : SDL_Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    paint(pal);
    return true;
}

void CharSpriteEdit::close() {
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = nullptr;
    raw_.clear();
    out_.clear();
    w_ = h_ = 0;
    bounds_ = SDL_Rect{0, 0, 0, 0};
}

void CharSpriteEdit::paint(const Palette& pal, int region, float pulse) {
    if (!tex_ || pal.bands < 2) return;
    paintTexels(pal, raw_.data(), out_.data(), static_cast<size_t>(w_) * h_, region, pulse);
    SDL_UpdateTexture(tex_, nullptr, out_.data(), w_ * 4);
}

int CharSpriteEdit::regionAt(int x, int y) const {
    if (!tex_) return 0;
    for (int rad = 0; rad <= 5; ++rad)
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != rad) continue;   // the ring at this distance
                const int px = x + dx, py = y + dy;
                if (px < 0 || py < 0 || px >= w_ || py >= h_) continue;
                const uint8_t* t = &raw_[(static_cast<size_t>(py) * w_ + px) * 4];
                const int band = t[1] >> 2;
                if (band > 0) return band;
            }
    return 0;
}

void CharSprites::draw(GameUi& gfx, const CharSprite& s, int ms, float x, float y, bool flip, float alpha) {
    if (s.idle.empty() || alpha <= 0.0f) return;
    const size_t k = static_cast<size_t>(std::max(0, ms) / kFrameMs) % s.idle.size();
    const CharSpriteFrame& f = s.idle[k];
    const float w = f.w * kScale, h = f.h * kScale;
    const float left = flip ? x - (w - f.hotX * kScale) : x - f.hotX * kScale;
    gfx.drawTexture(f.tex, left, y - f.hotY * kScale, w, h, 0, 0, 0, flip, false, Col{255, 255, 255, 255}, alpha);
}

}  // namespace menu
}  // namespace zengine
