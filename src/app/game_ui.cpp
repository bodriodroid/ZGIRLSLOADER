// game_ui.cpp - see game_ui.h.
#include "game_ui.h"

#include "gbs_arc.h"

#include <cstring>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zengine {

namespace {

bool endsWithNoCase(const std::string& s, const char* ext) {
    const size_t n = std::char_traits<char>::length(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != ext[i]) return false;
    }
    return true;
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace

void GameUi::attach(SDL_Renderer* r, const Vfs* vfs) {
    reset();
    r_ = r;
    vfs_ = vfs;
}

void GameUi::reset() {
    for (auto& kv : tex_)
        if (kv.second.t) SDL_DestroyTexture(kv.second.t);
    tex_.clear();
    atlasImg_.clear();
    for (auto& kv : fontGfx_)
        for (Tex& t : kv.second.pages)
            if (t.t) SDL_DestroyTexture(t.t);
    fontGfx_.clear();
    scene_ = nullptr;
    scenes_.clear();
}

void GameUi::clipCanvas(float x, float y, float w, float h) {
    if (!r_) return;
    SDL_Rect c{static_cast<int>(std::lround(ox_ + x * sc_)), static_cast<int>(std::lround(oy_ + y * sc_)),
               static_cast<int>(std::lround(w * sc_)), static_cast<int>(std::lround(h * sc_))};
    SDL_RenderSetClipRect(r_, &c);
}

void GameUi::clearClip() {
    if (r_) SDL_RenderSetClipRect(r_, nullptr);
}

bool GameUi::loadScene(const std::string& file) {
    auto it = scenes_.find(file);
    if (it != scenes_.end()) {
        scene_ = it->second.get();
        return true;
    }
    if (!vfs_) return false;
    std::vector<uint8_t> data;
    if (!vfs_->read(file, data)) return false;
    auto sc = std::make_unique<GbsScene>();
    std::string err;
    if (!parseGbsScene(data.data(), data.size(), *sc, &err)) {
        std::printf("[ui] %s: %s\n", file.c_str(), err.c_str());
        return false;
    }
    scene_ = sc.get();
    scenes_[file] = std::move(sc);
    return true;
}

// ---------------------------------------------------------------------------------------- textures

GameUi::Tex GameUi::loadDds(const std::string& realPath, int maxSize) {
    Tex out;
    std::vector<uint8_t> data;
    DdsImage img;
    if (!readFile(realPath, data) || !decodeDds(data, img, nullptr, maxSize) || img.empty()) return out;
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!t) return out;
    SDL_UpdateTexture(t, nullptr, img.rgba.data(), img.width * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    out.t = t;
    out.w = img.width;
    out.h = img.height;
    return out;
}

bool GameUi::resolveTexture(const std::string& name, std::string& real) const {
    if (!vfs_ || name.empty()) return false;
    std::string stem = name;
    for (const char* ext : {".png", ".tga", ".jpg", ".dds"})
        if (endsWithNoCase(stem, ext)) {
            stem.resize(stem.size() - 4);
            break;
        }
    std::vector<std::string> cands{stem + ".dds"};
    const size_t p = lower(stem).find(".dxt1");
    if (p != std::string::npos) cands.push_back(stem.substr(0, p) + stem.substr(p + 5) + ".dds");
    for (const std::string& c : cands)
        if (vfs_->resolve(c, &real)) return true;
    return false;
}

bool GameUi::hasTexture(const std::string& name) {
    std::string real;
    if (resolveTexture(name, real)) return true;
    if (scene_)
        if (const GbsTexture* gt = scene_->textureByName(name))
            if (gt->inAtlas())
                if (const GbsTexture* at = scene_->textureById(gt->atlasId)) return resolveTexture(at->name, real);
    return false;
}

// A texture the scene's table keeps inside an atlas: the atlas is decoded once (kept as pixels, not as a texture) and the
// rectangle is copied into a texture of its own, so drawing it needs no source rectangle and nothing bleeds in from the neighbours.
GameUi::Tex GameUi::loadAtlasRegion(const GbsTexture& gt) {
    Tex out;
    if (!scene_ || !r_) return out;
    const GbsTexture* at = scene_->textureById(gt.atlasId);
    std::string real;
    if (!at || !resolveTexture(at->name, real)) return out;
    auto it = atlasImg_.find(real);
    if (it == atlasImg_.end()) {
        auto img = std::make_shared<DdsImage>();
        std::vector<uint8_t> data;
        if (!readFile(real, data) || !decodeDds(data, *img, nullptr, 4096) || img->empty()) img.reset();
        else std::printf("[ui] atlas %s: %dx%d\n", real.c_str(), img->width, img->height);
        it = atlasImg_.emplace(real, img).first;
    }
    if (!it->second) return out;
    const DdsImage& im = *it->second;
    const int x0 = std::clamp(static_cast<int>(std::lround(gt.u0 * im.width)), 0, im.width - 1);
    const int y0 = std::clamp(static_cast<int>(std::lround(gt.v0 * im.height)), 0, im.height - 1);
    const int x1 = std::clamp(static_cast<int>(std::lround(gt.u1 * im.width)), x0 + 1, im.width);
    const int y1 = std::clamp(static_cast<int>(std::lround(gt.v1 * im.height)), y0 + 1, im.height);
    const int w = x1 - x0, h = y1 - y0;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) std::memcpy(&px[static_cast<size_t>(y) * w * 4], &im.rgba[(static_cast<size_t>(y0 + y) * im.width + x0) * 4], static_cast<size_t>(w) * 4);
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return out;
    SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    out.t = t;
    out.w = w;
    out.h = h;
    return out;
}

