// play.cpp - the Play screen (see play.h).
//
// Everything gameplay-affecting happens in src/core/fight.cpp (fixed point, one tick per frame,
// rollback-safe snapshots).  This file only: picks a fighter + a stage from the user's own data,
// turns keys/touches into the 16-bit input the runtime expects, and draws the result.
#include "play.h"
#include "zg_progress.h"

#include "app_paths.h"
#include "fx_store.h"
#include "dds.h"
#include "imgscale.h"
#include "level.h"
#include "savini.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace zengine {
namespace play {

namespace {
constexpr int kFloorY = 300;            // screen y of the floor line
constexpr int kSkyTop = 0;              // the match is full screen: the loader's tab bar is hidden while fighting
constexpr double kStageHalf = 1400.0;   // the runtime clamps fighters to +/-1400
constexpr double kCharHeight = 430.0;   // a fighter's height in stage units
constexpr double kVertExag = 2.2;       // jump height is small in units; stretch it on screen
constexpr double kTickMs = 1000.0 / 60.0;
constexpr int kAppearMaxTicks = 330;   // an entrance longer than this is not picked (and is cut off at this)
constexpr int kTakeTicks = 85, kActionTicks = 89;   // PrePostHud.gbs: "1v1 Match Intro" / "Action Animation" length

// The virtual pad lives in src/core/touch_pad.*: an 8-way stick (finger-tracked, floating) plus the arcade buttons.

// ZGirlsMobile: Android has no environment variables and no F1/F2 keys, so the switches below can also be turned on from a
// text file, one word per line ('#' starts a comment):  boxes  demo  pad  huddemo  superdemo
// Looked up in the app's external folder (Android/data/org.bodriodroid.zgirlsmobile/files/zgirls_flags.txt) and in the pref dir.
// The environment variable still wins on desktop.  Read once per match start.
bool zgFlag(const char* envName, const char* word) {
    if (std::getenv(envName)) return true;
    std::vector<std::string> paths;
#ifdef __ANDROID__
    if (const char* ext = SDL_AndroidGetExternalStoragePath()) paths.push_back(std::string(ext) + "/zgirls_flags.txt");
#endif
    if (const std::string f = apppaths::prefFile("zgirls_flags.txt"); !f.empty()) paths.push_back(f);
    for (const std::string& path : paths) {
        std::ifstream f(path);
        std::string line;
        while (f && std::getline(f, line)) {
            if (const size_t h = line.find('#'); h != std::string::npos) line.erase(h);
            std::string w;
            for (char c : line)
                if (c != ' ' && c != '\t' && c != '\r') w += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
            if (w == word) return true;
        }
    }
    return false;
}

Col mixCol(Col a, Col b, double t) {
    return Col{static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t),
               static_cast<uint8_t>(a.b + (b.b - a.b) * t), static_cast<uint8_t>(a.a + (b.a - a.a) * t)};
}

std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

std::string basename(const std::string& p) {
    size_t i = p.find_last_of("/\\");
    return i == std::string::npos ? p : p.substr(i + 1);
}

bool isAtk(fight::CharState s) {
    using fight::CharState;
    return s == CharState::kAttackLight || s == CharState::kAttackMedium || s == CharState::kAttackHeavy ||
           s == CharState::kAttackSpecial || s == CharState::kAttackBlockbuster || s == CharState::kThrow;
}
}  // namespace

Anim animFor(const fight::Fighter& f) {
    using fight::CharState;
    if (isAtk(f.state)) {
        if (f.state == CharState::kAttackHeavy || f.state == CharState::kAttackSpecial ||
            f.state == CharState::kAttackBlockbuster)
            return kAtkH;
        return f.state == CharState::kAttackMedium ? kAtkM : kAtkL;
    }
    switch (f.state) {
        case CharState::kWalkForward: return kWalkF;
        case CharState::kWalkBack: return kWalkB;
        case CharState::kCrouch: return kCrouch;
        case CharState::kJumpStart:
        case CharState::kJumpRise:
        case CharState::kAirDash: return f.vy > 0 ? kJump : kFall;
        case CharState::kJumpFall: return kFall;
        case CharState::kJumpLand: return kLand;
        case CharState::kDash: return kWalkF;
        case CharState::kBlockStand:
        case CharState::kBlockCrouch:
        case CharState::kBlockAir: return kBlock;
        case CharState::kHitstunStand:
        case CharState::kHitstunCrouch:
        case CharState::kHitstunAir:
        case CharState::kLauncher: return kHurt;
        case CharState::kKnockdown:
        case CharState::kWakeup:
        case CharState::kDead: return kDown;
        case CharState::kBurst:
        case CharState::kWin: return kWin;
        case CharState::kIntro: return kIdle;
        default: break;
    }
    return f.airborne ? (f.vy > 0 ? kJump : kFall) : kIdle;
}

// ---------------------------------------------------------------------------
// Skin
// ---------------------------------------------------------------------------

void Skin::freeTex(Tex& t, bool sdlAlive) {
    if (t.t && sdlAlive) SDL_DestroyTexture(t.t);
    t.t = nullptr;
    if (t.gl.valid() && fx_) fx_->destroy(t.gl);   // no-op once the GL context is gone
    t.gl = FxIndexTex{};
}

void Skin::freeAll(bool sdlAlive) {
    for (auto& kv : tex_) freeTex(kv.second, sdlAlive);
    tex_.clear();
    bytes_ = 0;
    if (palGl_.valid() && fx_) fx_->destroy(palGl_);
    palGl_ = FxPaletteTex{};
}

void Skin::setFx(FxRenderer* fx) {
    const bool was = gpu_;
    if (was) freeAll(true);   // the GPU textures belong to the old renderer: free them while it is alive
    fx_ = fx;
    gpuFailed_ = false;
    gpu_ = wantGpu();
    if (gpu_ != was) {
        if (!was) freeAll(true);   // entering the GPU route: the coloured textures are of no use
        ++epoch_;
    } else if (was) {
        ++epoch_;
    }
}

int Skin::gpuTextures() const {
    int n = 0;
    for (const auto& kv : tex_)
        if (kv.second.gl.valid()) ++n;
    return n;
}

bool Skin::ensureFxPalette() {
    if (palGl_.valid()) return true;
    if (!fx_ || !fx_->ready()) return false;
    palGl_ = fx_->createPalette(pal_);
    return palGl_.valid();
}

void Skin::dropGpu() {
    freeAll(true);
    gpu_ = false;
    gpuFailed_ = true;
    ++epoch_;
}

void Skin::reset(SDL_Renderer* r) {
    freeAll(r != nullptr);
    ms_ = fight::MoveSet{};
    art_.clear();
    artPt_.clear();
    tables_.clear();
    pal_ = Palette{};
    ok = false;
    err.clear();
    name.clear();
    ++epoch_;   // pixels decoded for the old data must not be uploaded into the new one
}

void Skin::configure(SDL_Renderer* r, int level, bool smooth, size_t cacheBytes) {
    level = std::clamp(level, 0, 3);
    cacheBytes = std::max<size_t>(cacheBytes, 8u * 1024u * 1024u);
    if (level == level_ && smooth == smooth_) {
        cacheLimit_ = cacheBytes;
        return;
    }
    freeAll(r != nullptr);
    level_ = level;
    smooth_ = smooth;
    cacheLimit_ = cacheBytes;
    gpu_ = wantGpu();   // the GPU route needs full-size, unfiltered sprites
    ++epoch_;
}

bool Skin::load(const GameData& gd, const CharacterAssets& c, int palIdx, SDL_Renderer* r, std::string* errOut, const std::string& palPath) {
    reset(r);
    return loadData(gd, c, palIdx, errOut, palPath);
}

bool Skin::loadData(const GameData& gd, const CharacterAssets& c, int palIdx, std::string* errOut, const std::string& palPath) {
    (void)gd;
    name = c.name;
    auto fail = [&](const std::string& m) {
        err = m;
        if (errOut) *errOut = m;
        return false;
    };
    std::vector<uint8_t> foitBytes;
    if (c.foitPath.empty() || !readFile(c.foitPath, foitBytes)) return fail("cannot read " + c.foitPath);
    auto ch = std::make_shared<foit::Character>();
    std::string e;
    if (!foit::parse(foitBytes.data(), foitBytes.size(), *ch, &e)) return fail(".foit: " + e);
    if (!c.artPath.empty()) readFile(c.artPath, art_);
    if (!c.artPtPath.empty()) readFile(c.artPtPath, artPt_);
    if (art_.empty() && artPt_.empty()) return fail("neither .art nor .art-pt could be read for " + c.name);

    Palette pal = fallbackPalette();
    if (!palPath.empty()) {   // the colours picked in the Character Select (the game's own or a mods/pal one)
        std::string pe;
        if (!GameData::loadPalette(palPath, pal, &pe)) pal = fallbackPalette();
    } else if (!c.palettes.empty()) {
        const int idx = std::clamp(palIdx, 0, static_cast<int>(c.palettes.size()) - 1);
        std::string pe;
        if (!GameData::loadPalette(c.palettes[idx].second, pal, &pe)) pal = fallbackPalette();
    }
    tables_ = buildRgbaTables(pal);
    pal_ = pal;
    if (!ms_.build(ch, &e)) return fail(e);
    ok = true;
    return true;
}

Skin::Decoded Skin::decode(uint16_t fidx, int slot) const {
    Decoded d;
    d.epoch = epoch_;
    const foit::Frame* fr = ms_.frame(fidx);
    if (!fr || slot < 0 || slot >= foit::kSlots || fr->img[slot].placeholder()) return d;
    static const std::vector<uint8_t> none;
    const std::vector<uint8_t>& src = fr->src == 5 ? art_ : (fr->src == 4 ? artPt_ : none);
    ArtPlanes pl;
    if (src.empty() || !foit::decodeImage(fr->img[slot], src.data(), src.size(), pl) || pl.width <= 0 || pl.height <= 0) return d;
    if (gpu_) {   // the GPU colours the planes itself: ship them as they are
        d.index = FxRenderer::packIndex(pl.band.data(), pl.grad.data(), pl.width, pl.height);
        d.fullW = d.w = pl.width;
        d.fullH = d.h = pl.height;
        d.ok = !d.index.empty();
        return d;
    }
    std::vector<uint8_t> rgba = toRgba(pl, tables_);
    d.fullW = pl.width;
    d.fullH = pl.height;
    if (level_ > 0) {
        rgba = shrinkRgba(rgba.data(), pl.width, pl.height, 1 << level_, d.w, d.h);
    } else {
        d.w = pl.width;
        d.h = pl.height;
    }
    if (smooth_) bleedTransparent(rgba.data(), d.w, d.h, 2);   // a filtered edge must not pull in the colour under the transparent border
    d.rgba = std::move(rgba);
    d.ok = true;
    return d;
}

void Skin::evict() {
    // drop the frame that was drawn longest ago until the budget holds again (the one just inserted has the newest clock)
    while (bytes_ > cacheLimit_ && tex_.size() > 1) {
        auto oldest = tex_.end();
        for (auto it = tex_.begin(); it != tex_.end(); ++it)
            if (oldest == tex_.end() || it->second.use < oldest->second.use) oldest = it;
        if (oldest == tex_.end()) break;
        if (oldest->second.t || oldest->second.gl.valid()) {
            bytes_ -= std::min(bytes_, static_cast<size_t>(oldest->second.w) * oldest->second.h * 4);
            freeTex(oldest->second, true);
        }
        tex_.erase(oldest);
    }
}

void Skin::insert(SDL_Renderer* r, uint16_t fidx, int slot, Decoded&& d) {
    if (!r || d.epoch != epoch_) return;
    const uint32_t k = key(fidx, slot);
    if (tex_.count(k)) return;
    Tex t;
    if (d.ok && gpu_) {
        FxIndexTex g;
        if (!d.index.empty() && fx_ && fx_->ready()) g = fx_->createIndex(d.index.data(), d.w, d.h);
        if (g.valid()) {   // refused (bigger than the GPU takes): nothing is drawn for this image, like a failed SDL texture
            t.gl = g;
            t.w = d.w;
            t.h = d.h;
            t.fullW = d.fullW;
            t.fullH = d.fullH;
            bytes_ += static_cast<size_t>(d.w) * d.h * 4;
        }
    } else if (d.ok && !d.rgba.empty()) {
        SDL_Texture* tx = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, d.w, d.h);
        if (tx) {
            SDL_UpdateTexture(tx, nullptr, d.rgba.data(), d.w * 4);
            SDL_SetTextureBlendMode(tx, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tx, smooth_ ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
            t.t = tx;
            t.w = d.w;
            t.h = d.h;
            t.fullW = d.fullW;
            t.fullH = d.fullH;
            bytes_ += static_cast<size_t>(d.w) * d.h * 4;
        }
    }
    t.use = ++useClock_;
    tex_[k] = t;
    evict();
}

const Skin::Tex& Skin::image(SDL_Renderer* r, uint16_t fidx, int slot) {
    const uint32_t k = key(fidx, slot);
    auto it = tex_.find(k);
    if (it == tex_.end()) {
        insert(r, fidx, slot, decode(fidx, slot));
        it = tex_.find(k);
        if (it == tex_.end()) {   // nothing was stored (no renderer): an empty entry, valid until the next call
            static Tex none;
            return none;
        }
    }
    it->second.use = ++useClock_;
    return it->second;
}

size_t Skin::frameBytes(uint16_t fidx) const {
    const foit::Frame* fr = ms_.frame(fidx);
    if (!fr) return 0;
    size_t total = 0;
    for (int slot = 0; slot < foit::kSlots; ++slot) {
        const foit::Image& im = fr->img[slot];
        if (im.placeholder()) continue;
        total += (static_cast<size_t>(im.w) * 16 >> level_) * (static_cast<size_t>(im.h) * 16 >> level_) * 4;
    }
    return total;
}

bool Skin::prefetch(SDL_Renderer* r, uint16_t idx) {
    const foit::Frame* fr = ms_.frame(idx);
    if (!r || !fr) return false;
    bool decoded = false;
    for (int slot = 0; slot < foit::kSlots; ++slot) {
        if (fr->img[slot].placeholder()) continue;
        if (tex_.count(key(idx, slot))) continue;
        image(r, idx, slot);
        decoded = true;
    }
    return decoded;
}

