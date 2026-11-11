#include "palgen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace zengine {
namespace palgen {

namespace {

constexpr int kTones = 16;

struct Stop {
    uint8_t r, g, b;
};
struct Ramp {
    const Stop* stops;
    int n;
};

// The Claude colours.  Neutrals: ink #141413 up to the cream #FAF9F5 through the warm greys (#B0AEA5 is the mid grey of the chat);
// orange #D97757 for the eyes.  Every ramp goes from dark to light and must get lighter at each stop (rampsValid()).
const Stop kNeutral[] = {{0x14, 0x14, 0x13}, {0x3D, 0x3D, 0x3A}, {0x73, 0x72, 0x6C}, {0xB0, 0xAE, 0xA5}, {0xE8, 0xE6, 0xDC}, {0xFA, 0xF9, 0xF5}};
const Stop kOrange[] = {{0x24, 0x12, 0x0C}, {0x7A, 0x3B, 0x25}, {0xD9, 0x77, 0x57}, {0xF0, 0xAE, 0x94}, {0xFF, 0xEF, 0xE6}};

const Ramp kNeutralRamp = {kNeutral, static_cast<int>(sizeof(kNeutral) / sizeof(Stop))};
const Ramp kOrangeRamp = {kOrange, static_cast<int>(sizeof(kOrange) / sizeof(Stop))};

// Rec. 601 luma of gamma-encoded colour, 0..1.
double luma(double r, double g, double b) { return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0; }
double stopLuma(const Stop& s) { return luma(s.r, s.g, s.b); }

// The ramp colour at luminance y (0..1): linear between the two stops that bracket y, the end stops outside.
void rampAt(const Ramp& ramp, double y, double out[3]) {
    const Stop* s = ramp.stops;
    const int n = ramp.n;
    const double y0 = stopLuma(s[0]), y1 = stopLuma(s[n - 1]);
    if (y <= y0) {
        out[0] = s[0].r; out[1] = s[0].g; out[2] = s[0].b;
        return;
    }
    if (y >= y1) {
        out[0] = s[n - 1].r; out[1] = s[n - 1].g; out[2] = s[n - 1].b;
        return;
    }
    int i = 0;
    while (i + 2 < n && stopLuma(s[i + 1]) < y) ++i;
    const double a = stopLuma(s[i]), b = stopLuma(s[i + 1]);
    const double f = b > a ? (y - a) / (b - a) : 0.0;
    out[0] = s[i].r + (s[i + 1].r - s[i].r) * f;
    out[1] = s[i].g + (s[i + 1].g - s[i].g) * f;
    out[2] = s[i].b + (s[i + 1].b - s[i].b) * f;
}

// The luminance range a role is painted in, per theme: the original luminance (0..1, stretched per role below) is mapped into [lo, hi].
struct Span {
    double lo, hi;
};
Span spanOf(Role role, Theme theme) {
    const bool dark = theme == Theme::ClaudeDark;
    switch (role) {
        case Role::Black: return {0.08, 0.26};                   // ink to the first charcoal
        case Role::Cloth: return dark ? Span{0.18, 0.56} : Span{0.30, 0.82};
        case Role::Skin: return dark ? Span{0.36, 0.70} : Span{0.52, 0.92};
        case Role::White: return dark ? Span{0.62, 0.90} : Span{0.74, 0.99};
        case Role::Eye: return {0.35, 0.78};                     // rust, the Claude orange, peach
    }
    return {0.0, 1.0};
}

// The theme's colour for an original colour (rgb 0..255) of a region with role `role`.
void themed(Role role, Theme theme, const double rgb[3], double out[3]) {
    double y = luma(rgb[0], rgb[1], rgb[2]);
    if (role == Role::Black) y = std::clamp(y / 0.35, 0.0, 1.0);                 // a black region's own tones fill its whole span
    else if (role == Role::White) y = std::clamp((y - 0.55) / 0.45, 0.0, 1.0);   // so do the tones of a white one
    const Span sp = spanOf(role, theme);
    rampAt(role == Role::Eye ? kOrangeRamp : kNeutralRamp, sp.lo + (sp.hi - sp.lo) * y, out);
}

uint32_t pack(const double c[3]) {
    auto q = [](double v) { return static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255)); };
    return 0xFF000000u | (q(c[0]) << 16) | (q(c[1]) << 8) | q(c[2]);
}

