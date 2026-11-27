#include "custom_cards.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <vector>

#include "target_switch.h"

namespace zengine {
namespace menu {
namespace customcards {

namespace {

constexpr int kIconW = 102, kIconH = 105;
constexpr int kPedW = 450, kPedH = 118;
constexpr int kTitleW = 514, kTitleH = 144;
constexpr int kPortW = 939, kPortH = 612;
constexpr int kCoin = 90;

// A texture being drawn on.  finish() reads it back as straight-alpha pixels and returns a plain texture (the render target itself is
// premultiplied by SDL's blending, and a plain texture does not depend on the render target surviving).
class Canvas {
public:
    Canvas(SDL_Renderer* r, int w, int h) : r_(r), w_(w), h_(h) {
        t_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!t_) return;
        SDL_SetTextureBlendMode(t_, SDL_BLENDMODE_BLEND);
        if (!sw_.enter(r, t_)) {
            SDL_DestroyTexture(t_);
            t_ = nullptr;
            return;
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
    ~Canvas() {
        sw_.leave();
        if (t_) SDL_DestroyTexture(t_);
    }
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    bool ok() const { return t_ != nullptr; }
    int w() const { return w_; }
    int h() const { return h_; }

    SDL_Texture* finish() {
        if (!t_) return nullptr;
        std::vector<uint8_t> px(static_cast<size_t>(w_) * h_ * 4);
        const bool read = SDL_RenderReadPixels(r_, nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w_ * 4) == 0;
        sw_.leave();
        if (!read) return nullptr;
        for (size_t i = 0; i < px.size(); i += 4) {
            const unsigned a = px[i + 3];
            if (a == 0 || a == 255) continue;
            for (int k = 0; k < 3; ++k) px[i + static_cast<size_t>(k)] = static_cast<uint8_t>(std::min(255u, (px[i + static_cast<size_t>(k)] * 255u + a / 2) / a));
        }
        SDL_Texture* out = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w_, h_);
        if (!out) return nullptr;
        SDL_UpdateTexture(out, nullptr, px.data(), w_ * 4);
        SDL_SetTextureBlendMode(out, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(out, SDL_ScaleModeLinear);
        return out;
    }

private:
    SDL_Renderer* r_ = nullptr;
    SDL_Texture* t_ = nullptr;
    int w_ = 0, h_ = 0;
    TargetSwitch sw_;
};

void fill(SDL_Renderer* r, SDL_Rect q, Col c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &q);
}

// vertical gradient, one line at a time
void gradient(SDL_Renderer* r, SDL_Rect q, Col top, Col bottom) {
    for (int y = 0; y < q.h; ++y) {
        const float f = q.h > 1 ? static_cast<float>(y) / static_cast<float>(q.h - 1) : 0.0f;
        auto mix = [&](Uint8 a, Uint8 b) { return static_cast<Uint8>(std::lround(a + (b - a) * f)); };
        fill(r, SDL_Rect{q.x, q.y + y, q.w, 1}, Col{mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), mix(top.a, bottom.a)});
    }
}

void frame(SDL_Renderer* r, SDL_Rect q, int thick, Col c) {
    fill(r, SDL_Rect{q.x, q.y, q.w, thick}, c);
    fill(r, SDL_Rect{q.x, q.y + q.h - thick, q.w, thick}, c);
    fill(r, SDL_Rect{q.x, q.y, thick, q.h}, c);
    fill(r, SDL_Rect{q.x + q.w - thick, q.y, thick, q.h}, c);
}

// a soft disc: `inner` at the centre, fading to alpha 0 at the rim
void glow(SDL_Renderer* r, float cx, float cy, float rx, float ry, Col inner) {
    constexpr int kSteps = 48;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.push_back(SDL_Vertex{SDL_FPoint{cx, cy}, SDL_Color{inner.r, inner.g, inner.b, inner.a}, SDL_FPoint{0, 0}});
    for (int i = 0; i <= kSteps; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / kSteps;
        v.push_back(SDL_Vertex{SDL_FPoint{cx + rx * std::cos(a), cy + ry * std::sin(a)}, SDL_Color{inner.r, inner.g, inner.b, 0}, SDL_FPoint{0, 0}});
    }
    for (int i = 1; i <= kSteps; ++i)
        for (int k : {0, i, i + 1}) idx.push_back(k);
    SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

void quad(SDL_Renderer* r, const SDL_FPoint p[4], Col c) {
    const SDL_Color col{c.r, c.g, c.b, c.a};
    SDL_Vertex v[4];
    for (int i = 0; i < 4; ++i) v[i] = SDL_Vertex{p[i], col, SDL_FPoint{0, 0}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

// a crop of the pose that holds the head and shoulders, in the aspect of the picture it goes into
SDL_Rect faceRect(const SDL_Rect& b, int fw, int fh, float aspect) {
    float sw = std::max(static_cast<float>(b.w), b.h * 0.42f);
    float sh = sw / aspect;
    if (sh > b.h) {   // a short figure: all of it
        sh = static_cast<float>(b.h);
        sw = sh * aspect;
    }
    sw = std::min(sw, static_cast<float>(fw));
    sh = sw / aspect;
    const float cx = b.x + b.w / 2.0f;
    float x = cx - sw / 2.0f, y = static_cast<float>(b.y);
    x = std::clamp(x, 0.0f, std::max(0.0f, fw - sw));
    y = std::clamp(y, 0.0f, std::max(0.0f, fh - sh));
    return SDL_Rect{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)), std::max(1, static_cast<int>(std::lround(sw))), std::max(1, static_cast<int>(std::lround(sh)))};
}

const GbsFont* nameFont(GameUi& gfx) {
    for (const char* n : {"Guilded Header", "Subheader w Wings", "Default Font"})
        if (const GbsFont* f = gfx.fontNamed(n)) return f;
    return nullptr;
}

std::string title(const std::string& name) {
    std::string t;
    for (char c : name) t += c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return t;
}

// The name in bronze letters (a dark outline, the top of the letters lighter), centred on (cx, cy) and no bigger than maxW x maxH.
void drawName(GameUi& gfx, SDL_Renderer* r, const GbsFont& f, const std::string& text, float cx, float cy, float maxW, float maxH) {
    const float tw = std::max(1.0f, gfx.textWidth(f, text, 1.0f));
    const float lh = static_cast<float>(std::max(1, f.lineHeight));
    const float sc = std::min(maxH / lh, maxW / tw);
    const float w = tw * sc, h = lh * sc;
    const float x = cx - w / 2.0f, y = cy - h / 2.0f;
    const int o = std::max(2, static_cast<int>(std::lround(sc * 3.0f)));
    const Col dark{58, 26, 8, 255};
    for (int dy = -o; dy <= o; dy += o)
        for (int dx = -o; dx <= o; dx += o)
            if (dx != 0 || dy != 0) gfx.drawLine(f, text, x + dx, y + dy, dark, sc);
    gfx.drawLine(f, text, x, y, Col{214, 128, 54, 255}, sc);
    const SDL_Rect top{0, static_cast<int>(y), 4096, static_cast<int>(h * 0.52f)};
    SDL_RenderSetClipRect(r, &top);
    gfx.drawLine(f, text, x, y, Col{255, 238, 198, 255}, sc);
    SDL_RenderSetClipRect(r, nullptr);
}

// the two bronze wings beside the name on the pedestal plate
void wings(SDL_Renderer* r, float cx, float cy, float gap, float span) {
    for (int side = -1; side <= 1; side += 2) {
        for (int k = 0; k < 3; ++k) {
            const float len = span - k * 26.0f, thick = 13.0f;
            const float x0 = cx + side * gap, x1 = x0 + side * len;
            const float y0 = cy - 22.0f + k * 17.0f;
            const float lean = -side * 20.0f;   // the outer end sits higher
            const SDL_FPoint rim[4] = {{x0, y0 - 2}, {x1, y0 - 2 + lean}, {x1, y0 + thick + 2 + lean}, {x0, y0 + thick + 2}};
            const SDL_FPoint fillQ[4] = {{x0, y0}, {x1, y0 + lean}, {x1, y0 + thick + lean}, {x0, y0 + thick}};
            quad(r, rim, Col{58, 26, 8, 255});
            quad(r, fillQ, Col{k == 0 ? Uint8(226) : Uint8(196 - k * 14), static_cast<Uint8>(138 - k * 14), static_cast<Uint8>(64 - k * 6), 255});
        }
    }
}

struct Put {
    GameUi& gfx;
    const std::string& name;
    bool any = false;
    void operator()(const char* what, const char* tail, Canvas& c) {
        int w = c.w(), h = c.h();
        if (SDL_Texture* t = c.finish()) {
            gfx.putTexture(texName(what, name, tail), t, w, h);
            any = true;
        }
    }
};

}  // namespace

std::string texName(const char* what, const std::string& name, const char* tail) { return std::string("Textures/charselect/") + what + name + tail + ".png"; }

void drop(GameUi& gfx, const std::string& name) {
    for (const char* t : {"01", "02", "03"}) gfx.putTexture(texName("charselect_select_icons_", name, t), nullptr, 0, 0);
    gfx.putTexture(texName("charselect_portraits_", name), nullptr, 0, 0);
    gfx.putTexture(texName("charselect_portrait_name_", name), nullptr, 0, 0);
    gfx.putTexture(texName("charselect_pedestal_name_", name), nullptr, 0, 0);
    gfx.putTexture(texName("data_charcoin_", name, "_glow01"), nullptr, 0, 0);
}

bool make(GameUi& gfx, const std::string& name, const CharSpriteEdit& pose) {
    SDL_Renderer* r = gfx.renderer();
    if (!r || !pose.isOpen() || pose.width() <= 0 || pose.height() <= 0) return false;
    SDL_Texture* pt = pose.texture();
    const SDL_Rect b = pose.bounds();
    const int fw = pose.width(), fh = pose.height();
    if (b.w <= 0 || b.h <= 0) return false;
    Put put{gfx, name};

    // the canvases are drawn in plain pixels: the screen's viewport is put back when this returns
    struct View {
        GameUi& g;
        float ox, oy, sc;
        explicit View(GameUi& gg) : g(gg), ox(gg.screenX(0)), oy(gg.screenY(0)), sc(gg.scale()) { g.setViewport(0, 0, 1.0f); }
        ~View() { g.setViewport(ox, oy, sc); }
    } view(gfx);

    // the pose is scaled up and down: smooth it for these pictures, give the mode back afterwards
    SDL_ScaleMode oldMode = SDL_ScaleModeLinear;
    SDL_GetTextureScaleMode(pt, &oldMode);
    SDL_SetTextureScaleMode(pt, SDL_ScaleModeLinear);
    SDL_SetTextureColorMod(pt, 255, 255, 255);
    SDL_SetTextureAlphaMod(pt, 255);

    // ---- the icons of the grid: head and shoulders on a dark violet ground; 01 normal, 02 lit (gold edge), 03 greyed (never used: custom fighters are not locked)
    for (int variant = 1; variant <= 3; ++variant) {
        Canvas c(r, kIconW, kIconH);
        if (!c.ok()) continue;
        const SDL_Rect all{0, 0, kIconW, kIconH};
        if (variant == 2) gradient(r, all, Col{118, 88, 150, 255}, Col{58, 42, 78, 255});
        else if (variant == 3) gradient(r, all, Col{70, 70, 74, 255}, Col{30, 30, 34, 255});
        else gradient(r, all, Col{78, 58, 98, 255}, Col{28, 20, 38, 255});
        const SDL_Rect src = faceRect(b, fw, fh, static_cast<float>(kIconW) / kIconH);
        if (variant == 3) SDL_SetTextureColorMod(pt, 120, 120, 128);
        SDL_RenderCopy(r, pt, &src, &all);
        SDL_SetTextureColorMod(pt, 255, 255, 255);
        if (variant == 2) {
            fill(r, all, Col{255, 244, 220, 34});
            frame(r, all, 3, Col{240, 190, 90, 255});
        } else {
            frame(r, all, 1, Col{16, 10, 20, 255});
        }
        put("charselect_select_icons_", variant == 1 ? "01" : variant == 2 ? "02" : "03", c);
    }

    // ---- the big picture behind the data bar: the whole figure in a pale glow, standing to the right of the middle
    {
        Canvas c(r, kPortW, kPortH);
        if (c.ok()) {
            glow(r, kPortW * 0.47f, kPortH * 0.55f, kPortW * 0.46f, kPortH * 0.62f, Col{255, 246, 222, 235});
            // head, chest and arms (the data bar hides the bottom of the picture anyway): the top of the figure fills the picture
            SDL_Rect s = b;
            s.h = std::max(1, static_cast<int>(b.h * 0.72f));
            const float sc = std::min(kPortW * 0.62f / s.w, static_cast<float>(kPortH) / s.h);
            const int dw = static_cast<int>(std::lround(s.w * sc)), dh = static_cast<int>(std::lround(s.h * sc));
            const SDL_Rect d{static_cast<int>(kPortW * 0.47f) - dw / 2, kPortH - dh, dw, dh};
            SDL_RenderCopy(r, pt, &s, &d);
            put("charselect_portraits_", "01", c);
        }
    }

    // ---- the name plates
    if (const GbsFont* f = nameFont(gfx)) {
        const std::string text = title(name);
        {
            Canvas c(r, kTitleW, kTitleH);
            if (c.ok()) {
                drawName(gfx, r, *f, text, kTitleW / 2.0f, kTitleH / 2.0f, kTitleW * 0.86f, kTitleH * 0.62f);
                put("charselect_portrait_name_", "01", c);
            }
        }
        {
            Canvas c(r, kPedW, kPedH);
            if (c.ok()) {
                wings(r, kPedW / 2.0f, kPedH / 2.0f - 6.0f, 104.0f, 168.0f);
                drawName(gfx, r, *f, text, kPedW / 2.0f, kPedH / 2.0f, 190.0f, 46.0f);
                put("charselect_pedestal_name_", "01", c);
            }
        }
    }

    // ---- the coin of the data bar: a gold ring round the face
    {
        Canvas c(r, kCoin, kCoin);
        if (c.ok()) {
            const float m = kCoin / 2.0f;
            glow(r, m, m, m, m, Col{255, 196, 80, 120});
            gfx.drawArc(m, m, 41.0f, 0.0f, 0.0f, 360.0f, Col{26, 16, 30, 255});
            const SDL_Rect dst{kCoin / 2 - 27, kCoin / 2 - 27, 54, 54};
            const SDL_Rect src = faceRect(b, fw, fh, 1.0f);
            SDL_RenderCopy(r, pt, &src, &dst);
            gfx.drawArc(m, m, 44.0f, 39.0f, 0.0f, 360.0f, Col{244, 176, 52, 255});
            put("data_charcoin_", "_glow01", c);
        }
    }

    SDL_SetTextureScaleMode(pt, oldMode);
    return put.any;
}

SDL_FRect standingRect(const CharSpriteEdit& pose, float x, float y, float height, bool flip) {
    const SDL_Rect b = pose.bounds();
    if (!pose.isOpen() || b.h <= 0) return SDL_FRect{x, y, 0, 0};
    const float s = height / static_cast<float>(b.h);
    const float w = pose.width() * s, h = pose.height() * s;
    const float left = flip ? x - (pose.width() - pose.hotX()) * s : x - pose.hotX() * s;
    return SDL_FRect{left, y - pose.hotY() * s, w, h};
}

void drawStanding(GameUi& gfx, const CharSpriteEdit& pose, float x, float y, float height, bool flip, float alpha) {
    if (!pose.isOpen()) return;
    const SDL_FRect q = standingRect(pose, x, y, height, flip);
    if (q.w <= 0 || q.h <= 0) return;
    gfx.drawTexture(pose.texture(), q.x, q.y, q.w, q.h, 0, 0, 0, flip, false, Col{255, 255, 255, 255}, alpha);
}

}  // namespace customcards
}  // namespace menu
}  // namespace zengine