int Skin::drawFrame(SDL_Renderer* r, uint16_t idx, double feetX, double feetY, double scale, bool faceRight, double xs, double ys, double alpha) {
    const foit::Frame* fr = ms_.frame(idx);
    if (!fr) return 0;
    if (xs == 0.0 || ys == 0.0 || alpha <= 0.0) return 0;   // scaled to nothing / invisible: a deliberate "not drawn", not a missing frame
    if (xs < 0) {   // a negative stretch mirrors the art
        faceRight = !faceRight;
        xs = -xs;
    }
    const Uint8 a8 = alpha >= 1.0 ? 255 : static_cast<Uint8>(std::lround(alpha * 255.0));
    const double sxk = scale * xs, syk = scale * ys;
    if (gpu_ && !ensureFxPalette()) dropGpu();   // the GPU would not take the palette: this skin goes back to the CPU route
    if (gpu_) {
        const int n = drawFrameGpu(r, idx, feetX, feetY, faceRight, sxk, syk, alpha);
        if (n >= 0) return n;
        dropGpu();   // the pass could not start (GL context changed under us): carry on with the CPU route, this frame included
    }
    int drawn = 0;
    const auto c8 = [](float v) { return static_cast<Uint8>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    // the palette effect of this object (a wash toward a colour): a multiply for the darker colours ...
    const float wa = std::clamp(wash_[3], 0.0f, 1.0f);
    float mul[3], add[3];
    for (int i = 0; i < 3; ++i) {
        const float wc = std::clamp(wash_[i], 0.0f, 1.0f);
        mul[i] = tint_[i] * ((1.0f - wa) + wa * wc);
        add[i] = wa * std::max(0.0f, wc - 0.5f) * 2.0f;   // ... and an additive pass for the brighter ones
    }
    const Uint8 tr = c8(mul[0]), tg = c8(mul[1]), tb = c8(mul[2]);
    const bool tinted = tr != 255 || tg != 255 || tb != 255;
    const Uint8 ar = c8(add[0]), ag = c8(add[1]), ab = c8(add[2]);
    const bool additive = (ar | ag | ab) > 4;
    for (int slot = 0; slot < foit::kSlots; ++slot) {
        if (fr->img[slot].placeholder()) continue;
        const Tex& t = image(r, idx, slot);
        if (!t.t) continue;
        const double dx = fr->drawX(slot), dy = fr->drawY(slot);
        double x0, x1;
        if (faceRight) {
            x0 = feetX + dx * sxk;
            x1 = feetX + (dx + t.fullW) * sxk;
        } else {
            x0 = feetX - (dx + t.fullW) * sxk;
            x1 = feetX - dx * sxk;
        }
        double y0 = feetY + dy * syk, y1 = feetY + (dy + t.fullH) * syk;
        if (!smooth_) {   // Pixelated: whole logical pixels, like the original renderer of this port
            x0 = std::round(x0);
            x1 = std::round(x1);
            y0 = std::round(y0);
            y1 = std::round(y1);
        }
        const SDL_FRect dst{static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(std::max(smooth_ ? 0.25 : 1.0, x1 - x0)),
                            static_cast<float>(std::max(smooth_ ? 0.25 : 1.0, y1 - y0))};
        if (a8 != 255) SDL_SetTextureAlphaMod(t.t, a8);
        if (tinted) SDL_SetTextureColorMod(t.t, tr, tg, tb);
        SDL_RenderCopyExF(r, t.t, nullptr, &dst, 0, nullptr, faceRight ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL);
        if (additive) {
            SDL_BlendMode old = SDL_BLENDMODE_BLEND;
            SDL_GetTextureBlendMode(t.t, &old);
            SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(t.t, ar, ag, ab);
            SDL_RenderCopyExF(r, t.t, nullptr, &dst, 0, nullptr, faceRight ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL);
            SDL_SetTextureBlendMode(t.t, old);
        }
        if (tinted || additive) SDL_SetTextureColorMod(t.t, 255, 255, 255);
        if (a8 != 255) SDL_SetTextureAlphaMod(t.t, 255);
        ++drawn;
    }
    return drawn;
}

void Skin::setSuperOutline(const float a[3], const float b[3], float mix, float strength) {
    for (int i = 0; i < 3; ++i) outA_[i] = a[i], outB_[i] = b[i];
    outMix_ = mix;
    outStrength_ = strength;
}

int Skin::drawShadow(SDL_Renderer* r, uint16_t idx, double feetX, double floorY, double air, double scale, bool faceRight, double flat,
                     double alpha, bool fade, double fadeDist) {
    const foit::Frame* fr = ms_.frame(idx);
    if (!fr) return 0;
    if (gpu_) {
        const int n = drawShadowGpu(r, idx, feetX, floorY, air, scale, faceRight, flat, alpha, fade, fadeDist);
        if (n >= 0) return n;
        dropGpu();
    }
    int drawn = 0;
    const double dist = std::max(50.0, fadeDist);
    auto alphaAt = [&](double heightAboveFloor) {
        const double k = fade ? std::clamp(1.0 - heightAboveFloor / dist, 0.0, 1.0) : std::clamp(1.0 - 0.5 * air / dist, 0.0, 1.0);
        return static_cast<Uint8>(std::lround(255.0 * std::clamp(alpha * k, 0.0, 1.0)));
    };
    for (int slot = 0; slot < foit::kSlots; ++slot) {
        if (fr->img[slot].placeholder()) continue;
        const Tex& t = image(r, idx, slot);
        if (!t.t) continue;
        const double dx = fr->drawX(slot), dy = fr->drawY(slot);
        double x0, x1;
        if (faceRight) {
            x0 = feetX + dx * scale;
            x1 = feetX + (dx + t.fullW) * scale;
        } else {
            x0 = feetX - (dx + t.fullW) * scale;
            x1 = feetX - dx * scale;
        }
        // heights above the feet of the image's top and bottom edges (stage units; the art's y grows downwards)
        const double hTop = -dy, hBot = -(dy + t.fullH);
        // each point lands on the floor pushed towards the camera by its height above the floor: the silhouette is mirrored and squashed
        const double yTop = floorY + (air + hTop) * flat * scale, yBot = floorY + (air + hBot) * flat * scale;
        Uint8 aTop = alphaAt(air + hTop);
        const Uint8 aBot = alphaAt(air + hBot);
        if (aTop == 0 && aBot == 0) continue;
        // SDL's software renderer treats four equal vertex colours as a plain modulation of the texture and leaves it set: the
        // next sprite of that texture would come out as the shadow.  One step of difference keeps it on the per-vertex path.
        if (aTop == aBot) aTop = aBot > 1 ? static_cast<Uint8>(aBot - 1) : static_cast<Uint8>(aBot + 1);
        const float u0 = faceRight ? 0.0f : 1.0f, u1 = faceRight ? 1.0f : 0.0f;
        SDL_Vertex v[4];
        // the mirrored image: its top edge (head) is the one that lies furthest from the feet
        v[0].position = {static_cast<float>(x0), static_cast<float>(yTop)};
        v[1].position = {static_cast<float>(x1), static_cast<float>(yTop)};
        v[2].position = {static_cast<float>(x0), static_cast<float>(yBot)};
        v[3].position = {static_cast<float>(x1), static_cast<float>(yBot)};
        v[0].tex_coord = {u0, 0.0f};
        v[1].tex_coord = {u1, 0.0f};
        v[2].tex_coord = {u0, 1.0f};
        v[3].tex_coord = {u1, 1.0f};
        for (int i = 0; i < 4; ++i) v[i].color = SDL_Color{0, 0, 0, i < 2 ? aTop : aBot};
        static const int idxs[6] = {0, 1, 2, 2, 1, 3};
        SDL_RenderGeometry(r, t.t, v, 4, idxs, 6);
        ++drawn;
    }
    return drawn;
}

// GPU route: the images are cached as index planes; one pass per call draws every slot of the frame through the effect programs.
// Two phases, because decoding / uploading an image can evict an older one: first make sure every slot is cached, then draw only
// what is still there.
int Skin::drawFrameGpu(SDL_Renderer* r, uint16_t idx, double feetX, double feetY, bool faceRight, double sxk, double syk, double alpha) {
    const foit::Frame* fr = ms_.frame(idx);
    if (!fr || !fx_) return -1;
    for (int slot = 0; slot < foit::kSlots; ++slot)
        if (!fr->img[slot].placeholder()) image(r, idx, slot);
    FxRenderer::Pass pass(*fx_);
    if (!pass.ok()) return -1;
    fx::SpriteParams sp;
    sp.alpha = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    sp.hueCos = hueCos_;
    sp.hueSin = hueSin_;
    sp.hueAmount = hueAmount_;
    sp.screen = screenFx_;
    int drawn = 0;
    for (int slot = 0; slot < foit::kSlots; ++slot) {
        if (fr->img[slot].placeholder()) continue;
        const auto it = tex_.find(key(idx, slot));
        if (it == tex_.end() || !it->second.gl.valid()) continue;
        const Tex& t = it->second;
        const double dx = fr->drawX(slot), dy = fr->drawY(slot);
        double x0, x1;
        if (faceRight) {
            x0 = feetX + dx * sxk;
            x1 = feetX + (dx + t.fullW) * sxk;
        } else {
            x0 = feetX - (dx + t.fullW) * sxk;
            x1 = feetX - dx * sxk;
        }
        double y0 = feetY + dy * syk, y1 = feetY + (dy + t.fullH) * syk;
        if (!smooth_) {   // Pixelated: whole logical pixels, like the CPU route
            x0 = std::round(x0);
            x1 = std::round(x1);
            y0 = std::round(y0);
            y1 = std::round(y1);
        }
        const SDL_FRect dst{static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(std::max(smooth_ ? 0.25 : 1.0, x1 - x0)),
                            static_cast<float>(std::max(smooth_ ? 0.25 : 1.0, y1 - y0))};
        pass.sprite(t.gl, palGl_, dst, !faceRight, sp, smooth_);
        if (outStrength_ > 0.0f && fx_->hasOutline())
            pass.outline(t.gl, dst, !faceRight, outStrength_ * sp.alpha, outA_, outB_, outMix_);
        ++drawn;
    }
    return drawn;
}

int Skin::drawShadowGpu(SDL_Renderer* r, uint16_t idx, double feetX, double floorY, double air, double scale, bool faceRight, double flat,
                        double alpha, bool fade, double fadeDist) {
    const foit::Frame* fr = ms_.frame(idx);
    if (!fr || !fx_) return -1;
    const double dist = std::max(50.0, fadeDist);
    for (int slot = 0; slot < foit::kSlots; ++slot)
        if (!fr->img[slot].placeholder()) image(r, idx, slot);
    FxRenderer::Pass pass(*fx_);
    if (!pass.ok()) return -1;
    int drawn = 0;
    for (int slot = 0; slot < foit::kSlots; ++slot) {
        if (fr->img[slot].placeholder()) continue;
        const auto it = tex_.find(key(idx, slot));
        if (it == tex_.end() || !it->second.gl.valid()) continue;
        const Tex& t = it->second;
        const double dx = fr->drawX(slot), dy = fr->drawY(slot);
        double x0, x1;
        if (faceRight) {
            x0 = feetX + dx * scale;
            x1 = feetX + (dx + t.fullW) * scale;
        } else {
            x0 = feetX - (dx + t.fullW) * scale;
            x1 = feetX - dx * scale;
        }
        // same geometry as the CPU route: the silhouette is mirrored and squashed onto the floor, each point pushed by its height
        const double hTop = -dy, hBot = -(dy + t.fullH);
        const double yTop = floorY + (air + hTop) * flat * scale, yBot = floorY + (air + hBot) * flat * scale;
        // what the programs fade by: the height above the floor of each corner (interpolated per pixel); without `fade` one value
        // for the whole silhouette - the fighter's height in the air
        const double zTop = fade ? air + hTop : 0.5 * air, zBot = fade ? air + hBot : 0.5 * air;
        if (zTop >= dist && zBot >= dist) continue;   // fully faded out
        FxShadowQuad q;
        const float u0 = faceRight ? 0.0f : 1.0f, u1 = faceRight ? 1.0f : 0.0f;
        const float xs4[4] = {static_cast<float>(x0), static_cast<float>(x1), static_cast<float>(x0), static_cast<float>(x1)};
        const float ys4[4] = {static_cast<float>(yTop), static_cast<float>(yTop), static_cast<float>(yBot), static_cast<float>(yBot)};
        const float zs4[4] = {static_cast<float>(zTop), static_cast<float>(zTop), static_cast<float>(zBot), static_cast<float>(zBot)};
        const float us4[4] = {u0, u1, u0, u1}, vs4[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            q.x[i] = xs4[i];
            q.y[i] = ys4[i];
            q.z[i] = zs4[i];
            q.u[i] = us4[i];
            q.v[i] = vs4[i];
        }
        pass.shadow(t.gl, q, static_cast<float>(dist), static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
        ++drawn;
    }
    return drawn;
}

// ---------------------------------------------------------------------------
// PlayScreen
// ---------------------------------------------------------------------------

void PlayScreen::setRoster(const std::vector<CharacterAssets>& roster) {
    roster_ = roster;
    if (roster_.empty()) {
        pick_[0] = pick_[1] = -1;
        return;
    }
    pick_[0] = std::clamp(pick_[0], 0, static_cast<int>(roster_.size()) - 1);
    pick_[1] = std::clamp(pick_[1], 0, static_cast<int>(roster_.size()) - 1);
    if (pick_[1] == pick_[0] && roster_.size() > 1) pick_[1] = (pick_[0] + 1) % static_cast<int>(roster_.size());
}

void PlayScreen::setLevels(const std::vector<LevelInfo>& levels) {
    levels_ = levels;
    stageIdx_ = std::clamp(stageIdx_, 0, std::max(0, static_cast<int>(levels_.size()) - 1));
}

namespace {
// ZDROID_DEV_<KEY without the ZD_DEV_ prefix> in the environment wins (desktop, captures); then the save.
std::string devGet(const char* key) {
    if (std::strncmp(key, "ZD_DEV_", 7) == 0) {
        const std::string env = std::string("ZDROID_DEV_") + (key + 7);
        if (const char* e = std::getenv(env.c_str())) return e;
    }
    return SavIni::shared().get("options", key, "");
}
}  // namespace

void PlayScreen::refreshDev() {
    dev_ = devopt::resolve([](const char* k) { return devGet(k); });
}

// "GPU effects" option: starts the effect renderer (loading the pack from the app's private folder, building it from the player's own
// Core the first time) and hands it to both skins, or takes it back.  Anything that goes wrong leaves the option off and the CPU
// route in charge; fxStatus_ says why.
void PlayScreen::syncFx(SDL_Renderer* r) {
    if (!dev_.gpuFx || !r) {
        for (Skin& sk : skin_) sk.setFx(nullptr);   // they free their GPU textures while the renderer lives
        fxr_.shutdown();
        fxStatus_ = "off";
        return;
    }
    if (!fxr_.ready() || fxr_.renderer() != r) {
        for (Skin& sk : skin_) sk.setFx(nullptr);
        fxr_.shutdown();
        std::string coreDir;
        std::string real;
        if (gd_ && gd_->vfs().pick("TiledSprite_fp.fx", &real)) coreDir = std::filesystem::path(real).parent_path().string();
        fx::FxPack pack;
        fx::FxStoreInfo info;
        std::string err;
        if (!fx::loadOrBuildFxPack(apppaths::coreStoreDir(), coreDir, pack, &info, &err) || !fxr_.init(r, pack, &err)) {
            fxStatus_ = "off (" + err + ")";
            std::printf("[play] GPU effects: %s\n", fxStatus_.c_str());
            return;
        }
        fxStatus_ = std::string("on, ") + (info.fromCore ? "numbers from your Core" : "neutral numbers") +
                    (info.built ? (info.persisted ? ", pack built and saved" : ", pack built (not saved)") : ", pack loaded") +
                    (fxr_.hasOutline() ? ", super outline" : ", no super outline program");
        std::printf("[play] GPU effects: %s\n", fxStatus_.c_str());
    }
    for (Skin& sk : skin_) sk.setFx(&fxr_);
}

// Sprite level, filter and texture budget of both fighters.  The level follows the closest camera zoom (the biggest the sprites
// ever get on screen): a 640x360 window needs half-size art, a phone needs every pixel.
void PlayScreen::applyDevToSkins(SDL_Renderer* r) {
    syncFx(r);
    float sx = 1.0f, sy = 1.0f;
    if (r) SDL_RenderGetScale(r, &sx, &sy);
    const double phys = std::max(0.1, static_cast<double>(std::min(sx, sy))) * dev_.renderScale;
    spritePx_ = 0.52 * phys;
    const int level = dev_.spriteLevel(spritePx_);
    for (Skin& sk : skin_) sk.configure(r, level, dev_.smooth, dev_.spriteCacheBytes);
}

const char* PlayScreen::phaseName() const {
    switch (phase_) {
        case Phase::Select: return "select";
        case Phase::Loading: return "loading";
        case Phase::Fight: return "fight";
        case Phase::RoundEnd: return "roundend";
        case Phase::MatchEnd: return "matchend";
    }
    return "?";
}

std::string PlayScreen::playerName(int i) const {
    if (i == 0 && skin_[0].ok && !skin_[0].name.empty()) return upper(skin_[0].name);
    if (i == 1 && skin_[1].ok && !skin_[1].name.empty()) return upper(skin_[1].name);
    if (pick_[i] >= 0 && pick_[i] < static_cast<int>(roster_.size())) return upper(roster_[pick_[i]].name);
    return "DUMMY";
}

bool PlayScreen::onBack() {
    if (phase_ == Phase::Loading) job_.reset();   // waits for the worker threads (a fraction of a second)
    if (overlay_ == Overlay::Dev) { openOverlay(Overlay::Options); menuSel_ = 6; return true; }
    if (overlay_ == Overlay::Help) { openOverlay(Overlay::Options); return true; }
    if (overlay_ == Overlay::Options) { openOverlay(Overlay::Pause); menuSel_ = 1; return true; }
    if (confirm_.isOpen()) { confirm_.close(); return true; }   // back = "No"
    if (overlay_ == Overlay::Pause) { overlay_ = Overlay::None; return true; }
    if (phase_ == Phase::Fight || phase_ == Phase::RoundEnd) { openOverlay(Overlay::Pause); return true; }   // like Start on the PC
    if (storyMode_) {   // Story Mode: Back gives the fight up and returns to the story
        storyOutcome_ = StoryOutcome::Quit;
        return true;
    }
    if (phase_ == Phase::Select) return false;
    leaveMatch();
    return true;
}

void PlayScreen::leaveMatch() {
    pf_.clear();
    job_.reset();
    arcadeMode_ = false;
    overlay_ = Overlay::None;
    phase_ = Phase::Select;
    wins_[0] = wins_[1] = 0;
    round_ = 1;
    exitToMenu_ = true;
}

namespace {
// "Ms. Fortune", "MsFortune", "msfortune" -> "msfortune"
std::string storyKey(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c + 32);
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}
}  // namespace

int PlayScreen::findFighter(const std::string& name) const {
    if (roster_.empty()) return -1;
    const std::string k = storyKey(name);
    if (k.empty() || k == "random") return std::rand() % static_cast<int>(roster_.size());
    for (size_t i = 0; i < roster_.size(); ++i)
        if (storyKey(roster_[i].name) == k) return static_cast<int>(i);
    for (size_t i = 0; i < roster_.size(); ++i) {   // "bossmarie" for "Marie", "robofortune" for "Robo" ...
        const std::string r = storyKey(roster_[i].name);
        if (!r.empty() && (r.find(k) != std::string::npos || k.find(r) != std::string::npos)) return static_cast<int>(i);
    }
    return -1;
}

void PlayScreen::enterMatch(SDL_Renderer* r, Mode mode, const Rules& rules, const std::string& p1, const std::string& p2,
                            const std::string& stage, const std::string& p1PalPath, const std::string& p2PalPath) {
    if (r) ren_ = r;
    storyMode_ = false;
    arcadeMode_ = mode == Mode::Arcade;
    arcadeCredits_ = false;
    storyOutcome_ = StoryOutcome::None;
    exitToMenu_ = false;
    lastWinner_ = -1;
    training_ = mode == Mode::Training;
    cpu_[0] = false;
    cpu_[1] = mode != Mode::Versus;
    difficulty_ = std::clamp(rules.difficulty, 0, 2);
    roundsToWin_ = std::clamp((rules.rounds + 1) / 2, 1, 4);
    timeLimitSec_ = std::max(0, rules.timeLimitSec);
    const int a = findFighter(p1);
    if (a >= 0) pick_[0] = a;
    int b = findFighter(p2);
    if (b < 0) b = std::rand() % std::max(1, static_cast<int>(roster_.size()));
    pick_[1] = b;
    palPath_[0] = p1PalPath;
    palPath_[1] = p2PalPath;
    if (arcadeMode_) {   // the ladder picks every opponent and stage from here on
        beginArcade(rules);
        return;
    }
    const std::string want = storyKey(stage);
    stageAsked_ = stage;
    stageUnresolved_ = false;
    if (want == "random" && !levels_.empty()) {
        stageIdx_ = randomLevelIndex();
    } else if (!want.empty()) {
        const int li = levelIndexFor(stage);
        if (li >= 0) {
            stageIdx_ = li;
        } else {
            // not the previous match's stage and not index 0 (which can be the test level): a real stage, and the problem line says so
            stageUnresolved_ = true;
            if (!levels_.empty()) stageIdx_ = randomLevelIndex();
            std::printf("[play] stage '%s' matches none of the %zu .lvl files (stages.ini rows: %zu); using '%s'\n", stage.c_str(), levels_.size(),
                        stageTable_.size(), stageIdx_ >= 0 && stageIdx_ < static_cast<int>(levels_.size()) ? levels_[static_cast<size_t>(stageIdx_)].name.c_str() : "?");
        }
    }
    startMatch();
}

int PlayScreen::levelIndexFor(const std::string& stage) const { return resolveLevel(levels_, stageTable_, stage); }

std::string PlayScreen::stageProblem() const {
    const bool es = lang_ == "es";
    if (levels_.empty())
        return es ? "NO hay ningun .lvl en los datos montados: escenario de prueba generado" : "NO .lvl in the mounted data: generated test stage";
    const bool have = stageIdx_ >= 0 && stageIdx_ < static_cast<int>(levels_.size());
    const std::string cur = have ? levels_[static_cast<size_t>(stageIdx_)].name : "?";
    if (stageUnresolved_)
        return (es ? "el escenario '" : "stage '") + stageAsked_ + (es ? "' no coincide con ningun .lvl (" : "' matches no .lvl (") +
               std::to_string(levels_.size()) + (es ? "): se usa " : "): using ") + cur;
    if (!job_ && !bg_.tex && have && dev_.stage != devopt::StageSource::Generated) {
        const std::string want = basename(levels_[static_cast<size_t>(stageIdx_)].background2D);
        return (es ? "sin imagen de fondo para " : "no backdrop picture for ") + cur + (want.empty() ? "" : (es ? " (pide " : " (wants ") + want + ")") +
               (es ? ": escenario generado" : ": generated stage");
    }
    return {};
}

int PlayScreen::randomLevelIndex() const {
    if (levels_.empty()) return 0;
    const std::vector<int> ok = randomLevels(levels_, stageTable_);
    return ok[static_cast<size_t>(std::rand()) % ok.size()];
}

void PlayScreen::enterStory(SDL_Renderer* r, const std::string& p1, const std::string& p2, const std::string& level, int rounds, int difficulty) {
    if (r) ren_ = r;
    storyMode_ = true;
    arcadeMode_ = false;
    storyOutcome_ = StoryOutcome::None;
    lastWinner_ = -1;
    training_ = false;
    cpu_[0] = false;
    cpu_[1] = true;
    difficulty_ = std::clamp(difficulty, 0, 2);
    roundsToWin_ = std::clamp((std::max(1, rounds) + 1) / 2, 1, 4);
    timeLimitSec_ = 99;
    const int a = findFighter(p1);
    if (a >= 0) pick_[0] = a;
    const int b = findFighter(p2);
    if (b >= 0) pick_[1] = b;
    if (!p1.empty() && a < 0) std::printf("[play] story: fighter '%s' is not in the mounted data\n", p1.c_str());
    if (!p2.empty() && b < 0) std::printf("[play] story: fighter '%s' is not in the mounted data\n", p2.c_str());
    stageAsked_ = level;
    stageUnresolved_ = false;
    if (!level.empty()) {
        const int li = levelIndexFor(level);
        if (li >= 0) stageIdx_ = li;
        else {
            stageUnresolved_ = true;
            if (!levels_.empty()) stageIdx_ = randomLevelIndex();
            std::printf("[play] story: stage '%s' matches none of the %zu .lvl files\n", level.c_str(), levels_.size());
        }
    } else if (!levels_.empty()) {
        stageIdx_ = randomLevelIndex();   // "World Random": any stage, not whichever the last fight left behind
    }
    startMatch();
}

// ---- Arcade Mode ---------------------------------------------------------------------------------------------------------
void PlayScreen::beginArcade(const Rules& rules) {
    arcadeRounds_ = rules.rounds;
    std::vector<std::string> names, stages;
    for (const CharacterAssets& ca : roster_) names.push_back(ca.name);
    for (int i : randomLevels(levels_, stageTable_)) stages.push_back(levels_[static_cast<size_t>(i)].name);   // no test1 / test2 / Evo arena in the ladder
    const std::string me = pick_[0] >= 0 && pick_[0] < static_cast<int>(roster_.size()) ? roster_[static_cast<size_t>(pick_[0])].name : std::string();
    const uint32_t seed = static_cast<uint32_t>(SDL_GetTicks()) * 2654435761u ^ static_cast<uint32_t>(std::rand());
    ladder_.begin(me, names, stages, "bossmarie", "falling_wreckage", rules.difficulty, seed);
    startArcadeFight();
}

void PlayScreen::startArcadeFight() {
    const arcade::Fight& f = ladder_.current();
    int b = findFighter(f.fighter);
    if (b < 0 && !roster_.empty()) b = std::rand() % static_cast<int>(roster_.size());
    if (b >= 0) pick_[1] = b;
    palPath_[1].clear();
    difficulty_ = std::clamp(f.difficulty, 0, 2);
    roundsToWin_ = std::clamp(((f.rounds > 0 ? f.rounds : arcadeRounds_) + 1) / 2, 1, 4);
    int si = f.stage.empty() ? -1 : levelIndexFor(f.stage);
    stageAsked_ = f.stage;
    stageUnresolved_ = false;   // an unlisted ladder stage just gets a random one: not a failure
    if (si < 0 && !levels_.empty()) si = randomLevelIndex();
    if (si >= 0) stageIdx_ = si;
    startMatch();
}

void PlayScreen::arcadeNext() {
    overlay_ = Overlay::None;
    if (ladder_.won()) {   // the boss fell: the run is complete
        if (pick_[0] >= 0 && pick_[0] < static_cast<int>(roster_.size())) {
            ZgProgress prog;
            prog.setArcadeDone(roster_[static_cast<size_t>(pick_[0])].name);
        }
        arcadeMode_ = false;
        arcadeCredits_ = true;
        leaveMatch();
        return;
    }
    startArcadeFight();
}

void PlayScreen::leaveStory() {
    job_.reset();
    overlay_ = Overlay::None;
    storyMode_ = false;
    storyOutcome_ = StoryOutcome::None;
    lastWinner_ = -1;
    phase_ = Phase::Select;
    wins_[0] = wins_[1] = 0;
    round_ = 1;
}

std::string PlayScreen::loadKey() const {
    // what the Developer Options change about the loaded data (stage picture, sprite level / filter) is part of the key: a rematch
    // reuses the loaded fighters only while those are the same
    const devopt::Settings d = devopt::resolve([](const char* k) { return devGet(k); });
    return std::to_string(pick_[0]) + "|" + palPath_[0] + "|" + std::to_string(pick_[1]) + "|" + palPath_[1] + "|" +
           std::to_string(stageIdx_) + "|" + std::to_string(static_cast<int>(d.stage)) + std::to_string(static_cast<int>(d.upscale)) +
           std::to_string(d.spriteMinLevel) + (d.spriteFullSize ? "F" : "a") + (d.smooth ? "s" : "p") + std::to_string(static_cast<int>(d.preload)) +
           std::to_string(static_cast<int>(d.renderScale * 100));
}

void PlayScreen::startMatch() {
    wins_[0] = wins_[1] = 0;
    round_ = 1;
    const std::string key = loadKey();
    if (!loadedKey_.empty() && key == loadedKey_ && !job_) {   // rematch / retry: same fighters, colours and stage
        finishLoad();
        return;
    }
    beginLoad(key);
}

namespace {
// Stage pictures that mark the fighters' centre line (Class Notes: a red line down the graph-paper floor) are not centred on it: the
// camera that took the picture looked a little off to one side.  The line is a thin, strongly red column in the bottom rows;
// returns where it is across the picture (0..1), 0.5 when the picture has none.
double findCenterLine(const DdsImage& img) {
    const int w = img.width, h = img.height;
    if (w < 64 || h < 64 || img.rgba.size() < static_cast<size_t>(w) * h * 4) return 0.5;
    const int y0 = h - h / 8, rows = h - y0;
    double sum = 0, wsum = 0;
    int bestCount = 0;
    std::vector<int> count(static_cast<size_t>(w), 0);
    for (int x = w * 35 / 100; x < w * 65 / 100; ++x) {
        for (int y = y0; y < h; ++y) {
            const uint8_t* px = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
            if (px[3] > 128 && px[0] > 150 && px[1] < 90 && px[2] < 90) ++count[static_cast<size_t>(x)];
        }
        bestCount = std::max(bestCount, count[static_cast<size_t>(x)]);
    }
    if (bestCount < rows / 2) return 0.5;
    for (int x = w * 35 / 100; x < w * 65 / 100; ++x)
        if (count[static_cast<size_t>(x)] >= bestCount / 2) {
            sum += (x + 0.5) * count[static_cast<size_t>(x)];
            wsum += count[static_cast<size_t>(x)];
        }
    return wsum > 0 ? sum / wsum / w : 0.5;
}
}  // namespace

std::vector<PlayScreen::WarmResult> PlayScreen::decodeItems(const Skin* a, const Skin* b, const std::vector<std::pair<int, uint16_t>>& items) {
    std::vector<WarmResult> out;
    for (const auto& it : items) {
        const Skin* sk = it.first == 0 ? a : b;
        const foit::Frame* fr = sk->moves().frame(it.second);
        if (!fr) continue;
        for (int slot = 0; slot < foit::kSlots; ++slot) {
            if (fr->img[slot].placeholder()) continue;
            WarmResult w;
            w.player = it.first;
            w.frame = it.second;
            w.slot = slot;
            w.d = sk->decode(it.second, slot);
            out.push_back(std::move(w));
        }
    }
    return out;
}

// Starts the loading on worker threads and switches to the Loading phase: update()/render() keep running, so the game's loading
// screen animates instead of the app freezing.  Order: 1. the stage (picture decoded + enlarged), 2. both fighters' data (.foit,
// .art, .art-pt, palette), 3. their sprites, decoded in batches on worker threads and uploaded here a few ms per frame.
void PlayScreen::beginLoad(const std::string& key) {
    pf_.clear();   // frames still being decoded for the fighters about to be replaced
    job_.reset();
    overlay_ = Overlay::None;
    loadedKey_.clear();
    refreshDev();
    for (int i = 0; i < 2; ++i) skin_[i].reset(ren_);
    applyDevToSkins(ren_);
    bg_.reset(ren_);
    shadow_.reset(ren_);
    hudPortraitsReset();
    bgName_.clear();
    bgFile_.clear();
    bgSrcW_ = bgSrcH_ = 0;
    bgPano_ = false;
    bgCenterU_ = 0.5;
    auto job = std::make_unique<LoadJob>();
    job->key = key;
    loadLabel_ = playerName(0) + "  VS  " + playerName(1);
    const LevelInfo* lv = (stageIdx_ >= 0 && stageIdx_ < static_cast<int>(levels_.size())) ? &levels_[stageIdx_] : nullptr;
    if (lv && gd_ && dev_.stage != devopt::StageSource::Generated) {
        job->bgLevel = lv->name;
        LoadJob* jp = job.get();
        const GameData* gd = gd_;
        const std::string name = lv->name, bg2d = basename(lv->background2D);
        const devopt::StageSource src = dev_.stage;
        const devopt::Upscale up = dev_.upscale;
        const int lvW = lv->width, lvH = lv->height;
        job->bg = std::async(std::launch::async, [jp, gd, name, bg2d, src, up, lvW, lvH]() {
            // The .lvl names one texture ("2D stages/textures/2D/stage_asg_labs.dds"); when that file is not part of the mounted data
            // the stage's own backdrop names are tried.  The Character Select picture of the stage (<stem>.dds, 960x540: a thumbnail,
            // not the stage) is only used when asked for (Stage backdrop = Portrait); otherwise there is no picture and the generated
            // stage is drawn.
            std::string stem = name;
            const size_t p3d = stem.find("_3d");
            if (p3d != std::string::npos) stem = stem.substr(0, p3d);
            // Every file the stage might have, real paths; the Character Select pictures are told apart by their folder
            // (Textures/charselect/stages/stage_<name>.dds - the .lvl's own "2D" name is the same file name, so the name alone is not enough).
            auto lower = [](std::string t) {
                for (char& c : t)
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
                return t;
            };
            std::vector<std::string> real2d, portraits;
            for (const std::string& want : {bg2d, stem + "_bg.dds", "stage_" + stem + ".dds", stem + ".dds"}) {
                if (want.empty()) continue;
                for (const std::string& path : gd->vfs().findByName(want)) {
                    const bool portrait = lower(path).find("charselect") != std::string::npos;
                    std::vector<std::string>& dst = portrait ? portraits : real2d;
                    if (std::find(dst.begin(), dst.end(), path) == dst.end()) dst.push_back(path);
                }
            }
            std::vector<std::string> order = real2d;
            if (src == devopt::StageSource::Portrait) order.insert(order.begin(), portraits.begin(), portraits.end());
            for (const std::string& real : order) {
                const std::string want = basename(real);
                std::vector<uint8_t> data;
                if (!readFile(real, data)) continue;
                DdsImage img;
                std::string e;
                if (!decodeDds(data, img, &e)) {
                    std::printf("[play] stage backdrop %s: %s\n", want.c_str(), e.c_str());
                    continue;
                }
                // stages/textures/2D/*.dds are stored with rotated channels (the PC's LevelFlatBG shader reads them as .argb)
                const std::string lreal = lower(real);
                const bool in2d = lreal.find("/2d/") != std::string::npos || lreal.find("\\2d\\") != std::string::npos;
                if (in2d && unrotateArgb(img.rgba.data(), img.width, img.height)) std::printf("[play] stage backdrop %s: ARGB channel order restored\n", want.c_str());
                jp->bgPano = in2d && img.width == lvW && img.height == lvH;
                jp->bgSrcW = img.width;
                jp->bgSrcH = img.height;
                jp->bgFile = basename(real);
                std::printf("[play] stage backdrop: %s (%s %dx%d, %s)\n", real.c_str(), img.formatName(), img.width, img.height, name.c_str());
                // A picture smaller than the screen it is shown on: a real bicubic 2x + a light unsharp mask instead of the GPU's bilinear.
                if (up == devopt::Upscale::Enhanced && img.width < 2048 && img.height < 1152) {
                    std::vector<uint8_t> big = upscaleCubic2x(img.rgba.data(), img.width, img.height);
                    if (!big.empty()) {
                        sharpenRgba(big.data(), img.width * 2, img.height * 2, 0.55f);
                        img.rgba = std::move(big);
                        img.width *= 2;
                        img.height *= 2;
                    }
                }
                jp->bgImg = std::move(img);
                return true;
            }
            return false;
        });
    }
    // the stage's theme (music.ini) decodes beside everything else; it starts with the match
    musicIdx_ = (music_ && music_->lib() && lv) ? music_->lib()->forFight(lv->name) : -1;
    if (musicIdx_ >= 0) {
        const MusicPlayer* mp = music_;
        const int idx = musicIdx_;
        job->music = std::async(std::launch::async, [mp, idx]() { return mp->load(idx); });
    }
    phase_ = Phase::Loading;
    job_ = std::move(job);
    if (zgFlag("ZDROID_PLAY_SYNCLOAD", "syncload")) {   // headless smoke runs: no waiting frames
        for (int guard = 0; guard < 60000 && phase_ == Phase::Loading; ++guard) {
            updateLoading(1000);
            SDL_Delay(1);
        }
    }
}

float PlayScreen::loadProgress() const {
    if (!job_) return 1.0f;
    const LoadJob& j = *job_;
    auto done = [](const auto& f) { return !f.valid() || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
    float p;
    if (!j.skinsStarted) p = done(j.bg) ? 0.25f : 0.08f;                                       // 1. stage
    else if (!j.collected) p = 0.25f + 0.2f * ((done(j.skin[0]) ? 1 : 0) + (done(j.skin[1]) ? 1 : 0)) / 2.0f;   // 2. fighters
    else p = 0.45f + 0.55f * (j.warm.empty() ? 1.0f : static_cast<float>(j.warmDone) / static_cast<float>(j.warm.size()));   // 3. sprites
    return std::clamp(p, 0.0f, 1.0f);
}

const char* PlayScreen::loadStageName() const {
    if (!job_) return "";
    return !job_->skinsStarted ? "stage" : !job_->collected ? "fighters" : "sprites";
}

void PlayScreen::updateLoading(int dtMs) {
    if (!job_) {
        finishLoad();
        return;
    }
    LoadJob& j = *job_;
    j.ms += dtMs;
    auto ready = [](auto& f) { return !f.valid() || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
    const bool sync = zgFlag("ZDROID_PLAY_SYNCLOAD", "syncload");

    // ---- 1. the stage is in: show it, then start the fighters
    if (!j.skinsStarted && ready(j.bg)) {
        j.skinsStarted = true;
        const bool haveBg = j.bg.valid() && j.bg.get();
        if (haveBg && ren_ && bg_.set(ren_, j.bgImg, dev_.smooth)) {
            bgName_ = j.bgLevel;
            bgFile_ = j.bgFile;
            bgSrcW_ = j.bgSrcW;
            bgSrcH_ = j.bgSrcH;
            bgPano_ = j.bgPano;
            bgCenterU_ = j.bgPano ? 0.5 : findCenterLine(j.bgImg);   // the panorama is the whole stage: its middle is the stage's middle
        }
        j.bgImg = DdsImage{};
        for (int i = 0; i < 2; ++i) {
            if (!gd_ || pick_[i] < 0 || pick_[i] >= static_cast<int>(roster_.size())) continue;
            Skin* sk = &skin_[i];
            const GameData* gd = gd_;
            const CharacterAssets ca = roster_[pick_[i]];
            const std::string pal = palPath_[i];
            j.skin[i] = std::async(std::launch::async, [sk, gd, ca, pal, i]() {
                std::string e;
                const bool ok = sk->loadData(*gd, ca, i, &e, pal);
                if (!ok) std::printf("[play] %s: %s\n", ca.name.c_str(), e.c_str());
                return ok;
            });
        }
    }

    // ---- 2. the fighters' data is in: plan which frames become textures before the first tick
    if (j.skinsStarted && !j.collected && ready(j.skin[0]) && ready(j.skin[1]) && (!j.music.valid() || j.music.wait_for(std::chrono::seconds(0)) == std::future_status::ready)) {
        j.collected = true;
        for (auto& f : j.skin) if (f.valid()) f.get();
        if (j.music.valid()) j.music.get();
        std::vector<uint32_t> seen;
        size_t spent[2] = {0, 0};
        auto add = [&](int p, uint16_t fi, bool limit) {
            if (fi == fight::kNoFrame) return;
            const uint32_t k = (static_cast<uint32_t>(p) << 16) | fi;
            if (std::find(seen.begin(), seen.end(), k) != seen.end()) return;
            const size_t cost = skin_[p].frameBytes(fi);
            if (limit && spent[p] + cost > skin_[p].cacheLimit() * 7 / 10) return;   // "Full" never overfills the texture budget
            spent[p] += cost;
            seen.push_back(k);
            j.warm.emplace_back(p, fi);
        };
        for (int p = 0; p < 2; ++p) {
            if (!skin_[p].hasMoves()) continue;
            const fight::MoveSet& m = skin_[p].moves();
            auto addState = [&](int s, int maxTicks, bool limit) {
                if (s < 0) return;
                for (int t = 0; t < m.ticks(s) && t < maxTicks; ++t) add(p, m.frameAt(s, t), limit);
            };
            // the loops of standing, crouching and walking (and the creature a companion object draws next to them)
            const int basic[] = {m.id.stand, m.id.crouch, m.id.crouching, m.id.walkF, m.id.walkFLoop, m.id.walkB, m.id.walkBLoop};
            for (int s : basic) {
                addState(s, 40, false);
                const int cs = s >= 0 ? m.companionFor(s, "Satiated") : -1;
                for (int t = 0; cs >= 0 && t < m.companionTicks(cs) && t < 40; ++t) add(p, m.companionFrameAt(cs, t), false);
            }
            if (dev_.preload == devopt::Preload::Minimal) continue;
            const int more[] = {m.id.jumpStart[0], m.id.jumpRise[0], m.id.dashF};
            for (int s : more) addState(s, 40, false);
            for (int b = 0; b < fight::kNormalCount; ++b)   // the first frame of each stand normal
                if (m.id.normal[0][b] >= 0) add(p, m.frameAt(m.id.normal[0][b], 0), false);
            if (dev_.preload != devopt::Preload::Full) continue;
            for (int s = 0; s < m.stateCount(); ++s) addState(s, 1 << 20, true);   // every frame the character can show, as far as the budget goes
        }
    }

    // ---- 3. sprites: hand batches to the workers, collect what they finished, upload under a time budget
    if (j.collected) {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const size_t workers = std::clamp<size_t>(hw > 1 ? hw - 1 : 1, 1, 4);
        while (j.inflight.size() < workers && j.warmNext < j.warm.size()) {
            const size_t n = std::min<size_t>(6, j.warm.size() - j.warmNext);
            std::vector<std::pair<int, uint16_t>> items(j.warm.begin() + static_cast<long>(j.warmNext), j.warm.begin() + static_cast<long>(j.warmNext + n));
            j.warmNext += n;
            const Skin *s0 = &skin_[0], *s1 = &skin_[1];
            LoadJob::Inflight b;
            b.items = n;
            b.f = std::async(std::launch::async, [s0, s1, items]() { return decodeItems(s0, s1, items); });
            j.inflight.push_back(std::move(b));
        }
        for (size_t i = 0; i < j.inflight.size();) {
            if (!ready(j.inflight[i].f)) {
                ++i;
                continue;
            }
            std::vector<WarmResult> v = j.inflight[i].f.get();
            for (WarmResult& w : v) j.ready.push_back(std::move(w));
            j.warmDone += j.inflight[i].items;
            j.inflight.erase(j.inflight.begin() + static_cast<long>(i));
        }
        const uint32_t t0 = SDL_GetTicks();
        const uint32_t budget = sync ? 100000u : 8u;
        while (j.readyPos < j.ready.size() && SDL_GetTicks() - t0 < budget) {
            WarmResult& w = j.ready[j.readyPos++];
            skin_[w.player].insert(ren_, w.frame, w.slot, std::move(w.d));
        }
        if (j.readyPos >= j.ready.size()) {
            j.ready.clear();
            j.readyPos = 0;
        }
        constexpr int kMinLoadMs = 900;   // the loading screen has to be seen, not flash by
        if (j.warmDone >= j.warm.size() && j.ready.empty() && j.ms >= kMinLoadMs) finishLoad();
    }
}

void PlayScreen::finishLoad() {
    if (job_) loadedKey_ = job_->key;
    job_.reset();
    if (const std::string prob = stageProblem(); !prob.empty()) {
        std::printf("[play] stage problem: %s\n", prob.c_str());
        stageNoteUntilMs_ = SDL_GetTicks() + 9000;
    }
    const LevelInfo* lv = (stageIdx_ >= 0 && stageIdx_ < static_cast<int>(levels_.size())) ? &levels_[stageIdx_] : nullptr;
    duelMode_ = skin_[0].hasMoves() && skin_[1].hasMoves();
    duel_ = fight::DuelContent{};
    if (duelMode_) {
        duel_.ms[0] = &skin_[0].moves();
        duel_.ms[1] = &skin_[1].moves();
        duel_.stageHalf = lv ? std::max(600, lv->width / 2 - 150) : 1700;
        for (int i = 0; i < 2; ++i)
            if (!skin_[i].hasArtPt())
                std::printf("[play] %s: no .art-pt - frames that live in it will be drawn as a dummy\n", playerName(i).c_str());
    }
    // debugging aids for headless runs (smoke screenshots): both sides on the AI, boxes visible
    if (zgFlag("ZDROID_PLAY_DEMO", "demo")) cpu_[0] = cpu_[1] = true;
    refreshDev();   // a rematch skips beginLoad
    showBoxes_ = dev_.boxes || zgFlag("ZDROID_PLAY_BOXES", "boxes");
    showFps_ = dev_.fps;
    padForced_ = zgFlag("ZDROID_PLAY_PAD", "pad");
    superDemo_ = zgFlag("ZDROID_PLAY_SUPERDEMO", "superdemo");
    superDemoTick_ = 0;
    fakeFingers_.clear();
    if (const char* ft = std::getenv("ZDROID_PLAY_FAKETOUCH")) {  // "x,y;x,y": fingers held all the time
        int64_t id = 1000;
        for (const char* p = ft; *p;) {
            int x = 0, y = 0;
            if (std::sscanf(p, "%d,%d", &x, &y) == 2) fakeFingers_.push_back(touch::Finger{id++, x, y});
            const char* semi = std::strchr(p, ';');
            if (!semi) break;
            p = semi + 1;
        }
    }
    {   // user layout: <pref dir>/touch_layout.txt (written by hand or by a future layout editor)
        touch::Layout lay = touch::Layout::defaults();
        {
            std::ifstream f(apppaths::prefFile("touch_layout.txt"));
            if (f) {
                std::stringstream ss;
                ss << f.rdbuf();
                touch::Layout user;
                if (touch::Layout::parse(ss.str(), user)) lay = user;
            }
        }
        baseLayout_ = lay;
        touchPad_.setLayout(touch::Layout::widened(baseLayout_, viewW_ - 640));
    }
    applyOptions();
    hudDemo_ = zgFlag("ZDROID_PLAY_HUDDEMO", "huddemo");
    st_ = fight::MatchState{};
    foitMode_ = buildFoit(lv);
    if (foitMode_) {   // the fight is run by the .foit data; the session drives it through the GGPO-shaped callbacks
        foit_->project(st_);
        sess_ = std::make_unique<fight::LocalSession>(st_, fight::foitDuelCallbacks(foit_.get()));
    } else {
        sess_ = std::make_unique<fight::LocalSession>(st_, duelMode_ ? fight::duelCallbacks(&duel_) : fight::defaultCallbacks());
    }
    foitPhase_ = -1;
    if (duelMode_) std::printf("[play] fight engine: %s\n", foitMode_ ? "the .foit data itself (foit_duel)" : "move tables (duel.cpp)");
    aiSeed_ = 0x9E3779B9u;
    phase_ = Phase::Fight;
    startRound();
    if (music_) {   // the stage's theme from music.ini (or a stand-in); silence when the data has no music at all
        if (!music_->play(musicIdx_)) music_->stop();
    }
    if (const char* o = std::getenv("ZDROID_PLAY_OVERLAY")) {   // headless captures of the menus
        const std::string w = o;
        if (w == "pause") openOverlay(Overlay::Pause);
        else if (w == "options") openOverlay(Overlay::Options);
        else if (w == "toast") { openOverlay(Overlay::Pause); showToast(tr("NotAvailableOffline", "Not available in this version")); }
        else if (w == "help") openOverlay(Overlay::Help);
        else if (w == "dev") openOverlay(Overlay::Dev);
        else if (w == "ko" || w == "timeup") {   // the between-rounds banner
            introStage_ = 0;
            phase_ = Phase::RoundEnd;
            endText_ = w == "ko" ? "K.O." : "TIME UP";
            endTimer_ = 100;
        }
        else if (w == "gameover") openOverlay(Overlay::GameOver);
        else if (w == "confirm") { openOverlay(Overlay::Pause); menuSel_ = 5; askConfirm(ConfirmAct::ExitMatch); }
        else if (w == "end") {
            phase_ = Phase::MatchEnd;
            lastWinner_ = 0;
            endText_ = "WIN";
            openOverlay(Overlay::End);
        }
    }
}

void PlayScreen::startRound() {
    if (foitMode_) {   // the world places the fighters and runs the entrance / card / fight itself: only the screen's own state restarts
        round_ = foit_->round();
        for (int i = 0; i < 2; ++i) {
            prevHealth_[i] = st_.p[i].health;
            hitFlash_[i] = 0;
            compState_[i] = -1;
        }
        superFx_.reset();
        superState_ = fx::SuperFxState();
        superOwner_ = -1;
        palfx_[0].reset();
        palfx_[1].reset();
        endTimer_ = 0;
        endReason_ = 0;
        endText_.clear();
        rwOn_ = rwBound_ = false;
        acc_ = 0;
        shake_ = 0;
        hudReset();
        introStage_ = 0;
        introT_ = 0;
        introBound_ = -1;
        appearSkip_ = false;
        appearPrev_ = true;
        foitPhase_ = -1;   // the phase handler below sets the card for the phase the world is in
        syncFoitFlow();
        return;
    }
    const bool intro = !training_ && dev_.roundIntro && !zgFlag("ZDROID_PLAY_NOINTRO", "nointro");
    const int introTicks = intro ? kTakeTicks : 30;
    const LevelInfo* lv = (stageIdx_ >= 0 && stageIdx_ < static_cast<int>(levels_.size())) ? &levels_[stageIdx_] : nullptr;
    // The first round of a match opens with each fighter's entrance animation (the game's IntroPose_Choice picks one at random).
    int entrance[2] = {-1, -1};
    if (intro && duelMode_ && round_ == 1 && dev_.entrances && !zgFlag("ZDROID_PLAY_NOAPPEAR", "noappear"))
        for (int i = 0; i < 2; ++i)
            if (skin_[i].hasMoves()) entrance[i] = pickIntro(skin_[i].moves());
    const bool appear = entrance[0] >= 0 || entrance[1] >= 0;
    if (duelMode_) {
        // real stage units: the spawn points of the .lvl relative to the stage centre
        const double sx = lv ? lv->start1 - lv->centre() : -200.0;
        const double ox = lv ? lv->start2 - lv->centre() : 200.0;
        for (int i = 0; i < 2; ++i)
            fight::duelStartRound(st_.p[i], skin_[i].moves(), fight::toFixed(static_cast<float>(i == 0 ? sx : ox)),
                                  i == 0 ? fight::Facing::kRight : fight::Facing::kLeft, appear ? 1 : introTicks, entrance[i]);
    } else {
        const double sx = lv ? lv->startFightX(0) : -380.0;
        const double ox = lv ? lv->startFightX(1) : 380.0;
        for (int i = 0; i < 2; ++i) {
            fight::Fighter& f = st_.p[i];
            f = fight::Fighter{};
            f.x = fight::toFixed(static_cast<float>(i == 0 ? sx : ox));
            f.health = 1000;
            f.state = fight::CharState::kIntro;
            f.facing = (i == 0) ? fight::Facing::kRight : fight::Facing::kLeft;
            f.stateTimer = static_cast<decltype(f.stateTimer)>(introTicks);  // intro before the fighters act
        }
    }
    for (int i = 0; i < 2; ++i) {
        prevHealth_[i] = 1000;
        hitFlash_[i] = 0;
    }
    st_.roundTimer = (training_ || timeLimitSec_ == 0) ? 0 : static_cast<uint16_t>(timeLimitSec_ * 60);
    st_.roundOver = 0;
    endTimer_ = 0;
    endReason_ = 0;
    endText_.clear();
    acc_ = 0;
    shake_ = 0;
    hudReset();
    introStage_ = appear ? 3 : (intro ? 1 : 0);
    introT_ = appear ? kAppearMaxTicks : kTakeTicks;
    introBound_ = -1;
    appearSkip_ = false;
    appearPrev_ = true;   // the press that confirmed the match must not skip it
    introTimer_ = st_.roundTimer;
}

// One of the move set's entrance animations: the ones that fit the cap, at random.
int PlayScreen::pickIntro(const fight::MoveSet& ms) {
    const std::vector<int> all = ms.introStates();
    if (all.empty()) return -1;
    std::vector<int> fit;
    for (int s : all)
        if (ms.ticks(s) <= kAppearMaxTicks) fit.push_back(s);
    const std::vector<int>& pool = fit.empty() ? all : fit;
    introSeed_ = introSeed_ * 1664525u + 1013904223u;
    if (const char* e = std::getenv("ZDROID_PLAY_INTRO")) return pool[static_cast<size_t>(std::atoi(e)) % pool.size()];   // smoke captures
    return pool[(introSeed_ >> 12) % pool.size()];
}

uint32_t PlayScreen::aiRandom() {
    aiSeed_ = aiSeed_ * 1664525u + 1013904223u;
    return (aiSeed_ >> 9) ^ (aiSeed_ << 7);
}

uint16_t PlayScreen::aiButtons(int me) {
    using namespace fight;
    const Fighter& a = st_.p[me];
    const Fighter& b = st_.p[1 - me];
    const double dx = fromFixed(b.x - a.x);
    const double u = duelMode_ ? 2.4 : 1.0;  // thresholds below are in the generic runtime's units
    const double dist = std::abs(dx) / u;
    const uint32_t r = aiRandom();
    const int react = difficulty_ == 0 ? 32 : (difficulty_ == 1 ? 68 : 94);
    uint16_t btn = 0;

    // Hold "away from the attacker" - that is what the runtime counts as a block.
    if (isAtk(b.state) && b.stateTimer > 1 && dist < 130 && (r % 100) < react) {
        btn |= (dx > 0) ? kRight : kLeft;
        return btn;
    }
    if (a.hitstun > 0 || a.blockstun > 0 || isAtk(a.state)) return 0;

    if (dist > 96) {
        if (dist > 380 && (r % 240) == 0 && !a.airborne) btn |= kUp;
        btn |= (dx > 0) ? kRight : kLeft;
        if ((r % 300) == 0) btn |= kDown;  // crouch now and then so it does not look robotic
    } else {
        const uint32_t pick = r % 100;
        if (pick < (difficulty_ == 0 ? 22 : 10)) {
            btn |= (dx > 0) ? kLeft : kRight;  // step out
        } else if (pick < 26) {
            btn |= kUp;
        } else if (a.airborne) {
            btn |= (r & 1) ? kLK : kMP;
        } else {
            switch (r % 8) {
                case 0:
                case 1:
                case 2: btn |= kLP; break;
                case 3:
                case 4:
                case 5: btn |= kMP; break;
                case 6: btn |= kHP; break;
                default: btn |= (r & 8) ? kLK : kMK; break;
            }
        }
    }
    return btn;
}

uint16_t PlayScreen::humanButtons(int player, Ui& ui) {
    using namespace fight;
    uint16_t b = 0;
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    if (player == 0) {
        if (ks) {
            if (ks[SDL_SCANCODE_A]) b |= kLeft;
            if (ks[SDL_SCANCODE_D]) b |= kRight;
            if (ks[SDL_SCANCODE_W]) b |= kUp;
            if (ks[SDL_SCANCODE_S]) b |= kDown;
            if (ks[SDL_SCANCODE_J]) b |= kLP;
            if (ks[SDL_SCANCODE_K]) b |= kMP;
            if (ks[SDL_SCANCODE_L]) b |= kHP;
            if (ks[SDL_SCANCODE_U]) b |= kLK;
            if (ks[SDL_SCANCODE_I]) b |= kMK;
            if (ks[SDL_SCANCODE_O]) b |= kHK;
            if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_R]) b |= kStart;
            // Arrow keys belong to P1 as long as P2 is driven by the CPU.
            if (cpu_[1]) {
                if (ks[SDL_SCANCODE_LEFT]) b |= kLeft;
                if (ks[SDL_SCANCODE_RIGHT]) b |= kRight;
                if (ks[SDL_SCANCODE_UP]) b |= kUp;
                if (ks[SDL_SCANCODE_DOWN]) b |= kDown;
            }
        }
        if (padVisible_) b |= touchPad_.update(fingers(ui));
    } else if (ks) {
        if (ks[SDL_SCANCODE_LEFT]) b |= kLeft;
        if (ks[SDL_SCANCODE_RIGHT]) b |= kRight;
        if (ks[SDL_SCANCODE_UP]) b |= kUp;
        if (ks[SDL_SCANCODE_DOWN]) b |= kDown;
        if (ks[SDL_SCANCODE_KP_1] || ks[SDL_SCANCODE_1]) b |= kLP;
        if (ks[SDL_SCANCODE_KP_2] || ks[SDL_SCANCODE_2]) b |= kMP;
        if (ks[SDL_SCANCODE_KP_3] || ks[SDL_SCANCODE_3]) b |= kHP;
        if (ks[SDL_SCANCODE_KP_4] || ks[SDL_SCANCODE_4]) b |= kLK;
        if (ks[SDL_SCANCODE_KP_5] || ks[SDL_SCANCODE_5]) b |= kMK;
        if (ks[SDL_SCANCODE_KP_6] || ks[SDL_SCANCODE_6]) b |= kHK;
    }
    return b;
}

// The picture of a super move.  The world only raises two switches on the super's owner (SUPERSHADOWS while the stage is dark around it,
// SUPERFLASH while the flash runs; the flash value is the 1-based look); the timeline turns them into smooth strengths.
void PlayScreen::superFxTick() {
    fx::SuperFxInput in;
    if (foit_) {
        const fight::FoitSuper s = foit_->superState();
        in.flash = s.flash;
        in.shadows = s.shadows;
        if (s.owner >= 0) superOwner_ = s.owner;
    }
    superFx_.step(in);
    superState_ = superFx_.state();
    if (!superState_.active() && in.flash == 0 && !in.shadows) superOwner_ = -1;
}

// What one object wears while a super is on: the owner's side stays bright, everything else goes dark; the whole picture takes the
// flash.  GPU route: the exact ScreenFx maths (and the owner's silhouette in the palette's super-outline colours during the pulse).
// CPU route: a colour multiplier per sprite (the CPU cannot remap pixels by luminance).
void PlayScreen::applySuperToSkin(Skin& sk, const fight::FoitView& v) {
    const fx::SuperFxState& st = superState_;
    if (!st.active() || superOwner_ < 0) {
        clearSuperOnSkins();
        return;
    }
    const bool lit = v.side == superOwner_ || v.superLit;
    const float dark = lit ? 0.0f : st.shadowT;
    const fx::FxTuning& tun = fxTuning();
    sk.setScreenFx(fx::ScreenFx::make(tun, false, st.look, dark, st.flashT));
    sk.setTint(1.0f - dark * (1.0f - tun.shadowTint[0]), 1.0f - dark * (1.0f - tun.shadowTint[1]), 1.0f - dark * (1.0f - tun.shadowTint[2]));
    const float a0[3] = {1, 1, 1};
    if (v.body && v.side == superOwner_ && st.flashT > 0.0f) {
        // the owner's silhouette: the two colours of its palette, or (a palette without them) the flash look's own colours
        const fx::FlashLook& look = tun.sprite.look[std::clamp(st.look, 1, 4) - 1];
        float fb0[3], fb1[3], c0[3], c1[3];
        for (int i = 0; i < 3; ++i) {
            fb0[i] = look.high[i];
            fb1[i] = look.low[i] + 0.4f * (look.high[i] - look.low[i]);
        }
        uint32_t s1 = 0, s2 = 0;
        if (gd_)
            for (const palmod::Entry& e : gd_->customPalettes().entries)
                if (!palPath_[v.side].empty() && e.dds == palPath_[v.side]) {
                    s1 = e.shadow1;
                    s2 = e.shadow2;
                    break;
                }
        fx::outlineColour(s1, fb0, c0);
        fx::outlineColour(s2, fb1, c1);
        sk.setSuperOutline(c0, c1, st.outlineMix, st.flashT);
    } else {
        sk.setSuperOutline(a0, a0, 0.0f, 0.0f);
    }
}

// Palette effects.  The script of a fighter keeps the 32-bit hash of an effect's name in four values (status / self / self2 / hit); the
// table of effects (_palette_effects.ini of the player's own data) is looked up by it and each effect plays one frame per tick.
void PlayScreen::palFxTick() {
    if (!palfxLoaded_) {
        palfxLoaded_ = true;
        std::string txt;
        if (gd_ && gd_->vfs().readText("_palette_effects.ini", txt)) std::printf("[play] palette effects: %zu\n", palfxLib_.parse(txt));
    }
    if (!foit_ || palfxLib_.size() == 0) return;
    static const bool log = zgFlag("ZDROID_PLAY_PALFXLOG", "palfxlog");   // capture aid: say when an effect starts
    for (int i = 0; i < 2; ++i) {
        const std::array<uint32_t, 4> h = foit_->palFx(i);
        if (log)
            for (int l = 0; l < palfx::kLayers; ++l)
                if (h[static_cast<size_t>(l)] != palfx_[i].layer(l).hash()) {
                    const palfx::Effect* e = palfxLib_.find(h[static_cast<size_t>(l)]);
                    std::printf("[palfx] side %d layer %d -> %08x %s\n", i, l, h[static_cast<size_t>(l)], e ? e->name.c_str() : (h[static_cast<size_t>(l)] ? "(not in the table)" : "(off)"));
                }
        palfx_[i].update(h, palfxLib_);
    }
}

// The wash of a body for the CPU route: the character's palette gives the "PAL n" colours (band n, middle tone).
void PlayScreen::applyPalFxToSkin(Skin& sk, const fight::FoitView& v) {
    if (!v.body || palfxLib_.size() == 0 || !palfx_[v.side].any()) {
        sk.setWash(1, 1, 1, 0);
        return;
    }
    const Palette& pal = sk.palette();
    const palfx::PalLookup lookup = [&pal](int band, float out[3]) {
        if (pal.tones <= 0 || band < 0 || band >= pal.bands) return false;
        const uint8_t* c = pal.at(band, pal.tones / 2);
        for (int i = 0; i < 3; ++i) out[i] = c[i] / 255.0f;
        return true;
    };
    const palfx::Flat f = palfx_[v.side].flat(lookup);
    sk.setWash(f.rgb[0], f.rgb[1], f.rgb[2], f.a);
}

void PlayScreen::clearSuperOnSkins() {
    for (Skin& sk : skin_) {
        sk.setScreenFx(fx::ScreenFx());
        sk.setTint(1, 1, 1);
        const float w[3] = {1, 1, 1};
        sk.setSuperOutline(w, w, 0.0f, 0.0f);
    }
}

void PlayScreen::superBackdropPass(SDL_Renderer* r) {
    const fx::SuperFxState& st = superState_;
    if (!r || !st.active() || superOwner_ < 0) return;
    SDL_BlendMode oldMode = SDL_BLENDMODE_NONE;
    Uint8 or_ = 0, og = 0, ob = 0, oa = 0;
    SDL_GetRenderDrawBlendMode(r, &oldMode);
    SDL_GetRenderDrawColor(r, &or_, &og, &ob, &oa);
    const fx::FxTuning& tun = fxTuning();
    if (st.shadowT > 0.0f) {   // multiply by the darkening tint, strength shadowT
        const auto c8 = [&](int i) { return static_cast<Uint8>(std::lround(255.0f * (1.0f - st.shadowT * (1.0f - tun.shadowTint[i])))); };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_MOD);
        SDL_SetRenderDrawColor(r, c8(0), c8(1), c8(2), 255);
        SDL_RenderFillRect(r, nullptr);
    }
    if (st.flashT > 0.0f && superGpu()) {   // the stage is drawn through SDL, not the effect programs: its flash is the overlay below
        const fx::FlashLook& look = tun.sprite.look[std::clamp(st.look, 1, 4) - 1];
        const auto c8 = [&](int i) { return static_cast<Uint8>(std::lround(255.0f * std::clamp(0.5f * (look.low[i] + look.high[i]), 0.0f, 1.0f))); };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, c8(0), c8(1), c8(2), static_cast<Uint8>(std::lround(140.0f * st.flashT)));
        SDL_RenderFillRect(r, nullptr);
    }
    SDL_SetRenderDrawBlendMode(r, oldMode);
    SDL_SetRenderDrawColor(r, or_, og, ob, oa);
}

