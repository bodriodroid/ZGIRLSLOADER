// sgscene.h - a 3D stage's scene file (*.sgi.msb, versions 2.0 and 2.1): the list of mesh instances that make up the backdrop, each with its place in
// the world and the animations it plays.
//
// Layout (big-endian, integers 8 bytes wide, strings = 8-byte length + bytes; see sgmesh.h).  Checked against every scene of the stage folders the
// player has, no exceptions:
//
//   str   version              "2.0" or "2.1"
//   u64   instance count
//   per instance:
//     str   path               the node's path in the authoring scene ("|BGMesh"); a few locators carry a bare name instead
//     str   mesh               the mesh it draws: the file "<mesh>.sgm.msb" next to the scene
//     f32 x16                  the world matrix, 4x4, row-vector convention (the translation is the last row)
//     u8 x2 (2.0) / x6 (2.1)   flag bytes, kept raw as `flags` and not interpreted: in 2.0 byte 0 is 1 on most meshes (not on every one that is
//                              drawn); in 2.1 the first four are 0 and the last two are a running number that grows through the file
//     u64   animation count
//     (str slot, str name) x animation count   e.g. "idle" -> "gunidle" (the animation file "<name>.sga.msb"); a few instances also carry
//                              a camera-shake slot
//
// This is only the reader: nothing here draws.  No game data is included.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zengine {
namespace sgscene {

struct Instance {
    std::string path, mesh;
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    uint8_t flags[6] = {0, 0, 0, 0, 0, 0};   // raw; 2.0 files only have the first two
    std::vector<std::pair<std::string, std::string>> anims;   // (slot, animation name)

    // The animation bound to a slot ("idle"), or "" when there is none.
    std::string anim(const std::string& slot) const {
        for (const auto& p : anims)
            if (p.first == slot) return p.second;
        return std::string();
    }
    // False for the locator nodes (no mesh to draw, e.g. the marker of the fight plane).
    bool hasGeometry() const { return !mesh.empty(); }
};

struct Scene {
    std::string version;
    std::vector<Instance> instances;
};

// Reads one scene file.  false + `err` (when given) on a file that does not follow the layout above.
bool parse(const std::vector<uint8_t>& data, Scene& out, std::string* err = nullptr);

}  // namespace sgscene
}  // namespace zengine
