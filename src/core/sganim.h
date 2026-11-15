// sganim.h - the texture-coordinate animations of the 3D stages (*.sga.msb, version "3.0"): sprite-sheet flipbooks (the NPCs in the
// background), scrolling smoke / water / clouds, flickering screens.
//
// Layout (big-endian, integers 8 bytes wide, strings = 8-byte length + bytes; see sgmesh.h).  577 of the 1460 animation files of the stage folders
// follow it to the last byte; the rest (883) animate a skeleton and are told apart by their joint count, which this reader refuses:
//
//   str   version          "3.0"
//   u32   a                the key count again on most files, 0 or 1 on some (not needed)
//   u32   b                0
//   u32   joints           0 for the files read here; > 0 = a skeleton animation (not read)
//   u32   c                0
//   u32   tracks           1 here
//   f32   duration         seconds
//   per track:
//     str   name           "uv_track"
//     u64   n, then n x (f32 su, f32 sv)     scale of the texture coordinates, one per key
//     u64   n, then n x  f32 rotation        (zero on every file seen; kept)
//     u64   n, then n x (f32 tu, f32 tv)     translation
//
// The keys are evenly spaced over the duration and the animation loops.  A flipbook (the translation jumps by a whole sprite cell between keys) shows each
// key for duration / n; anything else (scrolls: small steps, or a slow two-key drift) is interpolated linearly between keys, the last one meeting the first.
// The meshes store v upward; the translation is in that same space.
//
// Reader only; no game data is included.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {
namespace sganim {

struct UvTrack {
    std::string name;
    std::vector<float> scale;   // 2 per key
    std::vector<float> rot;     // 1 per key
    std::vector<float> trans;   // 2 per key
    size_t keys() const { return rot.size(); }
};

struct Anim {
    float duration = 1;                 // seconds
    std::vector<UvTrack> tracks;
    bool stepped = false;               // flipbook: keys are held, not blended
};

struct Uv {
    float su = 1, sv = 1, rot = 0, tu = 0, tv = 0;
    // u' = su * u + tu, v' = sv * v + tv  (rotation, never used by the data, is not applied)
    void apply(float& u, float& v) const {
        u = su * u + tu;
        v = sv * v + tv;
    }
};

// false + `err` when the file is not a UV animation (a skeleton animation says "skeleton animation").
bool parse(const std::vector<uint8_t>& data, Anim& out, std::string* err = nullptr);

// The transform at `seconds` (the animation loops).
Uv sample(const Anim& a, double seconds);

}  // namespace sganim
}  // namespace zengine