SDL_Texture* GameUi::texture(const std::string& name, int* w, int* h) {
    if (!r_ || name.empty()) return nullptr;
    const std::string key = lower(name);
    auto it = tex_.find(key);
    if (it == tex_.end()) {
        Tex t;
        std::string real;
        if (resolveTexture(name, real)) {
            t = loadDds(real, 2048);
        } else if (scene_) {
            const GbsTexture* gt = scene_->textureByName(name);
            if (gt && gt->inAtlas()) t = loadAtlasRegion(*gt);
        }
        it = tex_.emplace(key, t).first;
    }
    if (w) *w = it->second.w;
    if (h) *h = it->second.h;
    return it->second.t;
}

// ---------------------------------------------------------------------------------------- text

const GbsFont* GameUi::fontNamed(const std::string& name) const { return scene_ ? scene_->fontByName(name) : nullptr; }

const GbsFont* GameUi::font(uint32_t id) const {
    if (!scene_) return nullptr;
    const GbsFont* f = scene_->fontById(id);
    if (f && jp_ && !endsWithNoCase(f->name, "_jp"))
        if (const GbsFont* j = scene_->fontByName(f->name + "_JP")) return j;
    return f;
}

float GameUi::textWidth(const GbsFont& f, const std::string& s, float scale) const {
    float w = 0;
    const GbsGlyph* q = f.find('?');
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = Ui::nextCodepoint(s, i);
        if (cp == '\n') continue;
        const GbsGlyph* g = f.find(cp);
        if (cp == ' ' || cp == 0xA0) {   // same gap as drawGlyphs
            w += std::min(g && g->advance > 0 ? static_cast<float>(g->advance) : 1e9f, static_cast<float>(f.lineHeight) * 0.28f) * scale;
            continue;
        }
        if (!g) g = q;
        if (g) w += g->advance * scale;
    }
    return w;
}

std::vector<std::string> GameUi::wrap(const GbsFont& f, const std::string& s, float maxWidth, float scale) const {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        const std::string para = s.substr(pos, nl - pos);
        pos = nl + 1;
        // split into words (a word wider than the box is cut by character, which is also how Japanese wraps)
        std::string cur;
        size_t i = 0;
        while (i < para.size()) {
            size_t j = i;
            while (j < para.size() && para[j] != ' ') ++j;
            std::string word = para.substr(i, j - i);
            i = j < para.size() ? j + 1 : j;
            auto tryAdd = [&](const std::string& piece) {
                const std::string cand = cur.empty() ? piece : cur + " " + piece;
                if (cur.empty() || textWidth(f, cand, scale) <= maxWidth) {
                    cur = cand;
                    return true;
                }
                return false;
            };
            if (!tryAdd(word)) {
                lines.push_back(cur);
                cur.clear();
                // the word may still be too wide on its own
                if (textWidth(f, word, scale) > maxWidth) {
                    std::string piece;
                    for (size_t k = 0; k < word.size();) {
                        const size_t start = k;
                        Ui::nextCodepoint(word, k);
                        const std::string ch = word.substr(start, k - start);
                        if (!piece.empty() && textWidth(f, piece + ch, scale) > maxWidth) {
                            lines.push_back(piece);
                            piece.clear();
                        }
                        piece += ch;
                    }
                    cur = piece;
                } else {
                    cur = word;
                }
            }
        }
        lines.push_back(cur);
        if (nl >= s.size()) break;
    }
    return lines;
}