// The original palette row of one region as doubles: tone t, channel c.
struct Row {
    double c[kTones][3];
    void at(double tone, double out[3]) const {  // linear between the two tones around `tone`
        const double t = std::clamp(tone, 0.0, static_cast<double>(kTones - 1));
        const int i = std::min(static_cast<int>(t), kTones - 2);
        const double f = t - i;
        for (int k = 0; k < 3; ++k) out[k] = c[i][k] + (c[i + 1][k] - c[i][k]) * f;
    }
    void mean(double out[3]) const {
        out[0] = out[1] = out[2] = 0;
        for (int t = 0; t < kTones; ++t)
            for (int k = 0; k < 3; ++k) out[k] += c[t][k] / kTones;
    }
};

struct HintRow {
    const char* name;
    std::vector<int> eyes, ties;
};
const std::vector<HintRow>& hintTable() {
    static const std::vector<HintRow> t = {
#include "pal_theme_hints.inc"
    };
    return t;
}

}  // namespace

const char* themeLabel(Theme t) { return t == Theme::ClaudeDark ? "ClaudeDark" : "Claude"; }

const char* roleName(Role r) {
    switch (r) {
        case Role::Black: return "black";
        case Role::Cloth: return "cloth";
        case Role::Skin: return "skin";
        case Role::White: return "white";
        case Role::Eye: return "eye";
    }
    return "?";
}

Role classify(const uint8_t* rgb16) {
    // the middle tones tell the colour of a region; the darkest and the lightest are mostly shading
    double r = 0, g = 0, b = 0;
    int cnt = 0;
    for (int t = 3; t <= 12; ++t, ++cnt) {
        r += rgb16[t * 3 + 0];
        g += rgb16[t * 3 + 1];
        b += rgb16[t * 3 + 2];
    }
    r /= cnt; g /= cnt; b /= cnt;
    const double y = luma(r, g, b);
    const double rn = r / 255.0, gn = g / 255.0, bn = b / 255.0;
    const double mx = std::max({rn, gn, bn}), mn = std::min({rn, gn, bn});
    const double sat = mx > 0 ? (mx - mn) / mx : 0.0;
    if (y < 0.22) return Role::Black;
    if (y > 0.80 && sat < 0.25) return Role::White;
    double h = 0;
    if (sat > 0.0) {
        const double d = mx - mn;
        if (mx == rn) h = std::fmod((gn - bn) / d, 6.0);
        else if (mx == gn) h = (bn - rn) / d + 2.0;
        else h = (rn - gn) / d + 4.0;
        h *= 60.0;
        if (h < 0) h += 360.0;
    }
    const bool skinHue = h < 45.0 || h >= 345.0;
    if (skinHue && sat >= 0.12 && sat <= 0.62 && mx >= 0.55) return Role::Skin;
    return Role::Cloth;
}

Hints hintsFor(const std::string& normalisedName) {
    for (const HintRow& h : hintTable())
        if (palmod::normaliseName(h.name) == normalisedName) return {h.eyes, h.ties};
    return {};
}

bool rampsValid() {
    for (const Ramp* r : {&kNeutralRamp, &kOrangeRamp})
        for (int i = 0; i + 1 < r->n; ++i)
            if (!(stopLuma(r->stops[i]) < stopLuma(r->stops[i + 1]))) return false;
    for (Theme th : {Theme::Claude, Theme::ClaudeDark}) {
        for (int k = 0; k < kRoleCount; ++k) {
            const Span s = spanOf(static_cast<Role>(k), th);
            if (!(s.lo >= 0.0 && s.lo < s.hi && s.hi <= 1.0)) return false;
        }
        // the roles stay in order: black below the cloth, cloth below the white
        const Span b = spanOf(Role::Black, th), c = spanOf(Role::Cloth, th), w = spanOf(Role::White, th);
        if (!(b.lo < c.lo && b.hi < c.hi && c.hi < w.hi)) return false;
    }
    return true;
}

