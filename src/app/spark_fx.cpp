#include "spark_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "dds.h"
#include "spr_tiles.h"

namespace zengine {
namespace fx {

namespace {

// state of hitspark.foit -> picture (data/sprites/<name>.spr.msb).  Read off the names and the frame counts, see the header.
// Effects that are text, marks or art-point pieces (PBGC_*, hearts, points, ResetSpark, ReflectSpark, BT ...) have no entry.
struct SparkEntry {
    const char* state;
    const char* sprite;
};
constexpr SparkEntry kSparks[] = {
    {"Light", "hitspark_omni_l"},
    {"Medium", "hitspark_omni_m"},
    {"Hard", "hitspark_omni_h"},
    {"Special", "hitspark_omni_h"},
    {"Burstable", "infinite_spark_a"},
    {"Grab", "grab"},
    {"SilentLight", "hitspark_omni_l"},
    {"Block", "block"},
    {"BlockAG", "block_ag"},
    {"Pushblock", "pushblock"},
    {"ProjectileCollision", "hitspark_omni_m"},
    {"FloorBounce", "hitspark_ground_impact_h"},
    {"FloorShockwave_Assist", "hitspark_ground_impact_assist"},
    {"WallBounce", "hitspark_wall_impact_h"},
    {"FloorShockwaveH", "hitspark_blunt_shockwave_h"},
    {"FloorShockwaveHGeyser", "hitspark_blunt_shockwave_h_gr"},
    {"FloorImpact", "hitspark_ground_impact_m"},
    {"WallImpact", "hitspark_wall_impact_m"},
    {"DustPlume", "dustplume"},
    {"ThrowTech", "throwtech2"},
    {"RadialHaloFlat", "radial_halo_flat"},
    {"SJ_Dust", "dust"},
    {"Landing_Dust", "dust"},
    {"Infinite_Burst_Break", "infinite_burst_break_gold"},
    {"Super_Spark", "super_spark"},
    {"FloorBounce_Yellow", "hitspark_ground_impact_h_yellow"},
    {"SuperThrowDust", "superthrowdust"},
    {"DustCyclone_Right", "dustcyclone_right"},
    {"DustCyclone_Left", "dustcyclone_left"},
    {"DustSpecial", "dustspecial"},
    {"Directional_L", "hitspark_directional_l"},
    {"Directional_M", "hitspark_directional_m"},
    {"Directional_H", "hitspark_directional_h"},
    {"Slash_L", "hitspark_slash_l"},
    {"Slash_M", "hitspark_slash_m"},
    {"Slash_H", "hitspark_slash_h"},
    {"SnapBack", "snapback"},
    {"Blunt_L", "hitspark_blunt_m"},
    {"Blunt_M", "hitspark_blunt_m"},
    {"Blunt_H", "hitspark_blunt_m"},
    {"Radial_L", "radial_long"},
    {"Radial_M", "radial_long"},
    {"Radial_H", "radial_long"},
    {"HitstunRecovery", "auto_recovery_effect"},
    {"Whiff", "miss_spark"},
};

// the pictures of the commonest classes: light / medium / hard hit and the block mark
constexpr const char* kWarm[] = {"hitspark_omni_l", "hitspark_omni_m", "hitspark_omni_h", "block"};

constexpr int kSlotFrame = 81, kSlotXScale = 164, kSlotYScale = 165, kSlotAlpha = 166, kSlotClass = 168;

// Half-size copy of a straight-alpha RGBA picture: colours are averaged weighted by alpha so the edge of a spark does not darken.
void halve(const std::vector<uint8_t>& src, int w, int h, std::vector<uint8_t>& dst, int& nw, int& nh) {
    nw = std::max(1, w / 2);
    nh = std::max(1, h / 2);
    dst.assign(static_cast<size_t>(nw) * nh * 4, 0);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            uint32_t a = 0, r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                    const uint8_t* p = src.data() + (static_cast<size_t>(sy) * w + sx) * 4;
                    a += p[3];
                    r += static_cast<uint32_t>(p[0]) * p[3];
                    g += static_cast<uint32_t>(p[1]) * p[3];
                    b += static_cast<uint32_t>(p[2]) * p[3];
                }
            uint8_t* d = dst.data() + (static_cast<size_t>(y) * nw + x) * 4;
            if (a == 0) continue;
            d[0] = static_cast<uint8_t>(r / a);
            d[1] = static_cast<uint8_t>(g / a);
            d[2] = static_cast<uint8_t>(b / a);
            d[3] = static_cast<uint8_t>(a / 4);
        }
}

// The smallest box that holds every texel with some alpha; false when the picture is empty.
bool opaqueBounds(const std::vector<uint8_t>& px, int w, int h, int& x0, int& y0, int& x1, int& y1) {
    x0 = w;
    y0 = h;
    x1 = y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (px[(static_cast<size_t>(y) * w + x) * 4 + 3] != 0) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    return x1 >= x0 && y1 >= y0;
}

}  // namespace