void PlayScreen::superTopPass(SDL_Renderer* r) {
    const fx::SuperFxState& st = superState_;
    if (!r || st.flashT <= 0.0f || superOwner_ < 0 || superGpu()) return;
    SDL_BlendMode oldMode = SDL_BLENDMODE_NONE;
    Uint8 or_ = 0, og = 0, ob = 0, oa = 0;
    SDL_GetRenderDrawBlendMode(r, &oldMode);
    SDL_GetRenderDrawColor(r, &or_, &og, &ob, &oa);
    const fx::FlashLook& look = fxTuning().sprite.look[std::clamp(st.look, 1, 4) - 1];
    const auto c8 = [&](int i) { return static_cast<Uint8>(std::lround(255.0f * std::clamp(0.5f * (look.low[i] + look.high[i]), 0.0f, 1.0f))); };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c8(0), c8(1), c8(2), static_cast<Uint8>(std::lround(140.0f * st.flashT)));
    SDL_RenderFillRect(r, nullptr);
    SDL_SetRenderDrawBlendMode(r, oldMode);
    SDL_SetRenderDrawColor(r, or_, og, ob, oa);
}

// Hit flash and screen shake: a fighter that lost health this tick.
void PlayScreen::hitFlashTick() {
    for (int i = 0; i < 2; ++i) {
        if (st_.p[i].health < prevHealth_[i]) {
            hitFlash_[i] = 10;
            shake_ = std::max(shake_, 5);
        }
        prevHealth_[i] = st_.p[i].health;
        if (hitFlash_[i] > 0) --hitFlash_[i];
    }
    if (shake_ > 0) --shake_;
}

