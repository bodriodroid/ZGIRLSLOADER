// gbs_anim.h - plays the LAGG animations of a .gbs scene view (decoded by gbs_scene.h) on a fixed 60 Hz clock.
//
// An animation is a set of channels; each channel is the list of keyframes of one node, and a key is the node's whole
// pose (position, size, rotation, pivot, colour, label scale) at a tick.  Between two keys the pose is interpolated
// linearly (colour per channel, so "alpha 255 -> 0" is a fade); the key a segment STARTS on may carry an easing (type
// 1 in, 2 out, 3 in-out + an amount 0..100) that shapes that segment.  Keys at or past the animation's length are never
// reached.  Before the first key the first pose holds, after the last one the last holds.
//
// The player keeps several animations running at once, the way the game does (a looping background, the menu
// transition, a blink ...).  For every node the pose comes from the animation that was started last among those that
// have a channel for it; a finished animation keeps holding its last pose until another one takes the node over or
// clear() is called.  Nodes that no animation touches are left to the scene's resting pose.
//
// Time only moves when tick() is called, so the same calls always give the same poses (it is driven at 60 Hz by the
// screens; nothing here reads a clock).  Sound cues that were crossed are queued and taken with takeCues().
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gbs_scene.h"

namespace zengine {

// The animated state of one node at the current tick.
struct GbsPose {
    char kind = 'G';  // 'G' group, 'S' sprite, 'L' label, 'R' ring
    float pivotX = 0, pivotY = 0, x = 0, y = 0, w = 0, h = 0, rot = 0;
    uint32_t argb = 0xFFFFFFFFu;  // 0xAARRGGBB (sprites, labels, rings)
    float scaleX = 1, scaleY = 1; // labels
    float skew = 0;               // labels / sprites: degrees
    int dropShadow = 0;           // labels: px
    uint32_t raw[12] = {};        // ring keys: the stored words (arc data), taken from the earlier key
    float alpha() const { return static_cast<float>(argb >> 24) / 255.0f; }
};

// Interpolated pose of one channel at tick `t` (may be fractional).  `length` > 0 drops the keys at or past it.
GbsPose sampleChannel(const GbsChannel& ch, float t, int length = -1);

// Easing of a segment that starts on a key with (type, amount); u in [0,1] -> [0,1].
float easeSegment(int type, int amount, float u);

class GbsAnimPlayer {
public:
    // Binds the view whose animations are played (not owned: the scene must outlive the player).  Clears everything.
    void bind(const GbsSceneView* view);
    const GbsSceneView* view() const { return view_; }

    // Starts `name` at tick 0 (a running copy of the same animation is restarted).  False when the view has no such one.
    bool play(const std::string& name);
    // Starts `name` at tick `startTick` (clamped into the animation).
    bool playFrom(const std::string& name, int startTick);
    // Drops the animation: the nodes it held go back to what an earlier animation / the resting pose says.
    void stop(const std::string& name);
    // Freezes / releases a running animation (tick() leaves it where it is while held): the cutscene runner holds the "legacy" animation at its pause cues.
    void hold(const std::string& name, bool on);
    // Drops every animation (all nodes are back at their resting pose).
    void clear();
    // Advances every running animation by one tick (1/60 s).
    void tick();

    bool known(const std::string& name) const { return view_ && view_->anim(name); }
    bool running(const std::string& name) const;   // started and not finished (a looping one runs until stopped)
    bool finished(const std::string& name) const;  // started, not looping, last tick reached
    int tickOf(const std::string& name) const;     // -1 when it was not started

    // The animated pose of a node, nullptr when no started animation has a channel for it.
    const GbsPose* pose(uint32_t nodeId) const;

    // Sound cues crossed since the last call (ids are rows of the scene's sound table).
    std::vector<GbsSoundCue> takeCues();

private:
    struct Inst {
        const GbsAnim* anim = nullptr;
        int tick = 0;
        bool done = false;
        bool held = false;
    };
    void evaluate();
    void queueCues(const Inst& in, int fromExclusive, int toInclusive);

    const GbsSceneView* view_ = nullptr;
    std::vector<Inst> insts_;  // in start order: the last one wins
    std::unordered_map<uint32_t, GbsPose> poses_;
    std::vector<GbsSoundCue> cues_;
};

}  // namespace zengine