const char* sparkSprite(const std::string& state) {
    for (const SparkEntry& e : kSparks)
        if (state == e.state) return e.sprite;
    return nullptr;
}

SparkQuad placeSpark(const SparkCam& cam, double ux, double uy, int texW, int texH, float hotX, float hotY, bool faceRight, double xscale, double yscale) {
    SparkQuad q;
    // the pictures were halved when they were composed (kTexScale), so one texel is one stage unit
    const double sx = cam.scale * std::abs(xscale), sy = cam.scale * std::abs(yscale);
    const double px = cam.x0 + ux * cam.kx, py = cam.y0 - uy * cam.ky;
    q.w = static_cast<float>(texW * sx);
    q.h = static_cast<float>(texH * sy);
    q.flip = !faceRight;
    const double hx = q.flip ? (texW - hotX) : hotX;
    q.x = static_cast<float>(px - hx * sx);
    q.y = static_cast<float>(py - hotY * sy);
    return q;
}

SparkFx::SparkFx() = default;
SparkFx::~SparkFx() { release(false); }

bool SparkFx::load(const Vfs& vfs) {
    loaded_ = false;
    live_.clear();
    world_ = rt::World();
    std::vector<uint8_t> data;
    if (!vfs.read("hitspark.foit", data) || data.empty()) return false;
    foit::Character ch;
    std::string err;
    if (!foit::parse(data.data(), data.size(), ch, &err)) {
        std::printf("[sparks] hitspark.foit: %s\n", err.c_str());
        return false;
    }
    ch_ = std::move(ch);
    dummy_ = std::make_unique<rt::Object>();
    std::memset(dummy_->slots, 0, sizeof dummy_->slots);
    dummy_->slots[kSlotXScale] = 1.0f;
    dummy_->slots[kSlotYScale] = 1.0f;
    dummy_->slots[kSlotAlpha] = 1.0f;
    warm_.assign(std::begin(kWarm), std::end(kWarm));
    loaded_ = true;
    return true;
}

void SparkFx::reset() {
    live_.clear();
    world_.objects.clear();
    world_.pool.clear();
    world_.events.clear();
}

void SparkFx::spawn(const rt::Event& e) {
    if (e.kind != rt::Event::HitSpark && e.kind != rt::Event::BlockSpark) return;
    if (std::getenv("ZDROID_FX_LOG") && (!loaded_ || e.spark == 0))
        std::printf("[sparks] event kind %d class %d not drawn (%s)\n", e.kind, e.spark, loaded_ ? "no spark class" : "hitspark.foit not loaded");
    if (!loaded_ || e.spark == 0) return;
    if (static_cast<int>(live_.size()) >= kMaxLive) {   // the oldest one goes
        rt::Object* old = &live_.front()->o;
        world_.objects.erase(std::remove(world_.objects.begin(), world_.objects.end(), old), world_.objects.end());
        live_.erase(live_.begin());
    }
    auto f = std::make_unique<Fx>();
    rt::Object& o = f->o;
    rt::init(o, ch_, ch_.body, e.hitX, e.faceRight);
    o.y = e.hitY;
    o.leader = dummy_.get();
    o.slots[kSlotClass] = static_cast<float>(e.spark);
    world_.objects.push_back(&o);
    live_.push_back(std::move(f));
    ++spawned_;
    if (std::getenv("ZDROID_FX_LOG"))
        std::printf("[sparks] #%d tick %u class %d at (%.0f, %.0f) %s -> %s\n", spawned_, static_cast<unsigned>(e.tick), e.spark, e.hitX, e.hitY, e.faceRight ? "right" : "left",
                    live_.back()->o.def->states[static_cast<size_t>(live_.back()->o.state)].name.c_str());
}