// The data driven fight for the two loaded fighters (nullptr-safe: false = keep the move tables of duel.cpp).
bool PlayScreen::buildFoit(const LevelInfo* lv) {
    foit_.reset();
    if (!duelMode_ || zgFlag("ZDROID_PLAY_OLDDUEL", "oldduel")) return false;
    fight::FoitDuelConfig c;
    for (int i = 0; i < 2; ++i) {
        c.ch[i] = skin_[i].moves().characterPtr();
        if (!c.ch[i]) return false;
    }
    c.startX[0] = static_cast<float>(lv ? lv->start1 - lv->centre() : -200.0);
    c.startX[1] = static_cast<float>(lv ? lv->start2 - lv->centre() : 200.0);
    c.stageHalf = static_cast<float>(duel_.stageHalf);
    c.roundSeconds = timeLimitSec_;
    c.roundsToWin = roundsToWin_;
    c.training = training_;
    const bool intro = !training_ && dev_.roundIntro && !zgFlag("ZDROID_PLAY_NOINTRO", "nointro");
    c.introPoses = intro && dev_.entrances && !zgFlag("ZDROID_PLAY_NOAPPEAR", "noappear");
    c.readyTicks = intro ? kTakeTicks : 0;   // the "Take N" card
    c.endHold = 300;   // K.O. text (<= 90 ticks) + the 208 ticks of the "Round Win" animation                         // the K.O. / TIME UP banner
    c.tieBreak = true;
    // the player's own stun table, next to the character files (read at runtime, never shipped)
    if (pick_[0] >= 0 && pick_[0] < static_cast<int>(roster_.size())) {
        const std::string& fp = roster_[static_cast<size_t>(pick_[0])].foitPath;
        const size_t sl = fp.find_last_of("/\\");
        std::vector<uint8_t> d;
        if (sl != std::string::npos && readFile(fp.substr(0, sl + 1) + "_dizzy_values.ini", d)) c.dizzyText.assign(d.begin(), d.end());
    }
    auto fd = std::make_unique<fight::FoitDuel>();
    if (!fd->init(c)) return false;
    foit_ = std::move(fd);
    return true;
}

