// gbs_arc.cpp - see gbs_arc.h.
#include "gbs_arc.h"

#include <cmath>

namespace zengine {

GbsArcPoint gbsArcPoint(const GbsArcStyle& s, float t, float r) {
    const float deg = s.clockwise ? s.start + t * s.sweep : s.start - t * s.sweep;
    const float rad = deg * 3.14159265358979f / 180.0f;
    const float sign = s.clockwise ? 1.0f : -1.0f;
    return GbsArcPoint{r * std::cos(rad) * sign, r * std::sin(rad) * sign};
}

std::vector<GbsArcPoint> gbsArcTriangles(const GbsArcStyle& s) {
    std::vector<GbsArcPoint> out;
    if (s.segments < 2 || s.thickness == 0.0f || s.sweep == 0.0f) return out;
    const float ro = s.radius + s.thickness / 2, ri = s.radius - s.thickness / 2;
    out.reserve(static_cast<size_t>(s.segments) * 6);
    for (int i = 0; i < s.segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(s.segments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(s.segments);
        const GbsArcPoint o0 = gbsArcPoint(s, t0, ro), i0 = gbsArcPoint(s, t0, ri);
        const GbsArcPoint o1 = gbsArcPoint(s, t1, ro), i1 = gbsArcPoint(s, t1, ri);
        for (const GbsArcPoint& p : {o0, i0, o1, i0, i1, o1}) out.push_back(p);
    }
    return out;
}

}  // namespace zengine
