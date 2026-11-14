// sgmesh.h - the stage meshes (*.sgm.msb, "version 2.0"): vertices, triangle list, bounding box, and for skinned meshes the joint table.
//
// Layout (all numbers big-endian, integers 8 bytes wide, strings = 8-byte length + bytes; see msb.h).  Checked against every mesh of the
// stage folders the player has (1793 files, no exceptions):
//
//   str   version              "2.0"
//   str   material             the name of the texture set the mesh is drawn with (e.g. a stage's "<name>_ground")
//   f32 x13                    a transform block; in the files seen it is always 0 0 0 | 1 1 1 | 1 1 1 | 0 0 0 | 90 (kept as `xf`, not used)
//   str   vertex declaration   "float p[3],n[3],uv[2]; uchar4 c;"  or  "... uchar4 c; uchar4 j[4],jw[4];"  (skinned)
//   u64   stride               36 or 44 bytes per vertex
//   u64   vertex count
//   u64   triangle count       (the index list holds 3 u16 per triangle)
//   u64   joint count          0 for a static mesh
//   vertices                   position (3 f32), normal (3 f32), uv (2 f32), colour (4 bytes RGBA); skinned: + 4 joint indices, 4 weights (bytes, /255)
//   u16 x (3 * triangles)      indices (big-endian)
//   f32 x6                     bounding box: min xyz, max xyz
//   joints (skinned only)      `joint count` strings (the joint's path in the rig), then `joint count` 4x4 matrices (16 f32 each, the
//                              inverse bind pose in the row-vector convention: translation in the last row)
//
// This is only the reader: nothing here draws.  No game data is included.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {
namespace sgmesh {

struct Vertex {
    float p[3] = {0, 0, 0};
    float n[3] = {0, 0, 0};
    float uv[2] = {0, 0};
    uint8_t c[4] = {255, 255, 255, 255};
    uint8_t j[4] = {0, 0, 0, 0};    // skinned meshes: indices into Mesh::joints
    uint8_t jw[4] = {0, 0, 0, 0};   // skinned meshes: weights, 0..255 (they add up to 255)
};

struct Joint {
    std::string path;
    float invBind[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct Mesh {
    std::string version, material, decl;
    float xf[13] = {};
    bool skinned = false;
    std::vector<Vertex> verts;
    std::vector<uint16_t> idx;
    float bbMin[3] = {0, 0, 0}, bbMax[3] = {0, 0, 0};
    std::vector<Joint> joints;
    size_t triangles() const { return idx.size() / 3; }
};

// Reads one mesh file.  false + `err` (when given) on a file that does not follow the layout above (unknown vertex declaration, a count that
// runs past the end, an index outside the vertex list, bytes left over).
bool parse(const std::vector<uint8_t>& data, Mesh& out, std::string* err = nullptr);

}  // namespace sgmesh
}  // namespace zengine