// After every tick of the data driven fight: the world's round flow moves the screen along (cards, banners, wins, end of the match).
void PlayScreen::syncFoitFlow() {
    using M = fight::rt::Match;
    const int ph = foit_->phase();
    if (foit_->round() != round_ && ph != M::Over) {   // the world began the next round
        startRound();                                  // restarts the screen's own state, then comes back here
        return;
    }
    wins_[0] = foit_->wins(0);
    wins_[1] = foit_->wins(1);
    if (ph != foitPhase_) {
        foitPhase_ = ph;
        switch (ph) {
            case M::Intro:   // the entrance poses
                phase_ = Phase::Fight;
                introStage_ = 3;
                break;
            case M::Ready:   // the "Take N" card
                phase_ = Phase::Fight;
                introStage_ = 1;
                introT_ = kTakeTicks;
                introBound_ = -1;
                break;
            case M::Fight:   // "Action!" after the card; nothing when there was no intro
                phase_ = Phase::Fight;
                if (introStage_ == 1) {
                    introStage_ = 2;
                    introT_ = kActionTicks;
                    introBound_ = -1;
                } else {
                    introStage_ = 0;
                }
                break;
            case M::Settle:   // somebody ran out of life: the K.O. banner comes up while the fallen fighter is still on its way down
                introStage_ = 0;
                phase_ = Phase::RoundEnd;
                endReason_ = 1;
                endText_ = "K.O.";
                endTimer_ = 110;
                break;
            case M::Ending:   // the poses; a time over arrives here directly
                introStage_ = 0;
                if (phase_ != Phase::RoundEnd) {
                    phase_ = Phase::RoundEnd;
                    endTimer_ = 110;
                }
                endReason_ = foit_->roundReason() == 2 ? 2 : 1;
                endText_ = endReason_ == 2 ? "TIME UP" : "K.O.";
                beginRoundWin();
                break;
            case M::Over: {
                int w = foit_->matchWinner();
                if (w < 0) w = st_.p[0].health >= st_.p[1].health ? 0 : 1;   // only without the tie-break round
                lastWinner_ = w;
                endText_ = playerName(w) + " WINS";
                introStage_ = 0;
                phase_ = Phase::MatchEnd;
                openOverlay(Overlay::End);
                break;
            }
            default:
                break;
        }
    }
    if (introStage_ == 1 || introStage_ == 2) {   // card clock (PrePostHud.gbs animations run at 60 ticks / s like the sim)
        if (introBound_ == introStage_) introAnim_.tick();
        if (introStage_ == 2 && --introT_ <= 0) introStage_ = 0;
    }
    if (rwOn_) {   // the result banner: after the K.O. text, its own 60 Hz animation
        if (rwDelay_ > 0) --rwDelay_;
        else if (rwBound_) rwAnim_.tick();
    }
    if (endTimer_ > 0) --endTimer_;
}

void PlayScreen::stepSim(uint16_t p1, uint16_t p2) {
    if (superDemo_ && foitMode_ && foit_->fighting()) {   // capture aid: full bar, then down, down-forward, forward + two punches
        foit_->fillMeter(0);
        const int t = superDemoTick_++ - 30;
        p2 = 0;   // the other side stands still: nothing interrupts the motion
        const uint16_t kF = fight::kRight;
        const uint16_t motion[9] = {fight::kDown, fight::kDown, static_cast<uint16_t>(fight::kDown | kF), static_cast<uint16_t>(fight::kDown | kF), kF, kF,
                                    static_cast<uint16_t>(fight::kLP | fight::kMP), static_cast<uint16_t>(fight::kLP | fight::kMP),
                                    static_cast<uint16_t>(fight::kLP | fight::kMP)};
        if (t >= 0 && t < 9) p1 = motion[t];
        else if (t >= 9) p1 = 0;
    }
    int simulated = 0;
    sess_->addLocalInput(p1, p2, &simulated);
    hudTick();
    if (foitMode_) {
        syncFoitFlow();
        hitFlashTick();
        superFxTick();
        palFxTick();
        return;
    }
    if (introStage_ == 3) {   // the entrances: until both fighters are on their feet (or somebody presses an attack)
        st_.roundTimer = introTimer_;
        const bool done = st_.p[0].state != fight::CharState::kIntro && st_.p[1].state != fight::CharState::kIntro;
        if (done || appearSkip_ || --introT_ <= 0) {
            if (duelMode_)
                for (int i = 0; i < 2; ++i) fight::duelSkipIntro(st_.p[i], skin_[i].moves());
            introStage_ = 1;
            introT_ = kTakeTicks;
            introBound_ = -1;
            appearSkip_ = false;
        }
    } else if (introStage_ > 0) {   // round intro clock (PrePostHud.gbs animations run at 60 ticks / s like the sim)
        if (introStage_ == 1) st_.roundTimer = introTimer_;
        if (introBound_ == introStage_) introAnim_.tick();
        if (--introT_ <= 0) {
            if (introStage_ == 1) {
                introStage_ = 2;
                introT_ = kActionTicks;
                introBound_ = -1;
            } else {
                introStage_ = 0;
            }
        }
    }

    hitFlashTick();

    for (int i = 0; i < 2; ++i) {   // companion object clock (Umbrella's Hungern): restarts when its state changes
        compTick_[i]++;
        if (!duelMode_ || !skin_[i].hasMoves() || !skin_[i].moves().hasCompanion()) {
            compState_[i] = -1;
            continue;
        }
        const fight::MoveSet& m = skin_[i].moves();
        const int cs = m.companionFor(st_.p[i].mv, "Satiated");
        if (cs != compState_[i]) {
            compState_[i] = cs;
            compTick_[i] = 0;
        } else if (cs >= 0 && m.companionTicks(cs) > 0) {
            compTick_[i] %= m.companionTicks(cs);
        }
    }

    if (training_) {  // training never ends: health refills, the clock stays off
        for (int i = 0; i < 2; ++i) st_.p[i].health = 1000;
        st_.roundOver = 0;
    }

    if (!st_.roundOver && !training_ && timeLimitSec_ > 0 && st_.roundTimer == 0) {
        st_.roundOver = 1;
        endReason_ = 2;  // timeout
    }
    if (st_.roundOver && phase_ == Phase::Fight) {
        endReason_ = endReason_ ? endReason_ : 1;
        endText_ = endReason_ == 2 ? "TIME UP" : "K.O.";
        phase_ = Phase::RoundEnd;
        endTimer_ = 110;
        introStage_ = 0;
    }
    if (phase_ == Phase::RoundEnd && --endTimer_ <= 0) {
        const int loser = st_.p[0].health <= st_.p[1].health ? 0 : 1;
        const int winner = 1 - loser;
        if (st_.p[0].health != st_.p[1].health) ++wins_[winner];
        if (wins_[winner] >= roundsToWin_ || wins_[0] + wins_[1] >= 2 * roundsToWin_ + 1) {
            endText_ = playerName(winner) + " WINS";
            lastWinner_ = winner;
            phase_ = Phase::MatchEnd;
            openOverlay(Overlay::End);
        } else {
            ++round_;
            startRound();
            phase_ = Phase::Fight;
        }
    }
}

