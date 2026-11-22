#include "char_sprite.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "art.h"
#include "dds.h"
#include "foit.h"
#include "moveset.h"
#include "vfs.h"
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

bool CharSpriteEdit::openArt(GameUi& gfx, const std::string& foitPath, const std::string& artPath, const std::string& artPtPath, const Palette& pal) {
    close();
    SDL_Renderer* r = gfx.renderer();
    if (!r || pal.bands < 2 || foitPath.empty()) return false;
    std::vector<uint8_t> foitBytes, art, artPt;
    if (!readFile(foitPath, foitBytes)) return false;
    if (!artPath.empty()) readFile(artPath, art);
    if (!artPtPath.empty()) readFile(artPtPath, artPt);
    if (art.empty() && artPt.empty()) return false;
    auto ch = std::make_shared<foit::Character>();
    if (!foit::parse(foitBytes.data(), foitBytes.size(), *ch)) return false;
    fight::MoveSet ms;
    if (!ms.build(ch)) return false;
    const int stand = ms.id.stand;   // the plain standing loop: no attack, no intro, no hit reaction
    if (stand < 0) return false;
    const uint16_t fidx = ms.frameAt(stand, 0);
    const foit::Frame* fr = ms.frame(fidx);
    if (fidx == fight::kNoFrame || !fr) return false;
    // decode every image layer of the frame and find the box they cover together (pivot-relative, like the fight draws them)
    struct Layer {
        ArtPlanes pl;
        int x = 0, y = 0;
    };
    std::vector<Layer> layers;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (int slot = 0; slot < foit::kSlots; ++slot) {
        const foit::Image& im = fr->img[slot];
        if (im.placeholder()) continue;
        const std::vector<uint8_t>& src = fr->src == 5 ? art : artPt;
        Layer l;
        if (src.empty() || !foit::decodeImage(im, src.data(), src.size(), l.pl, false) || l.pl.width <= 0 || l.pl.height <= 0) continue;
        l.x = fr->drawX(slot);
        l.y = fr->drawY(slot);
        if (layers.empty()) {
            x0 = l.x;
            y0 = l.y;
            x1 = l.x + l.pl.width;
            y1 = l.y + l.pl.height;
        } else {
            x0 = std::min(x0, l.x);
            y0 = std::min(y0, l.y);
            x1 = std::max(x1, l.x + l.pl.width);
            y1 = std::max(y1, l.y + l.pl.height);
        }
        layers.push_back(std::move(l));
    }
    const int w = x1 - x0, h = y1 - y0;
    if (layers.empty() || w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    std::vector<uint8_t> band(static_cast<size_t>(w) * h, 0), grad(static_cast<size_t>(w) * h, 0);
    for (const Layer& l : layers)
        for (int y = 0; y < l.pl.height; ++y)
            for (int x = 0; x < l.pl.width; ++x) {
                const size_t si = static_cast<size_t>(y) * l.pl.width + x;
                const size_t di = static_cast<size_t>(y + l.y - y0) * w + (x + l.x - x0);
                const uint8_t sb = l.pl.band[si], sg = l.pl.grad[si];
                if (sb == 0 && (sg >> 5) == 0) continue;   // nothing there
                if (sb > 0 || band[di] == 0) {              // a colour wins; an outline only where nothing coloured is below it
                    if (sb > 0 || (sg >> 5) >= (grad[di] >> 5)) {
                        band[di] = sb;
                        grad[di] = sg;
                    }
                }
            }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return false;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    tex_ = t;
    w_ = w;
    h_ = h;
    hotX_ = static_cast<float>(-x0);
    hotY_ = static_cast<float>(-y0);
    band_ = std::move(band);
    grad_ = std::move(grad);
    out_.assign(static_cast<size_t>(w) * h * 4, 0);
    int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            if (band_[i] == 0 && (grad_[i] >> 5) == 0) continue;
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x);
            by1 = std::max(by1, y);
        }
    bounds_ = bx1 < bx0 ? SDL_Rect{0, 0, w, h} : SDL_Rect{bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1};
    paint(pal);
    return true;
}

void CharSpriteEdit::close() {
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = nullptr;
    raw_.clear();
    band_.clear();
    grad_.clear();
    out_.clear();
    w_ = h_ = 0;
    bounds_ = SDL_Rect{0, 0, 0, 0};
}

void CharSpriteEdit::paint(const Palette& pal, int region, float pulse) {
    if (!tex_ || pal.bands < 2) return;
    if (!band_.empty()) {   // the fight art: (row, grad) per pixel, coloured like the fight does it (keyRgb: tone interpolation + line darkening)
        std::vector<uint8_t> table(static_cast<size_t>(pal.bands) * 256 * 3);
        for (int b = 1; b < pal.bands; ++b)
            for (int g = 0; g < 256; ++g) {
                double c[3];
                keyRgb(pal, b, g, c);
                for (int ch = 0; ch < 3; ++ch)
                    table[(static_cast<size_t>(b) * 256 + g) * 3 + ch] = static_cast<uint8_t>(std::min(255, std::max(0, static_cast<int>(c[ch] + 0.5))));
            }
        const float lift = std::max(0.0f, std::min(1.0f, pulse));
        const size_t n = static_cast<size_t>(w_) * h_;
        for (size_t i = 0; i < n; ++i) {
            uint8_t* px = &out_[i * 4];
            const int band = band_[i], g = grad_[i];
            if (band == 0) {   // outline layer: the palette's outline colour (black unless a palette sets one), coverage = line level / 7
                px[0] = pal.at(0, 0)[0];
                px[1] = pal.at(0, 0)[1];
                px[2] = pal.at(0, 0)[2];
                px[3] = static_cast<uint8_t>(std::min(255, 255 * (g >> 5) / 7));
                continue;
            }
            const uint8_t* c = &table[(static_cast<size_t>(std::min(band, pal.bands - 1)) * 256 + g) * 3];
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
            px[3] = 255;
            if (region > 0 && band == region && lift > 0)
                for (int k = 0; k < 3; ++k) px[k] = static_cast<uint8_t>(std::min(255.0f, px[k] + (255.0f - px[k]) * 0.55f * lift + 40.0f * lift));
        }
        SDL_UpdateTexture(tex_, nullptr, out_.data(), w_ * 4);
        return;
    }
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
                const int band = !band_.empty() ? band_[static_cast<size_t>(py) * w_ + px] : (raw_[(static_cast<size_t>(py) * w_ + px) * 4 + 1] >> 2);
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
