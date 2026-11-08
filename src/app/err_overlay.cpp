#include "err_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "seg_font.h"

namespace zengine {

namespace {

constexpr float kAdvRatio = 52.22f / 64.0f;   // advance of a digit / its height (DSEG7 Classic: every cell is as wide as a digit)
constexpr uint32_t kFirstCp = 0x20, kLastCp = 0x7E;
constexpr size_t kGlyphCount = kLastCp - kFirstCp + 1;

constexpr Col kRed{255, 70, 56, 255};
constexpr Col kRedDim{150, 30, 28, 255};
constexpr Col kGreen{88, 255, 124, 255};
constexpr Col kAmber{255, 196, 64, 255};

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int ceilDiv(int a, int b) { return -floorDiv(-a, b); }

Col withAlpha(Col c, int a) { return Col{c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(a, 0, 255))}; }

void fillRect(SDL_Renderer* r, int x, int y, int w, int h, Col c) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    const SDL_Rect q{x, y, w, h};
    SDL_RenderFillRect(r, &q);
}

}  // namespace

bool ErrOverlay::init(SDL_Renderer* r) {
    shutdown();
    if (!r) return false;
    // zero-run decode of the full-size coverage bitmaps
    std::vector<uint8_t> raw(segfont::kRawSize, 0);
    size_t o = 0;
    for (size_t i = 0; i < sizeof(segfont::kRle) && o < raw.size();) {
        if (segfont::kRle[i] == 0 && i + 1 < sizeof(segfont::kRle)) {
            o += segfont::kRle[i + 1];
            i += 2;
        } else {
            raw[o++] = segfont::kRle[i++];
        }
    }
    const segfont::Glyph* gl[kGlyphCount] = {};
    for (const segfont::Glyph& g : segfont::kGlyphs)
        if (g.cp >= kFirstCp && g.cp <= kLastCp) gl[g.cp - kFirstCp] = &g;

    for (int k = 0; k < 3; ++k) {
        const int f = 1 << k;
        Level& L = levels_[k];
        L.digitH = segfont::kPx / f;
        L.cells.assign(kGlyphCount, Cell{});
        struct Tmp { int x0 = 0, y0 = 0, w = 0, h = 0; std::vector<uint8_t> a; };
        std::vector<Tmp> tmp(kGlyphCount);
        int maxW = 1, maxH = 1;
        for (size_t i = 0; i < kGlyphCount; ++i) {
            const segfont::Glyph* g = gl[i];
            if (!g) continue;
            Cell& c = L.cells[i];
            c.adv = g->adv / static_cast<float>(f);
            if (g->w <= 0 || g->h <= 0) continue;
            Tmp& t = tmp[i];
            t.x0 = floorDiv(g->xo, f);
            t.y0 = floorDiv(g->yo, f);
            t.w = ceilDiv(g->xo + g->w, f) - t.x0;
            t.h = ceilDiv(g->yo + g->h, f) - t.y0;
            t.a.assign(static_cast<size_t>(t.w) * static_cast<size_t>(t.h), 0);
            for (int ty = 0; ty < t.h; ++ty)
                for (int tx = 0; tx < t.w; ++tx) {
                    int sum = 0;
                    for (int dy = 0; dy < f; ++dy)
                        for (int dx = 0; dx < f; ++dx) {
                            const int gx = (t.x0 + tx) * f + dx - g->xo;   // back to the glyph's own pixel grid
                            const int gy = (t.y0 + ty) * f + dy - g->yo;
                            if (gx >= 0 && gy >= 0 && gx < g->w && gy < g->h) sum += raw[g->off + static_cast<size_t>(gy) * g->w + gx];
                        }
                    t.a[static_cast<size_t>(ty) * t.w + tx] = static_cast<uint8_t>(sum / (f * f));
                }
            c.xo = static_cast<int16_t>(t.x0);
            c.yo = static_cast<int16_t>(t.y0);
            c.w = static_cast<int16_t>(t.w);
            c.h = static_cast<int16_t>(t.h);
            maxW = std::max(maxW, t.w);
            maxH = std::max(maxH, t.h);
        }
        L.cellW = maxW + 2;
        L.cellH = maxH + 2;
        const int rows = static_cast<int>((kGlyphCount + L.cols - 1) / L.cols);
        const int tw = L.cellW * L.cols, th = L.cellH * rows;
        std::vector<uint32_t> px(static_cast<size_t>(tw) * th, 0x00FFFFFFu);
        for (size_t i = 0; i < kGlyphCount; ++i) {
            Cell& c = L.cells[i];
            c.sx = static_cast<int16_t>((i % L.cols) * L.cellW + 1);
            c.sy = static_cast<int16_t>((i / L.cols) * L.cellH + 1);
            const Tmp& t = tmp[i];
            for (int ty = 0; ty < t.h; ++ty)
                for (int tx = 0; tx < t.w; ++tx)
                    px[static_cast<size_t>(c.sy + ty) * tw + (c.sx + tx)] = 0x00FFFFFFu | (static_cast<uint32_t>(t.a[static_cast<size_t>(ty) * t.w + tx]) << 24);
        }
        L.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
        if (!L.tex) {
            shutdown();
            return false;
        }
        SDL_UpdateTexture(L.tex, nullptr, px.data(), tw * 4);
        SDL_SetTextureBlendMode(L.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(L.tex, SDL_ScaleModeLinear);
    }
    r_ = r;
    return true;
}

void ErrOverlay::shutdown() {
    for (Level& L : levels_) {
        if (L.tex) SDL_DestroyTexture(L.tex);
        L = Level{};
    }
    r_ = nullptr;
}

const ErrOverlay::Level& ErrOverlay::levelFor(int digitH) const {
    // the smallest atlas that is still at least as tall as the digits being asked for: always scaled down, never up
    if (digitH <= levels_[2].digitH) return levels_[2];
    if (digitH <= levels_[1].digitH) return levels_[1];
    return levels_[0];
}

int ErrOverlay::textWidth(const std::string& s, int digitH) const {
    float w = 0;
    for (char ch : s) {
        const bool ok = ch >= static_cast<char>(kFirstCp) && ch <= static_cast<char>(kLastCp);
        w += (ok ? ch == '.' ? 0.0f : kAdvRatio : kAdvRatio) * static_cast<float>(digitH);
    }
    return static_cast<int>(std::lround(w));
}

void ErrOverlay::drawText(const std::string& s, int x, int y, int digitH, Col lit, bool ghost, Col ghostCol) {
    if (!ready() || digitH < 4) return;
    const Level& L = levelFor(digitH);
    const float sc = static_cast<float>(digitH) / static_cast<float>(L.digitH);
    const float baseline = static_cast<float>(y + digitH);
    if (ghostCol.a == 0) ghostCol = withAlpha(lit, 34);
    auto put = [&](const Cell& c, float penX, Col col) {
        if (c.w <= 0 || c.h <= 0 || col.a == 0) return;
        const SDL_Rect src{c.sx, c.sy, c.w, c.h};
        const SDL_FRect dst{penX + static_cast<float>(c.xo) * sc, baseline + static_cast<float>(c.yo) * sc, static_cast<float>(c.w) * sc,
                            static_cast<float>(c.h) * sc};
        SDL_SetTextureColorMod(L.tex, col.r, col.g, col.b);
        SDL_SetTextureAlphaMod(L.tex, col.a);
        SDL_RenderCopyF(r_, L.tex, &src, &dst);
    };
    float pen = static_cast<float>(x);
    const Cell& eight = L.cells['8' - kFirstCp];
    for (char ch : s) {
        const uint32_t cp = static_cast<unsigned char>(ch);
        const Cell& c = (cp >= kFirstCp && cp <= kLastCp) ? L.cells[cp - kFirstCp] : L.cells[0];
        if (ghost && c.adv > 0.01f) put(eight, pen, ghostCol);   // every cell shows its unlit segments, blank ones too
        put(c, pen, lit);
        pen += c.adv * sc;
    }
}

Rect ErrOverlay::badgeRect(int lw, int lh, int digitH, int codes) {
    const int w = static_cast<int>(std::lround(5.0f * kAdvRatio * static_cast<float>(digitH))) + 16;
    const int h = digitH + 14 + (codes > 1 ? 6 : 0);
    return Rect{lw - w, lh - h, w, h};
}

void ErrOverlay::drawBadge(Ui& ui, uint32_t nowMs, const err::Registry& reg, bool debug) {
    const std::vector<err::Code> act = reg.active();
    if (act.empty()) return;
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(r_, &lw, &lh);
    if (lw <= 0 || lh <= 0) SDL_GetRendererOutputSize(r_, &lw, &lh);
    const uint32_t gen = reg.generation();
    if (gen != lastGen_) {
        lastGen_ = gen;
        genAtMs_ = nowMs;
    }
    const size_t idx = act.size() > 1 ? static_cast<size_t>((nowMs / 1800u) % act.size()) : 0;
    const err::Code code = act[idx];
    const bool fatal = err::isFatal(code);
    const uint32_t since = nowMs - genAtMs_;
    const bool blinkOff = !fatal && since < 2400 && ((since / 300u) % 2u) == 1u;   // a new code blinks for 2.4 s
    const Rect b = badgeRect(lw, lh, kErrDigitH, static_cast<int>(act.size()));

    fillRect(r_, b.x, b.y, b.w, b.h, fatal ? Col{96, 6, 8, 245} : Col{34, 3, 5, 238});
    fillRect(r_, b.x, b.y, b.w, 2, kRed);                       // the red corner: bright edges on the two sides facing the screen
    fillRect(r_, b.x, b.y, 2, b.h, kRed);
    fillRect(r_, b.x + 2, b.y + 2, 6, 2, kRedDim);              // small bracket in the corner
    fillRect(r_, b.x + 2, b.y + 2, 2, 6, kRedDim);
    drawText("Err " + std::to_string(static_cast<int>(code)), b.x + 8, b.y + 7, kErrDigitH, blinkOff ? withAlpha(kRed, 0) : kRed, true, withAlpha(kRed, 38));
    if (act.size() > 1) {   // one pip per raised code, the shown one lit
        for (size_t i = 0; i < act.size(); ++i)
            fillRect(r_, b.x + 8 + static_cast<int>(i) * 7, b.y + b.h - 7, 5, 3, i == idx ? kRed : kRedDim);
    }
    if (debug) {
        const std::vector<std::string> det = reg.details(code);
        std::string line = err::displayText(code) + ": " + (det.empty() ? std::string(err::meaning(code, false)) : det.front());
        line = Ui::fit(line, std::max(40, lw - 8));
        const int x = std::max(4, lw - Ui::textW(line) - 4);
        ui.fill(Rect{x - 3, b.y - 12, Ui::textW(line) + 6, 11}, Col{20, 2, 3, 215});
        ui.text(x, b.y - 10, line, Col{255, 120, 108, 235});
    }
}

void ErrOverlay::drawRam(Ui& ui, uint32_t nowMs, const DebugReadout* extra, int rightEdge) {
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(r_, &lw, &lh);
    if (lw <= 0 || lh <= 0) SDL_GetRendererOutputSize(r_, &lw, &lh);
    if (!procSeen_ || nowMs - procAtMs_ >= 500) {   // /proc is read twice a second, not every frame
        proc_ = ram::process();
        if (proc_.ok) tracker_.update(nowMs, proc_.rssBytes);
        procAtMs_ = nowMs;
        procSeen_ = true;
    }
    const uint64_t mb = proc_.rssBytes >> 20;
    Col col = kGreen;
    if (tracker_.growing()) col = kAmber;
    if (mb >= 1500 || tracker_.mbPerMin() > 40.0) col = kRed;

    const std::string big = proc_.ok ? ram::megabytes(proc_.rssBytes, 4) : "----";
    const std::string peak = proc_.ok ? ram::megabytes(std::max(proc_.peakBytes, tracker_.peakBytes()), 4) : "----";
    const double rate = proc_.ok ? tracker_.mbPerMin() : 0.0;
    std::string rateTxt = proc_.ok ? ram::megabytes(static_cast<uint64_t>(std::min(999.0, std::fabs(rate)) * 1048576.0), 3) : "---";
    rateTxt.insert(rateTxt.begin(), (proc_.ok && rate < -0.5) ? '-' : ' ');

    const bool strip = extra && (extra->haveWorld || extra->spriteBytes > 0);
    const int bigW = textWidth("0000", kRamBigH), smallW = textWidth("0000", kRamSmallH);
    int panelW = 8 + bigW + 14 + smallW + 8;
    const int panelH = 60 + (strip ? 22 : 0);
    std::string hash, spr;
    int stripW = 0;
    if (strip) {
        hash = extra->haveWorld ? ram::hex(extra->worldHash, 8) : "--------";
        spr = ram::megabytes(extra->spriteBytes, 4);
        stripW = 8 + 32 + textWidth(hash, kRamSmallH) + 12 + 24 + textWidth(spr, kRamSmallH) + 8;
        panelW = std::max(panelW, stripW);
    }
    const int x0 = rightEdge - panelW, y0 = lh - panelH;
    fillRect(r_, x0, y0, panelW, panelH, Col{5, 16, 9, 228});
    fillRect(r_, x0, y0, panelW, 1, col);
    fillRect(r_, x0, y0, 1, panelH, col);

    const Col dim = Col{150, 190, 160, 235};
    ui.text(x0 + 8, y0 + 4, "RAM MB", dim);
    drawText(big, x0 + 8, y0 + 14, kRamBigH, col, true, withAlpha(col, 30));
    const int x1 = x0 + 8 + bigW + 14;
    ui.text(x1, y0 + 4, "PEAK", dim);
    drawText(peak, x1, y0 + 14, kRamSmallH, col, true, withAlpha(col, 30));
    ui.text(x1, y0 + 31, "MB/MIN", dim);
    drawText(rateTxt, x1, y0 + 41, kRamSmallH, col, true, withAlpha(col, 30));
    if (strip) {
        const int ys = y0 + 62;
        ui.text(x0 + 8, ys + 3, "HASH", dim);
        drawText(hash, x0 + 8 + 32, ys, kRamSmallH, extra->haveWorld ? kGreen : kRedDim, true, withAlpha(kGreen, 30));
        const int xs = x0 + 8 + 32 + textWidth(hash, kRamSmallH) + 12;
        ui.text(xs, ys + 3, "SPR", dim);
        drawText(spr, xs + 24, ys, kRamSmallH, kGreen, true, withAlpha(kGreen, 30));
    }
}

void ErrOverlay::draw(Ui& ui, uint32_t nowMs, const err::Registry& reg, bool debug, const DebugReadout* extra) {
    if (!ready()) return;
    const bool codes = reg.any();
    if (!codes && !debug) return;
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(r_, &lw, &lh);
    if (lw <= 0 || lh <= 0) SDL_GetRendererOutputSize(r_, &lw, &lh);
    int right = lw;
    if (codes) {
        drawBadge(ui, nowMs, reg, debug);
        right = badgeRect(lw, lh, kErrDigitH, static_cast<int>(reg.active().size())).x - 6;
    }
    if (debug) drawRam(ui, nowMs, extra, right);
}

}  // namespace zengine