std::vector<touch::Finger> PlayScreen::fingers(const Ui& ui) const {
    std::vector<touch::Finger> out = fakeFingers_;
    for (size_t i = 0; i < ui.in.touches.size(); ++i) {
        const int64_t id = i < ui.in.touchIds.size() ? ui.in.touchIds[i] : static_cast<int64_t>(i);
        out.push_back(touch::Finger{id, ui.in.touches[i].first, ui.in.touches[i].second});
    }
    return out;
}

void PlayScreen::update(Ui& ui, int dtMs) {
    padVisible_ = padForced_ || SDL_GetNumTouchDevices() > 0;
    if (phase_ == Phase::Select) return;
    if (phase_ == Phase::Loading) {
        updateLoading(dtMs);
        return;
    }

    if (const Uint8* ks = SDL_GetKeyboardState(nullptr)) {
        const bool f1 = ks[SDL_SCANCODE_F1] != 0;
        if (f1 && !f1Prev_) showBoxes_ = !showBoxes_;
        f1Prev_ = f1;
        const bool f2 = ks[SDL_SCANCODE_F2] != 0;
        if (f2 && !f2Prev_) showFps_ = !showFps_;
        f2Prev_ = f2;
    }
    const uint16_t startBit = overlay_ == Overlay::End ? 0 : (humanButtons(0, ui) & fight::kStart);
    if (startBit && !startPrev_ && (phase_ == Phase::Fight || phase_ == Phase::RoundEnd)) {   // Start = pause, like the PC game
        if (overlay_ == Overlay::None) openOverlay(Overlay::Pause);
        else if (overlay_ == Overlay::Pause && !confirm_.isOpen()) overlay_ = Overlay::None;
    }
    startPrev_ = startBit != 0;
    if (overlay_ == Overlay::Pause || overlay_ == Overlay::Options || overlay_ == Overlay::Help || overlay_ == Overlay::Dev) return;   // the match is frozen behind the menu

    if (dtMs > 0) frameMs_ = frameMs_ * 0.92 + std::min(dtMs, 100) * 0.08;
    acc_ += dtMs;
    if (acc_ > 100) acc_ = 100;  // never fast-forward after a stall
    int guard = 0;
    while (acc_ >= kTickMs && guard++ < 5) {
        acc_ -= kTickMs;
        uint16_t p1 = cpu_[0] ? aiButtons(0) : humanButtons(0, ui);
        uint16_t p2 = cpu_[1] ? aiButtons(1) : humanButtons(1, ui);
        if (foitMode_) {
            // The world gates the pad itself (nobody acts during the entrance / the card); what must get through is a human's attack
            // press, which cuts the entrance short - decided inside the world from the replicated input.
            if (introStage_ == 3) {
                constexpr uint16_t kAtk = fight::kLP | fight::kMP | fight::kHP | fight::kLK | fight::kMK | fight::kHK;
                p1 = cpu_[0] ? 0 : static_cast<uint16_t>(p1 & kAtk);
                p2 = cpu_[1] ? 0 : static_cast<uint16_t>(p2 & kAtk);
            } else if (introStage_ == 1) {
                p1 = p2 = 0;
            }
            stepSim(p1, p2);
            continue;
        }
        if (introStage_ == 3) {   // an attack button cuts the entrances short (any human; the press that started the match does not count)
            const uint16_t atk = ((cpu_[0] ? 0 : humanButtons(0, ui)) | (cpu_[1] ? 0 : humanButtons(1, ui))) &
                                 (fight::kLP | fight::kMP | fight::kHP | fight::kLK | fight::kMK | fight::kHK);
            if (atk && !appearPrev_) appearSkip_ = true;
            appearPrev_ = atk != 0;
        }
        if (introStage_ == 1 || introStage_ == 3) p1 = p2 = 0;   // the entrances / the "Take N" card: nobody acts yet
        stepSim(p1, p2);
    }
    prefetchFrames();
}

