// palfx.h - palette effects: the colour washes a fighter wears for a counter hit, a status, a hit flash ...
//
// Where they come from: the game's text table of effects (characters/_palette_effects.ini).  Each effect is a name and a list of frames;
// a frame is  [high-colour low-colour] strength  plus optional modifiers (see parse()).  A fighter's script does not name an effect: it
// puts the 32-bit hash of the effect's name in one of the object's PALFX* values (status / self / self2 / hit), and the picture looks the
// effect up by that hash and plays it one frame per tick.
//
// What a frame means (the format is documented in the header of the table itself; the numbers below were checked against it):
//   * colours are 0..255 per channel (they may be negative or above 255); "PAL n" is entry n of the character's palette; "NORMAL" is
//     the character's own colour, i.e. no tint at that end.
//   * strength is 0..255 (0 = the normal colour, 255 = this colour); "HiMult a/b" and "LoMult a/b" scale the strength at the high /
//     low end (default 1; they may exceed 1 or be negative: "interpolates even more toward this color").
//   * "Loop" marks the frame playback jumps back to after the last one; without it the effect ends after its last frame.
//   * "NoLight" (own line): the tint ignores the stage lighting.   "MASK n [n n n]" (own line): palette indices the colour pair applies
//     to when the effect is used as the second ("self2") layer.
// The pixel's own intensity picks between the two ends: colour = mix(low, high, intensity), blended over the sprite by the strength.
//
// This is presentation only: nothing here is read by the simulation.  No game data is included; the parser takes the player's own table.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zengine {
namespace palfx {

// The hash scripts store: case-insensitive, h = 5381; for each character h = toupper(c) ^ (h * 33) (32-bit).
uint32_t nameHash(std::string_view name);

struct Colour {
    float rgb[3] = {0, 0, 0};   // 0..1 (outside that range for the "darken" / "overbright" colours)
    int pal = -1;               // >= 0: this entry of the character's palette instead of rgb
    bool actual = false;        // NORMAL: the character's own colour (no tint at this end)
};

struct Frame {
    Colour hi, lo;
    float strength = 1;         // 0..1 (the table's 0..255); NORMAL as the first colour makes the default 0
    float hiMult = 1, loMult = 1;
};

struct Effect {
    std::string name;
    uint32_t hash = 0;
    bool noLight = false;
    std::vector<int> mask;      // palette indices (at most 4)
    std::vector<Frame> frames;
    int loopFrom = -1;          // frame index playback returns to after the last one; -1 = it ends
    // The frame shown at tick `t` of playback (0-based), or -1 once a non-looping effect is over.
    int frameAt(int t) const;
};

class Library {
public:
    // Parses the table text.  Unknown lines are skipped; a frame line that does not parse is dropped (the effect keeps its other
    // frames).  An effect without frames is ignored.  Returns the number of effects added (later ones replace earlier ones of the same name).
    size_t parse(std::string_view text);
    const Effect* find(uint32_t hash) const;
    const Effect* byName(std::string_view name) const { return find(nameHash(name)); }
    size_t size() const { return fx_.size(); }
    // Hashes of effects whose names collide (two names, one hash): a table problem, reported for the tests / diagnostics.
    size_t collisions() const { return collisions_; }

private:
    std::vector<Effect> fx_;
    std::unordered_map<uint32_t, size_t> byHash_;
    size_t collisions_ = 0;
};

// Palette entry n of the character -> rgb 0..1.  Returns false when there is no such entry (the effect then uses mid grey).
using PalLookup = std::function<bool(int, float[3])>;

// One frame resolved for drawing: the colour at the low and the high end of the sprite's intensity and how strongly each is blended.
struct Tint {
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    float aLo = 0, aHi = 0;     // blend strength at each end (can be negative or above 1 before the final clamp)
    bool noLight = false;
    int mask[4] = {-1, -1, -1, -1};
    int maskCount = 0;
};
Tint resolve(const Effect& fx, const Frame& f, const PalLookup& pal);

// The whole sprite at once (what a renderer that cannot vary the tint per pixel can do): the middle of the two ends, as an rgb + alpha.
struct Flat {
    float rgb[3] = {1, 1, 1};
    float a = 0;
};
Flat flatten(const Tint& t);

// One slot's playback: the effect named by the hash the script keeps in the slot, one frame per tick.  A new hash restarts it.
class Layer {
public:
    // One tick with the slot holding `hash` (0 = nothing): a hash that differs from the last tick's starts that effect at its frame 0,
    // the same hash moves on one frame.  An unknown hash shows nothing.
    void update(uint32_t hash, const Library& lib);
    bool active() const { return fx_ != nullptr && frame_ >= 0; }
    const Effect* effect() const { return fx_; }
    int frame() const { return frame_; }
    uint32_t hash() const { return hash_; }
    bool tint(const PalLookup& pal, Tint& out) const;
    void reset() { *this = Layer(); }

private:
    uint32_t hash_ = 0;
    const Effect* fx_ = nullptr;
    int tick_ = 0, frame_ = -1;
    bool started_ = false;
};

// A fighter's four layers (the order the script slots come in).
enum LayerId { kStatus = 0, kSelf = 1, kSelf2 = 2, kHit = 3, kLayers = 4 };

class FighterFx {
public:
    // One tick: what the four PALFX values (status, self, self2, hit) hold now.
    void update(const std::array<uint32_t, kLayers>& hashes, const Library& lib);
    void reset();
    const Layer& layer(int i) const { return layers_[static_cast<size_t>(i)]; }
    bool any() const;
    // The washes of the whole-sprite layers (status, self, hit) laid over each other, in that order.  a == 0 when nothing is on.
    Flat flat(const PalLookup& pal) const;
    // The tint a per-pixel renderer feeds the shader's first colour pair: the topmost active whole-sprite layer (hit, then self, then
    // status).  False when none is active.
    bool primary(const PalLookup& pal, Tint& out) const;
    // The second pair, applied only to the palette indices of its mask (the self2 layer).  False when it is off or has no mask.
    bool secondary(const PalLookup& pal, Tint& out) const;

private:
    Layer layers_[kLayers];
};

}  // namespace palfx
}  // namespace zengine
