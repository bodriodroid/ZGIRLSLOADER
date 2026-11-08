// gbs_anim.cpp - see gbs_anim.h.
#include "gbs_anim.h"

#include <algorithm>
#include <cmath>

namespace zengine {

namespace {

float lerp(float a, float b, float u) { return a + (b - a) * u; }

uint32_t lerpArgb(uint32_t a, uint32_t b, float u) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = static_cast<float>((a >> shift) & 0xFF), cb = static_cast<float>((b >> shift) & 0xFF);
        const int v = static_cast<int>(std::lround(lerp(ca, cb, u)));
        out |= static_cast<uint32_t>(std::clamp(v, 0, 255)) << shift;
    }
    return out;
}

GbsPose poseOf(const GbsKey& k) {
    GbsPose p;
    p.kind = k.kind;
    p.pivotX = k.pivotX;
    p.pivotY = k.pivotY;
    p.x = k.x;
    p.y = k.y;
    p.w = k.w;
    p.h = k.h;
    p.rot = k.rot;
    p.argb = k.argb;
    p.scaleX = k.scaleX;
    p.scaleY = k.scaleY;
    p.skew = k.skew;
    p.dropShadow = k.dropShadow;
    for (int i = 0; i < 12; ++i) p.raw[i] = k.raw[i];
    return p;
}

}  // namespace

float easeSegment(int type, int amount, float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    if (type <= 0 || amount <= 0) return u;
    // The scene tool's easing: type 1 = in (slow start), 2 = out (soft landing), 3 = in-out; the amount (0..100) picks the
    // exponent, 1 at 0 up to 5 at 100.
    const float power = 1.0f + 4.0f * static_cast<float>(std::min(amount, 100)) / 100.0f;
    const auto in = [power](float f) { return std::pow(f, power); };
    const auto out = [power](float f) { return 1.0f - std::pow(1.0f - f, power); };
    if (type == 1) return in(u);
    if (type == 2) return out(u);
    return u < 0.5f ? in(u * 2.0f) / 2.0f : out((u - 0.5f) * 2.0f) / 2.0f + 0.5f;
}

GbsPose sampleChannel(const GbsChannel& ch, float t, int length) {
    const std::vector<GbsKey>& k = ch.keys;
    // keys placed at or after the end of the animation are never reached by the runtime: they neither start nor end a segment
    size_t n = k.size();
    if (length > 0)
        while (n > 0 && k[n - 1].t >= length) --n;
    if (n == 0) return k.empty() ? GbsPose{} : poseOf(k.front());
    if (t <= static_cast<float>(k.front().t) || n == 1) return poseOf(k.front());
    if (t >= static_cast<float>(k[n - 1].t)) return poseOf(k[n - 1]);
    size_t hi = 1;
    while (hi < n && static_cast<float>(k[hi].t) <= t) ++hi;
    const GbsKey& a = k[hi - 1];
    const GbsKey& b = k[hi];
    const float span = static_cast<float>(b.t - a.t);
    float u = span > 0 ? (t - static_cast<float>(a.t)) / span : 1.0f;
    u = std::clamp(u, 0.0f, 1.0f);
    u = std::floor(u * 1000.0f) / 1000.0f;  // the runtime works with the progress in thousandths
    u = easeSegment(a.modeA, a.modeB, u);   // the easing belongs to the key the segment starts on
    GbsPose p = poseOf(a);
    p.pivotX = lerp(a.pivotX, b.pivotX, u);
    p.pivotY = lerp(a.pivotY, b.pivotY, u);
    p.x = lerp(a.x, b.x, u);
    p.y = lerp(a.y, b.y, u);
    p.w = lerp(a.w, b.w, u);
    p.h = lerp(a.h, b.h, u);
    p.rot = lerp(a.rot, b.rot, u);
    p.scaleX = lerp(a.scaleX, b.scaleX, u);
    p.scaleY = lerp(a.scaleY, b.scaleY, u);
    p.skew = lerp(a.skew, b.skew, u);
    p.dropShadow = static_cast<int>(lerp(static_cast<float>(a.dropShadow), static_cast<float>(b.dropShadow), u));
    if (a.hasColour()) p.argb = lerpArgb(a.argb, b.argb, u);
    return p;
}

