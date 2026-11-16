#include "fighter_light.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace zengine {

namespace {
bool sameKind(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return i == a.size() && b[i] == 0;
}

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
}  // namespace

FighterLight FighterLight::fromLevel(const LevelInfo& lv, Ambient which) {
    FighterLight f;
    bool haveAmb = false, haveVita = false;
    float amb[3] = {1, 1, 1}, vita[3] = {1, 1, 1};
    for (const LevelLight& l : lv.lights) {
        const float c[3] = {l.r / 255.0f, l.g / 255.0f, l.b / 255.0f};
        if (sameKind(l.kind, "Amb")) {
            for (int i = 0; i < 3; ++i) amb[i] = c[i];
            haveAmb = true;
        } else if (sameKind(l.kind, "VitaAmb")) {
            for (int i = 0; i < 3; ++i) vita[i] = c[i];
            haveVita = true;
        } else if (sameKind(l.kind, "Dir")) {
            const double len = std::sqrt(l.x * l.x + l.y * l.y + l.z * l.z);
            if (!(len > 1e-6) || f.dirs.size() >= kMaxDir) continue;   // a light with no direction lights nothing
            Dir d;
            for (int i = 0; i < 3; ++i) d.colour[i] = c[i];
            d.dir[0] = static_cast<float>(l.x / len);
            d.dir[1] = static_cast<float>(l.y / len);
            d.dir[2] = static_cast<float>(l.z / len);
            f.dirs.push_back(d);
        } else if (sameKind(l.kind, "Pt")) {
            Pt p;
            for (int i = 0; i < 3; ++i) p.colour[i] = c[i];
            p.pos[0] = static_cast<float>(l.x);
            p.pos[1] = static_cast<float>(l.y);
            p.pos[2] = static_cast<float>(l.z);
            p.invRadius = l.range > 1e-6 ? static_cast<float>(1.0 / l.range) : 0.0f;
            f.pts.push_back(p);
        }
    }
    const bool useVita = which == Ambient::VitaAmb && haveVita;
    const float* a = useVita ? vita : (haveAmb ? amb : (haveVita ? vita : amb));
    for (int i = 0; i < 3; ++i) f.ambient[i] = a[i];
    return f;
}

bool FighterLight::active() const {
    if (!dirs.empty() || !pts.empty()) return true;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(ambient[i] - 1.0f) > 1.0f / 512.0f) return true;
    return false;
}

void FighterLight::baseMultiplier(float out[3]) const {
    for (int i = 0; i < 3; ++i) out[i] = ambient[i];
    for (const Dir& d : dirs) {
        const float k = clamp01(-d.dir[2]);   // a flat sprite facing the camera: only the light's depth component matters
        for (int i = 0; i < 3; ++i) out[i] += k * d.colour[i];
    }
}

void FighterLight::rim(float nx, float ny, bool mirrored, float out[3]) const {
    out[0] = out[1] = out[2] = 0;
    // measured against the original: with the stage's x axis, a sprite that faces right is lit on its left edges by a light travelling toward -x
    // (asg_labs: the warm rim is on the left of both fighters, the one that is flipped included), so the sprite's x is taken the other way round
    if (!mirrored) nx = -nx;
    const float nz = std::sqrt(clamp01(1.0f - (nx * nx + ny * ny)));
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!(len > 1e-6f)) return;
    nx /= len;
    ny /= len;
    const float nzn = nz / len;
    for (const Dir& d : dirs) {
        // the light's own front-to-back part is dropped when it comes from the camera's side (that is the diffuse), leaving the sideways light
        float lx = d.dir[0], ly = d.dir[1], lz = std::max(d.dir[2], 0.0f);
        const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
        if (!(ll > 1e-6f)) continue;
        lx /= ll;
        ly /= ll;
        lz /= ll;
        const float k = clamp01(-(nx * lx + ny * ly + nzn * lz));
        for (int i = 0; i < 3; ++i) out[i] += k * d.colour[i];
    }
}

std::array<std::array<float, 3>, 256> FighterLight::rimTable(bool mirrored) const {
    std::array<std::array<float, 3>, 256> t{};
    for (int v = 0; v < 256; ++v) {
        float nx, ny;
        normalOf(static_cast<uint8_t>(v), nx, ny);
        float r[3];
        rim(nx, ny, mirrored, r);
        t[static_cast<size_t>(v)] = {r[0], r[1], r[2]};
    }
    return t;
}

void FighterLight::pointDiffuse(float x, float y, float z, float out[3]) const {
    out[0] = out[1] = out[2] = 0;
    // the strongest kMaxPt of the level's point lights at this place (the game gives a fighter at most four)
    struct Hit {
        float w;
        float c[3];
    };
    std::vector<Hit> hits;
    for (const Pt& p : pts) {
        const float dx = p.pos[0] - x, dy = p.pos[1] - y, dz = p.pos[2] - z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(dist > 1e-6f)) continue;
        const float atten = 1.0f - clamp01(dist * p.invRadius);
        if (atten <= 0) continue;
        const float facing = clamp01(dz / dist);   // z toward the camera side of the fighter
        const float k = facing * atten;
        Hit h;
        h.w = k * (std::fabs(p.colour[0]) + std::fabs(p.colour[1]) + std::fabs(p.colour[2]));
        for (int i = 0; i < 3; ++i) h.c[i] = k * p.colour[i];
        hits.push_back(h);
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.w > b.w; });
    if (hits.size() > kMaxPt) hits.resize(kMaxPt);
    for (const Hit& h : hits)
        for (int i = 0; i < 3; ++i) out[i] += h.c[i];
}

size_t applyLighting(std::vector<uint8_t>& rgba, const ArtPlanes& pl, const std::vector<uint8_t>& lit, const FighterLight& light, bool mirrored) {
    const size_t n = static_cast<size_t>(pl.width) * pl.height;
    if (pl.normal.size() != n || rgba.size() != n * 4 || pl.band.size() != n || pl.grad.size() != n || lit.size() < 256u * 256u) return 0;
    float mul[3];
    light.baseMultiplier(mul);
    const auto rim = light.rimTable(mirrored);
    size_t changed = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t g = pl.grad[i], b = pl.band[i];
        if (rgba[i * 4 + 3] == 0 || !lit[static_cast<size_t>(b) * 256 + g]) continue;
        // the pixel's colour already carries the line darkening (1 - line / 7); the rim is darkened by the same line
        const float k = 1.0f - static_cast<float>(g >> 5) / 7.0f;
        const auto& r = rim[pl.normal[i]];
        for (int c = 0; c < 3; ++c) {
            const float v = static_cast<float>(rgba[i * 4 + static_cast<size_t>(c)]) * mul[c] + r[static_cast<size_t>(c)] * 255.0f * k;
            rgba[i * 4 + static_cast<size_t>(c)] = static_cast<uint8_t>(std::lround(std::min(255.0f, std::max(0.0f, v))));
        }
        ++changed;
    }
    return changed;
}

}  // namespace zengine
