#include "colour_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace zengine {
namespace menu {

using coledit::Hsv;
using coledit::Region;
using coledit::Rgb;

namespace {
constexpr int kW = 640, kH = 360;
const Rect kPreview{10, 34, 240, 316};
const Rect kPanel{258, 34, 372, 316};
constexpr int kLeft = 268, kWide = 352;   // the controls' column

Col col(const Rgb& c, uint8_t a = 255) { return Col{c[0], c[1], c[2], a}; }

std::string hexOf(const Rgb& c) {
    char b[10];
    std::snprintf(b, sizeof b, "#%02X%02X%02X", c[0], c[1], c[2]);
    return b;
}

// a horizontal bar whose colour runs through `at(0..1)`
template <class F>
void gradient(Ui& ui, Rect b, F at, int steps) {
    for (int i = 0; i < steps; ++i) {
        const int x0 = b.x + b.w * i / steps, x1 = b.x + b.w * (i + 1) / steps;
        ui.fill(Rect{x0, b.y, std::max(1, x1 - x0), b.h}, col(at((i + 0.5f) / steps)));
    }
    ui.outline(b, Col{12, 12, 16, 255});
}

void handle(Ui& ui, Rect bar, float v) {
    const int x = bar.x + static_cast<int>(std::lround(v * (bar.w - 1)));
    ui.fill(Rect{x - 2, bar.y - 3, 5, bar.h + 6}, Col{12, 12, 16, 255});
    ui.fill(Rect{x - 1, bar.y - 2, 3, bar.h + 4}, Col{245, 245, 250, 255});
}
}  // namespace

bool ColourEditor::open(GameUi& gfx, const coledit::Favourites& favs, Setup s) {
    closeNow();
    if (!s.scheme.valid()) return false;
    setup_ = std::move(s);
    favs_ = &favs;
    cur_ = setup_.scheme;
    orig_ = cur_;
    pal_ = coledit::bake(cur_);
    if (!sprite_.open(gfx, setup_.character, pal_)) return false;
    name_ = setup_.name;
    region_ = 0;
    keyIdx_ = -1;
    drag_ = 0;
    globalDeg_ = 0;
    dirty_ = true;
    cancelled_ = false;
    armed_ = false;
    msg_.clear();
    msgMs_ = 0;
    armDeleteMs_ = -10000;
    seed_ = 1;
    pulseT0_ = -100000;
    select(0, -1);
    open_ = true;
    return true;
}

void ColourEditor::closeNow() {
    sprite_.close();
    open_ = false;
    drag_ = 0;
}

Rgb ColourEditor::target() const {
    const Region& r = cur_.regions[static_cast<size_t>(region_)];
    if (keyIdx_ >= 0 && keyIdx_ < static_cast<int>(r.key.size())) return r.key[static_cast<size_t>(keyIdx_)];
    return coledit::mainKey(r);
}

void ColourEditor::loadHsv() { hsv_ = coledit::toHsv(target()); }

void ColourEditor::select(int region, int key) {
    const int n = static_cast<int>(cur_.regions.size());
    if (n == 0) return;
    region_ = std::max(0, std::min(n - 1, region));
    keyIdx_ = key;
    if (keyIdx_ >= static_cast<int>(cur_.regions[static_cast<size_t>(region_)].key.size())) keyIdx_ = -1;
    pulseT0_ = nowMs_;
    drag_ = 0;
    loadHsv();
    dirty_ = true;
}

// the slider values -> the colours (the part keeps its shading; a single shade is set as it is)
void ColourEditor::applyHsv() {
    Region& r = cur_.regions[static_cast<size_t>(region_)];
    const Rgb c = coledit::fromHsv(hsv_);
    if (keyIdx_ >= 0) {
        r.key[static_cast<size_t>(keyIdx_)] = c;
    } else {
        r = dragRegion_;
        coledit::setMain(r, c);
    }
    dirty_ = true;
}

void ColourEditor::say(const std::string& m) {
    msg_ = m;
    msgMs_ = nowMs_ + 2600;
}

ColourEditor::Result ColourEditor::frame(Ui& ui, int nowMs) {
    Result res;
    if (!open_) return res;
    nowMs_ = nowMs;
    // The finger that held the character is still down when the editor opens: its release is not a tap on the editor.
    struct Mute {
        Ui& ui;
        Input saved;
        bool on;
        Mute(Ui& u, bool mute) : ui(u), saved(u.in), on(mute) {
            if (on) ui.in.pressed = ui.in.released = ui.in.down = false;
        }
        ~Mute() {
            if (on) ui.in = saved;
        }
    } mute(ui, !armed_);
    if (!armed_ && !mute.saved.down) armed_ = true;
    const Input& in = ui.in;
    if (cancelled_ || in.back) {
        res.kind = Result::Kind::Cancelled;
        closeNow();
        return res;
    }
    const int nRegions = static_cast<int>(cur_.regions.size());

    // ---- drawing starts: the selector underneath is dimmed, the two panels sit on top
    ui.fill(Rect{0, 0, kW, kH}, Col{6, 5, 9, 238});
    ui.text(12, 10, std::string(tr("COLOR EDITOR", "EDITOR DE COLOR")) + " - " + setup_.title, kText, 2);
    ui.fill(kPreview, Col{20, 18, 26, 255});
    ui.outline(kPreview, kLine);
    ui.fill(kPanel, Col{20, 18, 26, 255});
    ui.outline(kPanel, kLine);

    // ---- the preview: the character, big, on a floor line; a tap picks the part under the finger
    const SDL_Rect bb = sprite_.bounds();   // only the part of the frame that has character in it
    const float scale = std::min((kPreview.w - 20.0f) / bb.w, (kPreview.h - 30.0f) / bb.h);
    const float dw = bb.w * scale, dh = bb.h * scale;
    const float dx = kPreview.x + (kPreview.w - dw) / 2, dy = kPreview.y + kPreview.h - 14 - dh;
    ui.fill(Rect{kPreview.x + 10, kPreview.y + kPreview.h - 14, kPreview.w - 20, 1}, Col{90, 84, 104, 255});
    {
        const bool pulsing = nowMs_ - pulseT0_ < 1400;
        if (pulsing) dirty_ = true;
        if (dirty_) {
            pal_ = coledit::bake(cur_);
            const float pulse = pulsing ? 0.5f + 0.5f * std::sin((nowMs_ - pulseT0_) * 0.012f) : 0.0f;
            sprite_.paint(pal_, region_ + 1, pulse);
            dirty_ = false;
        }
        const SDL_Rect dst{static_cast<int>(std::lround(dx)), static_cast<int>(std::lround(dy)), static_cast<int>(std::lround(dw)), static_cast<int>(std::lround(dh))};
        SDL_RenderCopy(ui.r, sprite_.texture(), &bb, &dst);
    }
    ui.textIn(Rect{kPreview.x + 4, kPreview.y + 3, kPreview.w - 8, 10}, tr("Tap a part of the character", "Toca una parte del personaje"), kDim, true);
    if (in.released && drag_ == 0 && kPreview.contains(in.mx, in.my) && kPreview.contains(in.pmx, in.pmy) &&
        std::abs(in.mx - in.pmx) < 8 && std::abs(in.my - in.pmy) < 8) {
        const int px = bb.x + static_cast<int>(std::floor((in.mx - dx) / scale)), py = bb.y + static_cast<int>(std::floor((in.my - dy) / scale));
        const int band = sprite_.regionAt(px, py);
        if (band >= 1 && band <= nRegions) select(band - 1, -1);
    }

    // ---- part navigation
    int y = 40;
    {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s %d / %d", tr("Part", "Parte"), region_ + 1, nRegions);
        const int steps[4] = {-5, -1, 1, 5};
        const char* lab[4] = {"<<", "<", ">", ">>"};
        const int bw = 40;
        for (int i = 0; i < 4; ++i) {
            const int bx = i < 2 ? kLeft + i * (bw + 4) : kLeft + kWide - (4 - i) * (bw + 4) + 4;
            if (ui.button(300 + i, Rect{bx, y, bw, 24}, lab[i])) {
                const int n = ((region_ + steps[i]) % nRegions + nRegions) % nRegions;
                select(n, -1);
            }
        }
        ui.textIn(Rect{kLeft + 2 * (bw + 4), y, kWide - 4 * (bw + 4), 24}, buf, kText, true);
    }

    // ---- the ramp of the part (lightest left) with its key shades under it
    y = 70;
    {
        const Region& r = cur_.regions[static_cast<size_t>(region_)];
        std::vector<Rgb> ramp;
        coledit::rampOf(r, 16, ramp);
        const Rect strip{kLeft, y, kWide, 22};
        for (int i = 0; i < 16; ++i) {
            const int x0 = strip.x + strip.w * i / 16, x1 = strip.x + strip.w * (i + 1) / 16;
            ui.fill(Rect{x0, strip.y, x1 - x0, strip.h}, col(ramp[static_cast<size_t>(15 - i)]));
        }
        ui.outline(strip, Col{12, 12, 16, 255});
        const int n = static_cast<int>(r.key.size());
        auto markerX = [&](int k) { return n <= 1 ? strip.x + strip.w / 2 - 8 : strip.x + static_cast<int>(std::lround(static_cast<double>(k) / (n - 1) * (strip.w - 16))); };
        const Rect row{kLeft, y + 24, kWide, 18};
        for (int k = 0; k < n; ++k) {
            const Rect m{markerX(k), row.y + 2, 16, 14};
            ui.fill(m, col(r.key[static_cast<size_t>(k)]));
            ui.outline(m, k == keyIdx_ ? Col{255, 255, 255, 255} : Col{12, 12, 16, 255});
        }
        // a tap on the strip or the markers picks the nearest shade
        if (in.released && drag_ == 0 && n > 0) {
            const Rect both{kLeft, y, kWide, 42};
            if (both.contains(in.mx, in.my) && both.contains(in.pmx, in.pmy) && std::abs(in.mx - in.pmx) < 8) {
                int best = 0, bd = 1 << 30;
                for (int k = 0; k < n; ++k) {
                    const int d = std::abs(markerX(k) + 8 - in.mx);
                    if (d < bd) {
                        bd = d;
                        best = k;
                    }
                }
                select(region_, n == 1 ? -1 : best);
            }
        }
    }

    // ---- whole part / one shade
    y = 116;
    {
        const bool one = keyIdx_ >= 0;
        if (ui.button(310, Rect{kLeft, y, 150, 22}, tr("Whole part", "Toda la parte"), true, !one)) select(region_, -1);
        const int nk = static_cast<int>(cur_.regions[static_cast<size_t>(region_)].key.size());
        if (ui.button(311, Rect{kLeft + 154, y, 96, 22}, tr("One shade", "Un tono"), nk > 1, one)) select(region_, keyIdx_ >= 0 ? keyIdx_ : nk / 2);
        const Rgb t = target();
        ui.fill(Rect{kLeft + 258, y, 22, 22}, col(t));
        ui.outline(Rect{kLeft + 258, y, 22, 22}, Col{12, 12, 16, 255});
        ui.text(kLeft + 286, y + 7, hexOf(t).substr(1), kDim);
    }

    // ---- hue / saturation / brightness: drag along the bar
    struct Slider {
        int id;
        const char* en;
        const char* esp;
        Rect bar;
        float v;
    };
    y = 144;
    Slider sl[3] = {{1, "Hue", "Matiz", Rect{kLeft, y + 11, kWide, 18}, hsv_.h / 360.0f},
                    {2, "Saturation", "Saturación", Rect{kLeft, y + 11 + 34, kWide, 18}, hsv_.s},
                    {3, "Brightness", "Brillo", Rect{kLeft, y + 11 + 68, kWide, 18}, hsv_.v}};
    for (Slider& s : sl) {
        char lab[40];
        std::snprintf(lab, sizeof lab, "%s  %d", tr(s.en, s.esp), s.id == 1 ? static_cast<int>(std::lround(hsv_.h)) : static_cast<int>(std::lround(s.v * 100)));
        ui.text(kLeft, s.bar.y - 11, lab, kDim);
        if (s.id == 1) {
            gradient(ui, s.bar, [](float t) { return coledit::fromHsv(Hsv{t * 360.0f, 1, 1}); }, 36);
        } else if (s.id == 2) {
            const Hsv h = hsv_;
            gradient(ui, s.bar, [h](float t) { return coledit::fromHsv(Hsv{h.h, t, std::max(h.v, 0.35f)}); }, 24);
        } else {
            const Hsv h = hsv_;
            gradient(ui, s.bar, [h](float t) { return coledit::fromHsv(Hsv{h.h, h.s, t}); }, 24);
        }
        handle(ui, s.bar, s.v);
        const Rect touch{s.bar.x - 6, s.bar.y - 8, s.bar.w + 12, s.bar.h + 16};
        if (in.pressed && drag_ == 0 && touch.contains(in.pmx, in.pmy)) {
            drag_ = s.id;
            dragRegion_ = cur_.regions[static_cast<size_t>(region_)];
        }
        if (drag_ == s.id) {
            if (in.down || in.released) {
                const float v = std::max(0.0f, std::min(1.0f, static_cast<float>(in.mx - s.bar.x) / static_cast<float>(s.bar.w - 1)));
                if (s.id == 1) hsv_.h = v * 359.99f;
                else if (s.id == 2) hsv_.s = v;
                else hsv_.v = v;
                applyHsv();
            }
            if (in.released || !in.down) drag_ = 0;
        }
    }

    // ---- the whole character: hue shift and one-tap looks
    y = 250;
    {
        ui.text(kLeft, y, tr("All parts: hue", "Todo el personaje: matiz"), kDim);
        const Rect bar{kLeft, y + 11, kWide, 14};
        gradient(ui, bar, [](float t) { return coledit::fromHsv(Hsv{t * 360.0f, 0.55f, 0.95f}); }, 36);
        handle(ui, bar, (globalDeg_ + 180.0f) / 360.0f);
        const Rect touch{bar.x - 6, bar.y - 8, bar.w + 12, bar.h + 16};
        if (in.pressed && drag_ == 0 && touch.contains(in.pmx, in.pmy)) {
            drag_ = 4;
            snap_ = cur_;
        }
        if (drag_ == 4) {
            if (in.down || in.released) {
                const float v = std::max(0.0f, std::min(1.0f, static_cast<float>(in.mx - bar.x) / static_cast<float>(bar.w - 1)));
                globalDeg_ = v * 360.0f - 180.0f;
                cur_ = snap_;
                coledit::adjustAll(cur_, globalDeg_, 1.0f, 1.0f);
                dirty_ = true;
            }
            if (in.released || !in.down) {
                drag_ = 0;
                globalDeg_ = 0;
                loadHsv();
            }
        }
    }
    y = 280;
    {
        struct Look {
            const char* en;
            const char* esp;
            float s, v;
        };
        const Look looks[4] = {{"Pastel", "Pastel", 0.7f, 1.08f}, {"Vivid", "Vivo", 1.3f, 1.0f}, {"Dark", "Oscuro", 1.0f, 0.8f}, {"Light", "Claro", 0.9f, 1.2f}};
        const int bw = 66;
        for (int i = 0; i < 4; ++i) {
            if (ui.button(320 + i, Rect{kLeft + i * (bw + 4), y, bw, 22}, tr(looks[i].en, looks[i].esp))) {
                coledit::adjustAll(cur_, 0, looks[i].s, looks[i].v);
                loadHsv();
                dirty_ = true;
            }
        }
        if (ui.button(324, Rect{kLeft + 4 * (bw + 4), y, kWide - 4 * (bw + 4), 22}, tr("Random", "Al azar"))) {
            cur_ = orig_;
            coledit::randomise(cur_, static_cast<uint32_t>(nowMs_ + 977 * ++seed_));
            loadHsv();
            dirty_ = true;
        }
    }

    // ---- name, save, cancel, back to the original
    y = 306;
    {
        ui.field(90, Rect{kLeft, y, 136, 20}, name_, tr("My colour", "Mi color"));
        if (ui.button(330, Rect{kLeft + 140, y, 108, 20}, tr("Part back", "Parte orig."))) {
            cur_.regions[static_cast<size_t>(region_)] = orig_.regions[static_cast<size_t>(region_)];
            loadHsv();
            dirty_ = true;
        }
        if (ui.button(331, Rect{kLeft + 252, y, 100, 20}, tr("All back", "Todo orig."))) {
            cur_ = orig_;
            loadHsv();
            dirty_ = true;
        }
    }
    y = 328;
    {
        const bool editing = !setup_.editing.file.empty();
        const int bw = editing ? 112 : 172;
        if (ui.button(340, Rect{kLeft, y, bw, 22}, tr("SAVE", "GUARDAR"))) {
            cur_.character = setup_.character;
            cur_.name = name_.empty() ? std::string(tr("My colour", "Mi color")) : name_;
            coledit::Fav out;
            std::string err;
            if (favs_ && favs_->save(cur_, coledit::bake(cur_), setup_.editing.file, out, &err)) {
                res.kind = Result::Kind::Saved;
                res.fav = out;
                closeNow();
                return res;
            }
            say(std::string(tr("Could not save: ", "No se pudo guardar: ")) + err);
        }
        if (ui.button(341, Rect{kLeft + bw + 4, y, bw, 22}, tr("CANCEL", "CANCELAR"))) {
            res.kind = Result::Kind::Cancelled;
            closeNow();
            return res;
        }
        if (editing) {
            const bool armed = nowMs_ - armDeleteMs_ < 3000;
            if (ui.button(342, Rect{kLeft + 2 * (bw + 4), y, kWide - 2 * (bw + 4), 22}, armed ? tr("SURE?", "¿SEGURO?") : tr("DELETE", "BORRAR"), true, armed)) {
                if (!armed) {
                    armDeleteMs_ = nowMs_;
                } else {
                    if (favs_) favs_->remove(setup_.editing);
                    res.kind = Result::Kind::Deleted;
                    res.fav = setup_.editing;
                    closeNow();
                    return res;
                }
            }
        }
    }
    if (nowMs_ < msgMs_ && !msg_.empty()) ui.textIn(Rect{kPreview.x + 4, kPreview.y + 16, kPreview.w - 8, 10}, msg_, kBad, true);
    return res;
}

}  // namespace menu
}  // namespace zengine