void SparkFx::tick() {
    if (!loaded_ || live_.empty()) return;
    rt::tickAll(world_, nullptr, 0);
    world_.events.clear();
    for (size_t i = 0; i < live_.size();) {
        if (live_[i]->o.alive) {
            ++i;
            continue;
        }
        rt::Object* dead = &live_[i]->o;
        world_.objects.erase(std::remove(world_.objects.begin(), world_.objects.end(), dead), world_.objects.end());
        live_.erase(live_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

std::string SparkFx::liveState(size_t i, int* frame) const {
    if (i >= live_.size()) return std::string();
    const rt::Object& o = live_[i]->o;
    if (frame) *frame = static_cast<int>(o.slots[kSlotFrame]);
    return o.def->states[static_cast<size_t>(o.state)].name;
}

void SparkFx::dropSprite(Sprite& s, bool sdlAlive) {
    for (FrameTex& f : s.frames)
        if (f.tex && sdlAlive) SDL_DestroyTexture(f.tex);
    texBytes_ = texBytes_ >= s.bytes ? texBytes_ - s.bytes : 0;
    s.frames.clear();
    s.bytes = 0;
}

void SparkFx::release(bool sdlAlive) {
    for (auto& kv : sprites_) dropSprite(kv.second, sdlAlive);
    sprites_.clear();
    texBytes_ = 0;
    owner_ = nullptr;
    if (loaded_) warm_.assign(std::begin(kWarm), std::end(kWarm));
}

void SparkFx::evict(const std::string& keep) {
    while (texBytes_ > kTexBudget) {
        auto victim = sprites_.end();
        for (auto it = sprites_.begin(); it != sprites_.end(); ++it) {
            if (it->first == keep || it->second.bytes == 0) continue;
            if (victim == sprites_.end() || it->second.used < victim->second.used) victim = it;
        }
        if (victim == sprites_.end()) return;
        dropSprite(victim->second, true);
        sprites_.erase(victim);
    }
}

SparkFx::Sprite& SparkFx::sprite(SDL_Renderer* r, const Vfs& vfs, const std::string& name) {
    auto it = sprites_.find(name);
    if (it != sprites_.end()) {
        it->second.used = ++clock_;
        return it->second;
    }
    Sprite& sp = sprites_[name];
    sp.used = ++clock_;
    std::vector<uint8_t> msb, dds;
    if ((!vfs.read("sprites/" + name + ".spr.msb", msb) && !vfs.read(name + ".spr.msb", msb)) ||
        (!vfs.read("sprites/" + name + ".dds", dds) && !vfs.read(name + ".dds", dds))) {
        if (std::getenv("ZDROID_FX_LOG")) std::printf("[sparks] picture %s: files not found\n", name.c_str());
        return sp;
    }
    SprTiles s;
    DdsImage img;
    std::string err;
    if (!parseSprTiles(msb, s, &err) || !decodeDds(dds, img) || img.empty()) {
        std::printf("[sparks] %s: %s\n", name.c_str(), err.empty() ? "cannot decode" : err.c_str());
        return sp;
    }
    for (size_t k = 0; k < s.frames.size(); ++k) {
        FrameTex ft;
        std::vector<uint8_t> px, half;
        int w = 0, h = 0, hw = 0, hh = 0;
        if (composeSprFrame(s, k, img.rgba.data(), img.width, img.height, px, w, h)) {
            halve(px, w, h, half, hw, hh);
            int x0, y0, x1, y1;
            if (opaqueBounds(half, hw, hh, x0, y0, x1, y1)) {
                const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
                std::vector<uint8_t> crop(static_cast<size_t>(cw) * ch * 4);
                for (int y = 0; y < ch; ++y)
                    std::memcpy(crop.data() + static_cast<size_t>(y) * cw * 4, half.data() + (static_cast<size_t>(y0 + y) * hw + x0) * 4, static_cast<size_t>(cw) * 4);
                SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, cw, ch);
                if (t) {
                    SDL_UpdateTexture(t, nullptr, crop.data(), cw * 4);
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
                    ft.tex = t;
                    ft.w = cw;
                    ft.h = ch;
                    // the frame's own offset is the hot spot (full-resolution pixels); the picture was halved and cropped
                    ft.hotX = s.frames[k].offsetX * 0.5f - static_cast<float>(x0);
                    ft.hotY = s.frames[k].offsetY * 0.5f - static_cast<float>(y0);
                    sp.bytes += static_cast<size_t>(cw) * ch * 4;
                }
            }
        }
        sp.frames.push_back(ft);   // an empty frame keeps the index
    }
    texBytes_ += sp.bytes;
    sp.ok = sp.bytes > 0;
    if (std::getenv("ZDROID_FX_LOG")) std::printf("[sparks] picture %s: %zu frames, %.1f MB, %s\n", name.c_str(), sp.frames.size(), static_cast<double>(sp.bytes) / 1048576.0, sp.ok ? "ok" : "EMPTY");
    evict(name);
    return sp;
}

void SparkFx::draw(SDL_Renderer* r, const Vfs& vfs, const SparkCam& cam) {
    if (!loaded_ || !r) return;
    if (owner_ != r) {   // a new renderer: the old textures went with it
        if (owner_) {
            for (auto& kv : sprites_) dropSprite(kv.second, false);
            sprites_.clear();
            texBytes_ = 0;
            warm_.assign(std::begin(kWarm), std::end(kWarm));
        }
        owner_ = r;
    }
    if (!warm_.empty()) {   // one queued picture per frame
        const std::string next = warm_.back();
        warm_.pop_back();
        sprite(r, vfs, next);
    }
    static const bool logDraw = std::getenv("ZDROID_FX_LOGDRAW") != nullptr;
    if (logDraw) std::printf("[sparks] draw live %zu\n", live_.size());
    for (const std::unique_ptr<Fx>& f : live_) {
        const rt::Object& o = f->o;
        const char* name = sparkSprite(o.def->states[static_cast<size_t>(o.state)].name);
        if (!name) continue;
        const double xs = o.slots[kSlotXScale], ys = o.slots[kSlotYScale], al = o.slots[kSlotAlpha];
        if (xs == 0 || ys == 0 || al <= 0) continue;
        Sprite& sp = sprite(r, vfs, name);
        if (!sp.ok || sp.frames.empty()) continue;
        int fi = static_cast<int>(o.slots[kSlotFrame]);
        fi = std::clamp(fi, 0, static_cast<int>(sp.frames.size()) - 1);
        const FrameTex& ft = sp.frames[static_cast<size_t>(fi)];
        if (!ft.tex) continue;
        const SparkQuad q = placeSpark(cam, o.x, o.y, ft.w, ft.h, ft.hotX, ft.hotY, o.facingRight(), xs, ys);
        const SDL_FRect dst{q.x, q.y, q.w, q.h};
        if (logDraw) std::printf("[sparks]   %s frame %d/%zu quad %.0f,%.0f %.0fx%.0f flip %d alpha %.2f at stage (%.0f, %.0f)\n", name, fi, sp.frames.size(), q.x, q.y, q.w, q.h, q.flip, al, o.x, o.y);
        SDL_SetTextureAlphaMod(ft.tex, static_cast<Uint8>(std::clamp(al, 0.0, 1.0) * 255.0 + 0.5));
        const int rc = SDL_RenderCopyExF(r, ft.tex, nullptr, &dst, 0.0, nullptr, q.flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        if (rc != 0 && logDraw) std::printf("[sparks] copy failed: %s\n", SDL_GetError());
    }
}

}  // namespace fx
}  // namespace zengine