// Looks a few ticks ahead on each fighter's own timeline and has the frames that are not cached yet decoded on worker threads;
// the finished pixels become textures here, a few ms per update, so no frame is ever decoded in the middle of a tick.
void PlayScreen::prefetchFrames() {
    if (!ren_ || !duelMode_ || !sess_) return;
    // 1. finished decodes -> textures
    const uint32_t t0 = SDL_GetTicks();
    for (size_t i = 0; i < pf_.size();) {
        if (pf_[i].f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++i;
            continue;
        }
        std::vector<WarmResult> v = pf_[i].f.get();
        for (WarmResult& w : v) skin_[w.player].insert(ren_, w.frame, w.slot, std::move(w.d));
        pf_.erase(pf_.begin() + static_cast<long>(i));
        if (SDL_GetTicks() - t0 > 4) break;
    }
    // 2. what is coming
    int launched = 0;
    auto want = [&](int p, uint16_t fi) {
        if (fi == fight::kNoFrame || pf_.size() >= 8 || launched >= 3) return;
        const foit::Frame* fr = skin_[p].moves().frame(fi);
        if (!fr) return;
        bool missing = false;
        for (int slot = 0; slot < foit::kSlots && !missing; ++slot)
            if (!fr->img[slot].placeholder() && !skin_[p].cached(fi, slot)) missing = true;
        if (!missing) return;
        for (const Prefetch& q : pf_)
            if (q.player == p && q.frame == fi) return;
        Prefetch q;
        q.player = p;
        q.frame = fi;
        const Skin *s0 = &skin_[0], *s1 = &skin_[1];
        std::vector<std::pair<int, uint16_t>> items{{p, fi}};
        q.f = std::async(std::launch::async, [s0, s1, items]() { return decodeItems(s0, s1, items); });
        pf_.push_back(std::move(q));
        ++launched;
    };
    if (foitMode_) {   // the world knows what is on screen and which nodes come next
        foit_->views(views_);
        for (const fight::FoitView& v : views_)   // spawned objects appear unannounced: their frame is wanted from the first tick
            if (v.frame >= 0) want(v.side, static_cast<uint16_t>(v.frame));
        std::vector<uint16_t> ahead;
        for (int p = 0; p < 2; ++p) {
            foit_->lookahead(p, ahead, 24);
            for (uint16_t fi : ahead) want(p, fi);
        }
        return;
    }
    for (int p = 0; p < 2; ++p) {
        Skin& sk = skin_[p];
        if (!sk.hasMoves()) continue;
        const fight::MoveSet& m = sk.moves();
        const fight::Fighter& f = st_.p[p];
        int s = f.mv, t = f.mvTick;
        for (int k = 0; k < 24; ++k) {
            ++t;
            while (m.ticks(s) > 0 && t >= m.ticks(s)) {
                const int n = m.next(s);
                if (n < 0 || n == s) {
                    t = m.ticks(s) - 1;
                    break;
                }
                t -= m.ticks(s);
                s = n;
            }
            if (m.ticks(s) <= 0) break;
            want(p, m.frameAt(s, t));
        }
        const int cs = compState_[p];   // the companion object's next frames (Umbrella's creature)
        for (int k = 1; cs >= 0 && k <= 12; ++k) {
            const int ct = m.companionTicks(cs) > 0 ? (compTick_[p] + k) % m.companionTicks(cs) : 0;
            want(p, m.companionFrameAt(cs, ct));
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void PlayScreen::render(Ui& ui, SDL_Renderer* r) {
    ren_ = r;
    if (phase_ == Phase::Select) {   // no match running: the menu's own Character Select is the front door
        ui.fill(Rect{0, 0, 640, 360}, kBg);
        return;
    }
    if (phase_ == Phase::Loading) {
        drawLoading(ui, r);
        return;
    }
    const bool scaled = beginScaled(r);   // Render resolution < Native: the whole match is drawn smaller, then stretched over the window
    drawWorld(ui, r);
    // No Limits Screen: the world above used the whole picture; the HUD, the banners and the menus are made for 640x360 and are drawn
    // into the middle of it (beginOverlay() / endOverlay() do nothing at 640).  The touch pad sits between them in the full width.
    const bool wide = viewW_ > 640;
    bool inSafe = wide && beginOverlay(r, ui);
    drawHud(ui, r);
    if (arcadeMode_ && overlay_ == Overlay::None && (phase_ == Phase::Fight || phase_ == Phase::RoundEnd)) drawArcadeTag(ui, r);
    if (inSafe) endOverlay(r, ui);
    if (padVisible_ && overlay_ == Overlay::None) drawPad(ui, r);

    inSafe = wide && beginOverlay(r, ui);
    if (phase_ == Phase::Fight && (introStage_ == 1 || introStage_ == 2)) drawRoundIntro(ui, r);
    if (phase_ == Phase::RoundEnd && overlay_ == Overlay::None) drawRoundEnd(ui, r);
    if (overlay_ != Overlay::None) drawOverlay(ui, r);
    if (inSafe) endOverlay(r, ui);
    if (scaled) endScaled(r);
    if (dev_.stageInfo) drawStageInfo(ui);   // outside the smaller picture: readable at any render resolution
    else if (SDL_GetTicks() < stageNoteUntilMs_) {   // the first seconds of a fight that is not on the stage that was asked for
        if (const std::string prob = stageProblem(); !prob.empty()) {
            ui.fill(Rect{3, 340, 4 + Ui::textW(prob), 12}, Col{0, 0, 0, 170});
            ui.text(5, 342, prob, Col{255, 120, 100, 255});
        }
    }
}

void PlayScreen::setViewWidth(int w) {
    w = std::clamp(w, 640, 960);
    if (w == viewW_) return;
    viewW_ = w;
    touchPad_.setLayout(touch::Layout::widened(baseLayout_, viewW_ - 640));   // the buttons on the right follow the real edge
}

// The 640x360 parts of a wide picture are drawn into a transparent target the size of the safe area on the device (same pixel density as the
// picture itself) and copied to its middle by endOverlay().  While they are drawn the pointer is expressed in 640-space too, so the menus'
// own hit tests keep working.  Returns false (and draws nothing special) when the target cannot be made: the parts are then drawn in place.
bool PlayScreen::beginOverlay(SDL_Renderer* r, Ui& ui) {
    if (!r) return false;
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(r, &sx, &sy);
    const int tw = std::max(64, static_cast<int>(std::lround(640.0 * sx)));
    const int th = std::max(36, static_cast<int>(std::lround(360.0 * sy)));
    if (!ovRt_ || ovW_ != tw || ovH_ != th) {
        if (ovRt_) SDL_DestroyTexture(ovRt_);
        ovRt_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
        ovW_ = tw;
        ovH_ = th;
        if (ovRt_) {
            SDL_SetTextureScaleMode(ovRt_, SDL_ScaleModeNearest);
            // The target holds colours already multiplied by their alpha (what drawing with BLEND over transparent leaves there), so it is
            // laid over the picture with "one minus source alpha" and no second multiply.  A renderer that cannot do that (the software
            // one) falls back to plain BLEND: translucent edges come out a little darker, nothing else changes.
            const SDL_BlendMode premul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                                                    SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
            if (SDL_SetTextureBlendMode(ovRt_, premul) != 0) SDL_SetTextureBlendMode(ovRt_, SDL_BLENDMODE_BLEND);
        }
    }
    if (!ovRt_) return false;
    ovPrev_ = SDL_GetRenderTarget(r);
    ovPrevSx_ = sx;
    ovPrevSy_ = sy;
    if (SDL_SetRenderTarget(r, ovRt_) != 0) return false;
    SDL_RenderSetScale(r, sx, sy);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    ovInput_ = ui.in;
    const int dx = safeX();
    ui.in.mx -= dx;
    ui.in.pmx -= dx;
    for (auto& t : ui.in.touches) t.first -= dx;
    return true;
}

void PlayScreen::endOverlay(SDL_Renderer* r, Ui& ui) {
    ui.in = std::move(ovInput_);
    SDL_SetRenderTarget(r, ovPrev_);
    if (ovPrev_) SDL_RenderSetScale(r, ovPrevSx_, ovPrevSy_);   // a texture target forgets the scale; the window restores its own
    if (!ovRt_) return;
    const int dx = safeX();
    const SDL_Rect mid{dx, 0, 640, 360};
    SDL_RenderCopy(r, ovRt_, nullptr, &mid);
    // Full-width bars and dimming (menus, banners) continue into the side bands: the outermost column is stretched out over them.  Parts
    // that stop short of the edge leave that column transparent, so nothing is added there.
    if (dx > 0) {
        const SDL_Rect colL{0, 0, 1, ovH_}, colR{ovW_ - 1, 0, 1, ovH_};
        const SDL_Rect bandL{0, 0, dx, 360}, bandR{dx + 640, 0, viewW_ - 640 - dx, 360};
        SDL_RenderCopy(r, ovRt_, &colL, &bandL);
        SDL_RenderCopy(r, ovRt_, &colR, &bandR);
    }
}

bool PlayScreen::beginScaled(SDL_Renderer* r) {
    if (!r || dev_.renderScale >= 0.999) {
        if (rt_ && !r) rt_ = nullptr;
        if (rt_ && r) {   // back to Native: the texture is not needed any more
            SDL_DestroyTexture(rt_);
            rt_ = nullptr;
        }
        return false;
    }
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(r, &sx, &sy);   // window pixels per logical pixel
    const int tw = std::max(64, static_cast<int>(std::lround(viewW_ * sx * dev_.renderScale)));
    const int th = std::max(36, static_cast<int>(std::lround(360.0 * sy * dev_.renderScale)));
    if (!rt_ || rtW_ != tw || rtH_ != th) {
        if (rt_) SDL_DestroyTexture(rt_);
        rt_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
        rtW_ = tw;
        rtH_ = th;
        if (rt_) SDL_SetTextureScaleMode(rt_, dev_.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    if (!rt_ || SDL_SetRenderTarget(r, rt_) != 0) return false;
    SDL_RenderSetScale(r, static_cast<float>(tw) / static_cast<float>(viewW_), static_cast<float>(th) / 360.0f);   // keep drawing in logical units
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    return true;
}

void PlayScreen::endScaled(SDL_Renderer* r) {
    SDL_SetRenderTarget(r, nullptr);   // restores the window's logical size and scale
    if (!rt_) return;
    SDL_SetTextureBlendMode(rt_, SDL_BLENDMODE_NONE);
    const SDL_Rect full{0, 0, viewW_, 360};
    SDL_RenderCopy(r, rt_, nullptr, &full);
}

void PlayScreen::drawStageInfo(Ui& ui) {
    char a[200], b[200];
    const std::string stage = (stageIdx_ >= 0 && stageIdx_ < static_cast<int>(levels_.size())) ? levels_[static_cast<size_t>(stageIdx_)].name : "?";
    if (bg_.tex)
        std::snprintf(a, sizeof a, "STAGE %s  backdrop %s  %dx%d%s", stage.c_str(), bgFile_.c_str(), bgSrcW_, bgSrcH_,
                      (bg_.w > bgSrcW_ && bgSrcW_ > 0) ? (" -> " + std::to_string(bg_.w) + "x" + std::to_string(bg_.h) + " enhanced").c_str() : "");
    else
        std::snprintf(a, sizeof a, "STAGE %s  backdrop: generated (no 2D picture in the data)", stage.c_str());
    char res[48];
    if (rt_ && dev_.renderScale < 0.999) std::snprintf(res, sizeof res, "%dx%d (%d%%)", rtW_, rtH_, static_cast<int>(std::lround(dev_.renderScale * 100)));
    else std::snprintf(res, sizeof res, "native");
    std::snprintf(b, sizeof b, "render %s  sprites level %d (%.2f px/art px)  textures %.0f/%.0f MB  %s", res, skin_[0].level(), spritePx_,
                  (skin_[0].cacheUsed() + skin_[1].cacheUsed()) / 1048576.0, (skin_[0].cacheLimit() + skin_[1].cacheLimit()) / 1048576.0,
                  dev_.smooth ? "smooth" : "pixelated");
    char c[200];
    std::snprintf(c, sizeof c, "GPU effects %s%s", fxStatus_.empty() ? "off" : fxStatus_.c_str(),
                  (skin_[0].gpuActive() || skin_[1].gpuActive()) ? "  (sprites on the GPU route)" : "");
    const int wmax = std::max({Ui::textW(a), Ui::textW(b), Ui::textW(c)});
    ui.fill(Rect{3, 316, 5 + wmax, 32}, Col{0, 0, 0, 150});
    ui.text(5, 318, a, Col{255, 236, 150, 255});
    ui.text(5, 328, b, Col{200, 230, 255, 255});
    ui.text(5, 338, c, Col{190, 255, 200, 255});
}

// The game's own loading screen (bomb + "LOADING..." over the line-art slideshow) with a thin progress bar and the
// names of the two fighters; without the loading_<lang> files a plain version of it.
void PlayScreen::drawLoading(Ui& ui, SDL_Renderer* r) {
    const int ms = job_ ? job_->ms : 0;
    // The stage is loaded first and shows as soon as it is in (cover-fitted, dimmed), with the loading animation over it while the
    // fighters and their sprites come in.
    const bool overStage = bg_.tex != nullptr;
    if (overStage) {
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 255});
        const double ba = static_cast<double>(bg_.w) / std::max(1, bg_.h), sa = 640.0 / 360.0;
        SDL_Rect src{0, 0, bg_.w, bg_.h};
        if (ba > sa) {   // wider than the screen: the middle of the stage
            src.w = static_cast<int>(std::lround(bg_.h * sa));
            src.x = static_cast<int>(std::lround(std::clamp(bgCenterU_ * bg_.w - src.w / 2.0, 0.0, static_cast<double>(bg_.w - src.w))));
        } else {
            src.h = static_cast<int>(std::lround(bg_.w / sa));
            src.y = std::max(0, bg_.h - src.h);   // the floor end of the picture
        }
        const SDL_Rect full{0, 0, 640, 360};
        SDL_RenderCopy(r, bg_.tex, &src, &full);
        ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, 60});
    }
    if (drawLoadingScene(r, ms, overStage)) {
        // LoadingScreen.gbs (the game's loader between screens): black + the heart bug
    } else {
        if (!splashTried_ && gd_) {   // no LoadingScreen.gbs: the first-boot splash, else a plain text
            splashTried_ = true;
            hud_.attach(r, &gd_->vfs());
            splash_.load(r, gd_->vfs(), lang_);
        }
        if (splash_.ok()) {
            hud_.setViewport(0, 0, 0.5f);
            splash_.draw(hud_, ms);
        } else {
            if (!overStage) ui.fill(Rect{0, 0, 640, 360}, Col{6, 6, 10, 255});
            const std::string t = ms % 900 < 450 ? "LOADING..." : "LOADING";
            ui.text(320 - Ui::textW(t, 3) / 2, 150, t, kAccent, 3);
        }
    }
    const float p = loadProgress();
    const bool es = lang_ == "es";
    const std::string st = job_ ? (std::string(loadStageName()) == "stage" ? (es ? "Cargando escenario..." : "Loading stage...")
                                   : std::string(loadStageName()) == "fighters" ? (es ? "Cargando luchadores..." : "Loading fighters...")
                                                                                : (es ? "Preparando sprites..." : "Preparing sprites..."))
                                : std::string();
    ui.text(22, 310, st, Col{232, 200, 120, 255});
    ui.text(22, 322, loadLabel_, Col{214, 208, 190, 255});
    if (const std::string prob = stageProblem(); !prob.empty()) {   // why this is not the stage that was picked
        ui.fill(Rect{14, 291, 4 + Ui::textW(prob), 12}, Col{0, 0, 0, 170});
        ui.text(16, 293, prob, Col{255, 120, 100, 255});
    }
    ui.fill(Rect{20, 338, 330, 5}, Col{30, 30, 38, 220});
    ui.fill(Rect{20, 338, static_cast<int>(330 * p), 5}, Col{232, 200, 120, 255});
}

namespace {
// A vertical colour ramp as one quad with per-vertex colours (smooth at any resolution, unlike one-row strips).
void gradientRect(SDL_Renderer* r, float x, float y, float w, float h, Col top, Col bot) {
    if (w <= 0 || h <= 0) return;
    SDL_Vertex v[4];
    v[0].position = {x, y};
    v[1].position = {x + w, y};
    v[2].position = {x, y + h};
    v[3].position = {x + w, y + h};
    v[0].color = v[1].color = SDL_Color{top.r, top.g, top.b, top.a};
    v[2].color = v[3].color = SDL_Color{bot.r, bot.g, bot.b, bot.a};
    for (SDL_Vertex& q : v) q.tex_coord = {0, 0};
    static const int idx[6] = {0, 1, 2, 2, 1, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}
void gradientRectH(SDL_Renderer* r, float x, float y, float w, float h, Col left, Col right) {
    if (w <= 0 || h <= 0) return;
    SDL_Vertex v[4];
    v[0].position = {x, y};
    v[1].position = {x + w, y};
    v[2].position = {x, y + h};
    v[3].position = {x + w, y + h};
    v[0].color = v[2].color = SDL_Color{left.r, left.g, left.b, left.a};
    v[1].color = v[3].color = SDL_Color{right.r, right.g, right.b, right.a};
    for (SDL_Vertex& q : v) q.tex_coord = {0, 0};
    static const int idx[6] = {0, 1, 2, 2, 1, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}
void fillRectF(SDL_Renderer* r, double x, double y, double w, double h, Col c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    const SDL_FRect rc{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    SDL_RenderFillRectF(r, &rc);
}
}  // namespace

// The stage when the data has no 2D picture for it: sky from the level's ambient light, three layers of skyline that slide at
// different speeds with the camera, a lit floor with perspective lines and a soft vignette.  A stand-in, not the PC stage (that is
// a 3D scene in meshes this port does not have).
void PlayScreen::drawGeneratedStage(Ui& ui, const LevelInfo* lv, double cx, double s, int floorY, double bound) {
    (void)bound;
    SDL_Renderer* r = ui.r;
    const int amb[3] = {lv ? lv->ambient[0] : 85, lv ? lv->ambient[1] : 96, lv ? lv->ambient[2] : 122};
    const Col ambient{static_cast<uint8_t>(amb[0]), static_cast<uint8_t>(amb[1]), static_cast<uint8_t>(amb[2]), 255};
    const Col skyTop = mixCol(Col{8, 8, 14, 255}, ambient, 0.30);
    const Col skyMid = mixCol(ambient, Col{255, 214, 190, 255}, 0.18);
    const Col skyBot = mixCol(ambient, Col{255, 226, 200, 255}, 0.34);
    const float fy = static_cast<float>(floorY);
    const float vw = static_cast<float>(viewW_);   // the picture's width (640, or wider with No Limits Screen)
    gradientRect(r, 0, 0, vw, fy * 0.62f, skyTop, skyMid);
    gradientRect(r, 0, fy * 0.62f, vw, fy * 0.38f + 1, skyMid, skyBot);

    // skyline: far (hazy, slow) to near (dark, faster); the buildings are fixed per stage
    struct Layer { double par, hMin, hRange, wMin, wRange, haze; };
    static const Layer layers[3] = {{0.10, 30, 70, 22, 40, 0.62}, {0.22, 34, 95, 26, 46, 0.40}, {0.38, 28, 120, 30, 56, 0.18}};
    uint32_t rs = 0x1234u + static_cast<uint32_t>(stageIdx_) * 7919u;
    for (int li = 0; li < 3; ++li) {
        const Layer& L = layers[li];
        const double reach = 560.0 + 0.5 * (viewW_ - 640);
        double wx = -reach;   // layer space: the layer slides by `par` of the camera's pan, so 560 px either side of the picture is always enough
        const Col body = mixCol(Col{16, 16, 26, 255}, skyBot, L.haze);
        const Col lit = Col{255, 226, 150, static_cast<uint8_t>(210 - 120 * L.haze)};
        while (wx < reach) {
            rs = rs * 1103515245u + 12345u;
            const double bw = L.wMin + static_cast<double>((rs >> 16) % static_cast<uint32_t>(L.wRange));
            const double bh = L.hMin + static_cast<double>((rs >> 9) % static_cast<uint32_t>(L.hRange));
            const double px = 0.5 * vw + wx - (cx * s) * L.par;
            if (px + bw >= 0 && px <= vw) {
                fillRectF(r, px, fy - bh, bw, bh + 1, body);
                // a few lit windows
                for (int k = 0; k < 4; ++k) {
                    rs = rs * 1103515245u + 12345u;
                    if ((rs >> 20) % 3 != 0) continue;
                    const double wxk = px + 3 + static_cast<double>((rs >> 8) % static_cast<uint32_t>(std::max(4.0, bw - 8)));
                    const double wyk = fy - bh + 5 + static_cast<double>((rs >> 14) % static_cast<uint32_t>(std::max(6.0, bh - 14)));
                    fillRectF(r, wxk, wyk, 2.0, 2.0, lit);
                }
            } else {
                rs = rs * 1103515245u + 12345u;   // keep the random sequence the same whether the building is on screen or not
                rs = rs * 1103515245u + 12345u;
                rs = rs * 1103515245u + 12345u;
                rs = rs * 1103515245u + 12345u;
            }
            wx += bw + 4 + static_cast<double>((rs >> 5) % 14);
        }
    }
    // haze where the sky meets the floor
    gradientRect(r, 0, fy - 40.0f, vw, 40.0f, Col{skyBot.r, skyBot.g, skyBot.b, 0}, Col{skyBot.r, skyBot.g, skyBot.b, 120});

    // floor: lit near the horizon, dark towards the camera, with perspective lines
    const Col floorFar = mixCol(Col{26, 22, 30, 255}, skyBot, 0.18);
    const Col floorNear = Col{8, 7, 11, 255};
    gradientRect(r, 0, fy, vw, 360.0f - fy + 1, floorFar, floorNear);
    fillRectF(r, 0, fy, vw, 1.5, mixCol(Col{120, 108, 120, 255}, skyBot, 0.35));
    const double vpX = 0.5 * vw - cx * s * 0.2;   // vanishing point of the floor lines
    for (int gx = -4000; gx <= 4000; gx += 200) {
        const double bx = 0.5 * vw + (gx - cx) * s;
        const double tx = vpX + (bx - vpX) * 0.30;
        if (std::max(bx, tx) < -20 || std::min(bx, tx) > vw + 20) continue;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 150, 140, 160, 46);
        SDL_RenderDrawLineF(r, static_cast<float>(tx), fy, static_cast<float>(bx), 360.0f);
    }
    for (int j = 1; j <= 5; ++j) {
        const double ty = fy + (360.0 - fy) * (1.0 - std::pow(0.55, j));
        fillRectF(r, 0, ty, vw, 1.0, Col{150, 140, 160, static_cast<uint8_t>(52 - 6 * j)});
    }
    // vignette
    gradientRectH(r, 0, 0, 90, 360, Col{0, 0, 0, 110}, Col{0, 0, 0, 0});
    gradientRectH(r, vw - 90, 0, 90, 360, Col{0, 0, 0, 0}, Col{0, 0, 0, 110});
}

void PlayScreen::drawWorld(Ui& ui, SDL_Renderer* r) {
    const LevelInfo* lv = (stageIdx_ >= 0 && stageIdx_ < static_cast<int>(levels_.size())) ? &levels_[stageIdx_] : nullptr;

    // ---- camera: keep both fighters on screen, stay inside the stage; lift it when somebody jumps high
    const double x0 = fight::fromFixed(st_.p[0].x), x1 = fight::fromFixed(st_.p[1].x);
    const double sep = std::abs(x1 - x0);
    double s;
    const double vex = duelMode_ ? 1.0 : kVertExag;  // the generic runtime's jumps are tiny: stretch them
    double bound;
    if (duelMode_) {  // stage units = sprite pixels
        s = std::clamp(640.0 / std::clamp(sep + 900.0, 1300.0, 2600.0), 0.22, 0.52);
        bound = duel_.stageHalf + 160.0;
    } else {
        s = std::clamp(600.0 / std::clamp(sep + 700.0, 1000.0, 2500.0), 0.15, 0.42);
        bound = kStageHalf;
    }
    if (bgPano_ && lv && duelMode_) bound = std::min(bound, lv->width * 0.5);   // the picture ends at the stage's edge: never show beyond it
    // No Limits Screen: a wider picture shows more stage at both sides; when even the whole stage is narrower than the picture, zoom in
    // just enough that it is not (nothing is stretched, the stage is never shown past its edges)
    if (viewW_ > 640 && 2.0 * bound * s < viewW_) s = viewW_ / (2.0 * bound);
    double lift = 0;
    if (duelMode_) {
        // somebody high up (a launch, a jump): the floor sinks a little to keep them in the picture, and past that the camera zooms out
        // instead of pushing the other fighter's feet off the bottom of the screen
        const double top = std::max(fight::fromFixed(st_.p[0].y), fight::fromFixed(st_.p[1].y)) + 340.0;
        lift = std::clamp(top * s - (kFloorY - kSkyTop - 6), 0.0, 40.0);
        const double room = kFloorY + lift - kSkyTop - 6;
        if (top * s > room) s = std::max(0.22, room / top);
    }
    const double halfSpan = 0.5 * viewW_ / s;
    double cx = (x0 + x1) * 0.5;
    if (halfSpan < bound) cx = std::clamp(cx, -bound + halfSpan, bound - halfSpan);
    const double floorYd = kFloorY + lift;
    const int floorY = static_cast<int>(floorYd);
    const int shx = shake_ ? ((shake_ % 2) ? 2 : -2) : 0;
    auto sx = [&](double ux) { return 0.5 * viewW_ + (ux - cx) * s + shx; };
    auto sy = [&](double uy) { return floorYd - uy * s * vex; };

    if (bg_.tex) {
        // ---- sky behind the picture (visible only where the picture does not reach)
        const int amb[3] = {lv ? lv->ambient[0] : 85, lv ? lv->ambient[1] : 96, lv ? lv->ambient[2] : 122};
        const Col ambient{static_cast<uint8_t>(amb[0]), static_cast<uint8_t>(amb[1]), static_cast<uint8_t>(amb[2]), 255};
        gradientRect(r, 0, 0, static_cast<float>(viewW_), static_cast<float>(floorYd), mixCol(Col{10, 10, 16, 255}, ambient, 0.35), mixCol(ambient, Col{255, 210, 180, 255}, 0.25));

        // ---- pre-rendered 2D backdrop from the .lvl (decoded while the loading screen was up, see beginLoad)
        // The picture is the whole stage seen from the front, so it is locked to the stage like every prop in the 3D level: its width
        // is the stage's width on screen (2 * bound * s), it slides with the camera and grows when the camera zooms in.  Its ground
        // line (~90% down: measured on the char-select pictures) sits on the fighters' floor, and what is below it is picture too, so
        // there is no band under the floor.  Positions are floats: the picture glides instead of stepping a logical pixel at a time.
        const double aspect = static_cast<double>(bg_.w) / std::max(1, bg_.h);
        // A real PC backdrop (3750x2000 = StageSize) is the stage's whole front view in stage units: 1 pixel = 1 unit, the floor is
        // BottomClearance units above its bottom edge.  The generated / Character Select pictures keep the measured 0.897.
        const bool pano = bgPano_ && lv;
        const double kGround = pano ? 1.0 - static_cast<double>(lv->bottomClearance) / std::max(1, lv->height) : 0.897;
        double dw = pano ? (duelMode_ ? static_cast<double>(lv->width) : 2.0 * bound) * s : std::max(static_cast<double>(viewW_), 2.0 * bound * s);
        double dh = dw / aspect;
        if (dh * kGround < floorYd) {   // zoomed far out and lifted: still cover the top of the screen
            dh = floorYd / kGround;
            dw = dh * aspect;
        }
        const double bx = sx(0.0) - dw * bgCenterU_;
        const SDL_FRect dst{static_cast<float>(bx), static_cast<float>(floorYd - kGround * dh), static_cast<float>(dw), static_cast<float>(dh)};
        SDL_RenderCopyF(r, bg_.tex, nullptr, &dst);
        const double bgBottom = dst.y + dst.h;
if (bgBottom < 360) {
            const int fy = std::max(0, static_cast<int>(bgBottom));
            if (pano) {   // the floor goes on below the picture: its last rows, stretched to the bottom of the screen
                const SDL_Rect strip{0, std::max(0, bg_.h - 3), bg_.w, std::min(3, bg_.h)};
                const SDL_FRect fill{dst.x, static_cast<float>(fy) - 1.0f, dst.w, static_cast<float>(360 - fy) + 2.0f};
                SDL_RenderCopyF(r, bg_.tex, &strip, &fill);
            } else {
                ui.fill(Rect{0, fy, viewW_, 360 - fy}, Col{6, 5, 8, 255});   // picture ends above the screen's bottom edge
            }
        }
    } else {
        drawGeneratedStage(ui, lv, cx, s, floorY, bound);
    }

    superBackdropPass(r);   // a super in progress: the stage goes dark behind the fighters

    // ---- fighters
    if (foitMode_) foit_->views(views_);   // what every object of the data driven fight shows this tick
    for (int i = 0; i < 2; ++i) {
        const fight::Fighter& f = st_.p[i];
        const fight::FoitView* bv = nullptr;   // this fighter's own body
        if (foitMode_)
            for (const fight::FoitView& v : views_)
                if (v.body && v.side == i) bv = &v;
        const bool hidden = bv && (bv->xscale == 0 || bv->yscale == 0 || bv->alpha <= 0);   // an entrance starts with the fighter not there yet
        const double feetX = sx(fight::fromFixed(f.x));
        const double feetY = sy(fight::fromFixed(f.y));
        const double unitPx = duelMode_ ? 330.0 * s : kCharHeight * s;  // a fighter's height on screen

        // Floor shadow.  Off / Low (flat hard ellipse) / Medium (feathered blob that shrinks and fades with height) / High (the fighter's
        // own silhouette flattened onto the floor) / Game (the same, fading with height above the floor like the PC game's FloorShadow:
        // alpha 0.75 at the floor, 0 at the stage's ShadowDist).
        const double shDist = lv && lv->shadowDist > 0 ? lv->shadowDist : 400.0;
        const double air = std::clamp(fight::fromFixed(f.y) / shDist, 0.0, 1.0);
        bool silhouetteDone = false;
        if ((dev_.shadow == devopt::ShadowMode::High || dev_.shadow == devopt::ShadowMode::Game) && duelMode_ && skin_[i].hasMoves() && !hidden) {
            const uint16_t fi = foitMode_ ? (bv && bv->frame >= 0 ? static_cast<uint16_t>(bv->frame) : fight::kNoFrame)
                                          : skin_[i].moves().frameAt(f.mv, f.mvTick);
            const bool game = dev_.shadow == devopt::ShadowMode::Game;
            if (fi != fight::kNoFrame)
                silhouetteDone = skin_[i].drawShadow(r, fi, feetX, floorYd + 2, std::max(0.0, static_cast<double>(fight::fromFixed(f.y))), s, f.facing == fight::Facing::kRight,
                                                     0.15, game ? 0.75 : 0.50, game, shDist) > 0;
        }
        if (dev_.shadow != devopt::ShadowMode::Off && !silhouetteDone && !hidden) {
            const int shadowRx = duelMode_ ? static_cast<int>(112 * s * (1.0 - 0.5 * air)) : static_cast<int>(unitPx * 0.26 * (1.0 - 0.5 * air));
            const int shadowRy = std::max(2, shadowRx / 4);
            if (dev_.shadow == devopt::ShadowMode::Low) {
                ui.disc(static_cast<int>(feetX), floorY + 3, std::max(3, shadowRx), shadowRy, Col{0, 0, 0, static_cast<uint8_t>(80 * (1.0 - 0.6 * air))});
            } else {
                if (!shadow_.tex) makeShadowTexture(r);
                if (shadow_.tex) {
                    const SDL_FRect dst{static_cast<float>(feetX - shadowRx), static_cast<float>(floorYd + 3 - shadowRy), static_cast<float>(std::max(6, shadowRx * 2)),
                                        static_cast<float>(std::max(3, shadowRy * 2))};
                    SDL_SetTextureAlphaMod(shadow_.tex, static_cast<Uint8>(std::lround(215 * (1.0 - 0.8 * air))));
                    SDL_RenderCopyF(r, shadow_.tex, nullptr, &dst);
                }
            }
        }

        if (foitMode_) {
            if (bv) drawFoitView(ui, r, *bv, feetX, feetY, s);
        } else {
            drawFighter(ui, r, i, feetX, feetY, s);
        }

        if (hitFlash_[i] > 0 && !hidden) {
            const int rad = static_cast<int>(unitPx * 0.28) + (10 - hitFlash_[i]) * 2;
            const Col flash = Col{255, 240, 180, static_cast<uint8_t>(200 * hitFlash_[i] / 10)};
            for (int k = 0; k < 8; ++k) {
                const double ang = 3.14159 * k / 4.0;
                ui.fill(Rect{static_cast<int>(feetX) + static_cast<int>(std::cos(ang) * rad) - 2,
                             static_cast<int>(feetY) - static_cast<int>(unitPx * 0.55) + static_cast<int>(std::sin(ang) * rad) - 2, 5, 5},
                        flash);
            }
        }
    }

    if (foitMode_) {   // spawned objects (projectiles, effects, helpers) in front of the bodies
        for (const fight::FoitView& v : views_)
            if (!v.body) drawFoitView(ui, r, v, sx(v.x), sy(v.y), s);
    }
    superTopPass(r);

    // ---- F1: the real boxes of the frame on screen (push blue, hurt green, hit red)
    if (showBoxes_ && foitMode_) {
        for (const fight::FoitView& v : views_) {
            if (v.frame < 0 || !v.ch) continue;
            const foit::Frame& fr = v.ch->frames[static_cast<size_t>(v.frame)];
            fight::Fighter at;   // worldBox() wants a Fighter: only where it stands and which way it faces matter
            at.x = fight::toFixed(v.x);
            at.y = fight::toFixed(v.y);
            at.facing = v.faceRight ? fight::Facing::kRight : fight::Facing::kLeft;
            auto draw = [&](const std::vector<foit::Box>& list, Col c) {
                for (const foit::Box& b : list) {
                    const fight::WorldBox w = fight::worldBox(at, b);
                    const int bx0 = static_cast<int>(sx(w.x0)), bx1 = static_cast<int>(sx(w.x1));
                    const int by0 = static_cast<int>(sy(w.y1)), by1 = static_cast<int>(sy(w.y0));
                    ui.outline(Rect{bx0, by0, std::max(1, bx1 - bx0), std::max(1, by1 - by0)}, c);
                }
            };
            draw(fr.push, Col{80, 140, 255, 255});
            draw(fr.hurt, Col{70, 230, 100, 255});
            draw(fr.hit, Col{255, 70, 70, 255});
        }
    } else if (showBoxes_ && duelMode_) {
        for (int i = 0; i < 2; ++i) {
            const fight::Fighter& f = st_.p[i];
            const foit::Frame* fr = fight::currentFrame(f, skin_[i].moves());
            if (!fr) continue;
            auto draw = [&](const std::vector<foit::Box>& list, Col c) {
                for (const foit::Box& b : list) {
                    const fight::WorldBox w = fight::worldBox(f, b);
                    const int bx0 = static_cast<int>(sx(w.x0)), bx1 = static_cast<int>(sx(w.x1));
                    const int by0 = static_cast<int>(sy(w.y1)), by1 = static_cast<int>(sy(w.y0));
                    ui.outline(Rect{bx0, by0, std::max(1, bx1 - bx0), std::max(1, by1 - by0)}, c);
                }
            };
            draw(fr->push, Col{80, 140, 255, 255});
            draw(fr->hurt, Col{70, 230, 100, 255});
            draw(fr->hit, Col{255, 70, 70, 255});
        }
    }
}

// A black ellipse, solid in the middle and feathered towards the rim (alpha only; the colour is always black).
void PlayScreen::makeShadowTexture(SDL_Renderer* r) {
    DdsImage img;
    img.width = 128;
    img.height = 32;
    img.rgba.assign(static_cast<size_t>(img.width) * img.height * 4, 0);
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            const double nx = (x + 0.5 - img.width / 2.0) / (img.width / 2.0), ny = (y + 0.5 - img.height / 2.0) / (img.height / 2.0);
            const double d = std::sqrt(nx * nx + ny * ny);
            const double t = std::clamp((d - 0.30) / 0.70, 0.0, 1.0);
            const double a = 1.0 - t * t * (3.0 - 2.0 * t);   // smoothstep: opaque core, soft edge
            img.rgba[(static_cast<size_t>(y) * img.width + x) * 4 + 3] = static_cast<uint8_t>(std::lround(255 * a));
        }
    if (shadow_.set(r, img) && shadow_.tex) SDL_SetTextureScaleMode(shadow_.tex, SDL_ScaleModeLinear);
}

// One object of the data driven fight at its feet point: the frame its node shows, stretched / faded by its own XSCALE / YSCALE / ALPHA.
void PlayScreen::drawFoitView(Ui& ui, SDL_Renderer* r, const fight::FoitView& v, double fx, double fy, double s) {
    if (v.frame < 0 || v.xscale == 0 || v.yscale == 0 || v.alpha <= 0) return;   // nothing to show (yet)
    Skin& sk = skin_[v.side];
    applySuperToSkin(sk, v);
    applyPalFxToSkin(sk, v);
    if (sk.drawFrame(r, static_cast<uint16_t>(v.frame), fx, fy, s, v.faceRight, v.xscale, v.yscale, v.alpha) > 0) return;
    if (v.body) drawDummy(ui, st_.p[v.side], animFor(st_.p[v.side]), static_cast<int>(fx), static_cast<int>(fy), 330.0 * s, v.side);   // frame with no picture
}

void PlayScreen::drawFighter(Ui& ui, SDL_Renderer* r, int player, double feetX, double feetY, double scale) {
    const fight::Fighter& f = st_.p[player];
    Skin& sk = skin_[player];
    if (duelMode_ && sk.hasMoves()) {
        if (compState_[player] >= 0) {   // the creature the body holds (drawn first: the hand covers it)
            const uint16_t cf = sk.moves().companionFrameAt(compState_[player], compTick_[player]);
            if (cf != fight::kNoFrame) sk.drawFrame(r, cf, feetX, feetY, scale, f.facing == fight::Facing::kRight);
        }
        const uint16_t fi = sk.moves().frameAt(f.mv, f.mvTick);
        if (fi != fight::kNoFrame && sk.drawFrame(r, fi, feetX, feetY, scale, f.facing == fight::Facing::kRight) > 0) return;
        drawDummy(ui, f, animFor(f), static_cast<int>(feetX), static_cast<int>(feetY), 330.0 * scale, player);  // nothing visible in this frame
        return;
    }
    drawDummy(ui, f, animFor(f), static_cast<int>(feetX), static_cast<int>(feetY), kCharHeight * scale, player);
}

void PlayScreen::drawDummy(Ui& ui, const fight::Fighter& f, Anim anim, int cx, int feetY, double px, int player) {
    const Col body = player == 1 ? Col{86, 142, 224, 255} : player == 0 ? Col{228, 86, 112, 255} : Col{150, 150, 160, 255};
    const Col dark = Col{static_cast<uint8_t>(body.r * 0.55), static_cast<uint8_t>(body.g * 0.55),
                         static_cast<uint8_t>(body.b * 0.55), 255};
    const bool low = (anim == kCrouch || anim == kBlock || anim == kDown);
    const int H = static_cast<int>(px * (low ? 0.68 : 1.0));
    if (H < 8) return;
    const int legH = static_cast<int>(H * 0.38), legW = std::max(2, static_cast<int>(H * 0.11));
    const int torsoH = static_cast<int>(H * 0.36), torsoW = std::max(3, static_cast<int>(H * 0.30));
    const int headS = std::max(3, static_cast<int>(H * 0.18));
    const int armW = std::max(2, static_cast<int>(H * 0.10));
    const int armH = (anim == kAtkL || anim == kAtkM || anim == kAtkH) ? std::max(2, static_cast<int>(H * 0.09))
                                                                      : static_cast<int>(H * 0.28);
    const int top = feetY - H;
    const int dir = (f.facing == fight::Facing::kRight) ? 1 : -1;

    // legs
    ui.fill(Rect{cx - legW - 1, feetY - legH, legW, legH}, dark);
    ui.fill(Rect{cx + 1, feetY - legH, legW, legH}, dark);
    // torso
    ui.fill(Rect{cx - torsoW / 2, feetY - legH - torsoH, torsoW, torsoH}, body);
    // head
    ui.fill(Rect{cx - headS / 2, top, headS, headS}, mixCol(body, Col{255, 235, 220, 255}, 0.55));
    // arms: forward on attacks, crossed when blocking, hanging otherwise
    const int shoulderY = feetY - legH - torsoH + static_cast<int>(H * 0.05);
    if (anim == kBlock) {
        ui.fill(Rect{cx - torsoW / 2 - armW / 2 + dir * 2, shoulderY, armW * 2, armH / 2}, dark);
    } else if (anim == kAtkL || anim == kAtkM || anim == kAtkH) {
        const int reach = static_cast<int>(H * (anim == kAtkH ? 0.44 : anim == kAtkM ? 0.36 : 0.28));
        const int x0 = dir > 0 ? cx : cx - reach;
        ui.fill(Rect{x0, shoulderY, reach, armH}, mixCol(body, Col{255, 240, 200, 255}, 0.3));
    } else if (anim == kHurt || anim == kDown) {
        ui.fill(Rect{cx - torsoW / 2 - armW - dir * 3, shoulderY, armW, armH}, dark);
    } else {
        ui.fill(Rect{cx - torsoW / 2 - armW, shoulderY, armW, armH}, dark);
        ui.fill(Rect{cx + torsoW / 2, shoulderY, armW, armH}, dark);
    }
}

void PlayScreen::drawHudFlat(Ui& ui) {
    for (int i = 0; i < 2; ++i) {
        const int hp = std::clamp(st_.p[i].health, 0, 1000);
        const double pct = hp / 10.0;
        const Rect bar = i == 0 ? Rect{16, 44, 252, 14} : Rect{372, 44, 252, 14};
        ui.fill(bar, Col{12, 12, 18, 255});
        const int fw = std::max(0, static_cast<int>(std::lround((bar.w - 2) * pct / 100.0)));
        const Rect fillRect = i == 0 ? Rect{bar.x + 1, bar.y + 1, fw, bar.h - 2}
                                     : Rect{bar.x + bar.w - 1 - fw, bar.y + 1, fw, bar.h - 2};
        ui.fill(fillRect, pct > 30 ? kGood : (pct > 12 ? kWarn : kBad));
        ui.outline(bar, kLine);
        // blockbuster meter
        const Rect m = i == 0 ? Rect{16, 61, 168, 6} : Rect{456, 61, 168, 6};
        ui.bar(m, st_.p[i].meter / 50.0, Col{96, 190, 236, 255});
        // name + round pips
        const std::string nm = playerName(i) + (cpu_[i] ? " [CPU]" : "");
        ui.text(i == 0 ? 16 : 624 - Ui::textW(nm), 31, nm, kText);
        for (int w = 0; w < wins_[i]; ++w) {
            const int px = i == 0 ? 16 + w * 12 : 624 - w * 12 - 10;
            ui.fill(Rect{px, 70, 10, 8}, kAccent);
        }
        if (st_.p[i].comboCount > 1) {
            const std::string cb = std::to_string(st_.p[i].comboCount) + " HITS";
            ui.text(i == 0 ? 20 : 620 - Ui::textW(cb), 82, cb, kWarn);
        }
    }
    const int secs = st_.roundTimer / 60;
    const std::string t = training_ ? "TRAINING" : (timeLimitSec_ == 0 ? "INF" : std::to_string(secs));
    ui.text(320 - Ui::textW(t, 2) / 2, 32, t, training_ ? kGood : kText, 2);
    if (phase_ == Phase::Fight) {
        const std::string st = std::string("P1 ") + fight::stateName(st_.p[0].state) + "   P2 " +
                               fight::stateName(st_.p[1].state);
        ui.text(320 - Ui::textW(st) / 2, 344, st, Col{96, 96, 112, 255});
    }
}

void PlayScreen::drawPad(Ui& ui, SDL_Renderer*) {
    const touch::Layout& L = touchPad_.layout();
    const uint8_t a = static_cast<uint8_t>(std::clamp(L.opacity, 0, 255));
    auto alpha = [&](int v) { return static_cast<uint8_t>(std::clamp(v * a / 255, 0, 255)); };

    // ---- stick: base ring, the 8 direction ticks (the active one lit), knob
    const int bx = touchPad_.baseX(), by = touchPad_.baseY(), rad = L.scaledRadius();
    ui.disc(bx, by, rad + 8, rad + 8, Col{20, 20, 30, alpha(150)});
    ui.disc(bx, by, rad, rad, Col{46, 46, 66, alpha(150)});
    static const int dirs[8][3] = {{6, 1, 0}, {9, 1, -1}, {8, 0, -1}, {7, -1, -1}, {4, -1, 0}, {1, -1, 1}, {2, 0, 1}, {3, 1, 1}};
    for (const auto& d : dirs) {
        const double len = (d[1] != 0 && d[2] != 0) ? 0.7071 : 1.0;
        const int tx = bx + static_cast<int>(d[1] * len * (rad + 2)), ty = by + static_cast<int>(d[2] * len * (rad + 2));
        const bool lit = touchPad_.direction() == d[0];
        ui.disc(tx, ty, lit ? 6 : 3, lit ? 6 : 3, lit ? kAccent : Col{150, 150, 175, alpha(200)});
    }
    const bool active = touchPad_.stickActive();
    const int kr = std::max(12, rad * 9 / 20);
    ui.disc(touchPad_.knobX(), touchPad_.knobY(), kr, kr, active ? kAccent : Col{120, 120, 150, alpha(210)});
    ui.disc(touchPad_.knobX(), touchPad_.knobY(), kr - 4, kr - 4, active ? Col{255, 255, 255, 230} : Col{170, 170, 195, alpha(210)});

    // ---- buttons: punches cool, kicks warm, macros / P+K violet, Start grey
    for (size_t i = 0; i < L.buttons.size(); ++i) {
        const touch::PadButton b = L.scaled(L.buttons[i]);
        const Rect r{b.x, b.y, b.w, b.h};
        const bool held = touchPad_.pressed(i);
        Col base{60, 60, 80, alpha(150)};
        if (b.bits & (fight::kLP | fight::kMP | fight::kHP)) base = Col{48, 70, 120, alpha(150)};
        if (b.bits & (fight::kLK | fight::kMK | fight::kHK)) base = Col{120, 60, 48, alpha(150)};
        if ((b.bits & (fight::kMacro1 | fight::kMacro2)) || ((b.bits & fight::kLP) && (b.bits & fight::kLK)))
            base = Col{90, 56, 120, alpha(150)};
        ui.fill(r, held ? kAccent : base);
        ui.outline(r, held ? kText : Col{200, 200, 220, alpha(160)});
        ui.textIn(r, b.label, held ? Col{255, 255, 255, 255} : Col{230, 230, 240, alpha(235)}, true);
    }
}

}  // namespace play
}  // namespace zengine