void GbsAnimPlayer::bind(const GbsSceneView* view) {
    view_ = view;
    insts_.clear();
    poses_.clear();
    cues_.clear();
}

void GbsAnimPlayer::clear() {
    insts_.clear();
    poses_.clear();
}

bool GbsAnimPlayer::play(const std::string& name) { return playFrom(name, 0); }

bool GbsAnimPlayer::playFrom(const std::string& name, int startTick) {
    if (!view_) return false;
    const GbsAnim* a = view_->anim(name);
    if (!a) return false;
    insts_.erase(std::remove_if(insts_.begin(), insts_.end(), [&](const Inst& i) { return i.anim == a; }), insts_.end());
    Inst in;
    in.anim = a;
    in.tick = std::clamp(startTick, 0, a->length - 1);
    in.done = !a->loops() && in.tick >= a->length - 1;
    insts_.push_back(in);
    queueCues(in, in.tick - 1, in.tick);
    evaluate();
    return true;
}

void GbsAnimPlayer::stop(const std::string& name) {
    if (!view_) return;
    const GbsAnim* a = view_->anim(name);
    if (!a) return;
    insts_.erase(std::remove_if(insts_.begin(), insts_.end(), [&](const Inst& i) { return i.anim == a; }), insts_.end());
    evaluate();
}

void GbsAnimPlayer::hold(const std::string& name, bool on) {
    if (!view_) return;
    const GbsAnim* a = view_->anim(name);
    for (Inst& in : insts_)
        if (in.anim == a) in.held = on;
}

void GbsAnimPlayer::queueCues(const Inst& in, int fromExclusive, int toInclusive) {
    for (const GbsSoundCue& c : in.anim->sounds)
        if (c.t > fromExclusive && c.t <= toInclusive) cues_.push_back(c);
}

void GbsAnimPlayer::tick() {
    for (Inst& in : insts_) {
        if (in.done || in.held) continue;
        const GbsAnim& a = *in.anim;
        const int before = in.tick;
        ++in.tick;
        // (one cutscene animation says "loop 0..30" for a 15 tick animation: the loop ends where the animation does)
        const int loopEnd = std::min(a.loopEnd, a.length - 1);
        if (a.loops() && in.tick > loopEnd) {
            // the cues of the last stretch (before + 1 .. loopEnd) play, then the loop restarts at loopStart
            queueCues(in, before, loopEnd);
            in.tick = std::min(a.loopStart, loopEnd);
            queueCues(in, in.tick - 1, in.tick);
        } else if (in.tick >= a.length - 1) {
            in.tick = a.length - 1;
            in.done = !a.loops();
            queueCues(in, before, in.tick);
        } else {
            queueCues(in, before, in.tick);
        }
    }
    evaluate();
}

void GbsAnimPlayer::evaluate() {
    poses_.clear();
    for (const Inst& in : insts_)
        for (const GbsChannel& ch : in.anim->channels)
            if (!ch.keys.empty() && ch.keys.front().t < in.anim->length)
                poses_[ch.nodeId] = sampleChannel(ch, static_cast<float>(in.tick), in.anim->length);
}

bool GbsAnimPlayer::running(const std::string& name) const {
    for (const Inst& in : insts_)
        if (in.anim->name == name) return !in.done;
    return false;
}

bool GbsAnimPlayer::finished(const std::string& name) const {
    for (const Inst& in : insts_)
        if (in.anim->name == name) return in.done;
    return false;
}

int GbsAnimPlayer::tickOf(const std::string& name) const {
    for (const Inst& in : insts_)
        if (in.anim->name == name) return in.tick;
    return -1;
}

const GbsPose* GbsAnimPlayer::pose(uint32_t nodeId) const {
    const auto it = poses_.find(nodeId);
    return it == poses_.end() ? nullptr : &it->second;
}

std::vector<GbsSoundCue> GbsAnimPlayer::takeCues() {
    std::vector<GbsSoundCue> out;
    out.swap(cues_);
    return out;
}

}  // namespace zengine