GameUi::FontGfx& GameUi::gfxFor(const GbsFont& f) {
    FontGfx& g = fontGfx_[f.name];
    if (g.pages.empty()) {
        g.pages.resize(static_cast<size_t>(std::max(1, f.pages)));
        g.tried.assign(g.pages.size(), 0);
    }
    return g;
}

float GameUi::drawGlyphs(const GbsFont& f, const std::string& s, float x, float baselineY, Col c, float scale, float alpha,
                         bool measureOnly, float squeezeX) {
    FontGfx& gfx = gfxFor(f);
    const GbsGlyph* q = f.find('?');
    float pen = 0;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = Ui::nextCodepoint(s, i);
        if (cp == '\n') continue;
        const GbsGlyph* g = f.find(cp);
        if (cp == ' ' || cp == 0xA0) {   // a space is only a gap: "Credits Names" has a placeholder box for it (or none)
            pen += std::min(g && g->advance > 0 ? static_cast<float>(g->advance) : 1e9f, static_cast<float>(f.lineHeight) * 0.28f) * scale * squeezeX;
            continue;
        }
        if (!g) g = q;
        if (!g) continue;
        if (!measureOnly && g->w > 0 && g->h > 0 && g->page >= 0 && static_cast<size_t>(g->page) < gfx.pages.size()) {
            const size_t pg = static_cast<size_t>(g->page);
            if (!gfx.tried[pg]) {
                gfx.tried[pg] = 1;
                std::string real;
                if (vfs_ && vfs_->resolve("Fonts/" + f.name + "_" + std::to_string(pg) + ".dds", &real)) gfx.pages[pg] = loadDds(real, 0);
            }
            const Tex& t = gfx.pages[pg];
            if (t.t) {
                const double sx = static_cast<double>(t.w) / f.atlasW, sy = static_cast<double>(t.h) / f.atlasH;
                SDL_Rect src{static_cast<int>(std::lround(g->x * sx)), static_cast<int>(std::lround(g->y * sy)),
                             std::max(1, static_cast<int>(std::lround(g->w * sx))), std::max(1, static_cast<int>(std::lround(g->h * sy)))};
                SDL_FRect dst{screenX(x + pen + g->bearingX * scale * squeezeX), screenY(baselineY - g->bearingY * scale),
                              g->w * scale * squeezeX * sc_, g->h * scale * sc_};
                SDL_SetTextureColorMod(t.t, c.r, c.g, c.b);
                SDL_SetTextureAlphaMod(t.t, static_cast<Uint8>(std::clamp(c.a * alpha, 0.0f, 255.0f)));
                SDL_RenderCopyF(r_, t.t, &src, &dst);
            }
        }
        pen += g->advance * scale * squeezeX;
    }
    return pen;
}

float GameUi::drawLine(const GbsFont& f, const std::string& s, float x, float y, Col c, float scale, float alpha, float squeezeX) {
    return drawGlyphs(f, s, x, y + f.baseline * scale, c, scale, alpha, false, squeezeX);
}

void GameUi::drawLabelText(const GbsFont& f, const std::string& s, float x, float y, float w, float h, int hAlign, bool vCentre,
                           Col c, float scale, float alpha, float squeezeX) {
    const std::vector<std::string> lines = wrap(f, s, w / squeezeX, scale);
    const float lh = f.lineHeight * scale;
    const float total = lh * static_cast<float>(lines.size());
    float yy = vCentre ? y + (h - total) / 2 : y;
    for (const std::string& l : lines) {
        const float lw = textWidth(f, l, scale) * squeezeX;
        const float xx = hAlign == 1 ? x + (w - lw) / 2 : hAlign == 2 ? x + w - lw : x;
        drawLine(f, l, xx, yy, c, scale, alpha, squeezeX);
        yy += lh;
    }
}