bool generate(const std::string& character, const Palette& src, Theme theme, palmod::PalFile& out, Report* rep, std::string* err) {
    const std::string norm = palmod::normaliseName(character);
    const std::vector<int>* counts = palmod::regionCounts(norm);
    if (!counts) {
        if (err) *err = "unknown character '" + character + "'";
        return false;
    }
    const int regions = static_cast<int>(counts->size());
    if (src.tones != kTones || src.bands != regions + 1 || src.rgb.size() != static_cast<size_t>(src.tones) * src.bands * 3) {
        if (err)
            *err = "'" + character + "': the palette texture has " + std::to_string(src.tones) + " tones x " + std::to_string(src.bands) +
                   " rows, the region table expects 16 x " + std::to_string(regions + 1) + " (a game build the table is not made for)";
        return false;
    }
    const Hints hints = hintsFor(norm);
    auto listed = [](const std::vector<int>& v, int row) { return std::find(v.begin(), v.end(), row) != v.end(); };

    palmod::PalFile pal;
    pal.character = character;
    pal.hueShiftInc = 0;
    pal.hueShiftInt = 0;
    pal.line = 0;                // the sprite's own black outline
    pal.shadow1 = 0xFFD97757u;   // the glow of a super: the orange, and a darker shade of it
    pal.shadow2 = 0xFF7A3B25u;

    Report local;
    // the recoloured 16 tones each region was aiming at, to measure the baked result against
    std::vector<double> aim(static_cast<size_t>(regions) * kTones * 3, 0.0);
    for (int reg = 0; reg < regions; ++reg) {
        const int row1 = reg + 1;   // palette row of this region
        const int n = (*counts)[static_cast<size_t>(reg)];
        Row row;
        uint8_t raw[kTones * 3];
        for (int t = 0; t < kTones; ++t)
            for (int k = 0; k < 3; ++k) {
                raw[t * 3 + k] = src.rgb[(static_cast<size_t>(row1) * kTones + t) * 3 + k];
                row.c[t][k] = raw[t * 3 + k];
            }
        Role role = classify(raw);
        if (listed(hints.ties, row1)) {
            role = Role::Black;
            ++local.ties;
        } else if (listed(hints.eyes, row1)) {
            role = Role::Eye;
        }
        ++local.perRole[static_cast<int>(role)];

        for (int t = 0; t < kTones; ++t) {
            double c[3];
            themed(role, theme, row.c[t], c);
            for (int k = 0; k < 3; ++k) aim[(static_cast<size_t>(reg) * kTones + t) * 3 + k] = c[k];
        }
        // the n keys: key 0 sits at tone 15 (lightest), key n-1 at tone 0, evenly between; a one-colour region is flat
        for (int i = 0; i < n; ++i) {
            double orig[3], c[3];
            if (n <= 1) row.mean(orig);
            else row.at(15.0 * (n - 1 - i) / (n - 1), orig);
            themed(role, theme, orig, c);
            pal.colors.push_back(pack(c));
        }
    }
    pal.numColors = static_cast<uint32_t>(pal.colors.size() + 1);

    // what the game bakes from the keys vs what the regions were aiming at
    Palette baked;
    std::string e2;
    if (!palmod::toPalette(pal, baked, &e2)) {
        if (err) *err = "internal: the generated palette does not bake: " + e2;
        return false;
    }
    double sum = 0, mx = 0;
    size_t cnt = 0;
    for (int reg = 0; reg < regions; ++reg)
        for (int t = 0; t < kTones; ++t)
            for (int k = 0; k < 3; ++k) {
                const double want = std::floor(aim[(static_cast<size_t>(reg) * kTones + t) * 3 + k] + 0.5);
                const double got = baked.rgb[(static_cast<size_t>(reg + 1) * kTones + t) * 3 + k];
                const double d = std::fabs(want - got);
                sum += d;
                mx = std::max(mx, d);
                ++cnt;
            }
    local.regions = regions;
    local.maxErr = mx;
    local.meanErr = cnt ? sum / static_cast<double>(cnt) : 0.0;
    if (rep) *rep = local;
    out = std::move(pal);
    return true;
}

}  // namespace palgen
}  // namespace zengine
