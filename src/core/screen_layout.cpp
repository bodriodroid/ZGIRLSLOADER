#include "screen_layout.h"

#include <algorithm>
#include <cmath>

namespace zengine {
namespace layout {

Layout compute(int physW, int physH, bool noLimits) {
    Layout l;
    if (!noLimits || physW <= 0 || physH <= 0) return l;
    // wider than 16:9 (with a hair of slack so a 16:9 window with a few pixels of rounding stays the base)
    if (static_cast<long long>(physW) * kBaseH <= static_cast<long long>(physH) * kBaseW + physH / 2) return l;
    int w = static_cast<int>(std::lround(static_cast<double>(kBaseH) * physW / physH));
    w = std::clamp(w, kBaseW, kMaxW);
    w += w & 1;   // even: the safe area is centred on whole units
    w = std::min(w, kMaxW);
    l.w = w;
    l.safeX = (w - kBaseW) / 2;
    return l;
}

}  // namespace layout
}  // namespace zengine