void GameUi::drawLabelBlock(const GbsFont& f, const std::string& s, float x, float y, float w, float h, int hAlign, int vAlign,
                            const GbsTextStyle& style, Col c, float scaleX, float scaleY, float alpha, float skewDeg, int dropShadow) {
    const GbsTextBlock blk = layoutGbsText(f, s, w, h, scaleX, scaleY, style);
    if (blk.empty()) return;
    FontGfx& gfx = gfxFor(f);
    const float shear = std::tan(skewDeg * 3.14159265f / 180.0f);
    // the page texture of every glyph is looked up once per call
    for (size_t li = 0; li < blk.lines.size(); ++li) {
        float dx = 0, dy = 0;
        gbsLineOffset(blk, li, hAlign, vAlign, dx, dy);
        for (int pass = dropShadow != 0 ? 0 : 1; pass < 2; ++pass) {   // pass 0 = the black copy behind, pass 1 = the text
            const float ox = pass == 0 ? static_cast<float>(dropShadow) : 0.0f;
            const Col col = pass == 0 ? Col{0, 0, 0, c.a} : c;
            for (const GbsTextQuad& q : blk.lines[li].quads) {
                const GbsGlyph* g = q.glyph;
                if (!g || g->w <= 0 || g->h <= 0 || g->page < 0 || static_cast<size_t>(g->page) >= gfx.pages.size()) continue;
                const size_t pg = static_cast<size_t>(g->page);
                if (!gfx.tried[pg]) {
                    gfx.tried[pg] = 1;
                    std::string real;
                    if (vfs_ && vfs_->resolve("Fonts/" + f.name + "_" + std::to_string(pg) + ".dds", &real)) gfx.pages[pg] = loadDds(real, 0);
                }
                const Tex& t = gfx.pages[pg];
                if (!t.t) continue;
                const double sx = static_cast<double>(t.w) / f.atlasW, sy = static_cast<double>(t.h) / f.atlasH;
                const float px = x + dx + ox + static_cast<float>(q.left) * blk.scaleX;
                const float py = y + dy + ox + static_cast<float>(q.top) * blk.scaleY;
                const float pw = static_cast<float>(q.right - q.left) * blk.scaleX;
                const float ph = static_cast<float>(q.bottom - q.top) * blk.scaleY;
                SDL_SetTextureColorMod(t.t, col.r, col.g, col.b);
                SDL_SetTextureAlphaMod(t.t, static_cast<Uint8>(std::clamp(col.a * alpha, 0.0f, 255.0f)));
                if (shear == 0.0f) {
                    SDL_Rect src{static_cast<int>(std::lround(g->x * sx)), static_cast<int>(std::lround(g->y * sy)),
                                 std::max(1, static_cast<int>(std::lround(g->w * sx))), std::max(1, static_cast<int>(std::lround(g->h * sy)))};
                    SDL_FRect dst{screenX(px), screenY(py), pw * sc_, ph * sc_};
                    SDL_RenderCopyF(r_, t.t, &src, &dst);
                } else {
                    // sheared about the baseline: a point `a` pixels above it moves right by tan(skew) * a
                    const float u0 = static_cast<float>(g->x * sx / t.w), v0 = static_cast<float>(g->y * sy / t.h);
                    const float u1 = static_cast<float>((g->x + g->w) * sx / t.w), v1 = static_cast<float>((g->y + g->h) * sy / t.h);
                    const float topShift = shear * static_cast<float>(g->bearingY);
                    const float botShift = shear * static_cast<float>(g->bearingY - g->h);
                    const SDL_Color k{col.r, col.g, col.b, static_cast<Uint8>(std::clamp(col.a * alpha, 0.0f, 255.0f))};
                    const SDL_Vertex v[4] = {{{screenX(px + topShift), screenY(py)}, k, {u0, v0}},
                                             {{screenX(px + pw + topShift), screenY(py)}, k, {u1, v0}},
                                             {{screenX(px + pw + botShift), screenY(py + ph)}, k, {u1, v1}},
                                             {{screenX(px + botShift), screenY(py + ph)}, k, {u0, v1}}};
                    const int idx[6] = {0, 1, 2, 0, 2, 3};
                    SDL_RenderGeometry(r_, t.t, v, 4, idx, 6);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------- sprites

void GameUi::drawTexture(SDL_Texture* t, float x, float y, float w, float h, float rotation, float pivotX, float pivotY,
                         bool flipX, bool flipY, Col tint, float alpha, bool additive) {
    if (!t || w <= 0 || h <= 0) return;
    SDL_FRect dst{screenX(x), screenY(y), w * sc_, h * sc_};
    SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(t, static_cast<Uint8>(std::clamp(tint.a * alpha, 0.0f, 255.0f)));
    if (additive) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    const int flip = (flipX ? SDL_FLIP_HORIZONTAL : 0) | (flipY ? SDL_FLIP_VERTICAL : 0);
    if (rotation == 0 && flip == 0) {
        SDL_RenderCopyF(r_, t, nullptr, &dst);
    } else {
        SDL_FPoint center{pivotX * sc_, pivotY * sc_};
        SDL_RenderCopyExF(r_, t, nullptr, &dst, rotation, &center, static_cast<SDL_RendererFlip>(flip));
    }
    if (additive) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
}

void GameUi::fillRect(float x, float y, float w, float h, Col c) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_FRect q{screenX(x), screenY(y), w * sc_, h * sc_};
    SDL_RenderFillRectF(r_, &q);
}

void GameUi::drawArc(float cx, float cy, float rOuter, float rInner, float a0, float a1, Col c) {
    if (!r_ || a1 <= a0 || rOuter <= 0) return;
    const int steps = std::max(2, static_cast<int>(std::ceil((a1 - a0) / 4.0f)));
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve(static_cast<size_t>(2 * (steps + 1)));
    const SDL_Color col{c.r, c.g, c.b, c.a};
    for (int i = 0; i <= steps; ++i) {
        const float ang = (a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(steps)) * 3.14159265f / 180.0f;
        const float sn = std::sin(ang), cs = std::cos(ang);
        v.push_back(SDL_Vertex{SDL_FPoint{screenX(cx + rOuter * sn), screenY(cy - rOuter * cs)}, col, SDL_FPoint{0, 0}});
        v.push_back(SDL_Vertex{SDL_FPoint{screenX(cx + rInner * sn), screenY(cy - rInner * cs)}, col, SDL_FPoint{0, 0}});
    }
    for (int i = 0; i < steps; ++i) {
        const int b = 2 * i;
        for (int k : {b, b + 1, b + 2, b + 1, b + 3, b + 2}) idx.push_back(k);
    }
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r_, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

namespace {
Col modulate(Col c, uint32_t argb) {
    auto mul = [](uint8_t a, uint32_t b) { return static_cast<uint8_t>((static_cast<uint32_t>(a) * b + 127) / 255); };
    return Col{mul(c.r, (argb >> 16) & 0xFF), mul(c.g, (argb >> 8) & 0xFF), mul(c.b, argb & 0xFF), c.a};
}
}  // namespace

int GameUi::unload(const GbsSceneView& v) {
    if (!scene_) return 0;
    int freed = 0;
    for (uint32_t id : v.textureIds) {
        const std::string& nm = scene_->textureName(id);
        if (nm.empty()) continue;
        const auto it = tex_.find(lower(nm));
        if (it == tex_.end()) continue;
        if (it->second.t) {
            SDL_DestroyTexture(it->second.t);
            ++freed;
        }
        tex_.erase(it);
    }
    return freed;
}

int GameUi::preload(const GbsSceneView& v, int budget) {
    if (!scene_) return 0;
    int decoded = 0;
    for (uint32_t id : v.textureIds) {
        const std::string& nm = scene_->textureName(id);
        if (nm.empty()) continue;
        const bool had = tex_.count(nm) != 0;
        texture(nm);
        if (!had && tex_.count(nm)) ++decoded;
        if (budget > 0 && decoded >= budget) break;
    }
    return decoded;
}

void GameUi::drawNodes(const std::vector<GbsNode>& nodes, float ox, float oy, const Hook& hook, int depth) {
    Xf root;
    root.tx = ox;
    root.ty = oy;
    drawNodesXf(nodes, root, hook, depth);
}

void GameUi::fillQuad(const Xf& at, float w, float h, Col col) {
    if (!r_ || w <= 0 || h <= 0 || col.a == 0) return;
    if (at.s == 0.0f && at.c == 1.0f) {
        fillRect(at.tx, at.ty, w, h, col);
        return;
    }
    float px[4], py[4];
    at.apply(0, 0, px[0], py[0]);
    at.apply(w, 0, px[1], py[1]);
    at.apply(w, h, px[2], py[2]);
    at.apply(0, h, px[3], py[3]);
    const SDL_Color sc{col.r, col.g, col.b, col.a};
    SDL_Vertex v[4];
    for (int i = 0; i < 4; ++i) v[i] = SDL_Vertex{SDL_FPoint{screenX(px[i]), screenY(py[i])}, sc, SDL_FPoint{0, 0}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

void GameUi::drawNodesXf(const std::vector<GbsNode>& nodes, const Xf& parent, const Hook& hook, int depth) {
    if (!scene_ || depth > 24) return;
    for (const GbsNode& n : nodes) {
        NodeState st;
        if (hook) hook(n, st);
        if (st.hide) continue;
        // geometry / colour: the animation's pose when there is one, the resting pose of the node otherwise
        float nx = n.x, ny = n.y, nw = n.w, nh = n.h, rot = n.rotation, pvx = n.pivotX, pvy = n.pivotY, lblScale = n.scaleX;
        float alpha = st.alpha;
        Col tint = st.tint, textColor = st.textColor;
        uint32_t flat = 0;      // pose colour (0xAARRGGBB) for a sprite without a texture
        bool hasFlat = false;
        if (const GbsPose* ps = anim_ ? anim_->pose(n.id) : nullptr) {
            nx = ps->x;
            ny = ps->y;
            nw = ps->w;
            nh = ps->h;
            rot = ps->rot;
            pvx = ps->pivotX;
            pvy = ps->pivotY;
            if (ps->kind == 'S' || ps->kind == 'L') {
                const uint32_t argb = st.ignorePoseAlpha ? (ps->argb | 0xFF000000u) : ps->argb;
                if (!st.ignorePoseAlpha) alpha *= ps->alpha();
                tint = modulate(tint, argb);
                textColor = modulate(textColor, argb);
                flat = argb;
                hasFlat = true;
            }
            if (ps->kind == 'L') lblScale = ps->scaleX;
            if (st.pivotAnchored && !otterPivot_) {
                nx -= pvx;
                ny -= pvy;
            }
        }
        if (otterPivot_ && !st.forceBox) {   // the position is where the pivot sits, for resting and animated nodes alike
            nx -= pvx;
            ny -= pvy;
        }
        if (st.forceBox) {
            nx = st.forceX;
            ny = st.forceY;
            nw = st.forceW;
            nh = st.forceH;
        }
        // this node's transform: translate by its position (+ hook offset), rotate about its pivot, below the parent's
        const float rr = rot * 3.14159265f / 180.0f;
        const float cr = std::cos(rr), sr = std::sin(rr);
        const float tAx = nx + st.dx + pvx - (cr * pvx - sr * pvy);
        const float tAy = ny + st.dy + pvy - (sr * pvx + cr * pvy);
        Xf me;
        me.c = parent.c * cr - parent.s * sr;
        me.s = parent.s * cr + parent.c * sr;
        parent.apply(tAx, tAy, me.tx, me.ty);
        const float angle = std::atan2(me.s, me.c) * 180.0f / 3.14159265f;
        if (n.type == GbsNode::Type::Sprite && !st.skipSelf && st.rawTex && st.rawW > 0 && st.rawH > 0) {
            // a picture that is not in the scene (gallery thumbnails / the full-size viewer): aspect-correct inside the node's box
            if (alpha > 0.0f) {
                SDL_Rect src{0, 0, st.rawW, st.rawH};
                SDL_FRect dst{screenX(me.tx), screenY(me.ty), nw * sc_, nh * sc_};
                const float boxA = nh > 0 ? nw / nh : 1.0f, imgA = static_cast<float>(st.rawW) / static_cast<float>(st.rawH);
                if (st.rawCover) {
                    if (imgA > boxA) {   // wider than the box: crop the sides
                        src.w = std::max(1, static_cast<int>(std::lround(st.rawH * boxA)));
                        src.x = (st.rawW - src.w) / 2;
                    } else {             // taller: crop top and bottom
                        src.h = std::max(1, static_cast<int>(std::lround(st.rawW / boxA)));
                        src.y = (st.rawH - src.h) / 2;
                    }
                } else if (imgA > boxA) {   // fit: bars above and below
                    const float h = dst.w / imgA;
                    dst.y += (dst.h - h) / 2;
                    dst.h = h;
                } else {
                    const float w = dst.h * imgA;
                    dst.x += (dst.w - w) / 2;
                    dst.w = w;
                }
                SDL_SetTextureColorMod(st.rawTex, tint.r, tint.g, tint.b);
                SDL_SetTextureAlphaMod(st.rawTex, static_cast<Uint8>(std::clamp(tint.a * alpha, 0.0f, 255.0f)));
                const int rawFlip = (n.flipX ? SDL_FLIP_HORIZONTAL : 0) | (n.flipY ? SDL_FLIP_VERTICAL : 0);
                if (rawFlip) SDL_RenderCopyExF(r_, st.rawTex, &src, &dst, 0.0, nullptr, static_cast<SDL_RendererFlip>(rawFlip));
                else SDL_RenderCopyF(r_, st.rawTex, &src, &dst);
                SDL_SetTextureAlphaMod(st.rawTex, 255);
            }
        } else if (n.type == GbsNode::Type::Sprite && !st.skipSelf) {
            const std::string& nm = st.texName.empty() ? scene_->textureName(n.texId) : st.texName;
            const float u0 = std::clamp(st.cropL, 0.0f, 1.0f), u1 = std::clamp(st.cropR, 0.0f, 1.0f);
            SDL_Texture* t = (nm.empty() || alpha <= 0.0f) ? nullptr : texture(nm);
            if (alpha <= 0.0f || u1 <= u0) {
                // nothing of it is visible
            } else if (t && u0 <= 0.0f && u1 >= 1.0f) {
                SDL_FRect dst{screenX(me.tx), screenY(me.ty), nw * sc_, nh * sc_};
                SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
                SDL_SetTextureAlphaMod(t, static_cast<Uint8>(std::clamp(tint.a * alpha, 0.0f, 255.0f)));
                if (st.additive) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
                int flip = (n.flipX ? SDL_FLIP_HORIZONTAL : 0) | (n.flipY ? SDL_FLIP_VERTICAL : 0);
                // A key can give a size below zero ("139,31 -95x98"): the picture is mirrored and reaches back from its position.  SDL ignores
                // a rectangle with a negative size, so it is drawn from the far corner with the matching flip.
                SDL_FPoint center{0, 0};
                if (dst.w < 0) {
                    dst.x += dst.w;
                    dst.w = -dst.w;
                    center.x = dst.w;
                    flip ^= SDL_FLIP_HORIZONTAL;
                }
                if (dst.h < 0) {
                    dst.y += dst.h;
                    dst.h = -dst.h;
                    center.y = dst.h;
                    flip ^= SDL_FLIP_VERTICAL;
                }
                if (std::fabs(angle) < 0.001f && flip == 0) {
                    SDL_RenderCopyF(r_, t, nullptr, &dst);
                } else {
                    SDL_RenderCopyExF(r_, t, nullptr, &dst, angle, &center, static_cast<SDL_RendererFlip>(flip));
                }
                if (st.additive) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            } else if (t) {  // a horizontal slice of the sprite (health bars)
                int tw = 0, th = 0;
                SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                const float s0 = n.flipX ? 1.0f - u1 : u0, s1 = n.flipX ? 1.0f - u0 : u1;
                const SDL_Rect src{static_cast<int>(std::lround(s0 * tw)), 0,
                                   std::max(1, static_cast<int>(std::lround((s1 - s0) * tw))), th};
                float sx0, sy0;
                parent.apply(nx + st.dx + u0 * nw, ny + st.dy, sx0, sy0);
                const SDL_FRect dst{screenX(sx0), screenY(sy0), (u1 - u0) * nw * sc_, nh * sc_};
                SDL_SetTextureColorMod(t, tint.r, tint.g, tint.b);
                SDL_SetTextureAlphaMod(t, static_cast<Uint8>(std::clamp(tint.a * alpha, 0.0f, 255.0f)));
                if (st.additive) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
                const int flip = (n.flipX ? SDL_FLIP_HORIZONTAL : 0) | (n.flipY ? SDL_FLIP_VERTICAL : 0);
                SDL_RenderCopyExF(r_, t, &src, &dst, 0, nullptr, static_cast<SDL_RendererFlip>(flip));
                if (st.additive) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            } else if (st.fallback.a > 0) {
                float fx, fy;
                parent.apply(nx + st.dx + u0 * nw, ny + st.dy, fx, fy);
                fillRect(fx, fy, (u1 - u0) * nw, nh,
                         Col{st.fallback.r, st.fallback.g, st.fallback.b, static_cast<uint8_t>(std::clamp(st.fallback.a * alpha, 0.0f, 255.0f))});
            } else if (nm.empty() && hasFlat && alpha > 0.0f) {
                // a sprite without a texture is a flat coloured quad (fades, dimmers): its colour is animated, so only a pose gives it one
                Xf q = me;
                const uint32_t a8 = flat >> 24;
                fillQuad(q, nw, nh, Col{static_cast<uint8_t>((flat >> 16) & 0xFF), static_cast<uint8_t>((flat >> 8) & 0xFF),
                                        static_cast<uint8_t>(flat & 0xFF), static_cast<uint8_t>(std::clamp(a8 * st.alpha, 0.0f, 255.0f))});
            }
        } else if (n.type == GbsNode::Type::Label && st.hasText && !st.skipSelf && alpha > 0.0f) {
            if (const GbsFont* f = font(n.fontId)) {
                // laid out by the scene's own rules: the label's leading / tracking / fit mode / vertical alignment (gbs_text.h);
                // `scaleX` is multiplied by the caller's sideways squeeze, which only ever narrows the glyphs
                GbsTextStyle ts;
                ts.leading = n.leading;
                ts.tracking = n.tracking;
                ts.fit = n.textFit;
                float skewDeg = n.skew;
                int shadow = n.dropShadow;
                if (const GbsPose* ps = anim_ ? anim_->pose(n.id) : nullptr)
                    if (ps->kind == 'L') {
                        skewDeg = ps->skew;
                        shadow = ps->dropShadow;
                    }
                const float sc = st.textScale * lblScale;
                if (st.fitOverride >= 0) ts.fit = static_cast<uint32_t>(st.fitOverride);
                float boxY = me.ty, boxH = nh;
                if (st.textBoxH > nh) {   // a taller layout box grows around the node's own box according to the vertical alignment
                    const float grow = st.textBoxH - nh;
                    boxY -= n.vAlign == 1 ? grow / 2 : (n.vAlign == 2 ? grow : 0.0f);
                    boxH = st.textBoxH;
                }
                drawLabelBlock(*f, st.text, me.tx, boxY, nw, boxH, st.alignOverride >= 0 ? st.alignOverride : n.hAlign, n.vAlign, ts, textColor,
                               sc * st.textSqueezeX, sc, alpha, skewDeg, shadow);
            }
        } else if (n.type == GbsNode::Type::Arc && !st.skipSelf && alpha > 0.0f) {
            // an arc node: a flat band around the middle of its box; an animation can move the sweep / start / colour (gbs_arc.h)
            GbsArcStyle as;
            as.segments = n.arcSegments;
            as.radius = n.arcRadius;
            as.thickness = n.arcThickness;
            as.sweep = n.arcSweep;
            as.start = n.arcStart;
            as.clockwise = n.arcClockwise;
            uint32_t argb = n.colour;
            if (const GbsPose* ps = anim_ ? anim_->pose(n.id) : nullptr)
                if (ps->kind == 'R') {
                    as.radius = ps->arcRadius;
                    as.thickness = ps->arcThickness;
                    as.sweep = ps->arcSweep;
                    as.start = ps->arcStart;
                    argb = ps->argb;
                }
            const std::vector<GbsArcPoint> tris = gbsArcTriangles(as);
            const float a8 = static_cast<float>(argb >> 24) * alpha;
            if (!tris.empty() && a8 >= 0.5f && r_) {
                const SDL_Color sc{static_cast<Uint8>((argb >> 16) & 0xFF), static_cast<Uint8>((argb >> 8) & 0xFF), static_cast<Uint8>(argb & 0xFF),
                                   static_cast<Uint8>(std::clamp(a8, 0.0f, 255.0f))};
                std::vector<SDL_Vertex> vx;
                vx.reserve(tris.size());
                for (const GbsArcPoint& p : tris) {
                    float qx, qy;
                    me.apply(nw / 2 + p.x, nh / 2 + p.y, qx, qy);
                    vx.push_back(SDL_Vertex{SDL_FPoint{screenX(qx), screenY(qy)}, sc, SDL_FPoint{0, 0}});
                }
                SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
                SDL_RenderGeometry(r_, nullptr, vx.data(), static_cast<int>(vx.size()), nullptr, 0);
            }
        }
        if (!n.kids.empty()) drawNodesXf(n.kids, me, hook, depth + 1);
    }
}

}  // namespace zengine
