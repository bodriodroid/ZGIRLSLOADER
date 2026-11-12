#include "super_fx.h"

#include <algorithm>
#include <cmath>

namespace zengine {
namespace fx {

namespace {
float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
}  // namespace

float SuperFxTimeline::pulseAt(int age) {
    if (age < 0) return 0.0f;
    if (age < kFlashUp) return smooth(static_cast<float>(age + 1) / static_cast<float>(kFlashUp));
    const int d = age - kFlashUp;
    if (d >= kFlashDown) return 0.0f;
    return smooth(1.0f - static_cast<float>(d + 1) / static_cast<float>(kFlashDown + 1));
}

float SuperFxTimeline::outlineAt(int age) {
    if (age < 0) return 0.0f;
    const int period = 12;   // ticks for one full there-and-back
    const float ph = static_cast<float>(age % period) / static_cast<float>(period);
    return ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f;
}

void SuperFxTimeline::step(const SuperFxInput& in) {
    // stage darkness: ramps towards 1 while the owner's SUPERSHADOWS is on, back to 0 after
    if (in.shadows) shadowLin_ = std::min(1.0f, shadowLin_ + 1.0f / static_cast<float>(kShadowIn));
    else shadowLin_ = std::max(0.0f, shadowLin_ - 1.0f / static_cast<float>(kShadowOut));

    // flash pulse: starts on the tick SUPERFLASH goes from off to on (or changes look, e.g. a DHC raising a different flash); it keeps
    // counting while the flag stays on (the outline wave uses that) and, once the flag is off, runs out its own fall
    if (in.flash > 0) {
        const int look = std::clamp(in.flash, 1, 4);
        if (flashAge_ < 0 || !flashOn_ || look != look_) flashAge_ = 0;
        else ++flashAge_;
        look_ = look;
        flashOn_ = true;
    } else {
        flashOn_ = false;
        if (flashAge_ >= 0 && ++flashAge_ >= kFlashUp + kFlashDown) flashAge_ = -1;
    }
}

SuperFxState SuperFxTimeline::state() const {
    SuperFxState s;
    s.shadowT = smooth(shadowLin_);
    s.flashT = flashAge_ >= 0 ? pulseAt(flashAge_) : 0.0f;
    s.look = look_;
    s.outlineMix = flashAge_ >= 0 ? outlineAt(flashAge_) : 0.0f;
    return s;
}

void outlineColour(uint32_t argb, const float fallback[3], float out[3]) {
    if (argb == 0) {
        for (int i = 0; i < 3; ++i) out[i] = fallback[i];
        return;
    }
    out[0] = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(argb & 0xFF) / 255.0f;
}

}  // namespace fx
}  // namespace zengine
